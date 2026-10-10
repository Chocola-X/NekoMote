import QtQuick
import QtQuick.Controls
import org.kde.plasma.plasmoid
import NekoMote.Wallpaper

WallpaperItem {
    id: root
    readonly property string assetsDir: configuration.assetsDir
    property string modelFile: configuration.modelFile
    property string sessionFile: configuration.sessionFile
    property real originX: configuration.originX
    property real originY: configuration.originY
    property real modelScale: configuration.scale
    property bool configurationReady: false
    property bool reloadQueued: false
    Component.onCompleted: configurationReady = true

    property bool panelOpen: false
    property bool userPaused: false
    property var characterList: []
    property var backgroundList: []
    property string currentBackground: ""

    function reloadSession() {
        if (!configurationReady || reloadQueued)
            return;
        reloadQueued = true;
        Qt.callLater(function() {
            sessionLoader.active = false;
            sessionLoader.active = true;
            reloadQueued = false;
        });
    }
    onModelFileChanged: reloadSession()
    onSessionFileChanged: reloadSession()
    onOriginXChanged: reloadSession()
    onOriginYChanged: reloadSession()
    onModelScaleChanged: reloadSession()

    function rescanCharacters() {
        if (!sessionLoader.item || assetsDir.length === 0) {
            characterList = [];
            backgroundList = [];
            return;
        }
        characterList = sessionLoader.item.scanCharacters();
        backgroundList = sessionLoader.item.scanBackgrounds();
        if (characterList.length > 0 && modelFile.length === 0) {
            configuration.modelFile = characterList[0].modelUrl;
            configuration.sessionFile = characterList[0].sessionUrl;
        }
    }

    function switchCharacter(modelUrl, sessionUrl) {
        configuration.modelFile = modelUrl;
        configuration.sessionFile = sessionUrl;
    }

    function switchBackground(bgUrl) {
        if (sessionLoader.item) {
            if (sessionLoader.item.setBackground(bgUrl))
                currentBackground = bgUrl;
        }
    }

    function clearBg() {
        if (sessionLoader.item) {
            sessionLoader.item.clearBackground();
            currentBackground = "";
        }
    }

    Loader {
        id: sessionLoader
        anchors.fill: parent
        active: true
        sourceComponent: NekoWallpaperItem {
            objectName: "nativeSession"
            modelFile: root.modelFile
            sessionFile: root.sessionFile
            originX: root.originX
            originY: root.originY
            scale: root.modelScale
            assetsDir: root.assetsDir
            paused: !root.visible || root.panelOpen || root.userPaused
        }
        onLoaded: root.rescanCharacters()
    }

    Connections {
        target: sessionLoader.item
        function onAssetsDirChanged() { root.rescanCharacters() }
    }

    Text {
        anchors.centerIn: parent
        width: parent.width
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: "#ff6666"
        text: {
            if (!sessionLoader.item) return "";
            if (sessionLoader.item.status === 3) return sessionLoader.item.errorString;
            if (sessionLoader.item.status === 2) return sessionLoader.item.errorString;
            if (sessionLoader.item.status === 0 && assetsDir.length === 0)
                return "请在壁纸配置中设置 Assets 目录";
            if (sessionLoader.item.status === 0)
                return "正在加载…";
            return "";
        }
        visible: text.length > 0
    }

    Rectangle {
        id: toggleButton
        width: 44; height: 44; radius: 22
        color: toggleMA.containsMouse ? "#e0e0e0" : "#c0c0c0"
        opacity: 0.85
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.rightMargin: 16; anchors.bottomMargin: 16
        visible: sessionLoader.item && sessionLoader.item.status === 1
        Text {
            anchors.centerIn: parent
            text: root.panelOpen ? "\u2715" : "\u2630"
            font.pixelSize: 20; color: "#333333"
        }
        MouseArea {
            id: toggleMA
            anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.panelOpen = !root.panelOpen
        }
    }

    Rectangle {
        id: panel
        width: 320; height: parent.height
        anchors.right: parent.right
        color: "#1e1e2e"; opacity: 0.92
        visible: root.panelOpen; z: 10

        ScrollView {
            anchors.fill: parent; anchors.margins: 12; clip: true
            Column {
                width: panel.width - 24; spacing: 10

                Text {
                    text: "NekoMote 角色选择"
                    color: "#f2a0c0"; font.pixelSize: 16; font.bold: true
                }
                Text {
                    text: "共 " + characterList.length + " 个角色"
                    color: "#888888"; font.pixelSize: 11
                    visible: characterList.length > 0
                }

                Repeater {
                    model: {
                        if (characterList.length === 0) return [];
                        var groups = {}; var order = [];
                        for (var i = 0; i < characterList.length; i++) {
                            var g = characterList[i].group;
                            if (!groups[g]) { groups[g] = []; order.push(g); }
                            groups[g].push(characterList[i]);
                        }
                        var res = [];
                        for (var j = 0; j < order.length; j++)
                            res.push({groupName: order[j], chars: groups[order[j]]});
                        return res;
                    }
                    delegate: Column {
                        width: parent.width; spacing: 4
                        Text {
                            text: modelData.groupName
                            color: "#a0a0b0"; font.pixelSize: 12; font.bold: true
                            topPadding: 8; bottomPadding: 2
                        }
                        Repeater {
                            model: modelData.chars
                            delegate: Rectangle {
                                width: parent.width; height: 32; radius: 6
                                color: charMA.containsMouse ? "#313148" :
                                       (root.modelFile === modelData.modelUrl ? "#2a2a3e" : "transparent")
                                border.color: root.modelFile === modelData.modelUrl ? "#f2a0c0" : "transparent"
                                border.width: 1
                                Text {
                                    anchors.left: parent.left; anchors.leftMargin: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.name
                                    color: root.modelFile === modelData.modelUrl ? "#f2a0c0" : "#d0d0e0"
                                    font.pixelSize: 13; elide: Text.ElideRight
                                    width: parent.width - 24
                                }
                                MouseArea {
                                    id: charMA; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.switchCharacter(modelData.modelUrl, modelData.sessionUrl)
                                }
                            }
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: "#333348"; visible: characterList.length > 0 }

                Text {
                    text: "背景"; color: "#f2a0c0"; font.pixelSize: 14; font.bold: true
                    topPadding: 8; visible: characterList.length > 0
                }
                Repeater {
                    model: backgroundList
                    delegate: Rectangle {
                        width: parent.width; height: 28; radius: 4
                        color: bgMA.containsMouse ? "#313148" :
                               (root.currentBackground === modelData.url ? "#2a2a3e" : "transparent")
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name; color: "#d0d0e0"; font.pixelSize: 12
                        }
                        MouseArea {
                            id: bgMA; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.switchBackground(modelData.url)
                        }
                    }
                }
                Button {
                    text: "清除背景"; flat: true; visible: backgroundList.length > 0
                    onClicked: root.clearBg()
                }

                Rectangle { width: parent.width; height: 1; color: "#333348"; visible: characterList.length > 0 }

                Text {
                    text: "设置"; color: "#f2a0c0"; font.pixelSize: 14; font.bold: true
                    topPadding: 8; visible: characterList.length > 0
                }
                Row {
                    spacing: 8; visible: characterList.length > 0
                    Button {
                        text: root.userPaused ? "\u25B6 播放" : "\u23F8 暂停"; flat: true
                        onClicked: root.userPaused = !root.userPaused
                    }
                    Button {
                        text: "\u21BB 重置"; flat: true
                        onClicked: {
                            configuration.originX = 0;
                            configuration.originY = 400;
                            configuration.scale = 1;
                        }
                    }
                }
                Text { text: "缩放"; color: "#a0a0b0"; font.pixelSize: 12; topPadding: 8; visible: characterList.length > 0 }
                Slider {
                    width: parent.width - 20; from: 0.1; to: 3.0
                    value: root.modelScale; stepSize: 0.05
                    visible: characterList.length > 0
                    onValueChanged: configuration.scale = value
                }
                Text {
                    text: Math.round(root.modelScale * 100) + "%"
                    color: "#888888"; font.pixelSize: 11; visible: characterList.length > 0
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent; visible: root.panelOpen; z: 5
        onClicked: root.panelOpen = false
    }
}
