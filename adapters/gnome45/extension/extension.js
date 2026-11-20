import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import {Extension, InjectionManager} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Background from 'resource:///org/gnome/shell/ui/background.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {DesktopPointer} from './pointer.js';
import {addWorkspacePreview} from './preview.js';

const RENDERER = 'nekomote_gnome45';
const SIGTERM = 15;

export default class NekoMoteExtension extends Extension {
    enable() {
        if (!Meta.is_wayland_compositor())
            throw new Error('NekoMote requires a GNOME Wayland session');

        this._renderers = new Set();
        this._backgrounds = new Map();
        this._injections = new InjectionManager();
        const extension = this;
        this._injections.overrideMethod(Background.BackgroundManager.prototype,
            '_createBackgroundActor', original => function (...args) {
                const actor = original.apply(this, args);
                if (!this._container.has_style_class_name?.('screen-shield-background') &&
                    !Main.layoutManager.screenShieldGroup.contains(this._container))
                    extension._registerBackground(actor,
                        this._container === Main.layoutManager._backgroundGroup);
                return actor;
            });
        for (const manager of Main.layoutManager._bgManagers)
            this._registerBackground(manager.backgroundActor, true);
        this._overviewId = Main.overview.connect('showing', () => {
            for (const renderer of this._renderers)
                renderer.pointer.leave();
            this._registerPreviewTree(Main.layoutManager.overviewGroup);
        });
        this._panelId = Main.layoutManager.panelBox.connect('notify::height', () => {
            for (const renderer of this._renderers)
                this._updateTopInset(renderer);
        });
        this._settings = this.getSettings();
        this._settingsId = this._settings.connect('changed', () => this._reload());
        this._monitorsId = Main.layoutManager.connect('monitors-changed', () => this._reload());
        this._mapId = global.window_manager.connect_after('map', (_wm, actor) => {
            this._attach(actor);
        });
        this._reload();
    }

    disable() {
        if (!this._renderers)
            return;
        global.window_manager.disconnect(this._mapId);
        Main.layoutManager.disconnect(this._monitorsId);
        this._settings.disconnect(this._settingsId);
        Main.overview.disconnect(this._overviewId);
        Main.layoutManager.panelBox.disconnect(this._panelId);
        this._injections.clear();
        this._stopRenderers();
        for (const background of this._backgrounds.values())
            background.actor.disconnect(background.destroyId);
        this._backgrounds.clear();
        this._renderers = null;
        this._settings = null;
    }

    _reload() {
        this._stopRenderers();
        const assets = this._settings.get_string('assets-dir');
        if (!assets)
            return;

        try {
            if (!GLib.path_is_absolute(assets) ||
                Gio.File.new_for_path(assets).get_child('characters')
                    .query_file_type(Gio.FileQueryInfoFlags.NONE, null) !== Gio.FileType.DIRECTORY)
                throw new Error('Choose a NekoMote assets directory containing characters/');

            const configuredPath = this._settings.get_string('renderer-path');
            if (configuredPath && !GLib.path_is_absolute(configuredPath))
                throw new Error('The renderer executable path must be absolute');
            const userRenderer = GLib.build_filenamev([GLib.get_home_dir(), '.local', 'bin', RENDERER]);
            const executable = configuredPath ||
                (GLib.file_test(userRenderer, GLib.FileTest.IS_EXECUTABLE) ? userRenderer :
                    GLib.find_program_in_path(RENDERER));
            if (!executable || !GLib.file_test(executable, GLib.FileTest.IS_EXECUTABLE))
                throw new Error('Install nekomote_gnome45 or set its path in Preferences');

            const configDir = GLib.build_filenamev([GLib.get_user_config_dir(), 'nekomote', 'gnome45']);
            const backend = this._settings.get_string('backend');
            for (const monitor of Main.layoutManager.monitors)
                this._start(executable, assets, backend, configDir, monitor);
        } catch (error) {
            this._stopRenderers();
            console.error(`NekoMote: ${error.message}`);
        }
    }

    _start(executable, assets, backend, configDir, monitor) {
        const config = GLib.build_filenamev([configDir, `monitor-${monitor.index}.json`]);
        const argv = [
            executable, '--assets', assets, '--backend', backend, '--config', config,
            '--width', String(monitor.width), '--height', String(monitor.height),
            '--desktop-input', '--global-gaze', 'off',
        ];
        if (monitor.index !== Main.layoutManager.primaryIndex)
            argv.push('--mute');

        const launcher = new Gio.SubprocessLauncher({flags: Gio.SubprocessFlags.STDIN_PIPE});
        let client;
        let process;
        try {
            if (typeof Meta.WaylandClient.new_subprocess === 'function') {
                client = Meta.WaylandClient.new_subprocess(global.context, launcher, argv);
                process = client.get_subprocess();
            } else {
                client = Meta.WaylandClient.new(global.context, launcher);
                process = client.spawnv(global.display, argv);
            }
        } finally {
            launcher.close();
        }

        const renderer = {
            client, process, monitor, source: null, desktopClone: null,
            window: null, unmanagedId: 0, opacityId: 0,
        };
        renderer.pointer = new DesktopPointer(process.get_stdin_pipe(), error => {
            console.error(`NekoMote: ${error.message}`);
            this._detach(renderer);
            process.send_signal(SIGTERM);
        });
        this._renderers.add(renderer);
        this._updateTopInset(renderer);
        process.wait_async(null, (child, result) => {
            child.wait_finish(result);
            if (this._renderers?.delete(renderer)) {
                this._detach(renderer);
                console.warn('NekoMote: renderer exited, showing the static background');
            }
        });
    }

