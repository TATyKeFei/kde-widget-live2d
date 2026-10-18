import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: root
    property string cfg_renderBackend: "auto"
    property alias cfg_modelPath: modelPath.text
    property alias cfg_running: running.checked
    property bool cfg_followCursor: true
    property string cfg_cursorFollowMode: ""
    property alias cfg_frameRate: frameRate.value
    property alias cfg_zoom: zoom.value
    property alias cfg_idleGroup: idleGroup.text

    RowLayout {
        Kirigami.FormData.label: i18n("模型文件：")
        QQC2.TextField {
            id: modelPath
            Layout.fillWidth: true
            placeholderText: "/path/to/character.model3.json"
            Accessible.name: i18n("模型文件路径")
        }
        QQC2.Button {
            text: i18n("选择…")
            icon.name: "document-open"
            onClicked: picker.open()
        }
    }
    QQC2.Label {
        Layout.fillWidth: true
        text: i18n("选择 .model3.json，并保留它旁边的模型、纹理和动作文件。")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
    QQC2.ComboBox {
        Kirigami.FormData.label: i18n("角色渲染：")
        model: [i18n("自动（优先 Vulkan）"), "Vulkan", "OpenGL"]
        currentIndex: Math.max(0, ["auto", "vulkan", "opengl"].indexOf(root.cfg_renderBackend))
        onActivated: root.cfg_renderBackend = ["auto", "vulkan", "opengl"][currentIndex]
    }
    QQC2.Label {
        Layout.fillWidth: true
        text: i18n("独立于 Plasma 的显示后端，支持 Wayland。自动模式在 Vulkan 不可用时尝试 OpenGL。")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
    QQC2.CheckBox {
        id: running
        Kirigami.FormData.label: i18n("动画：")
        text: i18n("播放动画")
    }
    QQC2.ComboBox {
        objectName: "cursorFollowModeSelector"
        Kirigami.FormData.label: i18n("指针跟随：")
        model: [i18n("关闭"), i18n("聚焦窗口时"), i18n("窗口非全屏时"), i18n("始终跟随")]
        currentIndex: Math.max(0, ["off", "focused", "notFullscreen", "always"].indexOf(
            root.cfg_cursorFollowMode || (root.cfg_followCursor ? "always" : "off")))
        onActivated: {
            root.cfg_cursorFollowMode = ["off", "focused", "notFullscreen", "always"][currentIndex];
            root.cfg_followCursor = currentIndex !== 0;
        }
    }
    QQC2.Label {
        Layout.fillWidth: true
        text: i18n("在整个桌面范围内跟随指针。“聚焦窗口时”要求普通应用窗口获得焦点；“窗口非全屏时”在前台窗口全屏时暂停，最大化仍会跟随。")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
    QQC2.SpinBox {
        id: frameRate
        Kirigami.FormData.label: i18n("帧率：")
        from: 1
        to: 60
        stepSize: 1
        editable: true
    }
    RowLayout {
        Kirigami.FormData.label: i18n("角色大小：")
        QQC2.Slider {
            id: zoom
            Layout.fillWidth: true
            from: 0.25
            to: 3.0
            stepSize: 0.05
        }
        QQC2.Label { text: Math.round(zoom.value * 100) + "%" }
    }
    QQC2.TextField {
        id: idleGroup
        Kirigami.FormData.label: i18n("待机动作组：")
        placeholderText: "Idle"
    }
    FileDialog {
        id: picker
        title: i18n("选择 Live2D 模型")
        nameFilters: [i18n("Live2D 模型 (*.model3.json)")]
        onAccepted: modelPath.text = selectedFile.toString()
    }
}
