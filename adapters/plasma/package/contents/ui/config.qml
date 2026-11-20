import QtQuick
import NekoMote.Wallpaper 1.0
import QtQuick.Dialogs

SettingsPage {
    id: page

    onBrowseRequested: assetsDialog.open()

    FolderDialog {
        id: assetsDialog
        title: qsTranslate("NekoMoteWallpaperConfig", "Select NekoMote assets root directory")
        onAccepted: page.cfg_assetsDir = selectedFolder.toString()
    }
}
