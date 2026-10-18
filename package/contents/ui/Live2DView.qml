import QtQuick
import org.kde.plasma.plasmoid
import org.kde.plasma.live2d 1.0

Live2DItem {
    id: character
    source: Plasmoid.configuration.modelPath
    renderBackend: Plasmoid.configuration.renderBackend
    running: Plasmoid.configuration.running
    frameRate: Plasmoid.configuration.frameRate
    zoom: Plasmoid.configuration.zoom
    cursorFollowMode: Plasmoid.configuration.cursorFollowMode || (Plasmoid.configuration.followCursor ? "always" : "off")
    idleGroup: Plasmoid.configuration.idleGroup

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onPositionChanged: mouse => character.setGaze(mouse.x / width * 2 - 1, 1 - mouse.y / height * 2)
        onExited: character.setGaze(0, 0)
        onClicked: mouse => character.tap(mouse.x / width, mouse.y / height)
    }
}
