import QtQuick

// Mini-map layers button (bottom-right, above the scale bar) and its panel, which opens to
// the left. Row 1: one tile per map source, in the Map Source dropdown's order (discovered
// earth files with an icon for their kind, "OSM Map" for MBTiles), then Hillshade (on/off
// over the base map). Below a separator: overlays Cities / Borders / Grid.
// The QQuickWidget sizes itself to this root and is anchored to the map's right edge by
// MapControlsOverlay, so the root grows left when the panel opens.
Item {
    id: root

    readonly property color cardColor: "#ffffff"
    readonly property color borderColor: "#dadce0"
    readonly property color textColor: "#3c4043"
    readonly property int panelGap: 8
    readonly property int tilesPerRow: 4
    readonly property var currentEntry: mapControls.currentBaseMap >= 0
                                        && mapControls.currentBaseMap < mapControls.baseMaps.length
                                        ? mapControls.baseMaps[mapControls.currentBaseMap] : null
    readonly property string currentIcon: currentEntry ? currentEntry.icon : "thumb_osm"

    width: button.width + (panel.visible ? panel.width + panelGap : 0)
    height: Math.max(button.height, panel.visible ? panel.height : 0)

    // Mini-map preview of the current base map.
    Rectangle {
        id: button
        anchors.right: parent.right
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
                source: "icons/" + root.currentIcon + ".svg"
                fillMode: Image.PreserveAspectCrop
                smooth: true
            }
            Image {
                anchors.fill: parent
                visible: mapControls.hillshadeVisible
                opacity: 0.4
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
                mapControls.showToolTip(root.currentEntry ? "Map layers – " + root.currentEntry.label
                                                          : "Map layers", p.x, p.y)
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
        anchors.right: button.left
        anchors.rightMargin: root.panelGap
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
            Flow {
                width: Math.min(baseMapRepeater.count + 1, root.tilesPerRow) * 76 + 9
                spacing: 4
                Repeater {
                    id: baseMapRepeater
                    model: mapControls.baseMaps
                    LayerTile {
                        required property int index
                        required property var modelData
                        iconSource: "icons/" + modelData.icon + ".svg"
                        label: modelData.label
                        selected: index === mapControls.currentBaseMap
                        onClicked: mapControls.currentBaseMap = index
                    }
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
