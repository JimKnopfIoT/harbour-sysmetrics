import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Function test of the built-in FM receiver: one sweep over 87.5–108 MHz.
//
// The chip is powered up muted, measures every 100 kHz channel and is powered
// down again. No sound is played and no audio route is changed. A chip that
// measures the whole band works; whether stations show up is a question of the
// antenna, and the page says so instead of calling a quiet band a fault.
Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    function _attachHelp() {
        if (_helpAttached) return
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"), { topics: ["fmradio"] })
        if (p) _helpAttached = true
    }
    onStatusChanged: {
        if (status === PageStatus.Active) {
            _attachHelp()
            fmtest.refresh()
        }
    }
    Component.onDestruction: fmtest.cancel()

    function resultColor() {
        switch (fmtest.result) {
        case "ok":   return Diag.green
        case "busy": return Diag.amber
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

            PageHeader { title: qsTr("FM radio test") }

            // --- the receiver ------------------------------------------------
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2

                KeyValue {
                    label: qsTr("Device node")
                    mono: true
                    value: fmtest.present ? "/dev/fm  ·  " + fmtest.nodeInfo : qsTr("none")
                }
                KeyValue {
                    label: qsTr("Driver")
                    mono: fmtest.driver.length > 0
                    value: fmtest.driver.length ? fmtest.driver : qsTr("not one this test knows")
                    valueColor: fmtest.driver.length ? Theme.primaryColor : Diag.amber
                }
                KeyValue {
                    label: qsTr("Access")
                    value: fmtest.accessible ? qsTr("read and write") : qsTr("denied")
                    valueColor: fmtest.accessible ? Theme.primaryColor : Diag.amber
                }
                KeyValue {
                    label: qsTr("In use by")
                    mono: fmtest.usedBy.length > 0
                    value: fmtest.usedBy.length ? fmtest.usedBy : qsTr("nobody")
                    valueColor: fmtest.usedBy.length ? Diag.amber : Theme.primaryColor
                }
            }

            // --- the test ----------------------------------------------------
            SectionHeader { text: qsTr("Test") }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("The antenna is the USB-C cable. For stations to come in, plug in a fully wired USB 3 or USB4 cable of at least one metre — charging and USB 2.0 cables lack the wires the antenna uses — let it hang straight and leave the other end free.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.highlightColor
            }

            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall

                ProgressBar {
                    width: parent.width
                    visible: fmtest.running
                    minimumValue: 0
                    maximumValue: 1
                    value: fmtest.progress
                    label: qsTr("Sweeping 87.5–108 MHz")
                }
                Label {
                    width: parent.width
                    visible: !fmtest.running
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: fmtest.resultText.length ? fmtest.resultText : qsTr("Not started")
                    font.pixelSize: Theme.fontSizeSmall
                    color: page.resultColor()
                }
                Label {
                    width: parent.width
                    visible: !fmtest.running && fmtest.seconds > 0
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("%1 s").arg(fmtest.seconds.toFixed(1))
                    font.pixelSize: Theme.fontSizeTiny
                    font.family: "monospace"
                    color: Theme.secondaryColor
                }
            }

            ButtonLayout {
                Button {
                    text: fmtest.running ? qsTr("Cancel") : qsTr("Sweep the band")
                    enabled: fmtest.running || (fmtest.supported && fmtest.accessible)
                    onClicked: fmtest.running ? fmtest.cancel() : fmtest.start()
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                visible: fmtest.present && !fmtest.supported
                text: qsTr("The FM ioctls depend on the driver and on 32 or 64 bit. They were measured on the Jolla Phone (2026) only, so on this phone the node is shown and the test is not offered.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }

            // --- spectrum ----------------------------------------------------
            SectionHeader {
                text: qsTr("Spectrum")
                visible: fmtest.spectrum.length > 0
            }

            Canvas {
                id: plot
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                height: Theme.itemSizeHuge * 1.4
                visible: fmtest.spectrum.length > 0

                property var data: fmtest.spectrum
                property var marks: fmtest.peaks
                onDataChanged: requestPaint()
                onWidthChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var d = data
                    if (!d || d.length < 2) return
                    var axisH = Theme.fontSizeTiny + Theme.paddingSmall
                    var h = height - axisH
                    var lo = d[0], hi = d[0]
                    for (var i = 1; i < d.length; ++i) { lo = Math.min(lo, d[i]); hi = Math.max(hi, d[i]) }
                    lo = Math.floor(lo / 10) * 10
                    hi = Math.max(lo + 20, Math.ceil(hi / 10) * 10)
                    function xOf(i) { return i * (width - 1) / (d.length - 1) }
                    function yOf(v) { return h - (v - lo) * h / (hi - lo) }

                    // dB grid every 10 dB, MHz ticks at 90/95/100/105
                    ctx.strokeStyle = Diag.grid
                    ctx.fillStyle = Theme.secondaryColor
                    ctx.lineWidth = 1
                    ctx.font = Theme.fontSizeTiny + "px monospace"
                    for (var v = lo; v <= hi; v += 10) {
                        ctx.beginPath(); ctx.moveTo(0, yOf(v)); ctx.lineTo(width, yOf(v)); ctx.stroke()
                    }
                    for (var m = 90; m <= 105; m += 5) {
                        var x = xOf((m - 87.5) * 10)
                        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke()
                        ctx.fillText(m, x - Theme.fontSizeTiny * 0.6, height - 2)
                    }

                    // floor, dashed by hand: the Qt 5.6 canvas has no setLineDash
                    ctx.strokeStyle = Diag.amber
                    var fy = yOf(fmtest.floorDbm)
                    ctx.beginPath()
                    for (var dx = 0; dx < width; dx += 10) { ctx.moveTo(dx, fy); ctx.lineTo(Math.min(dx + 5, width), fy) }
                    ctx.stroke()

                    // trace
                    ctx.strokeStyle = Diag.cyan
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    ctx.moveTo(xOf(0), yOf(d[0]))
                    for (var j = 1; j < d.length; ++j) ctx.lineTo(xOf(j), yOf(d[j]))
                    ctx.stroke()

                    // peaks
                    var p = marks || []
                    ctx.fillStyle = Diag.green
                    for (var k = 0; k < p.length; ++k) {
                        var px = xOf(Math.round((p[k].mhz - 87.5) * 10))
                        ctx.beginPath(); ctx.arc(px, yOf(p[k].dbm), 3, 0, 2 * Math.PI); ctx.fill()
                    }

                    // dB labels last, on a backing so the trace cannot run through them
                    for (var lv = lo; lv <= hi; lv += 10) {
                        var txt = lv + "\u00a0dBm"
                        var ly = Math.max(Theme.fontSizeTiny, yOf(lv) - 2)
                        ctx.fillStyle = Theme.rgba("black", 0.6)
                        ctx.fillRect(0, ly - Theme.fontSizeTiny, ctx.measureText(txt).width + 6, Theme.fontSizeTiny + 3)
                        ctx.fillStyle = Theme.secondaryColor
                        ctx.fillText(txt, 3, ly)
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                visible: fmtest.spectrum.length > 0
                text: qsTr("Floor (dashed) %1 dBm — the level 20 % of the channels stay under.")
                      .arg(fmtest.floorDbm.toFixed(0))
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }

            // --- peaks -------------------------------------------------------
            SectionHeader {
                text: qsTr("Peaks")
                visible: fmtest.spectrum.length > 0
            }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                visible: fmtest.spectrum.length > 0

                Repeater {
                    model: fmtest.peaks
                    KeyValue {
                        label: modelData.mhz.toFixed(1) + " MHz"
                        mono: true
                        value: modelData.dbm.toFixed(0) + " dBm"
                               + (modelData.valid ? "  ·  " + qsTr("chip: station") : "")
                        valueColor: modelData.valid ? Diag.green : Theme.primaryColor
                    }
                }
                Label {
                    width: parent.width
                    visible: fmtest.peaks.length === 0
                    text: qsTr("No channel stands 8 dB over the floor, and the chip flags none as a station.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
                Item { width: 1; height: Theme.paddingSmall }
                Label {
                    width: parent.width
                    text: qsTr("A peak is a channel at least 8 dB over the floor, or one the chip itself flags as a station. Without a cable the chip on the Jolla Phone (2026) still shows single peaks; in our measurements nothing audible was behind them. A quiet band means no antenna, not a broken chip.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeTiny
                    color: Theme.secondaryColor
                }
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
