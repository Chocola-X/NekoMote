import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Meta from 'gi://Meta';
import * as Config from 'resource:///org/gnome/shell/misc/config.js';
import {Extension, InjectionManager} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Background from 'resource:///org/gnome/shell/ui/background.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as WorkspaceThumbnails from 'resource:///org/gnome/shell/ui/workspaceThumbnail.js';
import {appendDiagnostic, createDiagnosticSession} from './diagnostics.js';
import {DesktopPointer} from './pointer.js';
import {addWorkspacePreview} from './preview.js';
import {defaultRendererPath} from './renderer.js';

const SIGTERM = 15;
const GAZE_INTERVAL_MS = 33;
const {WorkspaceThumbnail} = WorkspaceThumbnails;
const hasChildSignals = GObject.signal_lookup('child-added', Clutter.Actor.$gtype) !== 0;

export default class NekoMoteExtension extends Extension {
    enable() {
        // GNOME 50 removed both X11 sessions and this Mutter predicate.
        const wayland = typeof Meta.is_wayland_compositor === 'function'
            ? Meta.is_wayland_compositor()
            : typeof Meta.WaylandClient?.new_subprocess === 'function';
        if (!wayland)
            throw new Error('NekoMote requires a GNOME Wayland session');

        this._renderers = new Set();
        this._backgrounds = new Map();
        this._thumbnails = new Map();
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
        if (typeof WorkspaceThumbnail?.prototype.setPorthole === 'function') {
            this._injections.overrideMethod(WorkspaceThumbnail.prototype,
                'setPorthole', original => function (...args) {
                    const result = original.apply(this, args);
                    extension._syncThumbnail(this);
                    return result;
                });
        }
        for (const manager of Main.layoutManager._bgManagers)
            this._registerBackground(manager.backgroundActor, true);
        this._overviewId = Main.overview.connect('showing', () => {
            for (const renderer of this._renderers)
                renderer.pointer.leave();
            this._registerPreviewTree(Main.layoutManager.overviewGroup);
            this._updatePlayback();
        });
        this._overviewHiddenId = Main.overview.connect('hidden', () => this._updatePlayback());
        this._fullscreenId = global.display.connect('in-fullscreen-changed', () => this._updatePlayback());
        this._gazeTimer = 0;
        this._panelId = Main.layoutManager.panelBox.connect('notify::height', () => {
            for (const renderer of this._renderers)
                this._updateTopInset(renderer);
        });
        this._settings = this.getSettings();
        const installed = defaultRendererPath();
        if (installed && !this._settings.get_string('renderer-path'))
            this._settings.set_string('renderer-path', installed);
        this._settingsId = this._settings.connect('changed', (_settings, key) => {
            if (key === 'global-gaze')
                this._updateGazeTimer();
            else if (key === 'pause-fullscreen')
                this._updatePlayback();
            else if (key === 'diagnostic-directory' && !this._settings.get_boolean('diagnostic-logging'))
                return;
            else
                this._reload();
        });
        this._monitorsId = Main.layoutManager.connect('monitors-changed', () => this._reload());
        this._mapId = global.window_manager.connect_after('map', (_wm, actor) => {
            this._attach(actor);
        });
        if (Main.overview.visible)
            this._registerPreviewTree(Main.layoutManager.overviewGroup);
        this._reload();
    }

    disable() {
        if (!this._renderers)
            return;
        global.window_manager.disconnect(this._mapId);
        Main.layoutManager.disconnect(this._monitorsId);
        this._settings.disconnect(this._settingsId);
        Main.overview.disconnect(this._overviewId);
        Main.overview.disconnect(this._overviewHiddenId);
        global.display.disconnect(this._fullscreenId);
        Main.layoutManager.panelBox.disconnect(this._panelId);
        this._injections.clear();
        this._stopRenderers();
        this._diagnosticSession = null;
        for (const background of this._backgrounds.values())
            background.actor.disconnect(background.destroyId);
        this._backgrounds.clear();
        for (const [actor, thumbnail] of this._thumbnails)
            actor.disconnect(thumbnail.destroyId);
        this._thumbnails.clear();
        this._renderers = null;
        this._settings = null;
    }

