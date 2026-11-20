import QtQuick 2.12
import NekoMote.Wallpaper 1.0
import QtQuick.Dialogs 1.3

SettingsPage {
    id: page

    onBrowseRequested: assetsDialog.open()

    FileDialog {
        id: assetsDialog
        title: qsTranslate("NekoMoteWallpaperConfig", "Select NekoMote assets root directory")
        selectFolder: true
        onAccepted: page.cfg_assetsDir = fileUrl.toString()
    }
}
