import QtQuick

// One square choice in a panel (base map, overlay, projection): picture + label,
// blue outline when selected. `toggle` tiles also show a check badge when on.
Item {
    id: tile

    property url iconSource
    property string label
    property bool selected: false
    property bool available: true
    property bool toggle: false
    property string unavailableToolTip: ""
    signal clicked()

    readonly property color accentColor: "#1a73e8"
    readonly property color borderColor: "#dadce0"
    readonly property color textColor: "#3c4043"

    width: 72
    height: 86
    opacity: available ? 1.0 : 0.4

    Rectangle {
        id: frame
        width: 56
        height: 56
        anchors.horizontalCenter: parent.horizontalCenter
        radius: 10
        color: "#f8f9fa"
        clip: true
        border.width: tile.selected ? 2 : 1
        border.color: tile.selected ? tile.accentColor
                    : (tileMouse.containsMouse && tile.available ? "#9aa0a6" : tile.borderColor)
        Image {
            anchors.fill: parent
            anchors.margins: tile.selected ? 2 : 1
            sourceSize: Qt.size(112, 112)
            source: tile.iconSource
            fillMode: Image.PreserveAspectCrop
            smooth: true
        }
    }
    Rectangle {
        visible: tile.toggle && tile.selected
        width: 18
        height: 18
        radius: 9
        color: tile.accentColor
        border.color: "#ffffff"
        border.width: 2
        anchors.right: frame.right
        anchors.top: frame.top
        anchors.rightMargin: -5
        anchors.topMargin: -5
        Text {
            anchors.centerIn: parent
            text: "✓"
            color: "#ffffff"
            font.pixelSize: 11
            font.bold: true
        }
    }
    Text {
        anchors.top: frame.bottom
        anchors.topMargin: 6
        anchors.horizontalCenter: parent.horizontalCenter
        text: tile.label
        font.pixelSize: 12
        color: tile.selected ? tile.accentColor : tile.textColor
    }
    MouseArea {
        id: tileMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: tile.available ? Qt.PointingHandCursor : Qt.ArrowCursor
        onEntered: {
            if (!tile.available && tile.unavailableToolTip.length > 0) {
                const p = tile.mapToGlobal(0, tile.height)
                mapControls.showToolTip(tile.unavailableToolTip, p.x, p.y)
            }
        }
        onExited: mapControls.hideToolTip()
        onClicked: {
            if (tile.available)
                tile.clicked()
        }
    }
}
