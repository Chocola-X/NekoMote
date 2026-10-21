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
        title: "选择 NekoMote 资产根目录"
        selectFolder: true
        onAccepted: cfg_assetsDir = fileUrl.toString()
    }

    Label { text: "资产目录"; font.bold: true }
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
        text: "选择 NekoMote 资产库的根目录，即包含 characters 子目录的文件夹，"
              + "背景资源位于同级的 backgrounds 中。请勿选择单个角色或服装目录。\n"
              + "应用后会自动加载角色；点击壁纸上的 NekoMote 控制图标，"
              + "可以切换角色和背景、调整帧率与声音等设置。"
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
        text: "选择 NekoMote 绘制角色和背景所使用的图形接口。OpenGL 为默认选项；"
              + "Vulkan 需要显卡驱动支持 GPU 图像共享。\n"
              + "应用更改后会重建壁纸渲染设备，不会更改整个桌面的渲染后端。"
              + "如果 Vulkan 加载失败，请切回 OpenGL。"
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Label {
        text: "项目与作者"
        font.bold: true
        Layout.topMargin: 8
    }
    Label {
        text: 'GitHub：<a href="https://github.com/Chocola-X/NekoMote">'
              + 'github.com/Chocola-X/NekoMote</a><br>'
              + '个人网站：<a href="https://www.nekopara.uk">www.nekopara.uk</a>'
        textFormat: Text.RichText
        linkColor: palette.link
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        onLinkActivated: Qt.openUrlExternally(link)
    }
    Item { Layout.fillHeight: true }
}
