import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

export function defaultDiagnosticDirectory() {
    return GLib.build_filenamev([GLib.get_user_state_dir(), 'nekomote', 'gnome45', 'diagnostics']);
}

export function createDiagnosticSession(path) {
    const directory = path || defaultDiagnosticDirectory();
    if (!GLib.path_is_absolute(directory))
        throw new Error('The diagnostic log directory must be absolute');
    // A private, unique directory keeps previous logs and isolates monitor filenames.
    const session = GLib.build_filenamev([directory, `session-${GLib.uuid_string_random()}`]);
    if (GLib.mkdir_with_parents(session, 0o700) !== 0)
        throw new Error(`Cannot create diagnostic session in: ${directory}`);
    return session;
}

export function appendDiagnostic(session, message) {
    const stamp = GLib.DateTime.new_now_local().format_iso8601();
    const file = Gio.File.new_for_path(GLib.build_filenamev([session, 'extension.log']));
    const stream = file.append_to(Gio.FileCreateFlags.PRIVATE, null);
    try {
        stream.write_all(new TextEncoder().encode(`${stamp} ${message}\n`), null);
    } finally {
        stream.close(null);
    }
}
