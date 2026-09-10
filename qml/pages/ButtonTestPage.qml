import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Press a button, see it named. The event nodes are opened read-only and never
// grabbed, so every press still reaches the system as usual — volume changes,
// the power key blanks the screen. The test proves a button reached the kernel;
// it does not take the button away from anybody.
Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    property var helpTopics: ["buttons"]
    function _attachHelp() {
        if (_helpAttached) return
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"), { topics: helpTopics })
        if (p) _helpAttached = true
    }

    // Listening costs a handful of open descriptors and nothing else, but it
    // ends with the page: nothing keeps reading once the reader has left.
    onStatusChanged: {
        if (status === PageStatus.Active) {
            _attachHelp()
            // Only on the first visit: swiping to the glossary and back must
            // not wipe the tiles the reader has already lit.
            if (page.deviceGroups.length === 0) {
                keymon.refresh()
                page.buildGroups()
            }
            keymon.start()
        } else if (status === PageStatus.Deactivating) {
            keymon.stop()
        }
    }
    Component.onDestruction: keymon.stop()

    // The tiles are built once, from the codes the drivers declare, and never
    // rebuilt: a press only changes what a tile looks like. Rebuilding the row
    // on every event would make the whole list flicker under the finger.
    // Each tile keeps its index into keymon.keys and reads its own state there.
    property var deviceGroups: []
    readonly property int untestedCount: {
        var n = 0
        for (var i = 0; i < keymon.devices.length; ++i)
            if (keymon.devices[i].touch) ++n
        return n
    }
    function buildGroups() {
        var byNode = {}
        var order = []
        var all = keymon.keys
        for (var i = 0; i < all.length; ++i) {
            var k = all[i]
            if (byNode[k.node] === undefined) {
                byNode[k.node] = { name: k.device, node: k.node, keys: [] }
                order.push(k.node)
            }
            byNode[k.node].keys.push({ name: k.name, code: k.code, idx: i })
        }
        var out = []
        for (var j = 0; j < order.length; ++j)
            out.push(byNode[order[j]])
        page.deviceGroups = out
    }

    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        PullDownMenu {
            MenuItem {
                text: qsTr("Start over")
                onClicked: keymon.resetSeen()
            }
        }

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Button test")
                description: keymon.listening
                             ? qsTr("%1 of %2 codes seen").arg(keymon.seenCount).arg(keymon.keys.length)
                             : qsTr("not listening")
            }

            // --- what just happened ------------------------------------------
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall

                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: keymon.lastCode >= 0 ? keymon.lastKey : qsTr("Press a button")
                    font.pixelSize: keymon.lastCode >= 0 ? Theme.fontSizeExtraLarge
                                                         : Theme.fontSizeLarge
                    color: keymon.lastCode < 0 ? Theme.secondaryColor
                         : keymon.lastPressed ? Diag.green : Diag.cyan
                    wrapMode: Text.Wrap
                }
                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    visible: keymon.lastCode >= 0
                    text: (keymon.lastPressed ? qsTr("pressed") : qsTr("released"))
                          + "  ·  " + qsTr("code %1").arg(keymon.lastCode)
                          + "  ·  " + keymon.lastDevice
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    wrapMode: Text.Wrap
                }
                Label {
                    width: parent.width
                    visible: keymon.openError.length > 0
                    text: keymon.openError
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Diag.amber
                    wrapMode: Text.Wrap
                }
            }

            // --- the codes the drivers declare -------------------------------
            SectionHeader { text: qsTr("What the drivers declare") }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("One tile per key code the input drivers register, grouped by the device that registers it. A tile lights up the first time that code actually arrives. A tile that stays dark was not pressed — which is not the same as missing: a keypad driver registers its whole matrix whether the buttons are fitted or not, and the same button often appears under more than one device.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }

            Repeater {
                model: page.deviceGroups
                Column {
                    x: Theme.horizontalPageMargin
                    width: page.width - 2 * Theme.horizontalPageMargin
                    spacing: Theme.paddingSmall
                    Item { width: 1; height: Theme.paddingSmall }
                    Label {
                        width: parent.width
                        text: modelData.name + "  ·  " + modelData.node
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryHighlightColor
                        wrapMode: Text.Wrap
                    }
                    Flow {
                        width: parent.width
                        spacing: Theme.paddingSmall
                        Repeater {
                            model: modelData.keys
                            Rectangle {
                                property var st: keymon.keys[modelData.idx]
                                property bool seen: st !== undefined && st.seen === true
                                property bool down: st !== undefined && st.pressed === true
                                width: chip.width + 2 * Theme.paddingMedium
                                height: chip.height + Theme.paddingSmall
                                radius: Theme.paddingSmall
                                color: down ? Diag.green
                                     : seen ? Qt.rgba(Diag.green.r, Diag.green.g, Diag.green.b, 0.2)
                                     : Diag.panel
                                border.width: 1
                                border.color: seen ? Diag.green : Diag.grid
                                Label {
                                    id: chip
                                    anchors.centerIn: parent
                                    text: modelData.name
                                    font.pixelSize: Theme.fontSizeTiny
                                    font.family: "monospace"
                                    color: parent.down ? "black"
                                         : parent.seen ? Diag.green : Theme.secondaryColor
                                }
                            }
                        }
                    }
                }
            }

            // --- what is not tested ------------------------------------------
            SectionHeader {
                text: qsTr("Not part of the test")
                visible: page.untestedCount > 0
            }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                visible: page.untestedCount > 0
                Repeater {
                    model: keymon.devices
                    KeyValue {
                        visible: modelData.touch
                        label: modelData.name
                        value: qsTr("touch surface — its key codes belong to the glass, not to a button")
                        valueColor: Theme.secondaryColor
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("The presses are read, not taken: the nodes are opened read-only and never grabbed, so the system sees every one of them at the same time. Expect the volume to change and the power key to blank the screen while you test them.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
