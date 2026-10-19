import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ConfigForm {
    id: root
    property bool cfg_followCursor: true
    property string cfg_cursorFollowMode: ""
    property string cfg_mouseTrackingMode: "screen"
    property alias cfg_relativeSensitivity: relativeSensitivity.value
    property string cfg_inputMode: "focused"
    property alias cfg_keepAbove: keepAbove.checked
    property alias cfg_clickThrough: clickThrough.checked

    QQC2.CheckBox {
        id: keepAbove
        objectName: "keepAboveSelector"
        Kirigami.FormData.label: i18n("窗口层级：")
        text: i18n("始终置顶")
    }
    QQC2.Label {
        Layout.fillWidth: true
        Layout.preferredWidth: root.helpWidth
        Layout.maximumWidth: root.helpWidth
        text: i18n("将角色显示在独立透明悬浮层，覆盖应用和全屏窗口。鼠标移到角色上方可拖动，或从菜单返回桌面。显示器锁定时由系统隐藏。")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
    QQC2.CheckBox {
        id: clickThrough
        Kirigami.FormData.label: i18n("鼠标穿透：")
        objectName: "clickThroughSelector"
        text: i18n("让点击穿过角色")
    }
    QQC2.Label {
        Layout.fillWidth: true
        Layout.preferredWidth: root.helpWidth
        Layout.maximumWidth: root.helpWidth
        text: i18n("桌面和置顶角色均可穿透。开启后不能点击或拖动角色；桌面角色请先进入桌面编辑模式，再打开小部件设置关闭。置顶角色可从原小部件操作。全局跟随和“整个桌面”动作按键仍可使用。")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
    QQC2.ComboBox {
        Layout.preferredWidth: Math.min(root.helpWidth, Kirigami.Units.gridUnit * 16)
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
        Layout.preferredWidth: root.helpWidth
        Layout.maximumWidth: root.helpWidth
        text: i18n("在整个桌面范围内跟随指针。“聚焦窗口时”要求普通应用窗口获得焦点；“窗口非全屏时”在前台窗口全屏时暂停，最大化仍会跟随。")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
    QQC2.ComboBox {
        Layout.preferredWidth: Math.min(root.helpWidth, Kirigami.Units.gridUnit * 16)
        objectName: "mouseTrackingModeSelector"
        Kirigami.FormData.label: i18n("鼠标移动：")
        model: [i18n("屏幕位置"), i18n("相对位移（FPS 游戏）")]
        currentIndex: Math.max(0, ["screen", "relative"].indexOf(root.cfg_mouseTrackingMode))
        onActivated: root.cfg_mouseTrackingMode = ["screen", "relative"][currentIndex]
    }
    QQC2.Label {
        Layout.fillWidth: true
        Layout.preferredWidth: root.helpWidth
        Layout.maximumWidth: root.helpWidth
        text: root.cfg_mouseTrackingMode === "relative"
            ? i18n("读取鼠标移动量，即使游戏锁定指针也能响应，停止移动后逐渐回中。全屏游戏请将指针跟随设为“始终跟随”。需运行 scripts/enable-global-input.sh 授权鼠标设备，无需注销；与动作按键模式独立。")
            : i18n("根据指针在屏幕上的位置驱动角色；适合桌面使用。游戏锁定指针时请选择相对位移。")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
    RowLayout {
        visible: root.cfg_mouseTrackingMode === "relative"
        Kirigami.FormData.label: i18n("相对移动灵敏度：")
        QQC2.Slider {
            id: relativeSensitivity
            objectName: "relativeSensitivitySelector"
            Layout.fillWidth: true
            from: 0.1
            to: 5
            stepSize: 0.1
            value: 1
        }
        QQC2.Label { text: Math.round(relativeSensitivity.value * 100) + "%" }
    }
    QQC2.ComboBox {
        Layout.preferredWidth: Math.min(root.helpWidth, Kirigami.Units.gridUnit * 16)
        objectName: "inputModeSelector"
        Kirigami.FormData.label: i18n("模型动作按键：")
        model: [i18n("关闭"), i18n("聚焦小部件时"), i18n("整个桌面")]
        currentIndex: Math.max(0, ["off", "focused", "global"].indexOf(root.cfg_inputMode))
        onActivated: root.cfg_inputMode = ["off", "focused", "global"][currentIndex]
    }
    QQC2.Label {
        Layout.fillWidth: true
        Layout.preferredWidth: root.helpWidth
        Layout.maximumWidth: root.helpWidth
        text: root.cfg_inputMode === "global"
            ? i18n("读取模型 .vtube.json 中的按键；在其他应用中按键也能触发，原按键仍传给应用。Wayland 下需输入设备权限，运行 scripts/enable-global-input.sh 授权当前设备，无需注销。")
            : i18n("单击角色使小部件获得焦点后，使用模型 .vtube.json 中设置的按键播放动作或切换表情；按住生效的表情在松开时恢复。")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
}
