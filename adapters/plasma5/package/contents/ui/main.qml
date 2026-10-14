import QtQuick 2.12
import NekoMote.Wallpaper 1.0

Item {
    id: root
    // Plasma 5 injects the wallpaper interface into the package context.
    readonly property string assetsDir: wallpaper.configuration.assetsDir
    readonly property string renderBackend: wallpaper.configuration.renderBackend

    NekoWallpaperItem {
        id: nativeSession
        objectName: "nativeSession"
        anchors.fill: parent
        assetsDir: root.assetsDir
        renderBackend: root.renderBackend
        paused: !root.visible
    }
    Text {
        anchors.centerIn: parent
        width: parent.width
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: "#ff6666"
        text: nativeSession.status >= 2 ? nativeSession.errorString
            : nativeSession.status === 0 ? (root.assetsDir.length === 0
                ? "请在壁纸配置中设置 Assets 目录" : "正在加载…") : ""
        visible: text.length > 0
    }
}
