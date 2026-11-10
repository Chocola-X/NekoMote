import QtQuick
import org.kde.plasma.plasmoid
import NekoMote.Wallpaper

WallpaperItem {
    id: root
    readonly property string assetsDir: configuration.assetsDir
    readonly property string renderBackend: configuration.renderBackend

    Component.onCompleted: {
        if (configuration.panelInstanceId.length === 0) {
            configuration.panelInstanceId = wallpaper.createInstanceId();
            configuration.writeConfig();
        }
    }

    NekoWallpaperItem {
        id: wallpaper
        objectName: "nativeSession"
        anchors.fill: parent
        instanceId: configuration.panelInstanceId
        // Do not load the shared default panel file before the persisted ID arrives.
        assetsDir: instanceId.length > 0 ? root.assetsDir : ""
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
                return qsTranslate("NekoMoteWallpaperMain", "Please set the Assets directory in the wallpaper configuration");
            if (wallpaper.status === 0)
                return qsTranslate("NekoMoteWallpaperMain", "Loading…");
            return "";
        }
        visible: text.length > 0
    }
}
