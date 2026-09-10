import QtQuick 2.0
import Sailfish.Silica 1.0
// The Diag singleton lives in this directory's qmldir; without importing the
// directory it does not resolve, the colour binding fails and the value is
// drawn in an invalid colour - dark grey on a dark page.
import "."

// How fast this medium reads — measured, because nothing reports it.
//
// The figure comes from reading a file that is already on the medium, with the
// page cache bypassed. Nothing is written: a write would cost the card cells,
// and on a raw device a slip would cost the partition table. The class printed
// on a microSD is not shown at all, because it lives in a register the kernel
// does not export and every figure claiming otherwise is guessed.
Column {
    id: block
    width: parent ? parent.width : 0
    spacing: Theme.paddingSmall

    property string mountPoint
    property string mediumLabel
    property var sample: ({})
    property var outcome: ({})
    property var verdict: ({})
    // What the same measurement gives on this phone's built-in storage. The
    // only comparison here that is not borrowed from a catalogue.
    property real reference: 0
    property string problem: ""

    SectionHeader { text: qsTr("Read speed") }

    Label {
        x: Theme.horizontalPageMargin
        width: block.width - 2 * Theme.horizontalPageMargin
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryColor
        text: qsTr("No medium states how fast it is; a figure has to be measured. This reads a file that is already here, straight from the medium rather than out of the cache, and writes nothing.")
    }

    Label {
        x: Theme.horizontalPageMargin
        width: block.width - 2 * Theme.horizontalPageMargin
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Diag.amber
        visible: block.problem.length > 0
        text: block.problem
    }

    Column {
        x: Theme.horizontalPageMargin
        width: block.width - 2 * Theme.horizontalPageMargin
        spacing: Theme.paddingSmall / 2
        visible: block.outcome.ok === true

        KeyValue {
            label: qsTr("Read speed")
            value: block.outcome.mbPerSecond !== undefined
                   ? block.outcome.mbPerSecond.toFixed(1) + " MB/s" : ""
            valueColor: Diag.green
        }
        KeyValue {
            label: qsTr("Read")
            value: block.outcome.bytes !== undefined
                   ? sysmon.fmtBytes(block.outcome.bytes)
                     + "  ·  " + block.outcome.seconds.toFixed(2) + " s"
                     + "  ·  " + qsTr("%n pass(es)", "", block.outcome.runs || 1) : ""
        }
        KeyValue {
            label: qsTr("Spread between passes")
            value: block.outcome.spreadPercent !== undefined
                   ? block.outcome.spreadPercent.toFixed(1) + " %"
                     + "  ·  " + block.outcome.slowest.toFixed(0)
                     + "–" + block.outcome.fastest.toFixed(0) + " MB/s" : ""
            valueColor: block.verdict.reliable === true ? Diag.green : Diag.amber
        }
        KeyValue {
            label: qsTr("Block size")
            value: (block.outcome.blockKiB || 0) + " KiB"
        }
        KeyValue {
            label: qsTr("Straight from the medium")
            value: block.outcome.direct ? qsTr("yes") : qsTr("no — through the cache")
            valueColor: block.outcome.direct ? Diag.green : Diag.amber
        }
        KeyValue {
            label: qsTr("File read")
            value: block.outcome.path || ""
            mono: true
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeTiny
            color: Theme.secondaryColor
            visible: block.outcome.note !== undefined
            text: block.outcome.note !== undefined ? block.outcome.note : ""
        }

        // The reading of the figure, kept apart from the figure itself: a
        // judgement must not look like a measurement.
        Item { width: 1; height: Theme.paddingSmall }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: block.verdict.reliable === true ? Theme.highlightColor : Diag.amber
            visible: block.verdict.basis !== undefined
            text: block.verdict.basis !== undefined ? block.verdict.basis : ""
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            visible: block.verdict.verdict !== undefined
            text: block.verdict.verdict !== undefined ? block.verdict.verdict : ""
        }
    }

    ButtonLayout {
        Button {
            text: readtest.busy ? qsTr("Reading…") : qsTr("Measure reading")
            enabled: !readtest.busy && block.mountPoint.length > 0
            onClicked: {
                block.problem = ""
                block.sample = readtest.findSample(block.mountPoint)
                if (block.sample.ok !== true) {
                    block.problem = block.sample.error !== undefined
                                    ? block.sample.error : qsTr("Nothing to measure against.")
                    return
                }
                block.outcome = readtest.measure(block.sample.path, 64, 3)
                if (block.outcome.ok !== true) {
                    block.problem = block.outcome.error !== undefined
                                    ? block.outcome.error : qsTr("The measurement failed.")
                    return
                }
                block.verdict = readtest.rate(block.outcome.mbPerSecond,
                                              block.outcome.spreadPercent,
                                              block.reference)
            }
        }
    }
}
