import QtQuick
import org.kde.plasma.plasmoid
import org.kde.plasma.live2d 1.0

Live2DItem {
    id: character
    property var configuration: Plasmoid.configuration
    property bool localInputEnabled: true
    onLocalInputEnabledChanged: {
        if (!localInputEnabled) {
            if (inputMode === "focused") releaseInput();
            focus = false;
        }
    }
    source: configuration.modelPath
    renderBackend: configuration.renderBackend
    running: configuration.running
    frameRate: configuration.frameRate
    zoom: configuration.zoom
    cursorFollowMode: configuration.cursorFollowMode || (configuration.followCursor ? "always" : "off")
    mouseTrackingMode: configuration.mouseTrackingMode || "screen"
    relativeSensitivity: configuration.relativeSensitivity || 1
    idleGroup: configuration.idleGroup
    inputMode: configuration.inputMode

    MouseArea {
        enabled: character.localInputEnabled
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onPressed: mouse => {
            character.forceActiveFocus();
            character.inputButton("LeftMouseButton", true);
        }
        onReleased: character.inputButton("LeftMouseButton", false)
        onCanceled: character.inputButton("LeftMouseButton", false)
        onPositionChanged: mouse => character.setGaze(mouse.x / width * 2 - 1, 1 - mouse.y / height * 2)
        onExited: character.setGaze(0, 0)
        onClicked: mouse => character.tap(mouse.x / width, mouse.y / height)
    }
    TapHandler {
        enabled: character.localInputEnabled
        acceptedButtons: Qt.RightButton
        onPressedChanged: {
            if (pressed) character.forceActiveFocus();
            character.inputButton("RightMouseButton", pressed);
        }
    }
    TapHandler {
        enabled: character.localInputEnabled
        acceptedButtons: Qt.MiddleButton
        onPressedChanged: {
            if (pressed) character.forceActiveFocus();
            character.inputButton("MiddleMouseButton", pressed);
        }
    }
}
