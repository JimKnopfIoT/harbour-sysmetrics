pragma Singleton
import QtQuick 2.0
import Sailfish.Silica 1.0

// Shared palette and helpers. Cool accents tuned to read on the dark tile.
QtObject {
    readonly property color cyan:   "#19d2ff"
    readonly property color teal:   "#31e0a0"
    readonly property color green:  "#8ef94a"
    readonly property color amber:  "#ffb44a"
    readonly property color red:    "#ff5a52"
    readonly property color violet: "#b58cff"

    readonly property color panel:   Qt.rgba(1, 1, 1, 0.045)
    readonly property color panelHi: Qt.rgba(1, 1, 1, 0.09)
    readonly property color grid:    Qt.rgba(1, 1, 1, 0.07)

    // load -> color ramp: calm cyan up to hot red
    function loadColor(pct) {
        if (pct >= 80) return red
        if (pct >= 50) return amber
        if (pct >= 20) return teal
        return cyan
    }

    // severity level (0 ok .. 3 alarm) -> color
    function levelColor(lvl) {
        if (lvl >= 3) return red
        if (lvl === 2) return amber
        if (lvl === 1) return violet
        return teal
    }

    function stateColor(s) {
        if (s === "R") return green
        if (s === "D") return red
        if (s === "Z") return violet
        if (s === "T" || s === "t") return amber
        return Theme.secondaryColor
    }

    function stateLabel(s) {
        switch (s) {
        case "R": return qsTr("running")
        case "S": return qsTr("sleeping")
        case "D": return qsTr("uninterruptible")
        case "Z": return qsTr("zombie")
        case "T": return qsTr("stopped")
        case "t": return qsTr("traced")
        case "I": return qsTr("idle")
        default:  return s
        }
    }
}