    _reload() {
        this._stopRenderers();
        this._diagnosticSession = null;
        if (this._settings.get_boolean('diagnostic-logging')) {
            try {
                this._diagnosticSession = createDiagnosticSession(
                    this._settings.get_string('diagnostic-directory'));
                const backend = this._settings.get_string('backend');
                const monitors = Main.layoutManager.monitors.length;
                this._log(`GNOME Shell ${Config.PACKAGE_VERSION}, Wayland, backend ${backend}, ${monitors} monitor(s)`);
            } catch (error) {
                console.warn(`NekoMote: cannot save diagnostic logs: ${error.message}`);
            }
        }
        const assets = this._settings.get_string('assets-dir');
        if (!assets) {
            this._log('no assets directory configured');
            return;
        }

        try {
            if (!GLib.path_is_absolute(assets))
                throw new Error(`The assets directory must be absolute: ${assets}`);
            const characters = Gio.File.new_for_path(assets).get_child('characters');
            const info = characters.query_info('standard::type', Gio.FileQueryInfoFlags.NONE, null);
            if (info.get_file_type() !== Gio.FileType.DIRECTORY)
                throw new Error(`Expected an assets subdirectory: ${characters.get_path()}`);

            const configuredPath = this._settings.get_string('renderer-path');
            if (configuredPath && !GLib.path_is_absolute(configuredPath))
                throw new Error('The renderer executable path must be absolute');
            const executable = configuredPath || defaultRendererPath();
            if (!executable || !GLib.file_test(executable, GLib.FileTest.IS_EXECUTABLE))
                throw new Error('Install nekomote_gnome45 or set its path in Preferences');

            const configDir = GLib.build_filenamev([GLib.get_user_config_dir(), 'nekomote', 'gnome45']);
            const backend = this._settings.get_string('backend');
            for (const monitor of Main.layoutManager.monitors)
                this._start(executable, assets, backend, configDir, monitor);
            this._updatePlayback();
        } catch (error) {
            this._stopRenderers();
            this._log(error.message, 'error');
        }
    }

    _log(message, level = 'log') {
        console[level](`NekoMote: ${message}`);
        if (!this._diagnosticSession)
            return;
        try {
            appendDiagnostic(this._diagnosticSession, message);
        } catch (error) {
            console.warn(`NekoMote: cannot write extension diagnostic log: ${error.message}`);
            this._diagnosticSession = null;
        }
    }

    _start(executable, assets, backend, configDir, monitor) {
        const config = GLib.build_filenamev([configDir, `monitor-${monitor.index}.json`]);
        const argv = [
            executable, '--assets', assets, '--backend', backend, '--config', config,
            '--width', String(monitor.width), '--height', String(monitor.height),
            '--desktop-input', '--global-gaze', 'off',
        ];
        if (!this._settings.get_boolean('pause-session'))
            argv.push('--no-session-pause');
        if (monitor.index !== Main.layoutManager.primaryIndex)
            argv.push('--mute');

        let flags = Gio.SubprocessFlags.STDIN_PIPE;
        if (this._diagnosticSession)
            flags |= Gio.SubprocessFlags.STDERR_MERGE;
        const launcher = new Gio.SubprocessLauncher({flags});
        let client;
        let process;
        try {
            if (this._diagnosticSession) {
                launcher.set_stdout_file_path(GLib.build_filenamev([
                    this._diagnosticSession, `monitor-${monitor.index}.log`,
                ]));
            }
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
            window: null, unmanagedId: 0, minimizedId: 0, visibilityId: 0, covered: false,
        };
        renderer.pointer = new DesktopPointer(process.get_stdin_pipe(), error => {
            this._log(error.message, 'error');
            this._detach(renderer);
            process.send_signal(SIGTERM);
        });
        this._renderers.add(renderer);
        this._log(`starting ${backend} renderer on monitor ${monitor.index} (${monitor.width}x${monitor.height})`);
        this._updateTopInset(renderer);
        process.wait_async(null, (child, result) => {
            child.wait_finish(result);
            if (this._renderers?.delete(renderer)) {
                this._detach(renderer);
                this._updateGazeTimer();
                const status = child.get_if_exited() ? `exit status ${child.get_exit_status()}` :
                    `signal ${child.get_term_sig()}`;
                this._log(`renderer on monitor ${monitor.index} ended (${status})`, 'warn');
            }
        });
    }

