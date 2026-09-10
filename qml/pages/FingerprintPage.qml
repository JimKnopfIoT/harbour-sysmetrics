import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Ask the fingerprint daemon to identify the finger on the reader.
//
// This is the whole of what an ordinary application may do with the reader:
// sailfish-fpd's bus policy lets any process call Identify, and the answer is a
// name or a no. Enrolling and removing fingers are the system's business, and
// the device lock cannot be reached from here at all. Nothing on this page adds
// or deletes anything.
Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    function _attachHelp() {
        if (_helpAttached) return
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"),
                                       { topics: ["fingerprint"] })
        if (p) _helpAttached = true
    }
    onStatusChanged: {
        if (status === PageStatus.Active) {
            _attachHelp()
            fpmon.refresh()
        } else if (status === PageStatus.Deactivating) {
            fpmon.stopTest()
        }
    }
    Component.onDestruction: fpmon.stopTest()

    function resultColor() {
        switch (fpmon.result) {
        case "match":   return Diag.green
        case "nomatch": return Diag.amber
        case "busy":    return Diag.amber
        case "denied":  return Diag.red
        case "error":   return Diag.red
        case "started": return Diag.cyan
        default:        return Theme.secondaryColor
        }
    }

    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Fingerprint test") }

            // --- the reader itself -------------------------------------------
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2

                KeyValue {
                    label: qsTr("Sensor")
                    mono: true
                    value: fpmon.sensor.compatible ? fpmon.sensor.compatible
                                                   : qsTr("none named in the device tree")
                }
                KeyValue {
                    label: qsTr("Daemon state")
                    mono: true
                    value: fpmon.daemonPresent ? fpmon.state : qsTr("not answering")
                    valueColor: fpmon.daemonPresent ? Theme.primaryColor : Diag.amber
                }
                KeyValue {
                    label: qsTr("Enrolled fingers")
                    value: fpmon.fingers.length
                           ? fpmon.fingers.join(", ") : qsTr("none")
                }
            }

            // --- the test ----------------------------------------------------
            SectionHeader { text: qsTr("Test") }

            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall

                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: fpmon.testing
                          ? qsTr("Place a finger on the reader")
                          : (fpmon.resultText.length ? fpmon.resultText
                                                     : qsTr("Not started"))
                    font.pixelSize: fpmon.testing ? Theme.fontSizeLarge : Theme.fontSizeSmall
                    color: fpmon.testing ? Diag.cyan : page.resultColor()
                }
                // The daemon's own word for what it saw while the finger was
                // down — partial, too fast, imager dirty. Kept verbatim.
                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    visible: fpmon.hint.length > 0
                    text: fpmon.hint
                    font.pixelSize: Theme.fontSizeExtraSmall
                    font.family: "monospace"
                    color: Diag.amber
                    wrapMode: Text.Wrap
                }
                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    visible: !fpmon.testing && fpmon.resultText.length > 0
                    text: qsTr("daemon replied %1 (%2)").arg(fpmon.replyCode).arg(fpmon.replyName)
                    font.pixelSize: Theme.fontSizeTiny
                    font.family: "monospace"
                    color: Theme.secondaryColor
                    wrapMode: Text.Wrap
                }
            }

            ButtonLayout {
                Button {
                    text: fpmon.testing ? qsTr("Cancel") : qsTr("Identify a finger")
                    enabled: fpmon.daemonPresent
                    onClicked: fpmon.testing ? fpmon.stopTest() : fpmon.startTest()
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("The test asks the fingerprint daemon whether the finger on the reader is one of the enrolled ones. It gets back a name or a no — never the fingerprint, which never leaves the secure environment. Nothing here enrols a finger, removes one, or unlocks anything.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("With no finger enrolled the reader can still be tested: a finger it does not know is read and refused, and that refusal is the proof that it read something.")
                visible: fpmon.fingers.length === 0
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
