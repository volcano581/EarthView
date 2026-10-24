import QtQuick

// The layers chooser, after Google Maps: the button (bottom-right, above the scale bar) is a live
// picture of the current map at this place; its panel opens to the left with
// - Map type: one tile per map source, in the Map Source dropdown's order, each a live picture;
// - Map style: the OSM map's looks (Standard, Positron, Dark), when there is an OSM source;
// - Map details: every layer of the OSM (vector MBTiles) source, each on/off; drawn over any
//   map type and clickable on the map;
// - Overlays: Terrain (hillshade) and Grid.
// The QQuickWidget sizes itself to this root and is anchored to the map's right edge by
// MapControlsOverlay, so the root grows left when the panel opens.
Item {
    id: root

    readonly property color cardColor: "#ffffff"
    readonly property color borderColor: "#dadce0"
    readonly property color titleColor: "#202124"
    readonly property int panelGap: 10
    readonly property int columns: 4
    readonly property var currentEntry: mapControls.currentBaseMap >= 0
                                        && mapControls.currentBaseMap < mapControls.baseMaps.length
                                        ? mapControls.baseMaps[mapControls.currentBaseMap] : null
    readonly property string revision: "?" + mapControls.thumbnailRevision

    width: button.width + (panel.visible ? panel.width + panelGap : 0)
    height: Math.max(button.height, panel.visible ? panel.height : 0)

    component SectionTitle: Text {
        font.pixelSize: 14
        font.weight: Font.DemiBold
        color: root.titleColor
        bottomPadding: 2
    }

    component Separator: Rectangle {
        width: parent ? parent.width : 0
        height: 1
        color: "#e8eaed"
    }

    // Soft shadow under a card (no effects module needed).
    component Shadow: Item {
        property Item target
        x: target.x
        y: target.y
        width: target.width
        height: target.height
        Repeater {
            model: 4
            Rectangle {
                required property int index
                x: -index
                y: 1 + index * 0.6
                width: parent.width + 2 * index
                height: parent.height + 2 * index
                radius: 10 + index
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(0, 0, 0, 0.07 - index * 0.015)
            }
        }
    }

    // The button: a picture of the current map here.
    Shadow { target: button }
    Rectangle {
        id: button
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 74
        height: 74
        radius: 10
        color: root.cardColor
        border.width: mapControls.layersOpen ? 2 : 2
        border.color: mapControls.layersOpen ? "#1a73e8" : "#ffffff"

        Image {
            id: buttonDrawn
            anchors.fill: parent
            anchors.margins: 2
            sourceSize: Qt.size(140, 140)
            source: root.currentEntry ? "image://maplayers/icon/" + root.currentEntry.icon : ""
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
        }
        Image {
            anchors.fill: buttonDrawn
            sourceSize: Qt.size(140, 140)
            source: root.currentEntry && root.currentEntry.thumbnail
                    ? "image://maplayers/" + root.currentEntry.thumbnail + root.revision : ""
            cache: false
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            visible: status === Image.Ready && implicitWidth > 1
        }
        Image {
            anchors.fill: buttonDrawn
            visible: mapControls.hillshadeVisible
            opacity: 0.3
            sourceSize: Qt.size(140, 140)
            source: "image://maplayers/icon/thumb_hillshade"
            fillMode: Image.PreserveAspectCrop
        }
        Rectangle {
            anchors.left: buttonDrawn.left
            anchors.right: buttonDrawn.right
            anchors.bottom: buttonDrawn.bottom
            height: 26
            radius: 8
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 1.0; color: "#b0000000" }
            }
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            spacing: 4
            // Two stacked sheets: the layers glyph.
            Item {
                width: 12
                height: 12
                anchors.verticalCenter: parent.verticalCenter
                Rectangle { x: 1; y: 5; width: 10; height: 6; rotation: 0; radius: 1; color: "#ffffff"; opacity: 0.6 }
                Rectangle { x: 1; y: 1; width: 10; height: 6; radius: 1; color: "#ffffff" }
            }
            Text {
                text: "Layers"
                color: "#ffffff"
                font.pixelSize: 12
                font.weight: Font.DemiBold
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

    Shadow { target: panel; visible: panel.visible }
    Rectangle {
        id: panel
        visible: mapControls.layersOpen
        anchors.right: button.left
        anchors.rightMargin: root.panelGap
        anchors.bottom: parent.bottom
        width: root.columns * 72 + 32
        height: Math.min(content.height + 32, mapControls.panelMaxHeight)
        radius: 10
        color: root.cardColor

        Text {
            id: closeText
            z: 2
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            text: "✕"
            font.pixelSize: 14
            color: closeMouse.containsMouse ? "#202124" : "#5f6368"
            MouseArea {
                id: closeMouse
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: mapControls.layersOpen = false
            }
        }

        Flickable {
            anchors.fill: parent
            anchors.margins: 16
            contentWidth: width
            contentHeight: content.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height

            Column {
                id: content
                width: parent.width
                spacing: 12

                SectionTitle { text: "Map type" }
                Grid {
                    columns: root.columns
                    columnSpacing: 6
                    rowSpacing: 4
                    Repeater {
                        model: mapControls.baseMaps
                        LayerTile {
                            required property int index
                            required property var modelData
                            iconSource: "image://maplayers/icon/" + modelData.icon
                            imageSource: modelData.thumbnail ? "image://maplayers/" + modelData.thumbnail + root.revision : ""
                            label: modelData.label
                            selected: index === mapControls.currentBaseMap
                            onClicked: mapControls.currentBaseMap = index
                        }
                    }
                }

                Separator { visible: mapControls.featureLayersAvailable }
                SectionTitle { visible: mapControls.featureLayersAvailable; text: "Map style" }
                Grid {
                    visible: mapControls.featureLayersAvailable
                    columns: root.columns
                    columnSpacing: 6
                    rowSpacing: 4
                    Repeater {
                        model: mapControls.themes
                        LayerTile {
                            required property var modelData
                            iconSource: "image://maplayers/theme/" + modelData.id + "?drawn"
                            imageSource: "image://maplayers/theme/" + modelData.id + root.revision
                            label: modelData.label
                            selected: mapControls.currentTheme === modelData.id
                            onClicked: mapControls.currentTheme = modelData.id
                        }
                    }
                }

                Separator { visible: mapControls.featureLayersAvailable }
                SectionTitle { visible: mapControls.featureLayersAvailable; text: "Map details" }
                Grid {
                    visible: mapControls.featureLayersAvailable
                    columns: root.columns
                    columnSpacing: 6
                    rowSpacing: 4
                    Repeater {
                        model: mapControls.featureLayers
                        LayerTile {
                            required property var modelData
                            iconSource: "image://maplayers/layer/" + modelData.id
                            label: modelData.label
                            toggle: true
                            selected: mapControls.hiddenFeatureLayers.indexOf(modelData.id) < 0
                            onClicked: mapControls.toggleFeatureLayer(modelData.id)
                        }
                    }
                }

                Separator {}
                SectionTitle { text: "Overlays" }
                Row {
                    spacing: 6
                    LayerTile {
                        iconSource: "image://maplayers/icon/thumb_hillshade"
                        label: "Terrain"
                        toggle: true
                        selected: mapControls.hillshadeVisible
                        onClicked: mapControls.hillshadeVisible = !mapControls.hillshadeVisible
                    }
                    LayerTile {
                        iconSource: "image://maplayers/icon/overlay_grid"
                        label: "Grid"
                        toggle: true
                        selected: mapControls.gridVisible
                        onClicked: mapControls.gridVisible = !mapControls.gridVisible
                    }
                }
            }
        }
    }
}
