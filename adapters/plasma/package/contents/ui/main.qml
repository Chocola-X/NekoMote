import QtQuick
import org.kde.plasma.plasmoid
import NekoMote.Wallpaper

WallpaperItem {
    id: root
    readonly property string assetsDir: configuration.assetsDir
    readonly property string renderBackend: configuration.renderBackend

    readonly property string panelInstanceId: configuration.panelInstanceId
    property bool configurationReady: false
    property string retainedInstanceId: ""

    // Applying settings can replace/reset the map while this item stays alive.
    // Repair the active map after its dependent bindings have settled.
    function ensureInstanceId() {
        if (configurationReady && panelInstanceId.length === 0) {
            // A map reset is still the same wallpaper instance. Preserve its
            // known identity so applying a backend does not reset panel state.
            configuration.panelInstanceId = retainedInstanceId.length > 0
                ? retainedInstanceId : wallpaper.createInstanceId();
            configuration.writeConfig();
        }
    }
    onPanelInstanceIdChanged: {
        if (panelInstanceId.length > 0)
            retainedInstanceId = panelInstanceId;
        Qt.callLater(ensureInstanceId);
    }
    Component.onCompleted: {
        configurationReady = true;
        if (panelInstanceId.length > 0)
            retainedInstanceId = panelInstanceId;
        ensureInstanceId();
    }

    NekoWallpaperItem {
        id: wallpaper
        objectName: "nativeSession"
        anchors.fill: parent
        instanceId: root.panelInstanceId
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
