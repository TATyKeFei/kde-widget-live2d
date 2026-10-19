import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

ConfigForm {
    id: root
    property alias cfg_modelPath: modelPath.text
    property alias cfg_running: running.checked
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
        Layout.preferredWidth: root.helpWidth
        Layout.maximumWidth: root.helpWidth
        text: i18n("选择 .model3.json，并保留它旁边的模型、纹理和动作文件。")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
    QQC2.CheckBox {
        id: running
        Kirigami.FormData.label: i18n("动画：")
        text: i18n("播放动画")
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
