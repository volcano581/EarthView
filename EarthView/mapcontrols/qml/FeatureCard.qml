import QtQuick

// Details of the map feature the user clicked (attributes from the OSM MBTiles). Shown at the
// top-left of the map by MapControlsOverlay while a feature is selected; the selected feature
// is outlined on the map. ✕, or a click on an empty spot of the map, closes it.
Rectangle {
    id: card

    readonly property var feature: mapControls.selectedFeature
    readonly property color textColor: "#3c4043"
    readonly property color mutedColor: "#5f6368"
    readonly property color borderColor: "#dadce0"

    width: 300
    height: content.height + 24
    radius: 8
    color: "#ffffff"
    border.color: borderColor

    Column {
        id: content
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 8

        Item {
            width: parent.width
            height: Math.max(icon.height, titleColumn.height)

            Rectangle {
                id: icon
                width: 36
                height: 36
                radius: 8
                clip: true
                color: "#f8f9fa"
                border.color: card.borderColor
                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    sourceSize: Qt.size(72, 72)
                    source: card.feature.layer ? "image://maplayers/layer/" + card.feature.layer : ""
                    fillMode: Image.PreserveAspectCrop
                    smooth: true
                }
            }
            Column {
                id: titleColumn
                anchors.left: icon.right
                anchors.leftMargin: 10
                anchors.right: closeText.left
                anchors.rightMargin: 6
                spacing: 2
                Text {
                    width: parent.width
                    text: card.feature.title || ""
                    font.pixelSize: 15
                    font.bold: true
                    color: "#202124"
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: card.feature.subtitle || ""
                    font.pixelSize: 12
                    color: card.mutedColor
                    elide: Text.ElideRight
                }
            }
            Text {
                id: closeText
                anchors.right: parent.right
                anchors.top: parent.top
                text: "✕"
                font.pixelSize: 13
                color: closeMouse.containsMouse ? card.textColor : card.mutedColor
                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mapControls.clearSelectedFeature()
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: card.borderColor
        }

        Repeater {
            model: card.feature.rows || []
            Row {
                required property var modelData
                width: content.width
                spacing: 8
                Text {
                    width: 104
                    text: modelData.name
                    font.pixelSize: 12
                    color: card.mutedColor
                    elide: Text.ElideRight
                }
                Text {
                    width: content.width - 112
                    text: modelData.value
                    font.pixelSize: 12
                    color: card.textColor
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }
}
