import QtQuick

// One choice in a panel (map type, map style, map detail, projection), as Google Maps draws
// them: a rounded map picture with its name under it; blue outline and name when selected.
// `imageSource` is a live picture (image://maplayers/...) shown over `iconSource` once loaded.
Item {
    id: tile

    property url iconSource
    property url imageSource
    property string label
    property bool selected: false
    property bool available: true
    property bool toggle: false          // kept for callers; toggles look like choices now
    property string unavailableToolTip: ""
    signal clicked()

    readonly property color accentColor: "#1a73e8"
    readonly property color borderColor: "#dadce0"
    readonly property color textColor: "#3c4043"

    width: 66
    height: 80
    opacity: available ? 1.0 : 0.4

    Rectangle {
        id: frame
        width: 54
        height: 54
        anchors.horizontalCenter: parent.horizontalCenter
        radius: 9
        color: "#f1f3f4"
        Image {
            id: drawn
            anchors.fill: parent
            anchors.margins: 2
            sourceSize: Qt.size(108, 108)
            source: tile.iconSource
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
        }
        Image {
            anchors.fill: drawn
            sourceSize: Qt.size(108, 108)
            source: tile.imageSource
            cache: false
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            visible: status === Image.Ready && implicitWidth > 1
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: tile.selected ? 2 : 1
            border.color: tile.selected ? tile.accentColor
                        : (tileMouse.containsMouse && tile.available ? "#9aa0a6" : tile.borderColor)
        }
    }
    Text {
        anchors.top: frame.bottom
        anchors.topMargin: 5
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width + 6
        horizontalAlignment: Text.AlignHCenter
        text: tile.label
        font.pixelSize: 12
        font.weight: tile.selected ? Font.DemiBold : Font.Normal
        color: tile.selected ? tile.accentColor : tile.textColor
        elide: Text.ElideRight
    }
    MouseArea {
        id: tileMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: tile.available ? Qt.PointingHandCursor : Qt.ArrowCursor
        onEntered: {
            const p = tile.mapToGlobal(0, tile.height)
            if (!tile.available && tile.unavailableToolTip.length > 0)
                mapControls.showToolTip(tile.unavailableToolTip, p.x, p.y)
            else if (tile.label.length > 10)
                mapControls.showToolTip(tile.label, p.x, p.y)
        }
        onExited: mapControls.hideToolTip()
        onClicked: {
            if (tile.available)
                tile.clicked()
        }
    }
}
