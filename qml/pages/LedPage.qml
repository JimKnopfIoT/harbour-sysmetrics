import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// The LEDs in /sys/class/leds and a function test for the indicator LEDs.
//
// The test switches each indicator channel on alone, then all together, and
// puts the old values back. Whether it lit is for the eye to judge; the page
// shows which channel should be on. Channels a kernel trigger drives are left
// alone. Where brightness belongs to the system, the root helper writes it;
// the chip's own pattern (blink, breath) is writable by root only.
Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    function _attachHelp() {
        if (_helpAttached) return
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"), { topics: ["leds"] })
        if (p) _helpAttached = true
    }
    onStatusChanged: {
        if (status === PageStatus.Active) {
            _attachHelp()
            leds.refresh()
        }
    }
    Component.onDestruction: leds.cancel()

    // channel lists for the test buttons and the root note
    function channels(test) {
        var out = []
        for (var i = 0; i < leds.leds.length; ++i) {
            var l = leds.leds[i]
            if (l.testable && test(l)) out.push(l.name)
        }
        return out
    }
    readonly property var ownChannels: channels(function(l) { return l.writable })
    readonly property var rootChannels: channels(function(l) { return !l.writable })
    readonly property var patternChannels: channels(function(l) { return l.pattern.length > 0 })
    readonly property var ownPatternChannels: channels(function(l) { return l.patternWritable })
    readonly property bool canTest: ownChannels.length > 0 || (rootmon.active && rootChannels.length > 0)
    readonly property bool canPattern: ownPatternChannels.length > 0
                                       || (rootmon.active && patternChannels.length > 0)

    function colourOf(name) {
        var parts = name.toLowerCase().split(/[:_\-]/)
        var map = { red: "#ff3b30", green: "#34e05a", blue: "#3a7bff", white: "#ffffff",
                    amber: "#ffb000", yellow: "#ffe14a", orange: "#ff8a1f" }
        for (var i = 0; i < parts.length; ++i)
            if (map[parts[i]]) return map[parts[i]]
        return Theme.secondaryColor
    }
    function kindLabel(k) {
        switch (k) {
        case "indicator": return qsTr("indicator")
        case "backlight": return qsTr("backlight")
        case "vibrator":  return qsTr("vibration motor")
        case "flash":     return qsTr("camera flash")
        default:          return qsTr("other")
        }
    }
    function resultColor() {
        switch (leds.result) {
        case "ok":   return Diag.green
        case "note": return Diag.amber
        case "fail": return Diag.red
        default:     return Theme.secondaryColor
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

            PageHeader { title: qsTr("LEDs") }

            // --- the test ----------------------------------------------------
            SectionHeader { text: qsTr("Indicator test") }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Each indicator channel lights alone at full brightness for 1.5 s, then all together. Watch the LED; the circle below shows what should be on. Afterwards the previous values are written back.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.highlightColor
            }

            Item {
                width: page.width
                height: Theme.itemSizeHuge
                Rectangle {
                    id: swatch
                    anchors.centerIn: parent
                    width: Theme.itemSizeLarge; height: width; radius: width / 2
                    color: leds.step === "all" || leds.step === "pattern" ? "#ffffff"
                         : leds.step === "" ? "transparent"
                         : page.colourOf(leds.step)
                    border.width: 2
                    border.color: leds.running ? Theme.highlightColor : Theme.secondaryColor
                    SequentialAnimation on opacity {
                        running: leds.step === "pattern"
                        loops: Animation.Infinite
                        onStopped: swatch.opacity = 1
                        NumberAnimation { from: 1; to: 0.2; duration: 900 }
                        NumberAnimation { from: 0.2; to: 1; duration: 900 }
                    }
                }
                Label {
                    anchors { top: swatch.bottom; horizontalCenter: parent.horizontalCenter }
                    text: leds.step === "all" ? qsTr("all channels")
                        : leds.step === "pattern" ? qsTr("the chip's own pattern")
                        : leds.step
                    font.pixelSize: Theme.fontSizeExtraSmall
                    font.family: leds.step === "all" || leds.step === "pattern" ? Theme.fontFamily : "monospace"
                    color: Theme.primaryColor
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                visible: !leds.running
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: leds.resultText.length ? leds.resultText : qsTr("Not started")
                font.pixelSize: Theme.fontSizeSmall
                color: page.resultColor()
            }

            ButtonLayout {
                Button {
                    text: leds.running ? qsTr("Cancel") : qsTr("Test the colours")
                    enabled: leds.running || page.canTest
                    onClicked: leds.running ? leds.cancel() : leds.startTest()
                }
                Button {
                    visible: page.patternChannels.length > 0
                    text: qsTr("Chip pattern")
                    enabled: !leds.running && page.canPattern
                    onClicked: leds.startPatternTest()
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                visible: leds.indicatorCount === 0
                text: qsTr("No indicator LED found. The test only switches LEDs whose name names a colour or a status light; flash, backlight and vibration motor are left alone.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }

            // What the root helper adds on this phone, greyed while it is off.
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                visible: page.rootChannels.length > 0 || page.patternChannels.length > 0

                Label {
                    width: parent.width
                    text: rootmon.active ? qsTr("Root helper active") : qsTr("With the root helper")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: rootmon.active ? Diag.green : Theme.secondaryColor
                }
                Label {
                    width: parent.width
                    visible: page.rootChannels.length > 0
                    text: qsTr("brightness of %1 belongs to the system. The helper switches it for the test; without it these channels are left out.")
                          .arg(page.rootChannels.join(", "))
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: rootmon.active ? Theme.primaryColor : Theme.secondaryColor
                    opacity: rootmon.active ? 1 : 0.6
                }
                Label {
                    width: parent.width
                    visible: page.patternChannels.length > 0 && page.ownPatternChannels.length === 0
                    text: qsTr("“Chip pattern” starts the LED controller's own blinking on %1 for 6 s. It runs in the chip, without the processor. The switch for it belongs to root.")
                          .arg(page.patternChannels.join(", "))
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: rootmon.active ? Theme.primaryColor : Theme.secondaryColor
                    opacity: rootmon.active ? 1 : 0.6
                }
                Label {
                    width: parent.width
                    visible: !rootmon.active
                    text: qsTr("Settings → Use the root helper.")
                    font.pixelSize: Theme.fontSizeTiny
                    color: Theme.secondaryColor
                }
            }

            // --- vibration ---------------------------------------------------
            SectionHeader {
                text: qsTr("Vibration")
                visible: leds.vibrator.length > 0
            }
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                visible: leds.vibrator.length > 0
                text: qsTr("Three short pulses written straight to %1. This goes past the feedback service and the profile, so the motor runs even with vibration switched off in the settings.")
                      .arg(leds.vibrator)
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.highlightColor
            }
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                visible: leds.vibrator.length > 0 && !leds.vibrating && leds.vibeText.length > 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: leds.vibeText
                font.pixelSize: Theme.fontSizeSmall
                color: leds.vibeResult === "ok" ? Diag.green : Diag.red
            }
            ButtonLayout {
                visible: leds.vibrator.length > 0
                Button {
                    text: qsTr("Vibrate 3× briefly")
                    enabled: !leds.vibrating && leds.vibratorWritable
                    onClicked: leds.startVibration()
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                visible: leds.vibrator.length > 0 && !leds.vibratorWritable
                text: qsTr("activate and duration of %1 are not writable for this app.").arg(leds.vibrator)
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }

            // --- the nodes ---------------------------------------------------
            SectionHeader { text: qsTr("/sys/class/leds") }

            Repeater {
                model: leds.leds
                Column {
                    id: block
                    x: Theme.horizontalPageMargin
                    width: page.width - 2 * Theme.horizontalPageMargin
                    spacing: Theme.paddingSmall / 2
                    property bool open: false

                    Row {
                        spacing: Theme.paddingMedium
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.paddingLarge; height: width; radius: width / 2
                            color: modelData.kind === "indicator" ? page.colourOf(modelData.name) : "transparent"
                            border.width: 1
                            border.color: Theme.secondaryColor
                        }
                        Label {
                            text: modelData.name
                            font.family: "monospace"
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.highlightColor
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.kindLabel(modelData.kind)
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryColor
                        }
                    }
                    KeyValue {
                        label: qsTr("Driver")
                        mono: true
                        value: modelData.driver
                        visible: modelData.driver.length > 0
                    }
                    KeyValue {
                        label: qsTr("Device tree")
                        mono: true
                        value: modelData.compatible
                        visible: modelData.compatible.length > 0
                    }
                    KeyValue {
                        label: qsTr("Device")
                        mono: true
                        value: modelData.device
                        visible: modelData.device.length > 0
                    }
                    KeyValue {
                        label: qsTr("Brightness")
                        value: modelData.brightness >= 0
                               ? qsTr("%1 of %2").arg(modelData.brightness).arg(modelData.max)
                               : qsTr("max. %1 · not read").arg(modelData.max)
                    }
                    KeyValue {
                        label: qsTr("Trigger")
                        mono: modelData.trigger.length > 0
                        visible: modelData.triggerCount > 0
                        value: (modelData.trigger.length ? modelData.trigger : qsTr("none selected"))
                               + "  ·  " + qsTr("%1 available").arg(modelData.triggerCount)
                        valueColor: modelData.kind === "indicator" && modelData.trigger.length
                                    && modelData.trigger !== "none" ? Diag.amber : Theme.primaryColor
                    }
                    KeyValue {
                        label: qsTr("Switchable")
                        visible: modelData.kind === "indicator"
                        value: !modelData.testable ? qsTr("no — a trigger drives it")
                             : modelData.writable ? qsTr("yes, without root")
                             : qsTr("root helper only")
                        valueColor: !modelData.testable ? Diag.amber
                                  : modelData.writable ? Diag.green : Theme.primaryColor
                    }
                    BackgroundItem {
                        width: parent.width
                        height: Theme.itemSizeExtraSmall
                        onClicked: block.open = !block.open
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: (block.open ? "▲ " : "▼ ")
                                  + qsTr("%n attribute(s)", "", modelData.attrs.length)
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Diag.cyan
                        }
                    }
                    Repeater {
                        model: block.open ? modelData.attrs : []
                        KeyValue {
                            label: modelData.name
                            mono: true
                            value: modelData.perm
                            valueColor: modelData.perm.charAt(7) === "w" ? Diag.amber : Theme.secondaryColor
                        }
                    }
                    Item { width: 1; height: Theme.paddingMedium }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Attribute values are not read here: on some drivers a read is a register access, and flash controllers clear their fault latch on reading. Shown are owner and mode; amber marks a file any program may write.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
