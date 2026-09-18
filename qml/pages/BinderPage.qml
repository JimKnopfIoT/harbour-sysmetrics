import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Binder traffic, as the kernel itself tallies it.
//
// The process list can say that "binder:268_5" is burning a core; it cannot
// say whose thread that is or what it is doing. The binder driver keeps the
// answer in its own log directory and hands it out for free: how many calls
// each process made, which of them came back as an error, whose thread pool
// is exhausted, and which call has not returned. This page reads that and
// nothing else -- every figure here is the kernel's, every threshold that
// turns a figure into a verdict is named where the verdict stands.
Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    property var helpTopics: ["binder","procid"]
    function _attachHelp() {
        if (_helpAttached) return
        if (helpTopics && helpTopics.length === 0) { _helpAttached = true; return }
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"), { topics: helpTopics })
        if (p) _helpAttached = true
    }
    onStatusChanged: if (status === PageStatus.Active) _attachHelp()

    // Which findings the reader has opened. Kept here and keyed by the
    // finding's own key, because every reading builds the list afresh and the
    // delegates with it -- state inside a delegate would not survive that.
    property var openFindings: ({})
    function toggleFinding(key) {
        var m = {}
        for (var k in page.openFindings)
            m[k] = page.openFindings[k]
        m[key] = !m[key]
        page.openFindings = m
    }

    // per-list "first N / all" state
    property bool findingsExpanded: false
    property bool talkersExpanded: false
    property bool failuresExpanded: false
    property bool servicesExpanded: false
    readonly property int listCap: 8

    // folded blocks
    property bool introOpen: false
    property bool domainsOpen: false
    property bool deadOpen: false
    property bool rawOpen: false
    // read on demand only -- both come out of files that cost real work
    property var dead: ({})
    property var raw: ({})

    // Sampled twice on entry and then left alone. Once would be enough for the
    // counters, but not for the rates: those are the difference between two
    // readings, and a call that has not come back is one that is still the same
    // one on the second. So: read on arrival, read again two seconds later,
    // then stand still. Nothing here moves under the reader afterwards, and the
    // pull-down takes a fresh pair whenever it is wanted.
    Component.onCompleted: { binder.refresh(); secondReading.start() }
    Timer { id: secondReading; interval: 2000; repeat: false; onTriggered: binder.refresh() }

    function fmtRate(r) {
        if (r === undefined || r === null || r < 0) return "—"
        return (r >= 10 ? r.toFixed(0) : r.toFixed(1)) + qsTr("/s")
    }
    function fmtCount(n) {
        // Grouped, and without the two decimals QML's default would add to a
        // count of transactions.
        return n === undefined || n === null ? "—"
                                             : Number(n).toLocaleString(Qt.locale(), 'f', 0)
    }
    function openProcess(pid, name) {
        if (pid > 0)
            pageStack.push(Qt.resolvedUrl("ProcessDetailPage.qml"),
                           { pid: pid, pname: name ? name : "" })
    }
    // How a service's owner was established. Nothing here is concluded from a
    // name, so the page says for every entry what the claim rests on.
    function proofText(p) {
        if (p === "ping")  return qsTr("proven by ping")
        if (p === "state") return qsTr("from the node owner")
        if (p === "no answer") return qsTr("no answer")
        if (p === "gone")  return qsTr("node gone")
        return qsTr("not established")
    }
    function proofColor(p) {
        if (p === "ping" || p === "state") return Diag.teal
        if (p === "no answer") return Diag.red
        return Theme.secondaryColor
    }

    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        PullDownMenu {
            MenuItem {
                text: qsTr("Measure again")
                onClicked: { binder.refresh(); secondReading.restart() }
            }
            MenuItem {
                text: binder.scanning ? qsTr("Stop identifying") : qsTr("Identify services")
                visible: binder.canIdentify
                onClicked: binder.scanning ? binder.cancelScan() : binder.identifyServices()
            }
        }

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Binder")
                description: binder.available
                    ? qsTr("%1 processes · %2 calls/s").arg(binder.totals.procs || 0)
                          .arg((binder.totals.callRate || -1) < 0
                               ? "—" : binder.totals.callRate.toFixed(1))
                    : qsTr("not readable")
            }

            // --- where the figures come from ---------------------------------
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2

                KeyValue {
                    label: qsTr("Source")
                    mono: true
                    value: binder.source.length ? binder.source : qsTr("none found")
                }
                KeyValue {
                    label: qsTr("Read as")
                    value: binder.privileged ? qsTr("root helper") : qsTr("ordinary user")
                    valueColor: binder.privileged ? Diag.amber : Theme.primaryColor
                }
                KeyValue {
                    label: qsTr("Sampling interval")
                    visible: binder.interval > 0
                    value: qsTr("%1 s between the two readings the rates rest on — pull down to "
                        + "measure again").arg(binder.interval.toFixed(1))
                }
                Label {
                    visible: binder.error.length > 0
                    width: parent.width
                    text: binder.error
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Diag.amber
                }
            }

            // --- what this is, for whoever opens the page first ---------------
            // Folded: the explanation is worth having once and in the way every
            // time after that. The header line is the invitation.
            FoldHeader {
                text: qsTr("What binder is, and what stands here")
                open: page.introOpen
                onToggle: page.introOpen = !page.introOpen
            }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                visible: page.introOpen

                Label {
                    width: parent.width
                    text: qsTr("What it is")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Diag.cyan
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.primaryColor
                    text: qsTr("The IPC the Android side of this device is built on — what D-Bus "
                        + "is to the Sailfish side. Same purpose, different build: D-Bus relays "
                        + "through a daemon and copies every message twice, binder is a driver in "
                        + "the kernel that copies it once, straight into memory the receiver has "
                        + "mapped, and wakes one of its threads. Fast enough for every camera "
                        + "frame and every sensor reading — and the driver, sitting in the middle, "
                        + "counts all of it.")
                }
                Label {
                    width: parent.width
                    text: qsTr("What runs over it")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Diag.cyan
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.primaryColor
                    text: qsTr("Two things. The hardware: camera, GPS, sensors, radio, lights and "
                        + "bluetooth are Android services from the device maker, and Sailfish "
                        + "reaches them through the gbinder library — without binder a port has no "
                        + "camera. And App Support, which brings domains of its own and carries "
                        + "the traffic of every Android app in them. Everything else on the phone "
                        + "runs on D-Bus and does not appear here.")
                }
                Label {
                    width: parent.width
                    text: qsTr("What this page shows")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Diag.cyan
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.primaryColor
                    text: qsTr("Findings first — a failing caller, a thread pool with nothing "
                        + "free, a call that has not come back. Then the counters for the whole "
                        + "device, the domains and who declared them, the processes ranked by how "
                        + "much they call, and the failures grouped by caller and error code. "
                        + "Services can be identified on request; folded at the end are the "
                        + "orphaned nodes and the driver's own log lines.")
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Every figure here is the kernel's own; wherever one is turned into "
                        + "a verdict, the threshold that did it stands beside it. Nothing on this "
                        + "page changes anything — it reads, it does not repair. Anything that "
                        + "leads to a process can be tapped.")
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("The glossary beside this page (swipe left) explains node, handle, "
                        + "service manager, thread pool and the error codes.")
                }
                Item { width: 1; height: Theme.paddingSmall }
            }

            ViewPlaceholder {
                enabled: !binder.available
                text: qsTr("No binder log")
                hintText: qsTr("The kernel keeps these figures only when it is built with "
                    + "the binder logs enabled.")
            }

            // --- assessments, alarming first ---------------------------------
            SectionHeader {
                text: qsTr("Findings")
                visible: binder.available
            }
            Label {
                visible: binder.available && binder.findings.length === 0
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Nothing to report: no failing call in the kernel's ring, no "
                    + "exhausted thread pool, no call left unanswered between two readings.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }
            Column {
                width: parent.width
                visible: binder.available

                Repeater {
                    model: page.findingsExpanded ? binder.findings
                                                 : binder.findings.slice(0, page.listCap)
                    FindingItem {
                        finding: modelData
                        externalState: true
                        expanded: page.openFindings[modelData.key] === true
                        onToggled: page.toggleFinding(modelData.key)
                        onOpenProcess: page.openProcess(pid, modelData.title)
                    }
                }
                MoreToggle {
                    total: binder.findings.length; shown: page.listCap
                    expanded: page.findingsExpanded
                    onToggle: page.findingsExpanded = !page.findingsExpanded
                }
            }

            // --- the kernel's own totals -------------------------------------
            SectionHeader {
                text: qsTr("Across the whole device")
                visible: binder.available
            }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                visible: binder.available

                KeyValue {
                    label: qsTr("Calls")
                    mono: true
                    value: page.fmtRate(binder.totals.callRate) + "  ·  "
                           + qsTr("%1 since boot").arg(page.fmtCount(binder.totals.calls))
                }
                KeyValue {
                    label: qsTr("Replies")
                    mono: true
                    value: page.fmtRate(binder.totals.replyRate) + "  ·  "
                           + qsTr("%1 since boot").arg(page.fmtCount(binder.totals.replies))
                }
                KeyValue {
                    label: qsTr("Dead replies")
                    mono: true
                    value: page.fmtRate(binder.totals.deadReplyRate) + "  ·  "
                           + qsTr("%1 since boot").arg(page.fmtCount(binder.totals.deadReplies))
                    valueColor: (binder.totals.deadReplyRate || 0) > 0 ? Diag.amber
                                                                       : Theme.primaryColor
                }
                KeyValue {
                    label: qsTr("Refused transactions")
                    mono: true
                    value: page.fmtRate(binder.totals.failedReplyRate) + "  ·  "
                           + qsTr("%1 since boot").arg(page.fmtCount(binder.totals.failedReplies))
                    valueColor: (binder.totals.failedReplyRate || 0) > 0 ? Diag.amber
                                                                         : Theme.primaryColor
                }
                KeyValue {
                    label: qsTr("Processes / threads")
                    mono: true
                    value: page.fmtCount(binder.totals.procs) + " / "
                           + page.fmtCount(binder.totals.threads)
                }
                KeyValue {
                    label: qsTr("Nodes / references")
                    mono: true
                    value: page.fmtCount(binder.totals.nodes) + " / "
                           + page.fmtCount(binder.totals.refs)
                }
                KeyValue {
                    label: qsTr("In flight now")
                    mono: true
                    value: page.fmtCount(binder.totals.inFlight)
                }
                KeyValue {
                    label: qsTr("Death notifications")
                    mono: true
                    value: page.fmtCount(binder.totals.deaths)
                }
            }

            // --- domains ------------------------------------------------------
            FoldHeader {
                visible: binder.available
                text: qsTr("Domains")
                count: binder.domains.length
                open: page.domainsOpen
                onToggle: page.domainsOpen = !page.domainsOpen
            }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                visible: binder.available && page.domainsOpen

                Label {
                    width: parent.width
                    text: qsTr("A binder domain is one driver device with its own service "
                        + "registry; processes on different domains cannot reach each other. "
                        + "Which domain belongs to what is not read off its name but taken "
                        + "from the configuration file that declares it.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
                Repeater {
                    model: binder.domains
                    Column {
                        width: parent.width
                        spacing: Theme.paddingSmall / 2

                        Row {
                            width: parent.width
                            spacing: Theme.paddingMedium
                            Label {
                                text: modelData.context
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: "monospace"
                                color: Diag.cyan
                            }
                            Label {
                                text: page.fmtRate(modelData.callRate)
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.primaryColor
                            }
                        }
                        KeyValue {
                            label: qsTr("Device")
                            mono: true
                            value: modelData.device.length ? modelData.device
                                                           : qsTr("not present in /dev")
                        }
                        KeyValue {
                            label: qsTr("Protocol")
                            value: modelData.protocol.length ? modelData.protocol
                                                             : qsTr("not declared")
                        }
                        KeyValue {
                            label: qsTr("Declared by")
                            mono: true
                            value: modelData.declaredBy.length
                                   ? modelData.declaredBy
                                   : qsTr("no configuration file names it")
                        }
                        KeyValue {
                            label: qsTr("Processes")
                            value: modelData.procs + (modelData.servicesKnown > 0
                                   ? "  ·  " + qsTr("%1 services identified")
                                       .arg(modelData.servicesKnown) : "")
                        }
                        KeyValue {
                            label: qsTr("Calls since boot")
                            mono: true
                            value: page.fmtCount(modelData.calls)
                        }
                        Item { width: 1; height: Theme.paddingSmall }
                    }
                }
            }

            // --- who is talking -----------------------------------------------
            SectionHeader {
                text: qsTr("Busiest processes")
                visible: binder.available
            }
            Label {
                visible: binder.available && binder.interval <= 0
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Rates appear with the second reading, a moment from now.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }
            Column {
                width: parent.width
                visible: binder.available

                Repeater {
                    model: page.talkersExpanded ? binder.talkers
                                                : binder.talkers.slice(0, page.listCap)
                    ListItem {
                        id: talker
                        contentHeight: Theme.itemSizeSmall
                        onClicked: page.openProcess(modelData.pid, modelData.name)

                        Row {
                            anchors {
                                left: parent.left; right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: Theme.horizontalPageMargin
                                rightMargin: Theme.horizontalPageMargin
                            }
                            spacing: Theme.paddingMedium

                            Column {
                                width: parent.width - rateCol.width - parent.spacing
                                anchors.verticalCenter: parent.verticalCenter
                                Label {
                                    width: parent.width
                                    text: modelData.name
                                    truncationMode: TruncationMode.Fade
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: talker.highlighted ? Theme.highlightColor
                                                              : Theme.primaryColor
                                }
                                Label {
                                    width: parent.width
                                    // A kernel from before binderfs names no
                                    // domain; then the field stays out of the
                                    // line instead of leaving a gap in it.
                                    text: "PID " + modelData.pid
                                          + (modelData.context.length
                                             ? " · " + modelData.context : "")
                                          + " · " + qsTr("%1 of %2 threads")
                                              .arg(modelData.threads).arg(modelData.maxThreads)
                                          + (modelData.services.length
                                             ? " · " + modelData.services[0].name : "")
                                    truncationMode: TruncationMode.Fade
                                    font.pixelSize: Theme.fontSizeTiny
                                    color: Theme.secondaryColor
                                }
                            }
                            Column {
                                id: rateCol
                                width: Math.max(implicitWidth, Theme.itemSizeSmall)
                                anchors.verticalCenter: parent.verticalCenter
                                Label {
                                    anchors.right: parent.right
                                    text: page.fmtRate(modelData.callRate)
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Diag.cyan
                                }
                                Label {
                                    anchors.right: parent.right
                                    text: qsTr("incoming %1").arg(page.fmtRate(modelData.incomingRate))
                                    font.pixelSize: Theme.fontSizeTiny
                                    color: Theme.secondaryColor
                                }
                            }
                        }
                    }
                }
                MoreToggle {
                    total: binder.talkers.length; shown: page.listCap
                    expanded: page.talkersExpanded
                    onToggle: page.talkersExpanded = !page.talkersExpanded
                }
            }

            // --- calls that came back as an error -------------------------------
            SectionHeader {
                text: qsTr("Failing calls")
                visible: binder.available && binder.failures.length > 0
            }
            Column {
                width: parent.width
                visible: binder.available && binder.failures.length > 0

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    text: qsTr("The driver keeps the last 32 failed transactions. What stands "
                        + "here is that ring, grouped by caller and error code — a rate taken "
                        + "from it is a lower bound, never a total.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
                Item { width: 1; height: Theme.paddingSmall }

                Repeater {
                    model: page.failuresExpanded ? binder.failures
                                                 : binder.failures.slice(0, page.listCap)
                    ListItem {
                        id: fail
                        contentHeight: failCol.height + Theme.paddingMedium
                        onClicked: page.openProcess(modelData.fromPid, modelData.fromName)

                        Column {
                            id: failCol
                            anchors {
                                left: parent.left; right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: Theme.horizontalPageMargin
                                rightMargin: Theme.horizontalPageMargin
                            }
                            Label {
                                width: parent.width
                                text: modelData.fromName + " → "
                                      + (modelData.targetService.length
                                         ? modelData.targetService
                                         : (modelData.toName.length
                                            ? modelData.toName
                                            // Handle 0 is the domain's service
                                            // manager — that is the binder ABI,
                                            // not a guess about a node.
                                            : (modelData.handle === 0 && modelData.node <= 0
                                               ? qsTr("service manager")
                                               : qsTr("node %1").arg(modelData.node))))
                                truncationMode: TruncationMode.Fade
                                font.pixelSize: Theme.fontSizeSmall
                                color: fail.highlighted ? Theme.highlightColor
                                                        : Theme.primaryColor
                            }
                            Label {
                                width: parent.width
                                text: (modelData.retName.length ? modelData.retName
                                                                : qsTr("code %1").arg(modelData.retCode))
                                      + (modelData.errName.length ? " / " + modelData.errName : "")
                                      + " · " + modelData.context
                                truncationMode: TruncationMode.Fade
                                font.pixelSize: Theme.fontSizeTiny
                                font.family: "monospace"
                                color: Diag.red
                            }
                            Label {
                                width: parent.width
                                text: qsTr("PID %1 · %2 of %3 ring entries")
                                          .arg(modelData.fromPid).arg(modelData.inRing)
                                          .arg(modelData.ringSize)
                                      + (modelData.rate >= 0
                                         ? " · " + (modelData.rateIsLowerBound
                                                    ? qsTr("at least %1").arg(page.fmtRate(modelData.rate))
                                                    : page.fmtRate(modelData.rate))
                                         : "")
                                truncationMode: TruncationMode.Fade
                                font.pixelSize: Theme.fontSizeTiny
                                color: Theme.secondaryColor
                            }
                        }
                    }
                }
                MoreToggle {
                    total: binder.failures.length; shown: page.listCap
                    expanded: page.failuresExpanded
                    onToggle: page.failuresExpanded = !page.failuresExpanded
                }
            }

            // --- the registered services ----------------------------------------
            SectionHeader {
                text: qsTr("Services")
                visible: binder.available
            }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                visible: binder.available

                Label {
                    width: parent.width
                    text: qsTr("Which process serves which name is not guessed from the name. "
                        + "SysMetrics sends each service the ping every binder service must "
                        + "answer — a liveness check that performs no action inside it — and "
                        + "then reads back the kernel's own transaction log to see who "
                        + "answered. That log entry is the proof. It costs one ping per "
                        + "service and takes a moment.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
                Label {
                    visible: !binder.canIdentify
                    width: parent.width
                    text: qsTr("Not possible here: binder-list and binder-ping from the gbinder "
                        + "tools are not installed. The traffic figures above do not need them.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Diag.amber
                }
                Label {
                    visible: binder.scanning
                    width: parent.width
                    text: qsTr("%1 of %2 · %3").arg(binder.scanDone).arg(binder.scanTotal)
                                               .arg(binder.scanCurrent)
                    wrapMode: Text.WrapAnywhere
                    font.pixelSize: Theme.fontSizeExtraSmall
                    font.family: "monospace"
                    color: Diag.cyan
                }
                ProgressBar {
                    visible: binder.scanning
                    width: parent.width
                    minimumValue: 0
                    maximumValue: Math.max(1, binder.scanTotal)
                    value: binder.scanDone
                }
            }
            ButtonLayout {
                visible: binder.available && binder.canIdentify
                Button {
                    text: binder.scanning ? qsTr("Stop") : qsTr("Identify services")
                    onClicked: binder.scanning ? binder.cancelScan() : binder.identifyServices()
                }
            }
            Column {
                width: parent.width
                visible: binder.available && binder.services.length > 0

                Repeater {
                    model: page.servicesExpanded ? binder.services
                                                 : binder.services.slice(0, page.listCap)
                    ListItem {
                        id: svc
                        contentHeight: svcCol.height + Theme.paddingMedium
                        onClicked: page.openProcess(modelData.pid, modelData.process)

                        Column {
                            id: svcCol
                            anchors {
                                left: parent.left; right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: Theme.horizontalPageMargin
                                rightMargin: Theme.horizontalPageMargin
                            }
                            Label {
                                width: parent.width
                                text: modelData.name
                                truncationMode: TruncationMode.Fade
                                font.pixelSize: Theme.fontSizeExtraSmall
                                font.family: "monospace"
                                color: svc.highlighted ? Theme.highlightColor
                                                       : Theme.primaryColor
                            }
                            Label {
                                width: parent.width
                                text: (modelData.pid > 0
                                       ? modelData.process + " · PID " + modelData.pid
                                       : qsTr("owner unknown"))
                                      + " · " + modelData.context
                                      + (modelData.node >= 0
                                         ? " · " + qsTr("node %1").arg(modelData.node) : "")
                                      + " · " + page.proofText(modelData.proof)
                                truncationMode: TruncationMode.Fade
                                font.pixelSize: Theme.fontSizeTiny
                                color: page.proofColor(modelData.proof)
                            }
                        }
                    }
                }
                MoreToggle {
                    total: binder.services.length; shown: page.listCap
                    expanded: page.servicesExpanded
                    onToggle: page.servicesExpanded = !page.servicesExpanded
                }
            }

            // --- nodes nobody owns any more ---------------------------------------
            FoldHeader {
                visible: binder.available
                text: qsTr("Orphaned nodes")
                count: page.dead.count !== undefined ? page.dead.count : 0
                open: page.deadOpen
                onToggle: {
                    page.deadOpen = !page.deadOpen
                    if (page.deadOpen && page.dead.readable === undefined)
                        page.dead = binder.deadNodes()
                }
            }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                visible: binder.available && page.deadOpen

                Label {
                    width: parent.width
                    text: qsTr("A node whose owning process is gone while somebody still holds "
                        + "a reference to it. The processes listed under it are the ones that "
                        + "still believe they have that service: their next call over that "
                        + "reference comes back as a dead reply. Tap one to see it. Read on "
                        + "request only — the file this comes from dumps every node and every "
                        + "reference of every process and takes the driver's locks while doing "
                        + "it.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
                Label {
                    visible: page.dead.readable === false
                    width: parent.width
                    text: qsTr("The state file could not be read.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Diag.amber
                }
                Label {
                    visible: page.dead.readable === true && page.dead.count === 0
                    width: parent.width
                    text: qsTr("None — every node has a living owner.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
                Repeater {
                    model: page.dead.nodes !== undefined ? page.dead.nodes : []
                    Column {
                        width: parent.width
                        spacing: Theme.paddingSmall / 2

                        Label {
                            width: parent.width
                            text: qsTr("node %1").arg(modelData.node)
                            font.pixelSize: Theme.fontSizeExtraSmall
                            font.family: "monospace"
                            color: Diag.cyan
                        }
                        Label {
                            visible: modelData.holders.length === 0
                            width: parent.width
                            text: qsTr("The state file names no holder for it.")
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSizeTiny
                            color: Theme.secondaryColor
                        }
                        // One row per holder, and each one leads to that
                        // process: it is the process that still believes it
                        // has this service, so it is the one worth looking at.
                        Repeater {
                            model: modelData.holders
                            BackgroundItem {
                                width: parent.width
                                height: holderCol.height + Theme.paddingSmall
                                onClicked: page.openProcess(modelData.pid, modelData.name)

                                Column {
                                    id: holderCol
                                    width: parent.width
                                    anchors.verticalCenter: parent.verticalCenter

                                    Label {
                                        width: parent.width
                                        text: (modelData.name.length
                                               ? modelData.name
                                               : qsTr("process has ended")) + "  ›"
                                        truncationMode: TruncationMode.Fade
                                        font.pixelSize: Theme.fontSizeExtraSmall
                                        color: Theme.primaryColor
                                    }
                                    Label {
                                        width: parent.width
                                        text: "PID " + modelData.pid + " · "
                                              + qsTr("still holds a reference")
                                        truncationMode: TruncationMode.Fade
                                        font.pixelSize: Theme.fontSizeTiny
                                        color: Theme.secondaryColor
                                    }
                                }
                            }
                        }
                        Item { width: 1; height: Theme.paddingSmall }
                    }
                }
                Label {
                    visible: page.dead.bytes !== undefined
                    width: parent.width
                    text: qsTr("read from %1/state, %2 bytes").arg(binder.source)
                              .arg(page.dead.bytes || 0)
                    wrapMode: Text.WrapAnywhere
                    font.pixelSize: Theme.fontSizeTiny
                    color: Theme.secondaryColor
                }
            }

            // --- the lines themselves ----------------------------------------------
            FoldHeader {
                visible: binder.available
                text: qsTr("Kernel ring, verbatim")
                count: page.raw.transactions !== undefined
                       ? page.raw.transactions.length + page.raw.failed.length : 0
                open: page.rawOpen
                onToggle: {
                    page.rawOpen = !page.rawOpen
                    if (page.rawOpen)
                        page.raw = binder.rawLog()
                }
            }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                visible: binder.available && page.rawOpen

                Label {
                    width: parent.width
                    text: qsTr("The last 32 transactions and the last 32 failures, exactly as "
                        + "the driver prints them. Everything above was counted from these "
                        + "lines and from the counter file beside them.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
                Label {
                    visible: (page.raw.failed !== undefined) && page.raw.failed.length > 0
                    width: parent.width
                    text: "failed_transaction_log"
                    font.pixelSize: Theme.fontSizeTiny
                    color: Diag.red
                }
                Repeater {
                    model: page.raw.failed !== undefined ? page.raw.failed : []
                    Label {
                        width: parent.width
                        text: modelData
                        wrapMode: Text.WrapAnywhere
                        font.pixelSize: Theme.fontSizeTiny
                        font.family: "monospace"
                        color: Theme.secondaryColor
                    }
                }
                Label {
                    visible: (page.raw.transactions !== undefined)
                             && page.raw.transactions.length > 0
                    width: parent.width
                    text: "transaction_log"
                    font.pixelSize: Theme.fontSizeTiny
                    color: Diag.cyan
                }
                Repeater {
                    model: page.raw.transactions !== undefined ? page.raw.transactions : []
                    Label {
                        width: parent.width
                        text: modelData
                        wrapMode: Text.WrapAnywhere
                        font.pixelSize: Theme.fontSizeTiny
                        font.family: "monospace"
                        color: Theme.secondaryColor
                    }
                }
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
