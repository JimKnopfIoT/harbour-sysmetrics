import QtQuick 2.0
import Sailfish.Silica 1.0
import "."

// One measuring window of the redraw check: the verdict in words, then the
// figures it rests on, each threshold stated next to its value.
Column {
    id: block
    property var result: ({})
    property bool compact: false
    readonly property bool has: result !== undefined && result !== null && result.cls !== undefined
    x: Theme.horizontalPageMargin
    width: (parent ? parent.width : 0) - 2 * Theme.horizontalPageMargin
    spacing: Theme.paddingSmall
    visible: has || (result && result.message !== undefined)

    function f1(v) { return (v === undefined ? 0 : v).toFixed(1) }
    // "30/s" must not break at the slash: word joiners hold figure and unit.
    function nb(t) { return t.replace(/\/s/g, "\u2060/\u2060s") }

    function verdict(r) {
        if (r.cls === "quiet")
            return qsTr("lipstick is idle: nothing redraws without cause.")
        if (r.cls === "client") {
            var names = []
            for (var i = 0; i < r.clients.length; i++) names.push(r.clients[i].name)
            return qsTr("An app hands lipstick new frames: %1. lipstick only composes them.").arg(names.join(", "))
        }
        if (r.cls === "lipstick-main")
            return r.stuck
                ? qsTr("lipstick redraws every display frame without cause, and the load sits in its main thread (QML). Typical of an animation that keeps running out of sight.")
                : qsTr("lipstick draws; its main thread does most of the work.")
        return qsTr("lipstick draws demanding graphics; the load sits in its render thread.")
    }

    Label {
        visible: result && result.message !== undefined
        width: parent.width
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeSmall
        text: result && result.message !== undefined ? result.message : ""
    }
    Label {
        visible: has && !result.valid
        width: parent.width
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeSmall
        color: Diag.red
        text: has ? qsTr("Invalid: %1. Measure again.").arg(result.invalid) : ""
    }
    Label {
        visible: has
        width: parent.width
        wrapMode: Text.Wrap
        font.pixelSize: compact ? Theme.fontSizeExtraSmall : Theme.fontSizeSmall
        color: has && (result.stuck || result.cls === "lipstick-render") ? Diag.amber : Theme.primaryColor
        text: has ? verdict(result) : ""
    }
    KeyValue {
        visible: has
        label: qsTr("Render wakeups")
        value: has ? nb(qsTr("%1/s  ·  idle below %2/s, continuous from %3/s").arg(f1(result.renderWake)).arg(result.quietWake).arg(result.hotWake)) : ""
        valueColor: has && result.renderWake >= result.hotWake ? Diag.amber : Theme.primaryColor
    }
    KeyValue {
        visible: has
        label: qsTr("Frames to the display")
        value: !has ? ""
             : result.frames >= 0 ? nb(qsTr("%1/s  (%2, seen at zero while idle)").arg(f1(result.frames)).arg(result.frameSource))
             : qsTr("not countable here: no display interrupt has yet been seen at zero while lipstick was idle")
    }
    KeyValue {
        visible: has && result.wakePerFrame > 0 && result.renderWake >= result.quietWake
        label: qsTr("Wakeups per frame")
        value: has ? qsTr("%1  ·  stuck state ≈ 1, real animation 3–4").arg(f1(result.wakePerFrame)) : ""
    }
    KeyValue {
        visible: has
        label: qsTr("lipstick")
        value: has ? qsTr("%1 % of one core").arg(f1(result.lipstickPct)) : ""
        // Coloured by this measurement's verdict: 20 % is unremarkable on the
        // generic load ramp but is the fault here, against an idle 0.6 %.
        valueColor: has && result.stuck ? Diag.amber : Theme.primaryColor
    }
    KeyValue {
        visible: has && !compact
        label: qsTr("  main thread")
        value: has ? qsTr("%1 %  ·  QML, bindings, animations").arg(f1(result.mainPct)) : ""
    }
    KeyValue {
        visible: has && !compact
        label: qsTr("  render thread")
        value: has ? qsTr("%1 %  ·  frames to the GPU").arg(f1(result.renderPct)) : ""
    }
    KeyValue {
        visible: has && !compact
        label: qsTr("  GPU driver")
        value: has ? f1(result.gpuPct) + " %" : ""
    }
    KeyValue {
        visible: has && !compact
        label: qsTr("  composer calls")
        value: has ? f1(result.binderPct) + " %" : ""
    }
    KeyValue {
        visible: has && !compact
        label: qsTr("Composer service")
        value: has ? qsTr("%1 % of one core").arg(f1(result.composerPct)) : ""
    }
    Repeater {
        model: has && !compact ? result.clients : []
        KeyValue {
            label: qsTr("App in step")
            value: nb(qsTr("%1 (PID %2): %3 wakeups/s, %4 %").arg(modelData.name).arg(modelData.pid)
                   .arg(f1(modelData.wake)).arg(f1(modelData.cpu)))
            valueColor: Diag.amber
        }
    }
    Repeater {
        model: has && !compact ? result.threads : []
        KeyValue {
            label: modelData.name
            value: nb(qsTr("%1 %, %2 wakeups/s").arg(f1(modelData.cpu)).arg(f1(modelData.wake)))
                   + (modelData.role.length ? "  ·  " + modelData.role : "")
            mono: true
        }
    }
}
