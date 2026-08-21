import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    allowedOrientations: Orientation.All
    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height
        Column {
            id: col
            width: parent.width
            spacing: Theme.paddingMedium
            PageHeader { title: qsTr("About") }
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "image://theme/icon-launcher-default"
                visible: false
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: "SysMetrics " + appVersion
                font.pixelSize: Theme.fontSizeLarge
                color: Diag.cyan
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Built %1").arg(appBuildDate)
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Process and system monitor with known-issue diagnostics: "
                    + "processes, hardware, chipsets, HAL services, Android base, "
                    + "bug-report assistant. Reads /proc, /sys, D-Bus and rpm on the "
                    + "device — read-only, collects nothing, transmits nothing.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("The self-built Ultimate variant adds an online CVE search "
                    + "(EUVD/KEV) — the only feature that talks to the network. "
                    + "Build it yourself: --with ultimate.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WrapAnywhere
                font.pixelSize: Theme.fontSizeExtraSmall
                font.family: "monospace"
                color: Theme.secondaryColor
                text: qsTr("License: GPL-3.0-or-later\nSource:\ngithub.com/JimKnopfIoT/harbour-sysmetrics")
            }
            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
