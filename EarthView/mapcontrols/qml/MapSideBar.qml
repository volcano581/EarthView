import QtQuick

// On-map side bar: compass, 3D globe (disabled), measure, settings.
// The QQuickWidget sizes itself to this root (SizeViewToRootObject) and is anchored to the
// map's bottom-right corner by MapControlsOverlay, so the root grows left when the
// Settings panel opens and the rest of the map keeps its mouse input.
Item {
    id: root

    readonly property color cardColor: "#ffffff"
    readonly property color borderColor: "#dadce0"
    readonly property color hoverColor: "#f1f3f4"
    readonly property color accentColor: "#1a73e8"
    readonly property color textColor: "#3c4043"
    readonly property int buttonSize: 40
    readonly property int panelGap: 8

    width: bar.width + (panel.visible ? panel.width + panelGap : 0)
    height: Math.max(bar.height, panel.visible ? panel.height : 0)

    component BarButton: Item {
        id: button
        property url iconSource
        property string toolTip
        property bool available: true
        property bool checked: false
        property real iconRotation: 0
        signal clicked()

        width: root.buttonSize
        height: root.buttonSize

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: 6
            color: button.checked ? "#e8f0fe"
                 : (mouse.containsMouse && button.available ? root.hoverColor : "transparent")
        }
        Image {
            anchors.centerIn: parent
            width: 24
            height: 24
            sourceSize: Qt.size(48, 48)
            source: button.iconSource
            rotation: button.iconRotation
            opacity: button.available ? 1.0 : 0.38
            smooth: true
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: button.available ? Qt.PointingHandCursor : Qt.ArrowCursor
            onEntered: {
                const p = button.mapToGlobal(0, button.height + 4)
                mapControls.showToolTip(button.toolTip, p.x, p.y)
            }
            onExited: mapControls.hideToolTip()
            onClicked: {
                mapControls.hideToolTip()
                if (button.available)
                    button.clicked()
            }
        }
    }

    component Separator: Rectangle {
        width: root.buttonSize - 12
        height: 1
        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
        color: root.borderColor
    }

    component ProjectionTile: Item {
        id: tile
        property url iconSource
        property string label
        property bool selected: false
        signal clicked()

        width: 72
        height: 86

        Rectangle {
            id: frame
            width: 56
            height: 56
            anchors.horizontalCenter: parent.horizontalCenter
            radius: 10
            color: "#f8f9fa"
            border.width: tile.selected ? 2 : 1
            border.color: tile.selected ? root.accentColor
                        : (tileMouse.containsMouse ? "#9aa0a6" : root.borderColor)
            Image {
                anchors.centerIn: parent
                width: 40
                height: 40
                sourceSize: Qt.size(80, 80)
                source: tile.iconSource
                smooth: true
            }
        }
        Text {
            anchors.top: frame.bottom
            anchors.topMargin: 6
            anchors.horizontalCenter: parent.horizontalCenter
            text: tile.label
            font.pixelSize: 12
            color: tile.selected ? root.accentColor : root.textColor
        }
        MouseArea {
            id: tileMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tile.clicked()
        }
    }

    // Settings panel, opens to the left of the bar.
    Rectangle {
        id: panel
        visible: mapControls.settingsOpen
        anchors.right: bar.left
        anchors.rightMargin: root.panelGap
        anchors.bottom: parent.bottom
        width: panelContent.width + 24
        height: panelContent.height + 24
        radius: 8
        color: root.cardColor
        border.color: root.borderColor

        Column {
            id: panelContent
            anchors.centerIn: parent
            spacing: 10

            Text {
                text: "PROJECTION"
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 0.6
                color: root.textColor
            }
            Row {
                spacing: 4
                ProjectionTile {
                    iconSource: "icons/proj_mercator.svg"
                    label: "Mercator"
                    selected: !mapControls.orthographic
                    onClicked: mapControls.orthographic = false
                }
                ProjectionTile {
                    iconSource: "icons/proj_orthographic.svg"
                    label: "Orthographic"
                    selected: mapControls.orthographic
                    onClicked: mapControls.orthographic = true
                }
            }
        }
    }

    Rectangle {
        id: bar
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.buttonSize + 8
        height: barColumn.height + 8
        radius: 8
        color: root.cardColor
        border.color: root.borderColor

        Column {
            id: barColumn
            anchors.centerIn: parent
            spacing: 2

            BarButton {
                iconSource: "icons/compass.svg"
                toolTip: "Reset to north"
                iconRotation: -mapControls.bearingDegrees
                onClicked: mapControls.resetNorth()
            }
            Separator {}
            BarButton {
                iconSource: "icons/globe3d.svg"
                toolTip: mapControls.globe3DAvailable ? "3D view" : "3D view (not available yet)"
                available: mapControls.globe3DAvailable
            }
            Separator {}
            BarButton {
                iconSource: "icons/ruler.svg"
                toolTip: mapControls.measureAvailable ? "Measure distance" : "Measure distance (coming soon)"
                available: mapControls.measureAvailable
            }
            Separator {}
            BarButton {
                iconSource: "icons/settings.svg"
                toolTip: "Map settings"
                checked: mapControls.settingsOpen
                onClicked: mapControls.toggleSettings()
            }
        }
    }
}
