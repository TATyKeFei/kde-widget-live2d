import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: root
    default property alias formData: form.data
    implicitWidth: Kirigami.Units.gridUnit * 36
    implicitHeight: form.implicitHeight + Kirigami.Units.largeSpacing * 4
    readonly property real helpWidth: Math.max(1, form.width - (form.wideMode ? Kirigami.Units.gridUnit * 9 : 0))

    QQC2.ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
        clip: true

        Item {
            width: scroll.availableWidth
            implicitHeight: Math.max(scroll.availableHeight,
                                     form.implicitHeight + Kirigami.Units.largeSpacing * 4)

            Kirigami.FormLayout {
                id: form
                anchors.top: parent.top
                anchors.topMargin: Kirigami.Units.largeSpacing * 2
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(0, Math.min(root.implicitWidth,
                                          parent.width - Kirigami.Units.largeSpacing * 4))
                // Bound the internal grid too: long help text must wrap instead
                // of contributing an unbounded natural width to the form.
                implicitWidth: width
                wideMode: width >= Kirigami.Units.gridUnit * 26
            }
        }
    }
}
