import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// The diagnosis path for a home screen that keeps drawing, step by step:
// watch, measure the idle screen, reproduce the trigger, match the known
// defects, write it up. Reads only; restarting is left to the reader.
Page {
    id: page
    allowedOrientations: Orientation.All

    readonly property var m: redraw.measurement
    readonly property var t: redraw.trigger
    readonly property var w: redraw.watch
    readonly property var known: {
        var all = diagnostics.run(sysmon.cpuPercent, sysmon.load1)
        for (var i = 0; i < all.length; i++)
            if (all[i].id === "gpu-lipstick-redraw") return all[i]
        return null
    }

    function f1(v) { return (v === undefined ? 0 : v).toFixed(1) }
    function clock(ms) { return Qt.formatTime(new Date(ms), "hh:mm:ss") }

    function eventText(e) {
        if (e.kind === "notification")
            return e.actions > 0 ? qsTr("Notification from %1 with %2 action buttons").arg(e.app).arg(e.actions)
                                 : qsTr("Notification from %1 without action buttons").arg(e.app)
        if (e.kind === "screenshot")
            return qsTr("Screenshot %1").arg(e.app)
        return qsTr("Screen touched")
    }

    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height + Theme.paddingLarge

        VerticalScrollDecorator {}

        Column {
            id: col
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Home screen redraw") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("An idle home screen draws nothing. If lipstick keeps drawing anyway, it costs CPU time and battery for nothing. These steps find out whether it does, who drives it, and what set it off.")
            }

            // ---- Watch -------------------------------------------------
            SectionHeader { text: qsTr("Watch") }
            TextSwitch {
                text: qsTr("Watch in the background")
                description: qsTr("Every %1 s while the display is on and SysMetrics is not in front: lipstick's render thread, and who sent notifications (app name and buttons only, never their text).").arg(5)
                checked: redraw.watching
                onClicked: redraw.watching = !redraw.watching
            }
            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: redraw.watching
                spacing: Theme.paddingSmall
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: w.state === "suspect" ? Diag.amber : Theme.primaryColor
                    text: w.state === "suspect"
                          ? qsTr("lipstick has been redrawing without touch since %1. A closer look is worthwhile.").arg(clock(w.since))
                          : w.state === "quiet" ? qsTr("Last look: lipstick idle.")
                          : w.state === "drawing" ? qsTr("Last look: lipstick drawing, %1 wakeups/s.").arg(f1(w.renderWake))
                          : qsTr("Waiting: judged only while the display is on and SysMetrics is not in front.")
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeTiny
                    color: Theme.secondaryColor
                    text: qsTr("Marked when the render thread wakes at least %1 times a second for %2 s with no touch. Idle reference, measured on three devices: below 1 wakeup/s, lipstick below 1 % of a core.").arg(30).arg(30)
                }
                Repeater {
                    model: w.leads || []
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Diag.amber
                        text: qsTr("Before the onset: %1 — %2").arg(clock(modelData.ms)).arg(eventText(modelData))
                    }
                }
                Label {
                    visible: w.state === "suspect"
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.highlightColor
                    text: redraw.restartHint()
                }
            }

            // ---- Step 1: measure ----------------------------------------
            SectionHeader { text: qsTr("1 · Measure the idle screen") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Start, then minimise SysMetrics and do not touch the screen for 30 s. The cover holds still meanwhile, so the app does not measure itself. A touch or the display going off makes the measurement invalid.")
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !redraw.busy
                text: qsTr("Measure (30 s)")
                onClicked: redraw.startMeasurement()
            }
            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: redraw.busy
                spacing: Theme.paddingSmall
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: Diag.cyan
                    text: redraw.coverText.replace("\n", " · ") + "  (" + redraw.secondsLeft + " s)"
                }
                Button {
                    text: qsTr("Cancel")
                    onClicked: redraw.cancel()
                }
            }
            ResultBlock { result: page.m }

            // ---- Step 2/3: trigger test ---------------------------------
            SectionHeader { text: qsTr("2 · Reproduce the trigger") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("The counter-test from the original diagnosis: measure, send a notification without action buttons, measure, send one with buttons, measure. Takes about two minutes; minimise SysMetrics and do not touch the screen. The test notifications are removed afterwards.")
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !redraw.busy
                text: qsTr("Run trigger test")
                onClicked: redraw.startTriggerTest()
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: t.outcome !== undefined || t.message !== undefined
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: t.outcome === "actions" || t.outcome === "plain" || t.outcome === "busy-before"
                       ? Diag.amber : Theme.primaryColor
                text: t.message !== undefined ? t.message
                    : t.outcome === "busy-before" ? qsTr("lipstick was already redrawing before the test. End that first, then run the test again.") + "\n" + redraw.restartHint()
                    : t.outcome === "actions" ? qsTr("Trigger found: a notification with action buttons leaves lipstick redrawing; the one without did not.") + "\n" + redraw.restartHint()
                    : t.outcome === "plain" ? qsTr("Trigger found: any notification leaves lipstick redrawing, even without buttons.") + "\n" + redraw.restartHint()
                    : qsTr("Neither test notification left lipstick redrawing.")
            }
            Repeater {
                model: t.steps || []
                Column {
                    width: page.width
                    Label {
                        x: Theme.horizontalPageMargin
                        text: modelData.step === "baseline" ? qsTr("Before")
                            : modelData.step === "plain" ? qsTr("After a notification without buttons")
                                                         : qsTr("After a notification with buttons")
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.highlightColor
                    }
                    ResultBlock { result: modelData; compact: true }
                }
            }

            // ---- Step 4: known defects ----------------------------------
            SectionHeader { text: qsTr("3 · Known defects") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: known && known.level >= 2 && (t.outcome === "actions" || (m.stuck && m.cls === "lipstick-main"))
                       ? Diag.amber : Theme.secondaryColor
                text: !known ? qsTr("No catalogued defect applies to this device. If the steps above found redrawing, the report below holds what is needed to chase it.")
                    : known.level >= 2 && (t.outcome === "actions" || (m.stuck && m.cls === "lipstick-main"))
                      ? qsTr("The findings match a known defect. The file and the change are named below.")
                    : known.level === 0 && (m.stuck || t.outcome === "actions" || t.outcome === "plain")
                      ? qsTr("The known defect is already corrected here, yet lipstick still redraws: this is something else. The report below holds the evidence.")
                      : qsTr("Checked against the catalogue:")
            }
            FindingItem {
                showCheckLink: false
                visible: known !== null
                finding: known ? known : ({ title: "", verdict: "", level: 0, details: [], note: "", fixUrl: "", fixLabel: "" })
                expanded: known !== null && known.level >= 2
            }

            // ---- Report -------------------------------------------------
            SectionHeader { text: qsTr("4 · Report") }
            Label {
                id: reportText
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                font.family: "monospace"
                color: Theme.secondaryColor
                text: { redraw.measurement; redraw.trigger; redraw.watch; return redraw.report() }
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Copy report")
                onClicked: Clipboard.text = reportText.text
            }

            // ---- Events -------------------------------------------------
            SectionHeader {
                text: qsTr("Recent events")
                visible: redraw.events.length > 0
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: !redraw.notifyWatchAvailable
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
                text: qsTr("dbus-monitor is not installed; notifications cannot be matched to an onset.")
            }
            Repeater {
                model: redraw.events.slice(0, 12)
                KeyValue {
                    x: Theme.horizontalPageMargin
                    width: page.width - 2 * Theme.horizontalPageMargin
                    label: clock(modelData.ms)
                    value: eventText(modelData)
                }
            }
        }
    }
}
