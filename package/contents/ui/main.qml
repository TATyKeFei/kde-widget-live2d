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

    DesktopInputGate {
        targetItem: root
        active: Plasmoid.configuration.clickThrough && !Plasmoid.configuration.keepAbove
        editing: Plasmoid.containment.corona.editMode
    }

    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: i18n("始终置顶")
            icon.name: "window-pin"
            checkable: true
            checked: Plasmoid.configuration.keepAbove
            onTriggered: Plasmoid.configuration.keepAbove = !Plasmoid.configuration.keepAbove
        },
        PlasmaCore.Action {
            text: i18n("鼠标穿透")
            icon.name: "input-mouse"
            checkable: true
            checked: Plasmoid.configuration.clickThrough
            onTriggered: Plasmoid.configuration.clickThrough = !Plasmoid.configuration.clickThrough
        },
        PlasmaCore.Action {
            text: Plasmoid.configuration.running ? i18n("暂停动画") : i18n("继续动画")
            icon.name: Plasmoid.configuration.running ? "media-playback-pause" : "media-playback-start"
            onTriggered: Plasmoid.configuration.running = !Plasmoid.configuration.running
        },
        PlasmaCore.Action {
            text: i18n("随机动作")
            icon.name: "media-playback-start"
            enabled: root.character !== null && root.character.motionGroups.length > 0
            onTriggered: {
                const item = root.character;
                item.playMotion(item.motionGroups[Math.floor(Math.random() * item.motionGroups.length)]);
            }
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
        id: representation
        readonly property var currentCharacter: floatingLoader.item ? floatingLoader.item.character : modelLoader.item
        onCurrentCharacterChanged: root.character = currentCharacter
        Component.onDestruction: root.character = null
        Loader {
            id: floatingLoader
            active: Plasmoid.configuration.keepAbove
            source: "FloatingCharacter.qml"
            onLoaded: {
                item.configuration = Plasmoid.configuration;
                item.anchorItem = representation;
                item.width = Qt.binding(() => representation.width);
                item.height = Qt.binding(() => representation.height);
                item.showAtAnchor();
            }
        }
        Connections {
            target: floatingLoader.item
            function onReturnToDesktop() { Plasmoid.configuration.keepAbove = false; }
            function onConfigureRequested() { Plasmoid.internalAction("configure").trigger(); }
        }
        Loader {
            id: modelLoader
            anchors.fill: parent
            active: !Plasmoid.configuration.keepAbove
            source: "Live2DView.qml"
            onLoaded: item.localInputEnabled = Qt.binding(() => !Plasmoid.configuration.clickThrough)
        }
        QQC2.Button {
            anchors.centerIn: parent
            visible: Plasmoid.configuration.keepAbove
            text: i18n("角色置顶中 · 返回桌面")
            icon.name: "window-unpin"
            onClicked: Plasmoid.configuration.keepAbove = false
        }
        QQC2.Label {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: text.length > 0
            text: modelLoader.item ? [modelLoader.item.trackingError, modelLoader.item.inputError].filter(Boolean).join("\n") : ""
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            padding: Kirigami.Units.smallSpacing
            background: Rectangle { color: Kirigami.Theme.backgroundColor; opacity: 0.9 }
        }
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.max(0, parent.width - 24)
            spacing: Kirigami.Units.smallSpacing
            visible: !Plasmoid.configuration.keepAbove && (modelLoader.status === Loader.Error || !modelLoader.item || !modelLoader.item.ready)
            Kirigami.Icon {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Kirigami.Units.iconSizes.large
                implicitHeight: implicitWidth
                source: "org.kde.plasma.live2d"
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
