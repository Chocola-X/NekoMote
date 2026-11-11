import QtQuick 2.12
import NekoMote.Wallpaper 1.0

Item {
    id: root
    // Plasma 5 injects the wallpaper interface into the package context.
    readonly property string assetsDir: wallpaper.configuration.assetsDir
    readonly property string renderBackend: wallpaper.configuration.renderBackend
    readonly property string panelInstanceId: wallpaper.configuration.panelInstanceId
    property bool configurationReady: false

    // Applying the first settings can replace/reset the configuration map
    // without recreating this item. Re-establish the ID for that map too.
    function ensureInstanceId() {
        if (configurationReady && panelInstanceId.length === 0) {
            wallpaper.configuration.panelInstanceId = nativeSession.createInstanceId();
            // Older Plasma 5 property maps save automatically; newer hosts
            // expose an explicit write method like Plasma 6.
            if (typeof wallpaper.configuration.writeConfig === "function")
                wallpaper.configuration.writeConfig();
        }
    }
    // Let the map's dependent bindings settle before writing back to it.
    onPanelInstanceIdChanged: Qt.callLater(ensureInstanceId)
    Component.onCompleted: {
        configurationReady = true;
        ensureInstanceId();
    }

    NekoWallpaperItem {
        id: nativeSession
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
        text: nativeSession.status >= 2 ? nativeSession.errorString
            : nativeSession.status === 0 ? (root.assetsDir.length === 0
                ? qsTranslate("NekoMoteWallpaperMain", "Please set the Assets directory in the wallpaper configuration") : qsTranslate("NekoMoteWallpaperMain", "Loading…")) : ""
        visible: text.length > 0
    }
}
