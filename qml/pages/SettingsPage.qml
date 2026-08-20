import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

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
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Without root, foreign-user processes (system daemons) expose "
                    + "only their basic figures; open files, devices, sockets, the "
                    + "access monitor and connection ownership stay empty. Start the "
                    + "helper as root once, then the app connects to it automatically.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("In the Terminal app (Developer mode):")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WrapAnywhere
                font.pixelSize: Theme.fontSizeTiny
                font.family: "monospace"
                color: Diag.cyan
                text: "devel-su systemctl start harbour-sysmetrics-helper"
            }
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
                text: qsTr("Enter your developer password when asked. Use 'enable' instead "
                    + "of 'start' to keep it across reboots, 'stop' to end it. Alternatively "
                    + "run the helper directly:")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WrapAnywhere
                font.pixelSize: Theme.fontSizeTiny
                font.family: "monospace"
                color: Diag.cyan
                text: "devel-su /usr/bin/harbour-sysmetrics --root-helper"
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
            Button {
                x: Theme.horizontalPageMargin
                text: qsTr("Reconnect helper")
                onClicked: rootmon.probe()
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
