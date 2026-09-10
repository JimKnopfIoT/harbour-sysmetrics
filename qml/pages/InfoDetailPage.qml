import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Generic hardware/detail renderer.
// sections: [ { title?, note?, italic?, collapsed?, diagnosis?,
//               rows:[{k,v,active?,mono?,color?}],
//               bars?:[{label,value,max,caption,color}] } ]
// A row with active===false is a capability the hardware has but the phone is
// not using — rendered grayed/semi-transparent.
// collapsed===true folds the whole section behind its header: the figures a
// reader needs for an overview stay open at the top, the full dumps sit at the
// end and cost one line each until they are asked for.
// rowsFn is a section whose rows cost real time to produce — a helper process,
// a directory walk. It is called the first time the reader opens the section,
// never while the page is being built.
// groupHeader:<id> opens a run of sections of the same kind — one directory per
// power-supply stage, one per sound card — that would otherwise fill a page
// with near-identical headers; every section carrying group:<id> hides until
// that header is opened.
// diagnosis===true is not a section but a marker: the page's findings are
// rendered at that position instead of after everything else.
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
    property var openSections: []
    property var openGroups: ({})
    readonly property int cap: 10

    // A page that carries the marker places the diagnosis itself; one that does
    // not keeps it at the end, as it always was.
    readonly property bool diagInline: {
        for (var i = 0; i < sections.length; ++i)
            if (sections[i].diagnosis === true) return true
        return false
    }

    function _toggleOpen(i) {
        var a = page.openSections.slice()
        while (a.length <= i) a.push(false)
        a[i] = !a[i]
        page.openSections = a
    }
    function _toggleGroup(id) {
        var g = {}
        for (var k in page.openGroups) g[k] = page.openGroups[k]
        g[id] = !(g[id] === true)
        page.openGroups = g
    }

    // Findings for this page's subsystem, rendered above the info sections.
    property string diagTopic: ""
    property var findings: []
    Component.onCompleted: {
        if (diagTopic.length) {
            var all = diagnostics.run(sysmon.cpuPercent, sysmon.load1)
            // A finding may name more than one page it belongs on.
            findings = all.filter(function (f) {
                return f.topic === diagTopic
                    || (f.topics !== undefined && f.topics.indexOf(diagTopic) >= 0)
            })
        }
    }


    DiagBackground {}

    Component {
        id: diagnosisBlock
        Column {
            width: page.width
            SectionHeader { text: qsTr("Diagnosis") }
            Repeater {
                model: page.findings
                FindingItem { finding: modelData }
            }
            ButtonLayout {
                visible: typeof cve !== "undefined"
                Button {
                    text: qsTr("CVE search")
                    onClicked: pageStack.push(Qt.resolvedUrl("CvePage.qml"), { topic: page.diagTopic })
                }
            }
            Label {
                visible: typeof cve === "undefined"
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("The Ultimate version adds an online CVE search (EUVD/KEV) here. It is not available in any store — build it yourself from the source (see README, --with ultimate).")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }
        }
    }

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

                    // The section is read from page.sections, not from
                    // modelData. A Repeater hands its delegate the model entry
                    // converted to a QVariant, and a JavaScript function does
                    // not survive that conversion — rowsFn arrived as undefined
                    // and every lazy section stayed empty. The page property
                    // still holds the original objects, so it is asked instead.
                    property var sec: page.sections[si]

                    property bool exp: page.expandedSections[si] === true
                    property bool foldable: sec.collapsed === true
                    // the header that opens a run of same-kind sections
                    property string grpHead: sec.groupHeader ? sec.groupHeader : ""
                    // a member of such a run: gone until its group is opened
                    property string grp: sec.group ? sec.group : ""
                    property bool open: grpHead !== ""
                                        ? page.openGroups[grpHead] === true
                                        : (!foldable || page.openSections[si] === true)

                    // Built on first open and kept; null means "not asked yet".
                    property var lazyRows: null
                    onOpenChanged: {
                        if (open && lazyRows === null && sec.rowsFn)
                            lazyRows = sec.rowsFn()
                    }
                    // A section may set its own cap where the page-wide one
                    // would cut a list in the wrong place -- a ranking whose
                    // section also carries summary rows, for one: those three
                    // are not candidates for "show all" and must not eat the
                    // budget the ranking needs.
                    property int secCap: sec.cap !== undefined ? sec.cap : page.cap
                    property var sectionRows: sec.rows ? sec.rows
                                              : (lazyRows ? lazyRows : [])

                    visible: grp === "" || page.openGroups[grp] === true

                    SectionHeader {
                        visible: sec.title !== undefined && !foldable && grpHead === ""
                        text: sec.title ? sec.title : ""
                    }
                    FoldHeader {
                        visible: foldable || grpHead !== ""
                        height: visible ? Theme.itemSizeSmall : 0
                        text: sec.title ? sec.title : ""
                        // A lazy section counts nothing until it has been
                        // opened — the figure would cost what the fold saves.
                        count: grpHead !== "" ? (sec.count ? sec.count : 0)
                                              : sectionRows.length
                        // A member sits one step in from the run's own header,
                        // so an opened group reads as a list and not as more page.
                        indent: grp !== "" ? Theme.paddingLarge : 0
                        open: parent.open
                        onToggle: grpHead !== "" ? page._toggleGroup(grpHead) : page._toggleOpen(si)
                    }

                    // ---- the section body; hidden while the section is folded
                    Column {
                        width: page.width
                        visible: open

                        Label {
                            visible: sec.note !== undefined
                            x: Theme.horizontalPageMargin
                            width: page.width - 2 * Theme.horizontalPageMargin
                            text: sec.note ? sec.note : ""
                            wrapMode: Text.Wrap
                            font.italic: sec.italic === true
                            font.pixelSize: sec.italic === true ? Theme.fontSizeSmall : Theme.fontSizeTiny
                            color: sec.italic === true ? Theme.highlightColor : Theme.secondaryColor
                        }

                        // bars (usage etc.)
                        Repeater {
                            // Nothing behind a closed header is built. A page
                            // carries a hundred sections of which two are open,
                            // and building the other ninety-eight to hide them
                            // is what a fold is meant to avoid.
                            model: open && sec.bars ? sec.bars : []
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
                                if (!open) return []
                                var r = sectionRows
                                return exp ? r : r.slice(0, secCap)
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
                                    // vRight present: this label takes the left part
                                    // of the value column and the second one is set
                                    // flush right, so lists line up in two columns.
                                    width: parent.width - parent.spacing - Math.round(parent.width * 0.4)
                                           - (modelData.vRight !== undefined
                                              ? Math.round(parent.width * 0.28) : 0)
                                    text: modelData.v + (modelData.active === false ? "  ·  " + qsTr("unused") : "")
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                    font.family: modelData.mono === true ? "monospace" : Theme.fontFamily
                                    color: modelData.active === false ? Theme.secondaryColor
                                           : (modelData.color ? modelData.color : Theme.primaryColor)
                                    // Wrap, not WrapAnywhere: a list of figures
                                    // must break at the space after a comma and
                                    // never inside a number. A single token too
                                    // long for the column — a sysfs path — is
                                    // still broken, which is all WrapAnywhere
                                    // was ever needed for.
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    visible: modelData.vRight !== undefined
                                    width: visible ? Math.round(parent.width * 0.28) : 0
                                    horizontalAlignment: Text.AlignRight
                                    // Right-aligned text wider than its item paints
                                    // leftwards, straight over the value beside it.
                                    // Eliding keeps it inside its column; the left
                                    // end goes first because the tail of a sysfs
                                    // path or a source note is the telling part.
                                    elide: Text.ElideLeft
                                    maximumLineCount: 1
                                    text: modelData.vRight ? modelData.vRight : ""
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                    color: Theme.secondaryColor
                                }
                            }
                        }

                        MoreToggle {
                            total: open ? sectionRows.length : 0
                            shown: secCap; expanded: exp
                            onToggle: {
                                var a = page.expandedSections.slice()
                                while (a.length <= si) a.push(false)
                                a[si] = !a[si]
                                page.expandedSections = a
                            }
                        }
                    }

                    // ---- a measurement the page offers to run ---------------
                    // Marker, like the diagnosis: a section carrying
                    // readTest:{mount,label} is not content but a place for the
                    // button that starts it. Nothing measures itself on page
                    // load — a read test costs seconds and touches the medium,
                    // so it happens when the reader asks for it.
                    Loader {
                        width: page.width
                        active: sec.readTest !== undefined
                        sourceComponent: Component {
                            ReadTestBlock {
                                width: page.width
                                mountPoint: sec.readTest.mount
                                mediumLabel: sec.readTest.label
                            }
                        }
                    }

                    // ---- diagnosis, placed by the page rather than appended:
                    // it reads against the section above it. A Loader, not a
                    // hidden Column: an invisible item is still built, and one
                    // findings list plus a button per section on a page of a
                    // hundred sections is seconds of it.
                    Loader {
                        width: page.width
                        active: sec.diagnosis === true && page.findings.length > 0
                        sourceComponent: diagnosisBlock
                    }
                }
            }

            // A page that names no place for its diagnosis keeps it last, as
            // it always was — facts first, complaints at the end. Same block
            // as above, loaded from the same component: there is one
            // definition of what a diagnosis looks like, not two.
            Loader {
                width: page.width
                active: !page.diagInline && page.findings.length > 0
                sourceComponent: diagnosisBlock
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
