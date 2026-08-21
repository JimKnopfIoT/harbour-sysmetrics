import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Generic hardware/detail renderer.
// sections: [ { title?, note?, italic?, rows:[{k,v,active?,mono?,color?}], bars?:[{label,value,max,caption,color}] } ]
// A row with active===false is a capability the hardware has but the phone is
// not using — rendered grayed/semi-transparent.
Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    property var helpTopics: []
    function _attachHelp() {
        if (_helpAttached) return
        if (helpTopics && helpTopics.length === 0) { _helpAttached = true; return }
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"), { topics: helpTopics })
        if (p) _helpAttached = true
    }
    onStatusChanged: if (status === PageStatus.Active) _attachHelp()
    property string title
    property var sections: []
    property var expandedSections: []
    readonly property int cap: 10

    // Findings for this page's subsystem, rendered above the info sections.
    property string diagTopic: ""
    property var findings: []
    Component.onCompleted: {
        if (diagTopic.length) {
            var all = diagnostics.run(sysmon.cpuPercent, sysmon.load1)
            findings = all.filter(function (f) { return f.topic === diagTopic })
        }
    }


    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingSmall

            PageHeader { title: page.title }

            Repeater {
                model: page.sections
                Column {
                    width: page.width
                    property int si: index
                    property bool exp: page.expandedSections[index] === true
                    SectionHeader {
                        visible: modelData.title !== undefined
                        text: modelData.title ? modelData.title : ""
                    }

                    Label {
                        visible: modelData.note !== undefined
                        x: Theme.horizontalPageMargin
                        width: page.width - 2 * Theme.horizontalPageMargin
                        text: modelData.note ? modelData.note : ""
                        wrapMode: Text.Wrap
                        font.italic: modelData.italic === true
                        font.pixelSize: modelData.italic === true ? Theme.fontSizeSmall : Theme.fontSizeTiny
                        color: modelData.italic === true ? Theme.highlightColor : Theme.secondaryColor
                    }

                    // bars (usage etc.)
                    Repeater {
                        model: modelData.bars ? modelData.bars : []
                        LoadBar {
                            x: Theme.horizontalPageMargin
                            width: page.width - 2 * Theme.horizontalPageMargin
                            value: modelData.value; maxValue: modelData.max ? modelData.max : 100
                            color: modelData.color ? modelData.color : Diag.cyan
                            label: modelData.label; caption: modelData.caption ? modelData.caption : ""
                        }
                    }

                    // rows (key/value; grayed when active===false)
                    Repeater {
                        model: {
                            var r = modelData.rows ? modelData.rows : []
                            return exp ? r : r.slice(0, page.cap)
                        }
                        Row {
                            x: Theme.horizontalPageMargin
                            width: page.width - 2 * Theme.horizontalPageMargin
                            spacing: Theme.paddingMedium
                            opacity: modelData.active === false ? 0.4 : 1.0
                            Label {
                                text: modelData.k
                                font.pixelSize: Theme.fontSizeExtraSmall
                                color: Theme.secondaryColor
                                width: Math.round(parent.width * 0.4)
                                wrapMode: Text.Wrap
                            }
                            Label {
                                width: parent.width - parent.spacing - Math.round(parent.width * 0.4)
                                text: modelData.v + (modelData.active === false ? "  ·  " + qsTr("unused") : "")
                                font.pixelSize: Theme.fontSizeExtraSmall
                                font.family: modelData.mono === true ? "monospace" : Theme.fontFamily
                                color: modelData.active === false ? Theme.secondaryColor
                                       : (modelData.color ? modelData.color : Theme.primaryColor)
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                    }

                    MoreToggle {
                        total: modelData.rows ? modelData.rows.length : 0
                        shown: page.cap; expanded: exp
                        onToggle: {
                            var a = page.expandedSections.slice()
                            while (a.length <= si) a.push(false)
                            a[si] = !a[si]
                            page.expandedSections = a
                        }
                    }
                }
            }

            // ---- diagnosis: the subsystem's known issues, deliberately
            // last — facts first, complaints at the end -------------------
            SectionHeader {
                visible: page.findings.length > 0
                text: qsTr("Diagnosis")
            }
            Repeater {
                model: page.findings
                FindingItem { finding: modelData }
            }
            // Ultimate builds add the CVE search here; store builds say
            // what the self-built variant would offer.
            ButtonLayout {
                visible: page.findings.length > 0 && typeof cve !== "undefined"
                Button {
                    text: qsTr("CVE search")
                    onClicked: pageStack.push(Qt.resolvedUrl("CvePage.qml"), { topic: page.diagTopic })
                }
            }
            Label {
                visible: page.findings.length > 0 && typeof cve === "undefined"
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("The Ultimate version adds an online CVE search (EUVD/KEV) here. It is not available in any store — build it yourself from the source (see README, --with ultimate).")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
