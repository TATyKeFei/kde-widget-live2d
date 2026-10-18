import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.plasma.live2d 1.0

ApplicationWindow {
    id: root
    width: 540
    height: 720
    visible: true
    title: qsTr("Live2D · 原生预览")
    color: "#192026"

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            ToolButton {
                text: qsTr("打开模型…")
                onClicked: picker.open()
            }
            ToolButton {
                text: character.running ? qsTr("暂停") : qsTr("继续")
                enabled: character.ready
                onClicked: character.running = !character.running
            }
            ComboBox {
                model: [qsTr("关闭跟随"), qsTr("聚焦窗口时"), qsTr("窗口非全屏时"), qsTr("始终跟随")]
                currentIndex: ["off", "focused", "notFullscreen", "always"].indexOf(character.cursorFollowMode)
                onActivated: character.cursorFollowMode = ["off", "focused", "notFullscreen", "always"][currentIndex]
            }
            ComboBox {
                Layout.fillWidth: true
                model: character.expressions
                displayText: currentIndex >= 0 ? currentText : qsTr("表情")
                onActivated: character.setExpression(currentText)
            }
        }
    }

    Live2DItem {
        id: character
        objectName: "live2dModel"
        anchors.fill: parent
        source: initialModel
        renderBackend: initialBackend
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onPositionChanged: mouse => character.setGaze(mouse.x / width * 2 - 1, 1 - mouse.y / height * 2)
        onExited: character.setGaze(0, 0)
        onClicked: mouse => character.tap(mouse.x / width, mouse.y / height)
        onWheel: wheel => {
            character.zoom += wheel.angleDelta.y > 0 ? 0.05 : -0.05;
            wheel.accepted = true;
        }
    }
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 64
        visible: !character.ready
        spacing: 16
        Label {
            Layout.fillWidth: true
            text: character.errorString || (character.source.toString() ? qsTr("正在加载角色…") : qsTr("选择一个 Live2D 角色"))
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: "#f0f3f6"
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            visible: character.errorString.length > 0
            text: qsTr("重新加载")
            onClicked: character.reload()
        }
    }
    Label {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: text.length > 0
        text: character.trackingError
        wrapMode: Text.Wrap
        padding: 8
        color: "#f0f3f6"
        background: Rectangle { color: "#192026"; opacity: 0.9 }
    }
    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            ComboBox {
                model: [qsTr("自动"), "Vulkan", "OpenGL"]
                currentIndex: ["auto", "vulkan", "opengl"].indexOf(character.renderBackend)
                onActivated: character.renderBackend = ["auto", "vulkan", "opengl"][currentIndex]
            }
            Label { text: character.activeBackend; }
            Label { text: qsTr("缩放"); Layout.leftMargin: 12 }
            Slider {
                Layout.fillWidth: true
                from: 0.25
                to: 3
                value: character.zoom
                onMoved: character.zoom = value
            }
            Label { text: Math.round(character.zoom * 100) + "%"; Layout.rightMargin: 12 }
        }
    }
    FileDialog {
        id: picker
        title: qsTr("选择 Live2D 模型")
        nameFilters: [qsTr("Live2D 模型 (*.model3.json)")]
        onAccepted: character.source = selectedFile
    }
}
