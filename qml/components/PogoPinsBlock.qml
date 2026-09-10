import QtQuick 2.0
import Sailfish.Silica 1.0
import "."

// The spring contacts on the back: four over three, the lower right position
// empty, and on the device they sit on a square grid - 2.90 mm apart in both
// directions.
//
// So they are drawn on one here too. Every position is a cell of the same
// width, the circle centred in it, and the gap between the rows is the gap
// between the columns; the captions sit above the upper row and below the
// lower one, each over its own column. Sizing a cell to its caption is what
// pulled the rows out of line before: "Power in" is wide, "ID" is narrow, and
// the second row's circles ended up between the first row's instead of under
// them.
//
// The numbers follow the usual way of counting a two-row connector - down each
// column, so the top row is 1, 3, 5, 7 and the bottom row 2, 4, 6.
Column {
    id: block
    width: parent ? parent.width : 0
    spacing: 0

    readonly property real padSize: Theme.itemSizeExtraSmall / 1.5
    readonly property real gap: Theme.paddingLarge
    readonly property real cell: padSize + gap
    readonly property real gridWidth: cell * 4

    readonly property var topRow: [
        { n: 1, sig: qsTr("Power in"),  detail: "5–9 V" },
        { n: 3, sig: "ID",              detail: "3,3 V" },
        { n: 5, sig: "SCL",             detail: "3,3 V" },
        { n: 7, sig: qsTr("Power out"), detail: "5 V" }
    ]
    readonly property var bottomRow: [
        { n: 2, sig: "GND",  detail: "" },
        { n: 4, sig: "INT",  detail: "≤ 1,8 V" },
        { n: 6, sig: "SDA",  detail: "3,3 V" }
    ]

    Item {
        width: parent.width
        height: grid.height + 2 * Theme.paddingMedium

        Column {
            id: grid
            anchors.centerIn: parent
            width: block.gridWidth
            spacing: 0

            // Captions of the upper row, each over its own column.
            Row {
                Repeater {
                    model: block.topRow
                    Column {
                        width: block.cell
                        spacing: 0
                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.sig
                            font.pixelSize: Theme.fontSizeTiny
                            color: Theme.highlightColor
                            wrapMode: Text.Wrap
                        }
                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.detail
                            font.pixelSize: Theme.fontSizeTiny
                            color: Theme.secondaryColor
                            visible: text.length > 0
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.paddingSmall }

            Row {
                Repeater {
                    model: block.topRow
                    Item {
                        width: block.cell
                        height: block.cell
                        Rectangle {
                            anchors.centerIn: parent
                            width: block.padSize
                            height: width
                            radius: width / 2
                            color: "transparent"
                            border.color: Theme.primaryColor
                            border.width: 2
                            Label {
                                anchors.centerIn: parent
                                text: modelData.n
                                font.pixelSize: Theme.fontSizeExtraSmall
                            }
                        }
                    }
                }
            }

            // Same cell, same gap: the rows are one grid pitch apart, which is
            // what the connector does.
            Row {
                Repeater {
                    model: block.bottomRow
                    Item {
                        width: block.cell
                        height: block.cell
                        Rectangle {
                            anchors.centerIn: parent
                            width: block.padSize
                            height: width
                            radius: width / 2
                            color: "transparent"
                            border.color: Theme.primaryColor
                            border.width: 2
                            Label {
                                anchors.centerIn: parent
                                text: modelData.n
                                font.pixelSize: Theme.fontSizeExtraSmall
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.paddingSmall }

            Row {
                Repeater {
                    model: block.bottomRow
                    Column {
                        width: block.cell
                        spacing: 0
                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.sig
                            font.pixelSize: Theme.fontSizeTiny
                            color: Theme.highlightColor
                        }
                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.detail
                            font.pixelSize: Theme.fontSizeTiny
                            color: Theme.secondaryColor
                            visible: text.length > 0
                        }
                    }
                }
            }
        }
    }
}
