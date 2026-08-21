import QtQuick 2.0
import Sailfish.Silica 1.0
import "."

// One diagnostics finding: severity dot + verdict up front, measured details
// and the fix pointer behind a tap. Read-only — informs, never repairs.
Column {
    id: item
    property var finding
    property bool expanded: false
    width: parent ? parent.width : 0

    BackgroundItem {
        width: parent.width
        height: head.height + 2 * Theme.paddingMedium
        onClicked: item.expanded = !item.expanded

        Row {
            id: head
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.paddingMedium

            Rectangle {
                width: Theme.paddingMedium
                height: width
                radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: Diag.levelColor(item.finding.level)
            }
            Column {
                width: parent.width - parent.spacing * 2 - Theme.paddingMedium - arrow.width
                Label {
                    text: item.finding.title
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: item.finding.level >= 2 ? Diag.levelColor(item.finding.level)
                                                   : Theme.primaryColor
                }
                Label {
                    text: item.finding.verdict
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
            }
            Label {
                id: arrow
                text: item.expanded ? "▲" : "▼"
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    Column {
        visible: item.expanded
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        spacing: Theme.paddingSmall

        Repeater {
            model: item.expanded ? item.finding.details : []
            KeyValue {
                label: modelData.label
                value: modelData.value
                mono: true
                valueColor: modelData.color === "ok"  ? Diag.green
                          : modelData.color === "bad" ? Diag.red
                                                      : Theme.primaryColor
            }
        }
        Label {
            visible: item.finding.note.length > 0
            text: item.finding.note
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.highlightColor
        }
        // Reference address as plain information — deliberately not a link.
        Label {
            visible: item.finding.fixUrl.length > 0
            width: parent.width
            text: (item.finding.fixLabel.length ? item.finding.fixLabel + ":\n" : "")
                  + item.finding.fixUrl
            font.pixelSize: Theme.fontSizeTiny
            font.family: "monospace"
            color: Theme.secondaryColor
            wrapMode: Text.WrapAnywhere
        }
        Item { width: 1; height: Theme.paddingMedium }
    }
}
