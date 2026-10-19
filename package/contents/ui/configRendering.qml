import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ConfigForm {
    id: root
    property string cfg_renderBackend: "auto"
    property alias cfg_frameRate: frameRate.value

    QQC2.ComboBox {
        Layout.preferredWidth: Math.min(root.helpWidth, Kirigami.Units.gridUnit * 16)
        Kirigami.FormData.label: i18n("角色渲染：")
        model: [i18n("自动（优先 Vulkan）"), "Vulkan", "OpenGL"]
        currentIndex: Math.max(0, ["auto", "vulkan", "opengl"].indexOf(root.cfg_renderBackend))
        onActivated: root.cfg_renderBackend = ["auto", "vulkan", "opengl"][currentIndex]
    }
    QQC2.Label {
        Layout.fillWidth: true
        Layout.preferredWidth: root.helpWidth
        Layout.maximumWidth: root.helpWidth
        text: i18n("独立于 Plasma 的显示后端，支持 Wayland。自动模式在 Vulkan 不可用时尝试 OpenGL。")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
    QQC2.SpinBox {
        id: frameRate
        objectName: "frameRateSelector"
        Kirigami.FormData.label: i18n("帧率：")
        from: 0
        to: 240
        stepSize: 1
        editable: true
        textFromValue: function(value, locale) {
            return value === 0 ? i18n("无限制") : Number(value).toLocaleString(locale, 'f', 0);
        }
        valueFromText: function(text, locale) {
            return text === i18n("无限制") ? 0 : Number.fromLocaleString(locale, text);
        }
        validator: RegularExpressionValidator { regularExpression: /[0-9]{0,3}|无限制/ }
    }
    QQC2.Label {
        Layout.fillWidth: true
        Layout.preferredWidth: root.helpWidth
        Layout.maximumWidth: root.helpWidth
        text: i18n("设为 0 表示无限制，实际帧率取决于显卡和角色复杂度；屏幕显示仍受刷新率影响。")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
}
