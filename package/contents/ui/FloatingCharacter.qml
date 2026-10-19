import QtQuick
import QtQuick.Controls as QQC2
import org.kde.plasma.live2d 1.0

Live2DWindow {
    id: root
    property var configuration: ({modelPath: "", renderBackend: "auto", running: true, frameRate: 30,
                                  zoom: 1, cursorFollowMode: "off", followCursor: false,
                                  mouseTrackingMode: "screen", relativeSensitivity: 1,
                                  idleGroup: "Idle", inputMode: "focused", clickThrough: false})
    property alias character: model
    signal configureRequested()
    title: i18n("Live2D 桌面伙伴")
    visible: false
    clickThrough: configuration.clickThrough || false

    Live2DView {
        id: model
        anchors.fill: parent
        configuration: root.configuration
        localInputEnabled: !root.clickThrough
    }

    onClickThroughChanged: {
        if (clickThrough) menu.close();
    }

    HoverHandler { id: hover; enabled: !root.clickThrough }
    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        visible: !root.clickThrough && (hover.hovered || menu.visible || mover.pressed)
        QQC2.ToolButton {
            id: moveButton
            objectName: "floatingMoveButton"
            icon.name: "transform-move"
            Accessible.name: i18n("拖动角色")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.text: i18n("拖动角色")
            MouseArea {
                id: mover
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property point start
                onPressed: mouse => {
                    start = moveButton.mapToGlobal(mouse.x, mouse.y);
                    root.beginMove();
                    model.releaseInput();
                }
                onPositionChanged: mouse => {
                    if (!pressed) return;
                    const point = moveButton.mapToGlobal(mouse.x, mouse.y);
                    root.moveFromStart(Qt.point(point.x - start.x, point.y - start.y));
                }
            }
        }
        QQC2.ToolButton {
            text: "⋯"
            Accessible.name: i18n("角色菜单")
            onClicked: menu.open()
            QQC2.Menu {
                id: menu
                QQC2.MenuItem {
                    text: i18n("返回桌面（取消置顶）")
                    onTriggered: root.returnToDesktop()
                }
                QQC2.MenuItem {
                    text: root.configuration.running ? i18n("暂停动画") : i18n("继续动画")
                    onTriggered: root.configuration.running = !root.configuration.running
                }
                QQC2.MenuItem {
                    text: i18n("配置小部件…")
                    onTriggered: root.configureRequested()
                }
            }
        }
    }
    QQC2.Label {
        anchors.centerIn: parent
        width: parent.width - 24
        visible: !model.ready
        text: model.errorString || (root.configuration.modelPath ? i18n("正在加载角色…") : i18n("请在小部件设置中选择模型。"))
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        padding: 8
        background: Rectangle { color: "#dd20252a"; radius: 4 }
        color: "white"
    }
    QQC2.Label {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: text.length > 0
        text: [model.trackingError, model.inputError].filter(Boolean).join("\n")
        wrapMode: Text.Wrap
        padding: 8
        background: Rectangle { color: "#dd20252a"; radius: 4 }
        color: "white"
    }
}
