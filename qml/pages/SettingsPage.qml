import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    // Persisted; re-applied on launch by the main window.
    ConfigurationValue {
        id: cfgRootHelper
        key: "/apps/harbour-sysmetrics/rootHelperEnabled"
        defaultValue: false
    }

    // Turning root mode on is a deliberate act, so it goes through a dialog
    // that says what is being granted. Not an authentication: on Sailfish the
    // device-lock daemon is reachable only through /run/nemo-devicelock/socket,
    // mode 0660 group "privileged", which an unprivileged app is not in —
    // requests from here are ignored without an error. And it would not be a
    // security boundary anyway: the helper unit is startable by any process of
    // this user. What the dialog buys is that nothing starts it silently.
    function askForRoot() {
        var dlg = pageStack.push(Qt.resolvedUrl("RootConfirmDialog.qml"))
        dlg.accepted.connect(page.grantRoot)
    }

    function grantRoot() {
        cfgRootHelper.value = true
        rootmon.setHelper(true)
    }

    function stepInterval(delta) {
        var v = Math.round((sysmon.intervalMs + delta) / 100) * 100
        v = Math.max(500, Math.min(30000, v))
        sysmon.intervalMs = v
        intervalSlider.value = v
    }

    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Settings") }

            SectionHeader { text: qsTr("Sampling") }
            Slider {
                id: intervalSlider
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                minimumValue: 500; maximumValue: 30000; stepSize: 100
                value: sysmon.intervalMs
                valueText: (value / 1000).toFixed(1) + " s"
                label: qsTr("Refresh interval")
                onReleased: sysmon.intervalMs = value
            }

            // Fine positioning: ‹ ±0.1 s, ‹‹ ±1 s.
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingLarge
                Repeater {
                    model: [
                        { t: "‹‹", d: -1000 },
                        { t: "‹",  d: -100 },
                        { t: "›",  d: 100 },
                        { t: "››", d: 1000 }
                    ]
                    BackgroundItem {
                        width: Theme.itemSizeSmall
                        height: Theme.itemSizeSmall
                        onClicked: page.stepInterval(modelData.d)
                        Label {
                            anchors.centerIn: parent
                            text: modelData.t
                            font.pixelSize: Theme.fontSizeLarge
                            color: Diag.cyan
                        }
                    }
                }
            }
            TextSwitch {
                text: qsTr("Pause sampling")
                checked: sysmon.paused
                onClicked: sysmon.paused = checked
            }

            SectionHeader { text: qsTr("Language") }
            ComboBox {
                id: langCombo
                width: page.width
                label: qsTr("Language")
                currentIndex: applang.language === "de" ? 1 : applang.language === "en" ? 2 : 0
                menu: ContextMenu {
                    MenuItem { text: qsTr("System default") }
                    MenuItem { text: "Deutsch" }
                    MenuItem { text: "English" }
                }
                onCurrentIndexChanged: {
                    var v = currentIndex === 1 ? "de" : currentIndex === 2 ? "en" : "system"
                    if (v !== applang.language) {
                        applang.language = v
                        langHint.visible = true
                    }
                }
            }
            Label {
                id: langHint
                visible: false
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Diag.amber
                text: qsTr("Restart the app to apply the language.")
            }

            SectionHeader { text: qsTr("Root mode") }
            TextSwitch {
                text: qsTr("Use the root helper")
                description: qsTr("Without root, foreign-user processes (system daemons) "
                    + "expose only their basic figures; open files, devices, sockets, "
                    + "the access monitor and connection ownership stay empty. This "
                    + "starts a root helper service the app reads them through — it "
                    + "also unlocks the kernel charger log and journal excerpts for "
                    + "bug reports. It stops itself when the app is gone and is never "
                    + "started at boot.")
                checked: cfgRootHelper.value
                automaticCheck: false
                onClicked: {
                    if (cfgRootHelper.value) {     // giving rights back needs no question
                        cfgRootHelper.value = false
                        rootmon.setHelper(false)
                    } else {
                        page.askForRoot()
                    }
                }
            }
            Item {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                height: statusRow.height
                Row {
                    id: statusRow
                    spacing: Theme.paddingMedium
                    Rectangle {
                        width: Theme.paddingMedium; height: Theme.paddingMedium
                        radius: width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        color: rootmon.active ? Diag.green : Theme.secondaryColor
                    }
                    Label {
                        text: rootmon.active ? qsTr("Helper connected — full access")
                                          : qsTr("Helper not running")
                        color: rootmon.active ? Diag.green : Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
            SectionHeader { text: qsTr("About") }
            BackgroundItem {
                width: page.width
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("About SysMetrics")
                    color: Theme.primaryColor
                }
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