    _updateTopInset(renderer) {
        const height = renderer.monitor.index === Main.layoutManager.primaryIndex
            ? Main.layoutManager.panelBox.height : 0;
        renderer.pointer.topInset(height);
    }

    _attach(actor) {
        const window = actor.get_meta_window();
        const renderer = [...this._renderers].find(({client}) => client.owns_window(window));
        if (!renderer || renderer.source)
            return;
        const {monitor} = renderer;
        try {
            renderer.window = window;
            renderer.unmanagedId = window.connect('unmanaged', () => this._detach(renderer));
            window.move_to_monitor(monitor.index);
            renderer.source = actor;
            // A renderer mapped during the Overview can remain visible even
            // after minimization. Clutter clones use their own paint opacity.
            renderer.opacityId = actor.connect('notify::opacity', () => {
                if (actor.opacity !== 0)
                    actor.opacity = 0;
            });
            window.minimize();
            actor.opacity = 0;
            renderer.client.hide_from_window_list?.(window);
            // Keep desktop redraws directly in the background group so Mutter
            // continues presenting frames from the minimized render window.
            const {x, y, width, height} = monitor;
            renderer.desktopClone = new Clutter.Clone({
                source: actor, x, y, width, height, clip_to_allocation: true,
            });
            Main.layoutManager._backgroundGroup.add_child(renderer.desktopClone);
            for (const background of this._backgrounds.values())
                this._attachBackground(background, renderer);
        } catch (error) {
            this._detach(renderer);
            renderer.process.send_signal(SIGTERM);
            console.error(`NekoMote: could not attach renderer: ${error.message}`);
        }
    }

    _registerPreviewTree(root) {
        if (root instanceof Meta.BackgroundActor)
            this._registerBackground(root, false);
        for (const actor of root.get_children())
            this._registerPreviewTree(actor);
    }

    _registerBackground(actor, desktop) {
        if (this._backgrounds.has(actor))
            return;
        const background = {actor, desktop, preview: null, renderer: null, eventId: 0};
        background.destroyId = actor.connect('destroy', () => {
            if (background.desktop)
                background.renderer?.pointer.leave();
            background.preview?.destroy();
            this._backgrounds.delete(actor);
        });
        this._backgrounds.set(actor, background);
        for (const renderer of this._renderers)
            this._attachBackground(background, renderer);
    }

    _attachBackground(background, renderer) {
        const {actor} = background;
        if (!renderer.source || background.renderer || actor.monitor !== renderer.monitor.index)
            return;
        if (background.desktop) {
            // Consume left-button gestures before the background menu's
            // long-press action. Right clicks continue through GNOME's actor.
            background.eventId = actor.connect('captured-event', (_target, event) =>
                this._desktopEvent(renderer, event));
        } else {
            background.preview = addWorkspacePreview(actor, renderer.source, renderer.monitor);
        }
        background.renderer = renderer;
    }

    _desktopEvent(renderer, event) {
        // The desktop clone fills the monitor. The input actor and hidden
        // window can have different origins when the Shell reserves work area.
        if (Main.overview.visible || !renderer.desktopClone)
            return Clutter.EVENT_PROPAGATE;
        return renderer.pointer.event(renderer.desktopClone, event);
    }

    _detach(renderer) {
        renderer.pointer.close();
        if (renderer.unmanagedId)
            renderer.window.disconnect(renderer.unmanagedId);
        if (renderer.opacityId)
            renderer.source.disconnect(renderer.opacityId);
        renderer.unmanagedId = 0;
        renderer.opacityId = 0;
        renderer.window = null;
        renderer.source = null;
        renderer.desktopClone?.destroy();
        renderer.desktopClone = null;
        for (const background of this._backgrounds.values()) {
            if (background.renderer === renderer) {
                if (background.eventId)
                    background.actor.disconnect(background.eventId);
                background.eventId = 0;
                background.preview?.destroy();
                background.preview = null;
                background.renderer = null;
            }
        }
    }

    _stopRenderers() {
        for (const renderer of this._renderers) {
            this._detach(renderer);
            renderer.process.send_signal(SIGTERM);
        }
        this._renderers.clear();
    }
}
