import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Everything that reports the world to the phone: the QtSensors readings and
// GPS, the buttons on its sides, and the fingerprint reader.
//
// The live sensor values come through a Loader because QtSensors and
// QtPositioning are separate QML plugins that need not be installed. Buttons
// and the fingerprint reader do not depend on either, so they live on this page
// and stay readable when the sensor plugins are missing.
Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    property var helpTopics: ["sensors", "buttons", "fingerprint"]
    function _attachHelp() {
        if (_helpAttached) return
        if (helpTopics && helpTopics.length === 0) { _helpAttached = true; return }
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"), { topics: helpTopics })
        if (p) _helpAttached = true
    }
    onStatusChanged: if (status === PageStatus.Active) { _attachHelp(); page.refreshHw() }

    function refreshHw() {
        keymon.refresh()
        fpmon.refresh()
    }
    Component.onCompleted: refreshHw()

    // Devices that report keys and are not the touchscreen: the buttons.
    function buttonDevices() {
        var out = []
        var all = keymon.devices
        for (var i = 0; i < all.length; ++i)
            if (all[i].hasKeys && !all[i].touch)
                out.push(all[i])
        return out
    }

    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Sensors & GPS") }

            Loader {
                id: sensorLoader
                width: page.width
                // implicitHeight, not height. A Loader that has a size of its
                // own resizes the item it loaded to match, so binding the
                // Loader's height to the item's height is a circle: the item
                // reports back the height the Loader just gave it and both
                // settle at nothing. The loaded item is a Column, whose
                // implicit height is its content and does not depend on the
                // height it was assigned — that breaks the circle. With the
                // Loader at zero the sensor readings still painted, but
                // everything below them was positioned as if they took no
                // room, and the page drew two layers on top of each other.
                height: item ? item.implicitHeight : 0
                source: Qt.resolvedUrl("SensorContent.qml")
            }

            // --- QtSensors plugin missing -----------------------------------
            Column {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                visible: sensorLoader.status === Loader.Error
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Sensor and GPS support is not installed")
                    color: Diag.amber
                }
                BackgroundItem {
                    width: parent.width
                    height: cmdLabel.height + Theme.paddingMedium
                    onClicked: {
                        Clipboard.text = cmdLabel.text
                        copied.visible = true
                        copiedTimer.restart()
                    }
                    Label {
                        id: cmdLabel
                        anchors.centerIn: parent
                        width: parent.width - 2 * Theme.paddingMedium
                        wrapMode: Text.WrapAnywhere
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Theme.fontSizeExtraSmall
                        font.family: "monospace"
                        color: Diag.cyan
                        text: "devel-su pkcon install qt5-qtdeclarative-import-sensors qt5-qtdeclarative-import-positioning"
                    }
                }
                Label {
                    id: copied
                    width: parent.width
                    visible: false
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Copied to clipboard — paste in the Terminal app")
                    font.pixelSize: Theme.fontSizeTiny
                    color: Diag.green
                    Timer { id: copiedTimer; interval: 2500; onTriggered: copied.visible = false }
                }
            }

            // --- hardware buttons -------------------------------------------
            SectionHeader { text: qsTr("Hardware buttons") }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2

                Repeater {
                    model: page.buttonDevices()
                    KeyValue {
                        label: modelData.name
                        value: qsTr("%1 key codes declared").arg(modelData.keyCount)
                               + "  ·  " + modelData.node
                    }
                }
                Label {
                    width: parent.width
                    visible: page.buttonDevices().length === 0
                    text: qsTr("No button device found in /dev/input.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Diag.amber
                }
                Item { width: 1; height: Theme.paddingSmall }
                Label {
                    width: parent.width
                    text: qsTr("A driver registers every code it could ever send, so a count here is what the driver declares — not how many buttons are on the phone. Only a press proves a button.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeTiny
                    color: Theme.secondaryColor
                }
            }

            // Switches are a state, not an event: readable at any moment.
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                visible: keymon.switches.length > 0 || keymon.heldKeys.length > 0
                Item { width: 1; height: Theme.paddingSmall }
                Label {
                    width: parent.width
                    text: qsTr("Switches")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                }
                Repeater {
                    model: keymon.switches
                    KeyValue {
                        label: modelData.name
                        value: modelData.closed ? qsTr("set") : qsTr("not set")
                        valueColor: modelData.closed ? Diag.green : Theme.primaryColor
                    }
                }
                // A key code that is down while nothing is being pressed is a
                // position, not a press: a latching switch wired as a key.
                Repeater {
                    model: keymon.heldKeys
                    KeyValue {
                        label: modelData.name
                        value: qsTr("held down  ·  code %1").arg(modelData.code)
                               + "  ·  " + modelData.device
                        valueColor: Diag.green
                    }
                }
                Label {
                    width: parent.width
                    visible: keymon.heldKeys.length > 0
                    text: qsTr("A key code that is down while you are not pressing anything is a switch in a position, not a button. Nothing on this system is configured to act on such a code — it is reported and goes nowhere.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeTiny
                    color: Theme.secondaryColor
                }
            }

            ButtonLayout {
                Button {
                    text: qsTr("Test buttons")
                    enabled: page.buttonDevices().length > 0
                    onClicked: pageStack.push(Qt.resolvedUrl("ButtonTestPage.qml"))
                }
            }

            // --- fingerprint reader -----------------------------------------
            SectionHeader { text: qsTr("Fingerprint reader") }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2

                KeyValue {
                    label: qsTr("Sensor")
                    mono: true
                    value: fpmon.sensor.compatible ? fpmon.sensor.compatible
                                                   : qsTr("none named in the device tree")
                    valueColor: fpmon.sensor.compatible ? Theme.primaryColor : Theme.secondaryColor
                }
                KeyValue {
                    visible: fpmon.sensor.driver !== undefined
                    label: qsTr("Driver")
                    mono: true
                    value: fpmon.sensor.driver + "  ·  " + (fpmon.sensor.device || "")
                }
                KeyValue {
                    visible: fpmon.sensor.node !== undefined
                    label: qsTr("Device node")
                    mono: true
                    value: fpmon.sensor.node || ""
                }
                KeyValue {
                    label: qsTr("Fingerprint daemon")
                    value: fpmon.daemonPresent ? fpmon.state : qsTr("not answering")
                    valueColor: fpmon.daemonPresent ? Diag.green : Diag.amber
                    mono: fpmon.daemonPresent
                }
                KeyValue {
                    label: qsTr("Enrolled fingers")
                    value: fpmon.fingers.length
                           ? fpmon.fingers.length + "  ·  " + fpmon.fingers.join(", ")
                           : qsTr("none")
                }
            }

            ButtonLayout {
                Button {
                    text: qsTr("Test the reader")
                    enabled: fpmon.daemonPresent
                    onClicked: pageStack.push(Qt.resolvedUrl("FingerprintPage.qml"))
                }
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
