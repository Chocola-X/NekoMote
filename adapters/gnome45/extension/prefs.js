import Adw from 'gi://Adw';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk';
import {ExtensionPreferences, gettext as _} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';
import {defaultDiagnosticDirectory} from './diagnostics.js';
import {defaultRendererPath} from './renderer.js';

function addPath(window, group, settings, key, title, action, description, defaultPath = () => '') {
    const value = () => settings.get_string(key) || defaultPath() || '';
    const row = new Adw.EntryRow({title, text: value(), show_apply_button: true});
    row.connect('apply', () => {
        settings.set_string(key, row.text.trim());
        row.text = value();
    });
    const browse = new Gtk.Button({
        icon_name: 'folder-open-symbolic',
        tooltip_text: _('Browse…'),
        valign: Gtk.Align.CENTER,
    });
    browse.connect('clicked', () => {
        const chooser = new Gtk.FileChooserNative({
            title, action, transient_for: window, modal: true,
        });
        const path = row.text.trim();
        if (GLib.path_is_absolute(path)) {
            const file = Gio.File.new_for_path(path);
            const folder = action === Gtk.FileChooserAction.SELECT_FOLDER ? file : file.get_parent();
            if (folder?.query_exists(null))
                chooser.set_current_folder(folder);
        }
        chooser.connect('response', (_, response) => {
            if (response === Gtk.ResponseType.ACCEPT) {
                const selected = chooser.get_file()?.get_path();
                if (selected) {
                    row.text = selected;
                    settings.set_string(key, selected);
                }
            }
            chooser.destroy();
        });
        chooser.show();
    });
    row.add_suffix(browse);
    group.add(row);
    if (description)
        addDescription(group, description);
    return row;
}

function addDescription(group, description) {
    group.add(new Gtk.Label({
        label: description, wrap: true, xalign: 0,
        margin_start: 12, margin_end: 12, margin_top: 8, margin_bottom: 8,
    }));
}

function addToggle(group, settings, key, title, subtitle) {
    const row = new Adw.SwitchRow({title, subtitle});
    settings.bind(key, row, 'active', Gio.SettingsBindFlags.DEFAULT);
    group.add(row);
}

export default class NekoMotePreferences extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        const settings = this.getSettings();
        const page = new Adw.PreferencesPage();
        window.add(page);

        const paths = new Adw.PreferencesGroup({title: _('Wallpaper files')});
        page.add(paths);
        addPath(window, paths, settings, 'assets-dir', _('Assets directory'),
            Gtk.FileChooserAction.SELECT_FOLDER,
            _('Choose the library root containing characters/ and backgrounds/. Use the wallpaper’s control button to change characters, backgrounds, frame rate and sound.'));
        addPath(window, paths, settings, 'renderer-path', _('Renderer executable'),
            Gtk.FileChooserAction.OPEN,
            _('Choose the installed nekomote_gnome45 from the same release as this extension. An empty field uses the standard installation path or PATH.'),
            defaultRendererPath);

        const graphics = new Adw.PreferencesGroup({title: _('Rendering')});
        page.add(graphics);
        const backend = new Adw.ComboRow({
            title: _('Rendering backend'),
            model: Gtk.StringList.new(['OpenGL', 'Vulkan']),
            selected: settings.get_string('backend') === 'vulkan' ? 1 : 0,
        });
        backend.connect('notify::selected', () => {
            settings.set_string('backend', backend.selected === 1 ? 'vulkan' : 'opengl');
        });
        graphics.add(backend);
        addDescription(graphics,
            _('OpenGL is the default and affects NekoMote only. If Vulkan fails to start, switch back to OpenGL.'));

        const playback = new Adw.PreferencesGroup({title: _('Playback and interaction')});
        page.add(playback);
        addToggle(playback, settings, 'pause-session', _('Pause on lock, sleep or screen off'),
            _('Use system session events to pause playback and sound.'));
        addToggle(playback, settings, 'pause-fullscreen', _('Pause behind fullscreen windows'),
            _('Pauses on the covered monitor. Activities previews keep playing.'));
        addToggle(playback, settings, 'global-gaze', _('Follow the pointer outside the desktop'),
            _('Updates the character’s gaze across windows and monitors. Clicks and dragging work on the desktop.'));

        const diagnostics = new Adw.PreferencesGroup({title: _('Diagnostics')});
        page.add(diagnostics);
        addToggle(diagnostics, settings, 'diagnostic-logging', _('Save diagnostic logs'),
            _('Record extension events and renderer output. Clear the directory field to restore the default.'));
        const logDirectory = addPath(window, diagnostics, settings, 'diagnostic-directory',
            _('Log directory'), Gtk.FileChooserAction.SELECT_FOLDER, null, defaultDiagnosticDirectory);
        settings.bind('diagnostic-logging', logDirectory, 'visible', Gio.SettingsBindFlags.GET);

        const project = new Adw.PreferencesGroup({title: _('Project and authors')});
        page.add(project);
        for (const [title, uri] of [
            [_('NekoMote on GitHub'), 'https://github.com/Chocola-X/NekoMote'],
            [_('Project website'), 'https://www.nekopara.uk'],
        ]) {
            const row = new Adw.ActionRow({title});
            row.add_suffix(new Gtk.LinkButton({uri, label: _('Open'), valign: Gtk.Align.CENTER}));
            project.add(row);
        }
    }
}
