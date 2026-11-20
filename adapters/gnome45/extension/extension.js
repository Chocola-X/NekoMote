import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

const RENDERER = 'nekomote_gnome45';
const SIGTERM = 15;

export default class NekoMoteExtension extends Extension {
    enable() {
        if (!Meta.is_wayland_compositor())
            throw new Error('NekoMote requires a GNOME Wayland session');

        this._renderers = new Set();
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
        this._stopRenderers();
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
            '--input', 'passthrough', '--global-gaze', 'off',
        ];
        if (monitor.index !== Main.layoutManager.primaryIndex)
            argv.push('--mute');

        const launcher = new Gio.SubprocessLauncher({flags: Gio.SubprocessFlags.NONE});
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

        const renderer = {client, process, monitor, clone: null, window: null, unmanagedId: 0};
        this._renderers.add(renderer);
        process.wait_async(null, (child, result) => {
            child.wait_finish(result);
            if (this._renderers?.delete(renderer)) {
                this._detach(renderer);
                console.warn('NekoMote: renderer exited, showing the static background');
            }
        });
    }

    _attach(actor) {
        const window = actor.get_meta_window();
        const renderer = [...this._renderers].find(({client}) => client.owns_window(window));
        if (!renderer || renderer.clone)
            return;
        const {monitor} = renderer;
        try {
            renderer.window = window;
            renderer.unmanagedId = window.connect('unmanaged', () => this._detach(renderer));
            window.move_to_monitor(monitor.index);
            window.minimize();
            renderer.client.hide_from_window_list?.(window);
            renderer.clone = new Clutter.Clone({
                source: actor,
                x: monitor.x, y: monitor.y, width: monitor.width, height: monitor.height,
                reactive: false,
                clip_to_allocation: true,
            });
            Main.layoutManager._backgroundGroup.add_child(renderer.clone);
        } catch (error) {
            this._detach(renderer);
            renderer.process.send_signal(SIGTERM);
            console.error(`NekoMote: could not attach renderer: ${error.message}`);
        }
    }

    _detach(renderer) {
        if (renderer.unmanagedId)
            renderer.window.disconnect(renderer.unmanagedId);
        renderer.unmanagedId = 0;
        renderer.window = null;
        renderer.clone?.destroy();
        renderer.clone = null;
    }

    _stopRenderers() {
        for (const renderer of this._renderers) {
            this._detach(renderer);
            renderer.process.send_signal(SIGTERM);
        }
        this._renderers.clear();
    }
}
