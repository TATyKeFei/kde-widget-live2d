import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami

PlasmoidItem {
    id: root
    property var character: null
    implicitWidth: 300
    implicitHeight: 420
    Layout.minimumWidth: 100
    Layout.minimumHeight: 140
    preferredRepresentation: fullRepresentation
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground

    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: Plasmoid.configuration.running ? i18n("暂停动画") : i18n("继续动画")
            icon.name: Plasmoid.configuration.running ? "media-playback-pause" : "media-playback-start"
            onTriggered: Plasmoid.configuration.running = !Plasmoid.configuration.running
        },
        PlasmaCore.Action {
            text: i18n("随机表情")
            icon.name: "face-smile"
            enabled: root.character !== null && root.character.expressions.length > 0
            onTriggered: {
                const item = root.character;
                item.setExpression(item.expressions[Math.floor(Math.random() * item.expressions.length)]);
            }
        },
        PlasmaCore.Action {
            text: i18n("重新加载模型")
            icon.name: "view-refresh"
            enabled: root.character !== null
            onTriggered: root.character.reload()
        }
    ]

    fullRepresentation: Item {
        Loader {
            id: modelLoader
            anchors.fill: parent
            source: "Live2DView.qml"
            onLoaded: root.character = item
            Component.onDestruction: root.character = null
        }
        QQC2.Label {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: text.length > 0
            text: modelLoader.item ? modelLoader.item.trackingError : ""
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            padding: Kirigami.Units.smallSpacing
            background: Rectangle { color: Kirigami.Theme.backgroundColor; opacity: 0.9 }
        }
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.max(0, parent.width - 24)
            spacing: Kirigami.Units.smallSpacing
            visible: modelLoader.status === Loader.Error || !modelLoader.item || !modelLoader.item.ready
            Kirigami.Icon {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Kirigami.Units.iconSizes.large
                implicitHeight: implicitWidth
                source: "face-smile"
            }
            QQC2.Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: {
                    if (modelLoader.status === Loader.Error)
                        return i18n("无法加载 Live2D 组件，请检查原生插件是否已安装。");
                    if (modelLoader.item && modelLoader.item.errorString)
                        return modelLoader.item.errorString;
                    return Plasmoid.configuration.modelPath ? i18n("正在加载角色…") : i18n("让角色住进桌面");
                }
            }
            QQC2.Button {
                Layout.alignment: Qt.AlignHCenter
                text: i18n("选择角色…")
                icon.name: "configure"
                onClicked: Plasmoid.internalAction("configure").trigger()
            }
        }
    }
}
