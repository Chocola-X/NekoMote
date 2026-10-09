import QtQuick
import org.kde.plasma.plasmoid
import NekoMote.Wallpaper

WallpaperItem {
    id: root
    readonly property string modelFile: configuration.modelFile
    readonly property string sessionFile: configuration.sessionFile
    readonly property real originX: configuration.originX
    readonly property real originY: configuration.originY
    readonly property real modelScale: configuration.scale
    property bool reloadQueued: false

    function reloadSession() {
        if (reloadQueued)
            return;
        reloadQueued = true;
        Qt.callLater(function() {
            sessionLoader.active = false;
            sessionLoader.active = modelFile.length > 0 && sessionFile.length > 0;
            reloadQueued = false;
        });
    }
    onModelFileChanged: reloadSession()
    onSessionFileChanged: reloadSession()
    onOriginXChanged: reloadSession()
    onOriginYChanged: reloadSession()
    onModelScaleChanged: reloadSession()

    Loader {
        id: sessionLoader
        anchors.fill: parent
        active: root.modelFile.length > 0 && root.sessionFile.length > 0
        sourceComponent: NekoWallpaperItem {
            objectName: "nativeSession"
            modelFile: root.modelFile
            sessionFile: root.sessionFile
            originX: root.originX
            originY: root.originY
            scale: root.modelScale
            paused: !root.visible
        }
    }
    Text {
        anchors.centerIn: parent
        width: parent.width
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: "#ff6666"
        text: sessionLoader.item ? sessionLoader.item.errorString : "请选择模型和会话文件"
        visible: !sessionLoader.item || sessionLoader.item.status !== 1
    }
}
