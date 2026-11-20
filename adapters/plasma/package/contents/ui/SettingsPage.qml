import QtQuick 2.12
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.12

ScrollView {
    id: page

    property var configDialog
    property string cfg_assetsDir: ""
    property string cfg_renderBackend: "opengl"
    property bool cfg_pauseOnSessionEvents: true
    property bool cfg_globalGazeEnabled: true

    signal browseRequested()

    implicitWidth: 360
    // Plasma sizes its configuration stack from this hint. Keep a usable viewport
    // even if the outer dialog scrolls, without asking it for the entire form height.
    implicitHeight: Math.min(contentHeight + topPadding + bottomPadding, 160)

    clip: true
    padding: 16
    topPadding: 8
    bottomPadding: 8
    leftPadding: padding + (mirrored ? ScrollBar.vertical.implicitWidth : 0)
    rightPadding: padding + (mirrored ? 0 : ScrollBar.vertical.implicitWidth)
    contentWidth: availableWidth
    contentHeight: form.implicitHeight
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff

    ColumnLayout {
        id: form
        width: page.availableWidth
        spacing: 6

        Label {
            Layout.fillWidth: true
            text: qsTranslate("NekoMoteWallpaperConfig", "Assets Directory")
            font.bold: true
            wrapMode: Text.Wrap
        }
        GridLayout {
            Layout.fillWidth: true
            columns: width >= assetsField.implicitWidth + browseButton.implicitWidth + columnSpacing
                     ? 2 : 1
            columnSpacing: 8
            rowSpacing: 6

            TextField {
                id: assetsField
                Layout.fillWidth: true
                text: page.cfg_assetsDir
                onTextEdited: page.cfg_assetsDir = text
                placeholderText: "file:///…/assets"
            }
            Button {
                id: browseButton
                Layout.alignment: Qt.AlignTrailing
                text: qsTranslate("NekoMoteWallpaperConfig", "Browse…")
                onClicked: page.browseRequested()
            }
        }
        Label {
            text: qsTranslate("NekoMoteWallpaperConfig", "Select the root directory of the NekoMote assets library, i.e. the folder containing the characters subdirectory. Background resources are in the sibling backgrounds directory. Do not select a single character or costume directory.\nAfter applying, characters will be loaded automatically; click the NekoMote control icon on the wallpaper to switch characters and backgrounds, adjust frame rate and sound settings.")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            text: qsTranslate("NekoMoteWallpaperConfig", "Render Backend")
            font.bold: true
            wrapMode: Text.Wrap
        }
        ComboBox {
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            model: [qsTranslate("NekoMoteWallpaperConfig", "OpenGL (default)"), "Vulkan"]
            currentIndex: page.cfg_renderBackend === "vulkan" ? 1 : 0
            onActivated: page.cfg_renderBackend = currentIndex === 1 ? "vulkan" : "opengl"
        }
        Label {
            text: qsTranslate("NekoMoteWallpaperConfig", "Select the graphics API used by NekoMote to render characters and backgrounds. OpenGL is the default option; Vulkan requires GPU image sharing support from the graphics driver.\nAfter applying changes, the wallpaper rendering device will be rebuilt; this does not change the desktop's render backend. If Vulkan fails to load, switch back to OpenGL.")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        SettingsCheckBox {
            Layout.fillWidth: true
            Layout.topMargin: 8
            text: qsTranslate("NekoMoteWallpaperConfig", "Pause on lock, sleep or screen off")
            checked: page.cfg_pauseOnSessionEvents
            onToggled: page.cfg_pauseOnSessionEvents = checked
        }
        SettingsCheckBox {
            Layout.fillWidth: true
            text: qsTranslate("NekoMoteWallpaperConfig", "Follow the cursor outside the wallpaper")
            checked: page.cfg_globalGazeEnabled
            onToggled: page.cfg_globalGazeEnabled = checked
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            text: qsTranslate("NekoMoteWallpaperConfig", "Project & Authors")
            font.bold: true
            wrapMode: Text.Wrap
        }
        Label {
            text: qsTranslate("NekoMoteWallpaperConfig", 'GitHub: <a href="https://github.com/Chocola-X/NekoMote">github.com/Chocola-X/NekoMote</a><br>Website: <a href="https://www.nekopara.uk">www.nekopara.uk</a>')
            textFormat: Text.RichText
            linkColor: palette.link
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            onLinkActivated: Qt.openUrlExternally(link)
        }
    }
}
