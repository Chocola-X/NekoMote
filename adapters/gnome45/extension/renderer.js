import GLib from 'gi://GLib';

const RENDERER = 'nekomote_gnome45';

export function defaultRendererPath() {
    const installed = GLib.build_filenamev([GLib.get_home_dir(), '.local', 'bin', RENDERER]);
    return GLib.file_test(installed, GLib.FileTest.IS_EXECUTABLE)
        ? installed : GLib.find_program_in_path(RENDERER);
}
