import QtQuick 2.12
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.12
import QtQuick.Dialogs 1.3

ColumnLayout {
    property string cfg_assetsDir: ""
    property string cfg_renderBackend: "opengl"
    spacing: 8

    FileDialog {
        id: assetsDialog
        title: "选择 assets 目录"
        selectFolder: true
        onAccepted: cfg_assetsDir = fileUrl.toString()
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
        Button { text: "浏览…"; onClicked: assetsDialog.open() }
    }
    Label {
        text: "运行时点击右上角图标可切换角色、背景和调整设置。"
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Label { text: "渲染后端"; font.bold: true }
    ComboBox {
        model: ["OpenGL（默认）", "Vulkan"]
        currentIndex: cfg_renderBackend === "vulkan" ? 1 : 0
        onActivated: cfg_renderBackend = currentIndex === 1 ? "vulkan" : "opengl"
    }
    Label {
        text: "Vulkan 需要驱动支持 GPU 图像共享；加载失败时可改回 OpenGL。"
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
