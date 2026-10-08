import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    property string cfg_modelFile: ""
    property string cfg_sessionFile: ""
    property real cfg_originX: 0
    property real cfg_originY: 400
    property real cfg_scale: 1

    TextField {
        Layout.fillWidth: true
        text: cfg_modelFile
        onTextEdited: cfg_modelFile = text
        placeholderText: "model file path"
    }
    TextField {
        Layout.fillWidth: true
        text: cfg_sessionFile
        onTextEdited: cfg_sessionFile = text
        placeholderText: "session file path"
    }
    SpinBox {
        value: Math.round(cfg_originX)
        onValueModified: cfg_originX = value
    }
    SpinBox {
        value: Math.round(cfg_originY)
        onValueModified: cfg_originY = value
    }
    SpinBox {
        value: Math.round(cfg_scale * 100)
        onValueModified: cfg_scale = value / 100.0
        from: 10
        to: 300
    }
}
