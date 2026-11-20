import QtQuick 2.12
import QtQuick.Controls 2.5

CheckBox {
    id: option

    contentItem: Label {
        readonly property real indicatorSpace: option.indicator.width + option.spacing

        text: option.text
        font: option.font
        leftPadding: option.mirrored ? 0 : indicatorSpace
        rightPadding: option.mirrored ? indicatorSpace : 0
        horizontalAlignment: option.mirrored ? Text.AlignRight : Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: option.palette.highlight
        radius: 2
        visible: option.visualFocus
    }
}
