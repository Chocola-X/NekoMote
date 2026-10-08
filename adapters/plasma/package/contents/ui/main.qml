import QtQuick
import NekoMote.Wallpaper

Item {
    id: root

    property string modelFile: ""
    property string sessionFile: ""
    property real originX: 0
    property real originY: 400
    property real scale: 1
    property bool paused: false
    readonly property int status: wallpaper.status
    readonly property string errorString: wallpaper.errorString

    NekoWallpaperItem {
        id: wallpaper
        objectName: "wallpaper"
        anchors.fill: parent
        modelFile: root.modelFile
        sessionFile: root.sessionFile
        originX: root.originX
        originY: root.originY
        scale: root.scale
        paused: root.paused
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: wallpaper.errorString
        color: "red"
        visible: wallpaper.status === 2
    }
}
