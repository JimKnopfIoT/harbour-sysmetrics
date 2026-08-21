import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"
import "HwInfo.js" as HwInfo

// Bug-report assistant: the copy-ready device summary plus targeted log info
// for a component the user names (e.g. "sfmail"). Everything is gathered
// read-only and presented as copyable text — journal/kernel excerpts need
// the root helper, the rest works unprivileged.
Page {
    id: page
    allowedOrientations: Orientation.All

    property string deviceReport: ""
    property string logReport: ""
    property bool busy: false

    Component.onCompleted: deviceReport = HwInfo.cpu().report

    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Bug reports")
                description: qsTr("Copy-ready facts — gathered read-only")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Help improve the system and its apps: report bugs. Complaining alone won't cut it — this is where the rubber meets the road.")
                font.pixelSize: Theme.fontSizeSmall
                font.italic: true
                color: Theme.highlightColor
                wrapMode: Text.Wrap
            }

            SectionHeader { text: qsTr("Any bug report?") }
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Don't miss these details:")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                wrapMode: Text.Wrap
            }
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: page.deviceReport
                font.pixelSize: Theme.fontSizeExtraSmall
                font.family: "monospace"
                color: Theme.primaryColor
                wrapMode: Text.WrapAnywhere
            }
            ButtonLayout {
                Button {
                    text: qsTr("Copy device summary")
                    onClicked: Clipboard.text = page.deviceReport
                }
            }

            SectionHeader { text: qsTr("Generate log info") }
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Name the affected component (e.g. sfmail). Collected: exact package versions, running processes, and — with root mode active — matching journal and kernel-log lines.")
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
                wrapMode: Text.Wrap
            }
            SearchField {
                id: term
                width: parent.width
                placeholderText: qsTr("Component name")
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: page.generate()
            }
            ButtonLayout {
                Button {
                    text: qsTr("Generate")
                    enabled: term.text.trim().length > 0 && !page.busy
                    onClicked: page.generate()
                }
            }
            BusyIndicator {
                running: page.busy
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Label {
                visible: page.logReport.length > 0
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: page.logReport
                font.pixelSize: Theme.fontSizeTiny
                font.family: "monospace"
                color: Theme.primaryColor
                wrapMode: Text.WrapAnywhere
            }
            ButtonLayout {
                Button {
                    visible: page.logReport.length > 0
                    text: qsTr("Copy log info")
                    onClicked: Clipboard.text = page.logReport
                }
            }
            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }

    function generate() {
        var t = term.text.trim()
        if (!t.length) return
        busy = true
        logReport = ""
        // let the busy indicator paint before the blocking calls
        collectTimer.restart()
    }
    Timer {
        id: collectTimer
        interval: 50
        onTriggered: {
            var t = term.text.trim()
            var txt = sysmon.bugReportInfo(t)
            var rootActive = false
            try { rootActive = rootmon.active } catch (e) {}
            if (rootActive) {
                var logs = rootmon.logGrep(t)
                txt += "\n" + (logs.length ? logs
                       : qsTr("== journal/kernel log: no matching lines =="))
            } else {
                txt += "\n" + qsTr("== journal/kernel log: root mode required (enable the helper in Settings) ==")
            }
            page.logReport = txt
            page.busy = false
        }
    }
}
