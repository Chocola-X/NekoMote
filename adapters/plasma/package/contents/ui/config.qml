import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    property string cfg_modelFile: ""
    property string cfg_sessionFile: ""
    property real cfg_originX: 0
    property real cfg_originY: 400
    property real cfg_scale: 1

    spacing: 8

    FileDialog {
        id: modelDialog
        title: "选择模型文件"
        nameFilters: ["NekoMote 模型 (*.nkm.json)", "所有文件 (*)"]
        onAccepted: cfg_modelFile = selectedFile.toString()
    }
    FileDialog {
        id: sessionDialog
        title: "选择会话文件"
        nameFilters: ["NekoMote 会话 (*.nks.json)", "所有文件 (*)"]
        onAccepted: cfg_sessionFile = selectedFile.toString()
    }

    Label { text: "模型文件"; font.bold: true }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            Layout.fillWidth: true
            text: cfg_modelFile
            onTextEdited: cfg_modelFile = text
            placeholderText: "file:///…/model.nkm.json"
            readOnly: false
        }
        Button {
            text: "浏览…"
            onClicked: modelDialog.open()
        }
    }
    Label {
        text: cfg_modelFile.length > 0 && !cfg_modelFile.startsWith("file:///")
              ? "⚠ URL 应以 file:/// 开头" : ""
        color: "#aa6600"
        visible: text.length > 0
    }

    Label { text: "会话文件"; font.bold: true }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            Layout.fillWidth: true
            text: cfg_sessionFile
            onTextEdited: cfg_sessionFile = text
            placeholderText: "file:///…/session.nks.json"
        }
        Button {
            text: "浏览…"
            onClicked: sessionDialog.open()
        }
    }
    Label {
        text: cfg_sessionFile.length > 0 && !cfg_sessionFile.startsWith("file:///")
              ? "⚠ URL 应以 file:/// 开头" : ""
        color: "#aa6600"
        visible: text.length > 0
    }

    Label { text: "位置与缩放"; font.bold: true }
    RowLayout {
        Label { text: "X:" }
        SpinBox {
            from: -8192; to: 8192
            value: Math.round(cfg_originX)
            onValueModified: cfg_originX = value
        }
        Label { text: "Y:" }
        SpinBox {
            from: -8192; to: 8192
            value: Math.round(cfg_originY)
            onValueModified: cfg_originY = value
        }
    }
    RowLayout {
        Label { text: "缩放:" }
        SpinBox {
            from: 10; to: 300
            value: Math.round(cfg_scale * 100)
            onValueModified: cfg_scale = value / 100.0
        }
        Label { text: "%" }
    }
}
