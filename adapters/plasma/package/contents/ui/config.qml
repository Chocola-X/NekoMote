import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    property string cfg_assetsDir: ""

    spacing: 8

    FolderDialog {
        id: assetsDialog
        title: "选择 assets 目录"
        onAccepted: cfg_assetsDir = selectedFolder.toString()
    }

    Label { text: "Assets 目录"; font.bold: true }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            Layout.fillWidth: true
            text: cfg_assetsDir
            onTextEdited: cfg_assetsDir = text
            placeholderText: "file:///…/assets"
        }
        Button {
            text: "浏览…"
            onClicked: assetsDialog.open()
        }
    }
    Label {
        text: "设置后壁纸会自动加载角色。\n运行时点击右上角图标可切换角色、背景和调整设置。"
        color: "#666666"
        font.pixelSize: 11
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