    _updatePlayback() {
        const pause = this._settings.get_boolean('pause-fullscreen') && !Main.overview.visible;
        for (const renderer of this._renderers) {
            const covered = pause && renderer.monitor.inFullscreen;
            if (covered === renderer.covered)
                continue;
            renderer.covered = covered;
            renderer.pointer.setCovered(covered);
        }
        this._updateGazeTimer();
    }

    _updateGazeTimer() {
        if (!this._settings.get_boolean('global-gaze') || Main.overview.visible ||
            [...this._renderers].every(renderer => renderer.covered)) {
            if (this._gazeTimer) {
                GLib.source_remove(this._gazeTimer);
                this._gazeTimer = 0;
            }
            for (const renderer of this._renderers)
                renderer.pointer.clearGaze();
            return;
        }
        if (!this._gazeTimer) {
            this._gazeTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, GAZE_INTERVAL_MS, () => {
                if (!Main.sessionMode.isLocked && !Main.overview.visible) {
                    const [x, y] = global.get_pointer();
                    for (const renderer of this._renderers) {
                        if (!renderer.covered)
                            renderer.pointer.gaze(x - renderer.monitor.x, y - renderer.monitor.y);
                    }
                } else {
                    for (const renderer of this._renderers)
                        renderer.pointer.clearGaze();
                }
                return GLib.SOURCE_CONTINUE;
            });
        }
    }

    _updateTopInset(renderer) {
        const height = renderer.monitor.index === Main.layoutManager.primaryIndex
            ? Main.layoutManager.panelBox.height : 0;
        renderer.pointer.topInset(height);
    }

    _hideRendererWindow(renderer, window) {
        // Mutter disables minimization while the window is excluded from task lists.
        // GNOME 50 moved the list controls from WaylandClient to Window.
        const windowListOnWindow = typeof window.hide_from_window_list === 'function';
        if (!window.minimized) {
            if (window.is_skip_taskbar()) {
                if (windowListOnWindow)
                    window.show_in_window_list();
                else
                    renderer.client.show_in_window_list(window);
            }
            window.minimize();
        }
        if (windowListOnWindow)
            window.hide_from_window_list();
        else
            renderer.client.hide_from_window_list(window);
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
            // Mutter may keep a minimized renderer visible after the Overview.
            // Hide it from the window group so its clip cannot affect the clone.
            renderer.visibilityId = actor.connect('notify::visible', () => {
                if (actor.visible)
                    actor.hide();
            });
            this._hideRendererWindow(renderer, window);
            renderer.minimizedId = window.connect('notify::minimized', () => {
                if (!window.minimized)
                    this._hideRendererWindow(renderer, window);
            });
            actor.hide();
            // Keep desktop redraws directly in the background group so Mutter
            // continues presenting frames from the minimized render window.
            const {x, y, width, height} = monitor;
            renderer.desktopClone = new Clutter.Clone({
                source: actor, x, y, width, height, clip_to_allocation: true,
            });
            Main.layoutManager._backgroundGroup.add_child(renderer.desktopClone);
            for (const background of this._backgrounds.values())
                this._attachBackground(background, renderer);
            for (const thumbnail of this._thumbnails.keys())
                this._syncThumbnail(thumbnail);
            this._log(`attached renderer to desktop on monitor ${monitor.index}`);
        } catch (error) {
            this._detach(renderer);
            renderer.process.send_signal(SIGTERM);
            this._log(`could not attach renderer to monitor ${monitor.index}: ${error.message}`, 'error');
        }
    }

    _registerPreviewTree(root) {
        if (root instanceof Meta.BackgroundActor)
            this._registerBackground(root, false);
        else if (WorkspaceThumbnail && root instanceof WorkspaceThumbnail)
            this._syncThumbnail(root);
        for (const actor of root.get_children())
            this._registerPreviewTree(actor);
    }

    _syncThumbnail(actor) {
        // The viewport shows the work area. Keep the wallpaper behind the
        // separate container whose window clones Shell restacks while dragging.
        const viewport = actor._viewport;
        const contents = actor._contents;
        if (!viewport || contents?.get_parent() !== viewport)
            return;
        let thumbnail = this._thumbnails.get(actor);
        if (!thumbnail) {
            const destroyId = actor.connect('destroy', () => this._thumbnails.delete(actor));
            thumbnail = {destroyId, clone: null, renderer: null, hiddenBackgrounds: new Set()};
            this._thumbnails.set(actor, thumbnail);
        }
        if (!thumbnail.clone) {
            const renderer = [...this._renderers].find(({monitor, source}) =>
                source && monitor.index === actor.monitorIndex);
            if (!renderer)
                return;
            const {width, height} = renderer.monitor;
            thumbnail.clone = new Clutter.Clone({source: renderer.source, width, height});
            thumbnail.renderer = renderer;
            viewport.add_child(thumbnail.clone);
            viewport.set_child_below_sibling(thumbnail.clone, contents);

            // GNOME UI Tune adds static wallpaper inside the window container.
            // Hide only its background actors, including replacements on a theme change.
            const hideBackground = child => {
                if (child instanceof Meta.BackgroundActor && child.visible) {
                    thumbnail.hiddenBackgrounds.add(child);
                    child.hide();
                }
            };
            contents.connectObject(
                hasChildSignals ? 'child-added' : 'actor-added',
                (_container, child) => hideBackground(child),
                hasChildSignals ? 'child-removed' : 'actor-removed',
                (_container, child) => thumbnail.hiddenBackgrounds.delete(child),
                thumbnail.clone);
            for (const child of contents.get_children())
                hideBackground(child);
        }
        const x = thumbnail.renderer.monitor.x + contents.x;
        const y = thumbnail.renderer.monitor.y + contents.y;
        thumbnail.clone.set_position(x, y);
        thumbnail.clone.set_clip(-x, -y, viewport.width, viewport.height);
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
        if (Main.overview.visible || renderer.covered || !renderer.desktopClone)
            return Clutter.EVENT_PROPAGATE;
        return renderer.pointer.event(renderer.desktopClone, event);
    }

    _detach(renderer) {
        renderer.pointer.close();
        if (renderer.unmanagedId)
            renderer.window.disconnect(renderer.unmanagedId);
        if (renderer.minimizedId)
            renderer.window.disconnect(renderer.minimizedId);
        if (renderer.visibilityId)
            renderer.source.disconnect(renderer.visibilityId);
        renderer.unmanagedId = 0;
        renderer.minimizedId = 0;
        renderer.visibilityId = 0;
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
        for (const thumbnail of this._thumbnails.values()) {
            if (thumbnail.renderer === renderer) {
                thumbnail.clone.destroy();
                for (const background of thumbnail.hiddenBackgrounds)
                    background.show();
                thumbnail.hiddenBackgrounds.clear();
                thumbnail.clone = null;
                thumbnail.renderer = null;
            }
        }
    }

    _stopRenderers() {
        if (this._diagnosticSession && this._renderers.size)
            this._log('stopping renderers');
        for (const renderer of this._renderers) {
            this._detach(renderer);
            renderer.process.send_signal(SIGTERM);
        }
        this._renderers.clear();
        this._updateGazeTimer();
    }
}
