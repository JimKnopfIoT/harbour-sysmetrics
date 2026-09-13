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
//
// Two kinds of figure stand under each contact and they must not look alike.
// Grey is what the maker publishes the contact is for; it is true of every
// phone of this model and was read from no device. Orange is what this device
// reports right now - the interrupt line, the voltage at the identify contact,
// the state of the 5 V output, and whether a memory chip answered on the bus.
// A contact with nothing measurable keeps its grey line alone rather than
// being given a number that would only look like one.
Column {
    id: block
    width: parent ? parent.width : 0
    spacing: 0

    // The decoded memory chip, handed in by the page. Only the bus contacts
    // take anything from it: that a chip answered is a statement about SCL
    // and SDA, not about the cover.
    property var mem: undefined

    readonly property var live: tohmon.pins
    readonly property bool haveLive: live && live.supported === true

    Component.onCompleted: tohmon.setWatching(true)
    Component.onDestruction: tohmon.setWatching(false)

    readonly property real padSize: Theme.itemSizeExtraSmall / 1.5
    // The pitch of the grid, and with it the width of every caption: four
    // columns share the page, so a column is a share of the page rather than
    // the pad plus a fixed gap. Squeezed down to the pad, a caption no longer
    // fits on one line and Text.Wrap breaks it wherever it can — "antwortet"
    // lost its last letter to a line of its own, "Stromeingang" broke mid-word.
    // The cap keeps the drawing a connector rather than four dots spread over
    // a page, and the same figure is the vertical gap, so the grid stays
    // square the way the contacts are.
    readonly property real cell: Math.min(block.width / 5,
                                          padSize + 2 * Theme.paddingLarge)
    readonly property real gridWidth: cell * 4

    //: State of the interrupt contact: the line is pulled down
    readonly property string lowText: qsTr("low")
    //: State of the interrupt contact: the line is released
    readonly property string highText: qsTr("high")

    function measured(n) {
        if (n === 3)
            return haveLive && live.idMillivolt >= 0
                    ? live.idMillivolt + " mV" : ""
        if (n === 4)
            return haveLive && live.intState >= 0
                    ? (live.intState === 0 ? lowText : highText) : ""
        if (n === 7)
            return haveLive && live.powerOut >= 0
                    ? (live.powerOut ? qsTr("on") : qsTr("off")) : ""
        // Nothing answered on the bus. "N/A" rather than a sentence: it is
        // the same in both languages and it fits the column, where "keine
        // Antwort" needed a second line of its own.
        if (n === 5 || n === 6)
            return mem === undefined ? ""
                 : (mem.ok === true ? qsTr("chip answers") : "N/A")
        return ""
    }

    readonly property var topRow: [
        { n: 1, sig: qsTr("Power in"),  detail: "5–9 V" },
        { n: 3, sig: "ID",              detail: "3,3 V" },
        { n: 5, sig: "SCL",             detail: "3,3 V" },
        { n: 7, sig: qsTr("Power out"), detail: "5 V" }
    ]
    readonly property var bottomRow: [
        { n: 2, sig: "GND",  detail: "" },
        { n: 4, sig: "INT",  detail: "≤ 1,8 V" },
        { n: 6, sig: "SDA",  detail: "3,3 V" }
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
                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: block.measured(modelData.n)
                            font.pixelSize: Theme.fontSizeTiny
                            color: Diag.amber
                            wrapMode: Text.Wrap
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
                            // A ring in orange marks a contact this device
                            // says something about; the rest keep the outline
                            // of a drawing.
                            border.color: block.measured(modelData.n).length > 0
                                          ? Diag.amber : Theme.primaryColor
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
                            border.color: block.measured(modelData.n).length > 0
                                          ? Diag.amber : Theme.primaryColor
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
                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: block.measured(modelData.n)
                            font.pixelSize: Theme.fontSizeTiny
                            color: Diag.amber
                            wrapMode: Text.Wrap
                            visible: text.length > 0
                        }
                    }
                }
            }
        }
    }

    // What the two colours mean, said once instead of in every caption.
    Label {
        width: parent.width - 2 * Theme.horizontalPageMargin
        x: Theme.horizontalPageMargin
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSizeTiny
        color: Theme.secondaryColor
        wrapMode: Text.Wrap
        visible: block.haveLive
        //: Legend under the connector drawing. Keep both colour names.
        text: qsTr("Grey is what the contact is for, orange what this device reports right now.")
    }

    Item { width: 1; height: Theme.paddingMedium; visible: block.haveLive }
}
