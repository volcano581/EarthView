import QtQuick

// Mini-map layers button (bottom-left) and its panel, which opens to the right.
// Row 1: base maps OSM / Imagery (one at a time) and Hillshade (on/off over the base map).
// Below a separator: overlays Cities / Borders / Grid.
// The QQuickWidget sizes itself to this root and is anchored bottom-left by
// MapControlsOverlay, so the root grows right when the panel opens.
Item {
    id: root

    readonly property color cardColor: "#ffffff"
    readonly property color borderColor: "#dadce0"
    readonly property color textColor: "#3c4043"
    readonly property int panelGap: 8
    readonly property bool imagery: mapControls.baseMap === 1

    width: button.width + (panel.visible ? panel.width + panelGap : 0)
    height: Math.max(button.height, panel.visible ? panel.height : 0)

    // Mini-map preview of the current base map.
    Rectangle {
        id: button
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: 68
        height: 68
        radius: 10
        color: root.cardColor
        border.width: mapControls.layersOpen ? 2 : 1
        border.color: mapControls.layersOpen ? "#1a73e8" : (buttonMouse.containsMouse ? "#9aa0a6" : root.borderColor)

        Item {
            anchors.fill: parent
            anchors.margins: 3
            clip: true
            Image {
                anchors.fill: parent
                sourceSize: Qt.size(128, 128)
                source: root.imagery ? "icons/thumb_imagery.svg" : "icons/thumb_osm.svg"
                fillMode: Image.PreserveAspectCrop
                smooth: true
            }
            Image {
                anchors.fill: parent
                visible: mapControls.hillshadeVisible
                opacity: 0.45
                sourceSize: Qt.size(128, 128)
                source: "icons/thumb_hillshade.svg"
                fillMode: Image.PreserveAspectCrop
                smooth: true
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 20
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00000000" }
                    GradientStop { position: 1.0; color: "#99000000" }
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 3
                text: "Layers"
                color: "#ffffff"
                font.pixelSize: 11
                font.bold: true
            }
        }
        MouseArea {
            id: buttonMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: {
                const p = button.mapToGlobal(0, button.height + 4)
                mapControls.showToolTip("Map layers", p.x, p.y)
            }
            onExited: mapControls.hideToolTip()
            onClicked: {
                mapControls.hideToolTip()
                mapControls.toggleLayers()
            }
        }
    }

    Rectangle {
        id: panel
        visible: mapControls.layersOpen
        anchors.left: button.right
        anchors.leftMargin: root.panelGap
        anchors.bottom: parent.bottom
        width: content.width + 28
        height: content.height + 24
        radius: 8
        color: root.cardColor
        border.color: root.borderColor

        Column {
            id: content
            anchors.centerIn: parent
            spacing: 10

            Text {
                text: "MAP TYPE"
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 0.6
                color: root.textColor
            }
            Row {
                spacing: 4
                LayerTile {
                    iconSource: "icons/thumb_osm.svg"
                    label: "OSM"
                    selected: !root.imagery
                    available: mapControls.osmAvailable
                    unavailableToolTip: "No OSM map source found"
                    onClicked: mapControls.baseMap = 0
                }
                LayerTile {
                    iconSource: "icons/thumb_imagery.svg"
                    label: "Imagery"
                    selected: root.imagery
                    available: mapControls.imageryAvailable
                    unavailableToolTip: "No imagery map source found"
                    onClicked: mapControls.baseMap = 1
                }
                Rectangle {
                    width: 1
                    height: 56
                    color: root.borderColor
                }
                LayerTile {
                    iconSource: "icons/thumb_hillshade.svg"
                    label: "Hillshade"
                    toggle: true
                    selected: mapControls.hillshadeVisible
                    onClicked: mapControls.hillshadeVisible = !mapControls.hillshadeVisible
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: root.borderColor
            }

            Text {
                text: "OVERLAYS"
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 0.6
                color: root.textColor
            }
            Row {
                spacing: 4
                LayerTile {
                    iconSource: "icons/overlay_cities.svg"
                    label: "Cities"
                    toggle: true
                    selected: mapControls.citiesVisible
                    onClicked: mapControls.citiesVisible = !mapControls.citiesVisible
                }
                LayerTile {
                    iconSource: "icons/overlay_borders.svg"
                    label: "Borders"
                    toggle: true
                    selected: mapControls.bordersVisible
                    onClicked: mapControls.bordersVisible = !mapControls.bordersVisible
                }
                LayerTile {
                    iconSource: "icons/overlay_grid.svg"
                    label: "Grid"
                    toggle: true
                    selected: mapControls.gridVisible
                    onClicked: mapControls.gridVisible = !mapControls.gridVisible
                }
            }
        }
    }
}
