import QtQuick
import org.kde.plasma.plasmoid
import NekoMote.Wallpaper

WallpaperItem {
    id: root
    readonly property string assetsDir: configuration.assetsDir
    readonly property bool muted: configuration.muted
    readonly property string renderBackend: configuration.renderBackend

    NekoWallpaperItem {
        id: wallpaper
        objectName: "nativeSession"
        anchors.fill: parent
        assetsDir: root.assetsDir
        muted: root.muted
        renderBackend: root.renderBackend
        paused: !root.visible
    }

    Text {
        anchors.centerIn: parent
        width: parent.width
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: "#ff6666"
        text: {
            if (wallpaper.status === 3) return wallpaper.errorString;
            if (wallpaper.status === 2) return wallpaper.errorString;
            if (wallpaper.status === 0 && root.assetsDir.length === 0)
                return "请在壁纸配置中设置 Assets 目录";
            if (wallpaper.status === 0)
                return "正在加载…";
            return "";
        }
        visible: text.length > 0
    }
}
