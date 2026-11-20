import Adw from 'gi://Adw';
import Gtk from 'gi://Gtk';
import {ExtensionPreferences} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

export default class NekoMotePreferences extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        const settings = this.getSettings();
        const page = new Adw.PreferencesPage();
        const group = new Adw.PreferencesGroup({title: 'NekoMote'});
        page.add(group);
        window.add(page);

        for (const [key, title] of [
            ['assets-dir', 'Assets directory (absolute path)'],
            ['renderer-path', 'Renderer executable (optional absolute path)'],
        ]) {
            const row = new Adw.EntryRow({
                title,
                text: settings.get_string(key),
                show_apply_button: true,
            });
            row.connect('apply', () => settings.set_string(key, row.text.trim()));
            group.add(row);
        }

        const backend = new Adw.ComboRow({
            title: 'Rendering backend',
            model: Gtk.StringList.new(['OpenGL', 'Vulkan']),
            selected: settings.get_string('backend') === 'vulkan' ? 1 : 0,
        });
        backend.connect('notify::selected', () => {
            settings.set_string('backend', backend.selected === 1 ? 'vulkan' : 'opengl');
        });
        group.add(backend);
    }
}
