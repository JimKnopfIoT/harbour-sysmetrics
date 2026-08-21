import QtQuick 2.0
import Sailfish.Silica 1.0
import "." // Diag

// Titled panel with a big value, optional unit and an embedded graph slot.
Rectangle {
    id: card
    property string title
    property string value
    // unit is the symbol that belongs to the number (%, °C) and is coloured with
    // it; note is the descriptive tail beside it and stays secondary.
    property string unit
    property string note
    property color accent: Diag.cyan
    property alias content: slot.data
    default property alias extra: slot.data
    property bool drilldown: false
    // worst diagnostics level of this card's subsystem (0 = nothing to show)
    property int diagLevel: 0
    signal clicked()

    width: parent ? parent.width : implicitWidth
    implicitHeight: col.implicitHeight + 2 * Theme.paddingMedium
    radius: Theme.paddingMedium
    color: pressArea.pressed ? Diag.panelHi : Diag.panel
    border.width: 1
    border.color: Qt.rgba(accent.r, accent.g, accent.b, 0.28)

    // On top of the content so a tap anywhere on the card reliably opens it.
    MouseArea {
        id: pressArea
        anchors.fill: parent
        z: 100
        enabled: card.drilldown
        onClicked: card.clicked()
    }

    Label {
        visible: card.drilldown
        anchors { top: parent.top; right: parent.right; margins: Theme.paddingMedium }
        text: "›"
        font.pixelSize: Theme.fontSizeLarge
        color: card.accent
    }

    Column {
        id: col
        anchors {
            left: parent.left; right: parent.right; top: parent.top
            margins: Theme.paddingMedium
        }
        spacing: Theme.paddingSmall

        Row {
            width: parent.width
            spacing: Theme.paddingSmall
            Rectangle {
                width: Theme.paddingSmall / 2; height: titleLabel.height
                radius: width / 2; color: card.accent
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                id: titleLabel
                text: card.title.toUpperCase()
                font.pixelSize: Theme.fontSizeExtraSmall
                font.letterSpacing: 1.5
                color: Theme.secondaryHighlightColor
            }
            Rectangle {
                visible: card.diagLevel > 0
                width: Theme.paddingMedium; height: width; radius: width / 2
                color: Diag.levelColor(card.diagLevel)
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Row {
            spacing: 0
            Label {
                text: card.value
                font.pixelSize: Theme.fontSizeExtraLarge
                color: card.accent
                anchors.baseline: unitLabel.baseline
            }
            Label {
                id: unitLabel
                text: card.unit
                font.pixelSize: Theme.fontSizeSmall
                color: card.accent
            }
            Item {
                width: card.note.length ? Theme.paddingMedium : 0
                height: 1
            }
            Label {
                text: card.note
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                anchors.baseline: unitLabel.baseline
            }
        }

        Item {
            id: slot
            width: parent.width
            height: childrenRect.height
        }
    }
}
