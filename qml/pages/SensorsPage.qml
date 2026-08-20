import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Shell page: loads the QtSensors/QtPositioning content via a Loader so a
// missing QML plugin degrades to a message instead of failing the whole page.
Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    property var helpTopics: ["sensors"]
    function _attachHelp() {
        if (_helpAttached) return
        if (helpTopics && helpTopics.length === 0) { _helpAttached = true; return }
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"), { topics: helpTopics })
        if (p) _helpAttached = true
    }
    onStatusChanged: if (status === PageStatus.Active) _attachHelp()


    DiagBackground {}

    Loader {
        id: sensorLoader
        anchors.fill: parent
        source: Qt.resolvedUrl("SensorContent.qml")
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.horizontalPageMargin
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
}
