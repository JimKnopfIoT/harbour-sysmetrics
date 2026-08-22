import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Fallback for devices without a configured device lock: the same decision,
// spelled out, without an authentication step there is nothing to authenticate
// against.
Dialog {
    id: dialog
    allowedOrientations: Orientation.All

    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        Column {
            id: col
            width: parent.width
            spacing: Theme.paddingMedium

            DialogHeader { acceptText: qsTr("Enable root mode") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.primaryColor
                text: qsTr("This starts a helper service running as root. While it runs, the app can:")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("• list open files, sockets and watchers of processes belonging to other users\n"
                    + "• read the WLAN firmware counters in debugfs\n"
                    + "• read the kernel charger log\n"
                    + "• pull journal and kernel-log excerpts for a bug report")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Diag.red
                text: qsTr("Journal excerpts come from the whole system and can contain sensitive data. Read them before passing them on.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("The helper stops itself once the app is gone. It is never started at boot.")
            }
            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
