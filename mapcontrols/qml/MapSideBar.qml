import QtQuick

// On-map side bar: compass, 3D globe (disabled), measure, settings.
// The QQuickWidget sizes itself to this root (SizeViewToRootObject) and MapControlsOverlay
// keeps it at the map's right edge, vertically centred, so the root grows left when a panel
// opens and the rest of the map keeps its mouse input.
Item {
    id: root

    readonly property color cardColor: "#ffffff"
    readonly property color borderColor: "#dadce0"
    readonly property color hoverColor: "#f1f3f4"
    readonly property color accentColor: "#1a73e8"
    readonly property color textColor: "#3c4043"
    readonly property color mutedColor: "#5f6368"
    readonly property int buttonSize: 40
    readonly property int panelGap: 8
    readonly property Item openPanel: settingsPanel.visible ? settingsPanel
                                    : (measurePanel.visible ? measurePanel : null)

    width: bar.width + (openPanel ? openPanel.width + panelGap : 0)
    height: Math.max(bar.height, openPanel ? openPanel.height : 0)

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

    // Pill-shaped choice (measure mode, unit).
    component Chip: Rectangle {
        id: chip
        property string text
        property bool selected: false
        signal clicked()

        width: Math.max(40, chipText.implicitWidth + 20)
        height: 28
        radius: 14
        color: selected ? "#e8f0fe" : (chipMouse.containsMouse ? root.hoverColor : root.cardColor)
        border.color: selected ? root.accentColor : root.borderColor
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.text
            font.pixelSize: 12
            font.bold: chip.selected
            color: chip.selected ? root.accentColor : root.textColor
        }
        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.clicked()
        }
    }

    component PanelTitle: Text {
        font.pixelSize: 11
        font.bold: true
        font.letterSpacing: 0.6
        color: root.textColor
    }

    // Settings panel, opens to the left of the bar.
    Rectangle {
        id: settingsPanel
        visible: mapControls.settingsOpen
        anchors.right: bar.left
        anchors.rightMargin: root.panelGap
        anchors.verticalCenter: parent.verticalCenter
        width: settingsContent.width + 24
        height: settingsContent.height + 24
        radius: 8
        color: root.cardColor
        border.color: root.borderColor

        Column {
            id: settingsContent
            anchors.centerIn: parent
            spacing: 10

            PanelTitle { text: "PROJECTION" }
            Row {
                spacing: 4
                LayerTile {
                    iconSource: "icons/proj_mercator.svg"
                    label: "Mercator"
                    selected: !mapControls.orthographic
                    onClicked: mapControls.orthographic = false
                }
                LayerTile {
                    iconSource: "icons/proj_orthographic.svg"
                    label: "Orthographic"
                    selected: mapControls.orthographic
                    onClicked: mapControls.orthographic = true
                }
            }
        }
    }

    // Measure panel: mode, unit, result. Open = measuring (map clicks add points).
    Rectangle {
        id: measurePanel
        visible: mapControls.measureOpen
        anchors.right: bar.left
        anchors.rightMargin: root.panelGap
        anchors.verticalCenter: parent.verticalCenter
        width: 236
        height: measureContent.height + 24
        radius: 8
        color: root.cardColor
        border.color: root.borderColor

        Column {
            id: measureContent
            x: 12
            y: 12
            width: parent.width - 24
            spacing: 10

            Item {
                width: parent.width
                height: closeText.height
                PanelTitle { text: "MEASURE DISTANCE" }
                Text {
                    id: closeText
                    anchors.right: parent.right
                    text: "✕"
                    font.pixelSize: 13
                    color: closeMouse.containsMouse ? root.textColor : root.mutedColor
                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: mapControls.measureOpen = false
                    }
                }
            }

            Row {
                spacing: 6
                Chip {
                    text: "Line"
                    selected: measure.mode === 0
                    onClicked: measure.mode = 0
                }
                Chip {
                    text: "Path"
                    selected: measure.mode === 1
                    onClicked: measure.mode = 1
                }
            }

            Flow {
                width: parent.width
                spacing: 4
                Repeater {
                    model: ["km", "mi", "nmi", "m", "ft"]
                    Chip {
                        required property int index
                        required property string modelData
                        text: modelData
                        selected: measure.unit === index
                        onClicked: measure.unit = index
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: root.borderColor
            }

            Text {
                width: parent.width
                text: measure.totalText.length > 0 ? measure.totalText : "—"
                font.pixelSize: 22
                font.bold: true
                color: measure.totalText.length > 0 ? "#202124" : root.mutedColor
            }
            Text {
                width: parent.width
                visible: measure.lastLegText.length > 0
                text: "Last leg: " + measure.lastLegText
                font.pixelSize: 12
                color: root.mutedColor
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: root.mutedColor
                text: measure.pointCount === 0
                      ? "Click on the map to set the start point."
                      : (measure.mode === 0
                         ? (measure.pointCount === 1 ? "Click the end point." : "Click again to start a new line.")
                         : "Click to add points. Right-click removes the last one.")
            }

            Chip {
                text: "Clear"
                visible: measure.pointCount > 0
                onClicked: measure.clear()
            }
        }
    }

    Rectangle {
        id: bar
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
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
                toolTip: mapControls.measureAvailable ? "Measure distance" : "Measure distance (not available)"
                available: mapControls.measureAvailable
                checked: mapControls.measureOpen
                onClicked: mapControls.toggleMeasure()
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
