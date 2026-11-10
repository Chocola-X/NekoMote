import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NekoMote.Wallpaper 1.0
import QtQuick.Dialogs

ColumnLayout {
    property string cfg_assetsDir: ""
    property string cfg_renderBackend: "opengl"

    spacing: 8

    FolderDialog {
        id: assetsDialog
        title: qsTranslate("NekoMoteWallpaperConfig", "Select NekoMote assets root directory")
        onAccepted: cfg_assetsDir = selectedFolder.toString()
    }

    Label { text: qsTranslate("NekoMoteWallpaperConfig", "Assets Directory"); font.bold: true }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            Layout.fillWidth: true
            text: cfg_assetsDir
            onTextEdited: cfg_assetsDir = text
            placeholderText: "file:///…/assets"
        }
        Button {
            text: qsTranslate("NekoMoteWallpaperConfig", "Browse…")
            onClicked: assetsDialog.open()
        }
    }
    Label {
        text: qsTranslate("NekoMoteWallpaperConfig", "Select the root directory of the NekoMote assets library, i.e. the folder containing the characters subdirectory. Background resources are in the sibling backgrounds directory. Do not select a single character or costume directory.\nAfter applying, characters will be loaded automatically; click the NekoMote control icon on the wallpaper to switch characters and backgrounds, adjust frame rate and sound settings.")
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Label { text: qsTranslate("NekoMoteWallpaperConfig", "Render Backend"); font.bold: true }
    ComboBox {
        model: [qsTranslate("NekoMoteWallpaperConfig", "OpenGL (default)"), "Vulkan"]
        currentIndex: cfg_renderBackend === "vulkan" ? 1 : 0
        onActivated: cfg_renderBackend = currentIndex === 1 ? "vulkan" : "opengl"
    }
    Label {
        text: qsTranslate("NekoMoteWallpaperConfig", "Select the graphics API used by NekoMote to render characters and backgrounds. OpenGL is the default option; Vulkan requires GPU image sharing support from the graphics driver.\nAfter applying changes, the wallpaper rendering device will be rebuilt; this does not change the desktop's render backend. If Vulkan fails to load, switch back to OpenGL.")
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Label {
        text: qsTranslate("NekoMoteWallpaperConfig", "Project & Authors")
        font.bold: true
        Layout.topMargin: 8
    }
    Label {
        text: qsTranslate("NekoMoteWallpaperConfig", 'GitHub: <a href="https://github.com/Chocola-X/NekoMote">github.com/Chocola-X/NekoMote</a><br>Website: <a href="https://www.nekopara.uk">www.nekopara.uk</a>')
        textFormat: Text.RichText
        linkColor: palette.link
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        onLinkActivated: Qt.openUrlExternally(link)
    }
    Item { Layout.fillHeight: true }
}
