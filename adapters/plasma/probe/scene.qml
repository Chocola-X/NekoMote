import QtQuick
import NekoMote.Wallpaper

Item {
    id: root

    property string assetsDir: ""
    property bool paused: false
    property string renderBackend: "opengl"
    readonly property int status: wallpaper.status
    readonly property string errorString: wallpaper.errorString

    NekoWallpaperItem {
        id: wallpaper
        objectName: "wallpaper"
        anchors.fill: parent
        assetsDir: root.assetsDir
        paused: root.paused
        renderBackend: root.renderBackend
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: wallpaper.errorString
        color: "red"
        visible: wallpaper.status === 2
    }
}
