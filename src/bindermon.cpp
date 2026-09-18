#include "bindermon.h"

#include "rootclient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <algorithm>

namespace {

// /proc and the binder logs are seq_files: they report size 0 and hand out
// their content in chunks, so a single read() may stop short of the end.
QByteArray slurp(const QString &path, int cap = 4 * 1024 * 1024)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    QByteArray out;
    char buf[64 * 1024];
    qint64 n;
    while ((n = f.read(buf, sizeof(buf))) > 0) {
        out.append(buf, int(n));
        if (out.size() >= cap)
            break;
    }
    return out;
}

QString firstLine(const QString &path)
{
    return QString::fromLocal8Bit(slurp(path, 4096)).section(QLatin1Char('\n'), 0, 0).trimmed();
}

// The BR_* code the kernel handed back to a failed caller. These are ioctl
// numbers built with _IO('r', n), so the low byte is the ordinal -- decoded
// here rather than printed raw, because "29189" says nothing and
// "BR_DEAD_REPLY" says everything.
QString brName(int code)
{
    switch (code) {
    case 0x7201: return QStringLiteral("BR_OK");
    case 0x7205: return QStringLiteral("BR_DEAD_REPLY");
    case 0x7206: return QStringLiteral("BR_TRANSACTION_COMPLETE");
    case 0x7211: return QStringLiteral("BR_FAILED_REPLY");
    case 0x7212: return QStringLiteral("BR_FROZEN_REPLY");
    case 0x7213: return QStringLiteral("BR_ONEWAY_SPAM_SUSPECT");
    case 0x7214: return QStringLiteral("BR_TRANSACTION_PENDING_FROZEN");
    default: break;
    }
    // BR_ERROR carries a payload, so its ioctl number has the size bits set.
    if ((code & 0xffff) == 0x7200)
        return QStringLiteral("BR_ERROR");
    return QString();
}

// What the kernel means by a BR_* return, in the terms of the caller.
QString brMeaning(int code)
{
    switch (code) {
    case 0x7205:
        return BinderMon::tr("the target was gone or the handle was invalid — the call never reached a service");
    case 0x7211:
        return BinderMon::tr("the driver refused the transaction before delivering it");
    case 0x7212:
        return BinderMon::tr("the target process is frozen and did not take the call");
    case 0x7213:
        return BinderMon::tr("the driver flagged the sender for flooding a service with one-way calls");
    case 0x7214:
        return BinderMon::tr("the call is held back because the target process is frozen");
    default:
        break;
    }
    if ((code & 0xffff) == 0x7200)
        return BinderMon::tr("the driver reported an error before delivery");
    return QString();
}

// Only the errno values binder itself hands back; anything else stays a
// number rather than becoming a guess.
QString errnoName(int negative)
{
    switch (-negative) {
    case 1:   return QStringLiteral("EPERM");
    case 2:   return QStringLiteral("ENOENT");
    case 3:   return QStringLiteral("ESRCH");
    case 9:   return QStringLiteral("EBADF");
    case 11:  return QStringLiteral("EAGAIN");
    case 12:  return QStringLiteral("ENOMEM");
    case 14:  return QStringLiteral("EFAULT");
    case 16:  return QStringLiteral("EBUSY");
    case 22:  return QStringLiteral("EINVAL");
    case 28:  return QStringLiteral("ENOSPC");
    case 35:  return QStringLiteral("EDEADLK");
    case 71:  return QStringLiteral("EPROTO");
    case 110: return QStringLiteral("ETIMEDOUT");
    default:  return QString();
    }
}

} // namespace

BinderMon::BinderMon(QObject *parent)
    : QObject(parent)
{
    m_bootId = firstLine(QStringLiteral("/proc/sys/kernel/random/boot_id"));
    loadGbinderConfig();
    locateSource();
    // binder-list and binder-ping come from the gbinder tools. Without them
    // the traffic figures still work; only naming the services does not.
    m_canIdentify = m_available
            && QFileInfo(QStringLiteral("/usr/bin/binder-list")).isExecutable()
            && QFileInfo(QStringLiteral("/usr/bin/binder-ping")).isExecutable();
    loadServiceCache();
}

// Where the kernel keeps the binder logs. binderfs carries them world
// readable, which is why this needs no privilege on a current port; the old
// debugfs location usually does, and then the helper is asked.
bool BinderMon::locateSource()
{
    const QString binderfs = QStringLiteral("/dev/binderfs/binder_logs");
    const QString debugfs = QStringLiteral("/sys/kernel/debug/binder");
    const QString root = QFileInfo(binderfs).isDir() ? binderfs
                       : QFileInfo(debugfs).exists() ? debugfs : QString();
    if (root.isEmpty()) {
        m_available = false;
        m_error = tr("This device has no binder logs — the kernel was built without them.");
        return false;
    }
    m_root = root;
    m_source = root;
    // A read decides it, not the mode bits: debugfs is 0700 on most ports, but
    // not on all of them, and the helper may be able to read it either way.
    const QByteArray probe = readLog(QStringLiteral("stats"));
    m_available = !probe.isEmpty();
    if (!m_available)
        m_error = tr("%1 exists but cannot be read. On this port the logs belong to root; "
                     "the helper in Settings reads them.").arg(root);
    else
        m_error.clear();
    return m_available;
}

QByteArray BinderMon::readLog(const QString &name)
{
    const QByteArray direct = slurp(m_root + QLatin1Char('/') + name);
    if (!direct.isEmpty()) {
        m_privileged = false;
        return direct;
    }
    const QByteArray viaRoot = RootClient::instance()->binderLog(name);
    if (!viaRoot.isEmpty())
        m_privileged = true;
    return viaRoot;
}

// /etc/gbinder.conf and /etc/gbinder.d/*.conf say which binder devices exist
// and which protocol each speaks. Read rather than assumed: this is the only
// thing that can say a domain belongs to App Support, and it says it by
// having been installed by App Support.
void BinderMon::loadGbinderConfig()
{
    QStringList files;
    if (QFile::exists(QStringLiteral("/etc/gbinder.conf")))
        files << QStringLiteral("/etc/gbinder.conf");
    const QDir d(QStringLiteral("/etc/gbinder.d"));
    for (const QString &e : d.entryList(QStringList() << QStringLiteral("*.conf"), QDir::Files))
        files << d.filePath(e);

    for (const QString &path : files) {
        QString section;
        const QString blob = QString::fromLocal8Bit(slurp(path, 256 * 1024));
        for (const QString &raw : blob.split(QLatin1Char('\n'))) {
            const QString line = raw.trimmed();
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                section = line.mid(1, line.size() - 2);
                continue;
            }
            if (!line.startsWith(QLatin1String("/dev/")) || !line.contains(QLatin1Char('=')))
                continue;
            if (section != QLatin1String("Protocol") && section != QLatin1String("ServiceManager"))
                continue;
            const QString dev = line.section(QLatin1Char('='), 0, 0).trimmed();
            const QString val = line.section(QLatin1Char('='), 1).trimmed();
            const QString ctx = dev.section(QLatin1Char('/'), -1);
            if (section == QLatin1String("Protocol"))
                m_ctxProtocol.insert(ctx, val);
            m_ctxDeclaredBy.insert(ctx, QFileInfo(path).fileName());
        }
    }
}

QString BinderMon::procCmdline(int pid) const
{
    QByteArray raw = slurp(QStringLiteral("/proc/%1/cmdline").arg(pid), 8192);
    while (raw.endsWith('\0'))
        raw.chop(1);
    raw.replace('\0', ' ');
    return QString::fromLocal8Bit(raw).trimmed();
}

// comm is capped at 15 characters by the kernel, argv[0] is whatever the
// process wrote there. Prefer the basename of argv[0] when comm is only its
// truncation, otherwise keep comm -- it is what the kernel itself calls the
// process and what the thread names are built from.
QString BinderMon::procName(int pid) const
{
    const QString comm = firstLine(QStringLiteral("/proc/%1/comm").arg(pid));
    const QString cmd = procCmdline(pid);
    if (cmd.isEmpty())
        return comm;
    const QString base = cmd.section(QLatin1Char(' '), 0, 0).section(QLatin1Char('/'), -1);
    if (base.isEmpty())
        return comm;
    // A process that joins the binder thread pool with its main thread gets
    // that thread renamed to "binder:<pid>_<n>" -- and comm is the main
    // thread's name, so the kernel then calls the whole process after its
    // pool. Exactly the processes this page is about are the ones affected:
    // pid 2541 reads as "binder:2541_2" while it is in truth
    // /vendor/bin/hw/android.hardware.gnss-service.mediatek. Wherever comm is
    // a pool name, the command line is the honest answer.
    const QString lower = comm.toLower();
    if (lower.startsWith(QLatin1String("binder:"))
            || lower.startsWith(QLatin1String("hwbinder:"))
            || lower.startsWith(QLatin1String("vndbinder:")))
        return base;
    if (comm.isEmpty() || base.startsWith(comm))
        return base;
    return comm;
}

// ---------------------------------------------------------------- parsing --

void BinderMon::parseStats(const QByteArray &blob, QHash<int, ProcStat> *procs)
{
    QHash<QString, quint64> global;
    ProcStat cur;
    bool inProc = false;

    const QList<QByteArray> lines = blob.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromLatin1(raw).trimmed();
        if (line.isEmpty())
            continue;

        // "proc 3610" opens a process section; "proc: active 78 total 1266"
        // is a global tally and keeps its colon.
        if (line.startsWith(QLatin1String("proc "))) {
            if (inProc && cur.pid > 0)
                procs->insert(cur.pid, cur);
            cur = ProcStat();
            cur.pid = line.mid(5).trimmed().toInt();
            inProc = true;
            continue;
        }

        if (!inProc) {
            if (line.contains(QLatin1String(": active "))) {
                const QString key = line.section(QLatin1Char(':'), 0, 0);
                const QStringList f = line.section(QLatin1Char(':'), 1).simplified()
                                          .split(QLatin1Char(' '));
                // "active N total M"
                if (f.size() >= 4) {
                    global.insert(key + QStringLiteral("_active"), f.at(1).toULongLong());
                    global.insert(key + QStringLiteral("_total"), f.at(3).toULongLong());
                }
            } else if (line.contains(QLatin1Char(':'))) {
                global.insert(line.section(QLatin1Char(':'), 0, 0),
                              line.section(QLatin1Char(':'), 1).trimmed().toULongLong());
            }
            continue;
        }

        if (line.startsWith(QLatin1String("context "))) {
            cur.context = line.mid(8).trimmed();
        } else if (line.startsWith(QLatin1String("requested threads:"))) {
            // "requested threads: 0+2/15"
            const QString v = line.section(QLatin1Char(':'), 1).trimmed();
            cur.requestedNow = v.section(QLatin1Char('+'), 0, 0).toInt();
            cur.requestedStarted = v.section(QLatin1Char('+'), 1).section(QLatin1Char('/'), 0, 0).toInt();
            cur.maxThreads = v.section(QLatin1Char('/'), 1).toInt();
        } else if (line.startsWith(QLatin1String("ready threads"))) {
            cur.readyThreads = line.mid(13).trimmed().toInt();
        } else if (line.startsWith(QLatin1String("threads:"))) {
            cur.threads = line.mid(8).trimmed().toInt();
        } else if (line.startsWith(QLatin1String("free async space"))) {
            cur.freeAsync = line.mid(16).trimmed().toLongLong();
        } else if (line.startsWith(QLatin1String("nodes:"))) {
            cur.nodes = line.mid(6).trimmed().toInt();
        } else if (line.startsWith(QLatin1String("refs:"))) {
            cur.refs = line.mid(5).trimmed().section(QLatin1Char(' '), 0, 0).toInt();
        } else if (line.startsWith(QLatin1String("buffers:"))) {
            cur.buffers = line.mid(8).trimmed().toInt();
        } else if (line.startsWith(QLatin1String("pending transactions:"))) {
            cur.pending = line.section(QLatin1Char(':'), 1).trimmed().toInt();
        } else if (line.contains(QLatin1Char(':'))) {
            const QString key = line.section(QLatin1Char(':'), 0, 0);
            const quint64 v = line.section(QLatin1Char(':'), 1).trimmed().toULongLong();
            if (key == QLatin1String("BC_TRANSACTION") || key == QLatin1String("BC_TRANSACTION_SG"))
                cur.calls += v;
            else if (key == QLatin1String("BC_REPLY") || key == QLatin1String("BC_REPLY_SG"))
                cur.replies += v;
            else if (key == QLatin1String("BR_TRANSACTION"))
                cur.incoming += v;
        }
    }
    if (inProc && cur.pid > 0)
        procs->insert(cur.pid, cur);

    // Global counters, and the rates the kernel's own tallies allow.
    QVariantMap t;
    const double dt = m_interval;
    auto rate = [&](const QString &key) -> double {
        if (dt <= 0 || !m_prevGlobal.contains(key) || !global.contains(key))
            return -1;
        const quint64 now = global.value(key), before = m_prevGlobal.value(key);
        return now >= before ? (now - before) / dt : -1;
    };
    t.insert(QStringLiteral("calls"), QVariant::fromValue(global.value(QStringLiteral("BC_TRANSACTION"))));
    t.insert(QStringLiteral("replies"), QVariant::fromValue(global.value(QStringLiteral("BC_REPLY"))));
    t.insert(QStringLiteral("deadReplies"), QVariant::fromValue(global.value(QStringLiteral("BR_DEAD_REPLY"))));
    t.insert(QStringLiteral("failedReplies"), QVariant::fromValue(global.value(QStringLiteral("BR_FAILED_REPLY"))));
    t.insert(QStringLiteral("callRate"), rate(QStringLiteral("BC_TRANSACTION")));
    t.insert(QStringLiteral("replyRate"), rate(QStringLiteral("BC_REPLY")));
    t.insert(QStringLiteral("deadReplyRate"), rate(QStringLiteral("BR_DEAD_REPLY")));
    t.insert(QStringLiteral("failedReplyRate"), rate(QStringLiteral("BR_FAILED_REPLY")));
    t.insert(QStringLiteral("procs"), QVariant::fromValue(global.value(QStringLiteral("proc_active"))));
    t.insert(QStringLiteral("threads"), QVariant::fromValue(global.value(QStringLiteral("thread_active"))));
    t.insert(QStringLiteral("nodes"), QVariant::fromValue(global.value(QStringLiteral("node_active"))));
    t.insert(QStringLiteral("refs"), QVariant::fromValue(global.value(QStringLiteral("ref_active"))));
    t.insert(QStringLiteral("deaths"), QVariant::fromValue(global.value(QStringLiteral("death_active"))));
    t.insert(QStringLiteral("inFlight"), QVariant::fromValue(global.value(QStringLiteral("transaction_active"))));
    t.insert(QStringLiteral("transactionsTotal"), QVariant::fromValue(global.value(QStringLiteral("transaction_total"))));
    m_totals = t;
    m_prevGlobal = global;
}

// "18368224: call  from 21530:21530 to 2556:0 context binder node 268
//  handle 1 size 56:0 ret 0/0 l=0" -- and the same line without the context
// and ret parts on a kernel from before binderfs. Read by keyword so both fit.
QList<BinderMon::LogEntry> BinderMon::parseLog(const QByteArray &blob) const
{
    QList<LogEntry> out;
    for (const QByteArray &raw : blob.split('\n')) {
        const QString line = QString::fromLatin1(raw).simplified();
        if (line.isEmpty() || !line.contains(QLatin1Char(':')))
            continue;
        const QStringList tok = line.split(QLatin1Char(' '));
        if (tok.size() < 4 || !tok.at(0).endsWith(QLatin1Char(':')))
            continue;
        bool ok = false;
        const quint64 id = tok.at(0).left(tok.at(0).size() - 1).toULongLong(&ok);
        if (!ok)
            continue;

        LogEntry e;
        e.id = id;
        e.kind = tok.at(1);
        for (int i = 2; i < tok.size(); ++i) {
            const QString &k = tok.at(i);
            const QString v = i + 1 < tok.size() ? tok.at(i + 1) : QString();
            if (k == QLatin1String("from") && v.contains(QLatin1Char(':'))) {
                e.fromPid = v.section(QLatin1Char(':'), 0, 0).toInt();
                e.fromTid = v.section(QLatin1Char(':'), 1).toInt();
            } else if (k == QLatin1String("to") && v.contains(QLatin1Char(':'))) {
                e.toPid = v.section(QLatin1Char(':'), 0, 0).toInt();
                e.toTid = v.section(QLatin1Char(':'), 1).toInt();
            } else if (k == QLatin1String("context")) {
                e.context = v;
            } else if (k == QLatin1String("node")) {
                e.node = v.toInt();
            } else if (k == QLatin1String("handle")) {
                e.handle = v.toInt();
            } else if (k == QLatin1String("ret") && v.contains(QLatin1Char('/'))) {
                e.retCode = v.section(QLatin1Char('/'), 0, 0).toInt();
                e.retParam = v.section(QLatin1Char('/'), 1).toInt();
                e.hasRet = true;
            }
        }
        out.append(e);
    }
    return out;
}

// ---------------------------------------------------------------- refresh --

void BinderMon::refresh()
{
    if (m_root.isEmpty() && !locateSource()) {
        emit updated();
        return;
    }

    const QByteArray statsBlob = readLog(QStringLiteral("stats"));
    if (statsBlob.isEmpty()) {
        m_available = false;
        emit updated();
        return;
    }
    m_available = true;

    m_interval = m_havePrev && m_since.isValid() ? m_since.elapsed() / 1000.0 : 0;
    m_since.restart();

    QHash<int, ProcStat> now;
    parseStats(statsBlob, &now);

    buildTalkers(now, m_interval);
    buildDomains(now, m_interval);
    buildFailures(parseLog(readLog(QStringLiteral("failed_transaction_log"))), m_interval);
    buildStuck(now);
    assess(now);

    m_prev = now;
    m_havePrev = true;
    emit updated();
}

void BinderMon::buildTalkers(const QHash<int, ProcStat> &now, double dt)
{
    QVariantList list;
    m_byPid.clear();
    for (QHash<int, ProcStat>::const_iterator it = now.constBegin(); it != now.constEnd(); ++it) {
        const ProcStat &p = it.value();
        QVariantMap m;
        m.insert(QStringLiteral("pid"), p.pid);
        m.insert(QStringLiteral("name"), procName(p.pid));
        m.insert(QStringLiteral("cmdline"), procCmdline(p.pid));
        m.insert(QStringLiteral("context"), p.context);
        m.insert(QStringLiteral("threads"), p.threads);
        m.insert(QStringLiteral("maxThreads"), p.maxThreads);
        m.insert(QStringLiteral("readyThreads"), p.readyThreads);
        m.insert(QStringLiteral("requestedNow"), p.requestedNow);
        m.insert(QStringLiteral("pending"), p.pending);
        m.insert(QStringLiteral("freeAsync"), p.freeAsync);
        m.insert(QStringLiteral("nodes"), p.nodes);
        m.insert(QStringLiteral("refs"), p.refs);
        m.insert(QStringLiteral("buffers"), p.buffers);
        m.insert(QStringLiteral("calls"), QVariant::fromValue(p.calls));
        m.insert(QStringLiteral("replies"), QVariant::fromValue(p.replies));
        m.insert(QStringLiteral("incoming"), QVariant::fromValue(p.incoming));

        // A rate needs a previous sample of the same process. A pid that was
        // reused since would produce a counter that ran backwards, and that
        // is treated as "no previous sample" rather than as a negative rate.
        double callRate = -1, replyRate = -1, incomingRate = -1;
        const QHash<int, ProcStat>::const_iterator prev = m_prev.constFind(p.pid);
        if (dt > 0 && prev != m_prev.constEnd() && prev.value().calls <= p.calls) {
            callRate = (p.calls - prev.value().calls) / dt;
            replyRate = prev.value().replies <= p.replies ? (p.replies - prev.value().replies) / dt : -1;
            incomingRate = prev.value().incoming <= p.incoming ? (p.incoming - prev.value().incoming) / dt : -1;
        }
        m.insert(QStringLiteral("callRate"), callRate);
        m.insert(QStringLiteral("replyRate"), replyRate);
        m.insert(QStringLiteral("incomingRate"), incomingRate);
        m.insert(QStringLiteral("services"), servicesOf(p.pid));

        list.append(m);
        m_byPid.insert(p.pid, m);
    }

    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        const double xr = x.value(QStringLiteral("callRate")).toDouble();
        const double yr = y.value(QStringLiteral("callRate")).toDouble();
        if (xr != yr)
            return xr > yr;
        return x.value(QStringLiteral("calls")).toULongLong()
             > y.value(QStringLiteral("calls")).toULongLong();
    });
    m_talkers = list;
}

void BinderMon::buildDomains(const QHash<int, ProcStat> &now, double dt)
{
    QHash<QString, int> procCount;
    QHash<QString, double> rate;
    QHash<QString, quint64> total;
    for (QHash<int, ProcStat>::const_iterator it = now.constBegin(); it != now.constEnd(); ++it) {
        const ProcStat &p = it.value();
        // A kernel from before binderfs prints no context at all; there is
        // exactly one domain there, and it is called what the device is called.
        const QString ctx = p.context.isEmpty() ? QStringLiteral("binder") : p.context;
        procCount[ctx] += 1;
        total[ctx] += p.calls;
        const QHash<int, ProcStat>::const_iterator prev = m_prev.constFind(p.pid);
        if (dt > 0 && prev != m_prev.constEnd() && prev.value().calls <= p.calls)
            rate[ctx] += (p.calls - prev.value().calls) / dt;
    }

    QVariantList list;
    const QStringList keys = procCount.keys();
    for (const QString &ctx : keys) {
        QVariantMap m;
        m.insert(QStringLiteral("context"), ctx);
        const QString dev = QStringLiteral("/dev/") + ctx;
        m.insert(QStringLiteral("device"), QFile::exists(dev) ? dev : QString());
        m.insert(QStringLiteral("protocol"), m_ctxProtocol.value(ctx));
        m.insert(QStringLiteral("declaredBy"), m_ctxDeclaredBy.value(ctx));
        m.insert(QStringLiteral("procs"), procCount.value(ctx));
        m.insert(QStringLiteral("callRate"), dt > 0 ? rate.value(ctx) : -1);
        m.insert(QStringLiteral("calls"), QVariant::fromValue(total.value(ctx)));
        int named = 0;
        for (const Service &s : m_registry)
            if (s.context == ctx && s.pid > 0)
                ++named;
        m.insert(QStringLiteral("servicesKnown"), named);
        list.append(m);
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("procs")).toInt()
             > b.toMap().value(QStringLiteral("procs")).toInt();
    });
    m_domains = list;
}

// The kernel's ring of failed transactions holds 32 entries, so a rate taken
// from it is a lower bound whenever more than 32 arrived between two reads.
// Said outright rather than rounded away.
void BinderMon::buildFailures(const QList<LogEntry> &entries, double dt)
{
    struct Group {
        LogEntry sample;
        int inRing = 0;
        int fresh = 0;
    };
    QHash<QString, Group> groups;
    quint64 newest = m_lastFailedId;
    int freshTotal = 0;

    for (const LogEntry &e : entries) {
        const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                .arg(e.fromPid).arg(e.toPid).arg(e.context).arg(e.retCode).arg(e.retParam);
        Group &g = groups[key];
        if (g.inRing == 0)
            g.sample = e;
        ++g.inRing;
        if (e.id > m_lastFailedId) {
            ++g.fresh;
            ++freshTotal;
        }
        newest = qMax(newest, e.id);
    }

    const bool capped = freshTotal >= entries.size() && entries.size() > 0 && m_lastFailedId > 0;
    QVariantList list;
    for (QHash<QString, Group>::const_iterator it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const Group &g = it.value();
        const LogEntry &e = g.sample;
        QVariantMap m;
        m.insert(QStringLiteral("fromPid"), e.fromPid);
        m.insert(QStringLiteral("fromName"), procName(e.fromPid));
        m.insert(QStringLiteral("fromCmdline"), procCmdline(e.fromPid));
        m.insert(QStringLiteral("toPid"), e.toPid);
        m.insert(QStringLiteral("toName"), e.toPid > 0 ? procName(e.toPid) : QString());
        m.insert(QStringLiteral("context"), e.context);
        m.insert(QStringLiteral("node"), e.node);
        m.insert(QStringLiteral("handle"), e.handle);
        m.insert(QStringLiteral("kind"), e.kind);
        m.insert(QStringLiteral("retCode"), e.retCode);
        m.insert(QStringLiteral("retName"), brName(e.retCode));
        m.insert(QStringLiteral("retMeaning"), brMeaning(e.retCode));
        m.insert(QStringLiteral("errParam"), e.retParam);
        m.insert(QStringLiteral("errName"), errnoName(e.retParam));
        m.insert(QStringLiteral("inRing"), g.inRing);
        m.insert(QStringLiteral("ringSize"), entries.size());
        m.insert(QStringLiteral("rate"), dt > 0 && m_lastFailedId > 0 ? g.fresh / dt : -1);
        m.insert(QStringLiteral("rateIsLowerBound"), capped);
        // The service the call was aimed at, when it is one we have proof for.
        QString target;
        for (const Service &s : m_registry)
            if (s.node >= 0 && s.node == e.node && s.context == e.context)
                target = s.name;
        m.insert(QStringLiteral("targetService"), target);
        list.append(m);
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("inRing")).toInt()
             > b.toMap().value(QStringLiteral("inRing")).toInt();
    });
    m_failures = list;
    m_lastFailedId = newest;
}

// A transaction that is still the same one on the next refresh has not come
// back. Anything seen only once is ordinary traffic in flight and is not
// reported as stuck -- binder calls normally finish in microseconds.
void BinderMon::buildStuck(const QHash<int, ProcStat> &now)
{
    // Read the per-process files of the processes that can plausibly hold one:
    // everything with a pending transaction, plus the busiest handful. Reading
    // all of them costs 240 KB and takes the binder locks on the way.
    QList<int> candidates;
    for (QHash<int, ProcStat>::const_iterator it = now.constBegin(); it != now.constEnd(); ++it)
        if (it.value().pending > 0 || it.value().buffers > 0)
            candidates.append(it.key());
    for (int i = 0; i < m_talkers.size() && i < 8; ++i) {
        const int pid = m_talkers.at(i).toMap().value(QStringLiteral("pid")).toInt();
        if (!candidates.contains(pid))
            candidates.append(pid);
    }

    QHash<QString, QVariantMap> seen;
    QVariantList list;
    for (int pid : candidates) {
        const QByteArray blob = readLog(QStringLiteral("proc/%1").arg(pid));
        if (blob.isEmpty())
            continue;
        for (const QByteArray &raw : blob.split('\n')) {
            const QString line = QString::fromLatin1(raw).simplified();
            if (!line.contains(QLatin1String("transaction ")))
                continue;
            const QStringList tok = line.split(QLatin1Char(' '));
            QString kind;
            quint64 txid = 0;
            int fromPid = 0, fromTid = 0, toPid = 0, toTid = 0, code = -1;
            for (int i = 0; i < tok.size(); ++i) {
                const QString &k = tok.at(i);
                const QString v = i + 1 < tok.size() ? tok.at(i + 1) : QString();
                if (k == QLatin1String("transaction") && v.endsWith(QLatin1Char(':'))) {
                    kind = i > 0 ? tok.at(i - 1) : QString();
                    txid = v.left(v.size() - 1).toULongLong();
                } else if (k == QLatin1String("from") && v.contains(QLatin1Char(':'))) {
                    fromPid = v.section(QLatin1Char(':'), 0, 0).toInt();
                    fromTid = v.section(QLatin1Char(':'), 1).toInt();
                } else if (k == QLatin1String("to") && v.contains(QLatin1Char(':'))) {
                    toPid = v.section(QLatin1Char(':'), 0, 0).toInt();
                    toTid = v.section(QLatin1Char(':'), 1).toInt();
                } else if (k == QLatin1String("code")) {
                    code = v.toInt(nullptr, 16);
                }
            }
            if (txid == 0)
                continue;

            const QString key = QStringLiteral("%1:%2").arg(pid).arg(txid);
            QVariantMap m = m_inFlight.value(key);
            const bool second = !m.isEmpty();
            if (!second) {
                m.insert(QStringLiteral("pid"), pid);
                m.insert(QStringLiteral("txid"), QVariant::fromValue(txid));
                m.insert(QStringLiteral("kind"), kind);
                m.insert(QStringLiteral("fromPid"), fromPid);
                m.insert(QStringLiteral("fromTid"), fromTid);
                m.insert(QStringLiteral("toPid"), toPid);
                m.insert(QStringLiteral("toTid"), toTid);
                m.insert(QStringLiteral("code"), code);
                m.insert(QStringLiteral("seconds"), 0.0);
            } else {
                m.insert(QStringLiteral("seconds"),
                         m.value(QStringLiteral("seconds")).toDouble() + m_interval);
            }
            seen.insert(key, m);

            if (second) {
                QVariantMap r = m;
                r.insert(QStringLiteral("fromName"), procName(fromPid ? fromPid : pid));
                r.insert(QStringLiteral("toName"), toPid > 0 ? procName(toPid) : QString());
                QString svc;
                for (const Service &s : m_registry)
                    if (s.pid == toPid && toPid > 0)
                        svc = svc.isEmpty() ? s.name
                                            : svc + QStringLiteral(", ") + s.name;
                r.insert(QStringLiteral("toService"), svc);
                list.append(r);
            }
        }
    }
    m_inFlight = seen;
    m_stuck = list;
}

// Judgments, kept apart from the measurements they rest on. Every finding
// carries the kernel's own figures in its detail rows and states the
// threshold that produced the verdict, so a coloured line never passes for a
// reading. Level 0 nothing, 1 worth knowing, 2 wrong, 3 alarming.
void BinderMon::assess(const QHash<int, ProcStat> &now)
{
    QVariantList out;
    auto detail = [](const QString &label, const QString &value,
                     const QString &colour = QString()) {
        QVariantMap d;
        d.insert(QStringLiteral("label"), label);
        d.insert(QStringLiteral("value"), value);
        d.insert(QStringLiteral("color"), colour);
        return QVariant(d);
    };
    // The key identifies a finding across refreshes. It is built only from
    // what does not move -- caller, domain, error code -- never from a figure,
    // because the page keeps the reader's open/closed state under it and a key
    // that changed with the rate would snap every opened finding shut twice a
    // second.
    auto add = [&out](int level, const QString &title, const QString &verdict,
                      const QVariantList &details, const QString &note, int pid,
                      const QString &key) {
        QVariantMap f;
        f.insert(QStringLiteral("key"), key);
        f.insert(QStringLiteral("level"), level);
        f.insert(QStringLiteral("title"), title);
        f.insert(QStringLiteral("verdict"), verdict);
        f.insert(QStringLiteral("details"), details);
        f.insert(QStringLiteral("note"), note);
        f.insert(QStringLiteral("fixUrl"), QString());
        f.insert(QStringLiteral("fixLabel"), QString());
        f.insert(QStringLiteral("pid"), pid);
        out.append(f);
    };
    const QString from = tr("Source: %1").arg(m_source);

    // 1. Calls that come back as an error, per caller.
    for (const QVariant &v : m_failures) {
        const QVariantMap f = v.toMap();
        const int fromPid = f.value(QStringLiteral("fromPid")).toInt();
        const double rate = f.value(QStringLiteral("rate")).toDouble();
        const int inRing = f.value(QStringLiteral("inRing")).toInt();
        const int ringSize = f.value(QStringLiteral("ringSize")).toInt();
        const QString ret = f.value(QStringLiteral("retName")).toString();
        const QString err = f.value(QStringLiteral("errName")).toString();
        const QString target = f.value(QStringLiteral("targetService")).toString();

        QVariantList d;
        d << detail(tr("Caller"), QStringLiteral("%1 (%2)")
                        .arg(f.value(QStringLiteral("fromName")).toString()).arg(fromPid));
        if (!f.value(QStringLiteral("fromCmdline")).toString().isEmpty())
            d << detail(tr("Command line"), f.value(QStringLiteral("fromCmdline")).toString());
        d << detail(tr("Domain"), f.value(QStringLiteral("context")).toString());
        if (!target.isEmpty())
            d << detail(tr("Target service"), target);
        else if (f.value(QStringLiteral("handle")).toInt() == 0
                 && f.value(QStringLiteral("node")).toInt() <= 0)
            // Handle 0 is the service manager of the domain -- that is the
            // binder ABI itself, not something identified here. A dead reply
            // on it means the domain has no service manager answering.
            d << detail(tr("Target"), tr("the service manager of this domain (handle 0)"));
        else if (f.value(QStringLiteral("node")).toInt() >= 0)
            d << detail(tr("Target"), tr("node %1, handle %2 — not identified; "
                                         "the service list can name it")
                            .arg(f.value(QStringLiteral("node")).toInt())
                            .arg(f.value(QStringLiteral("handle")).toInt()));
        // The kernel's raw figure stands beside the reading of it.
        d << detail(tr("Driver answer"),
                    QStringLiteral("%1%2  (%3/%4)")
                        .arg(ret.isEmpty() ? tr("unknown code") : ret)
                        .arg(err.isEmpty() ? QString() : QStringLiteral(" / ") + err)
                        .arg(f.value(QStringLiteral("retCode")).toInt())
                        .arg(f.value(QStringLiteral("errParam")).toInt()),
                    QStringLiteral("bad"));
        const QString meaning = f.value(QStringLiteral("retMeaning")).toString();
        if (!meaning.isEmpty())
            d << detail(tr("What that means"), meaning);
        if (rate >= 0)
            d << detail(tr("Rate"),
                        f.value(QStringLiteral("rateIsLowerBound")).toBool()
                            ? tr("at least %1 per second").arg(rate, 0, 'f', 1)
                            : tr("%1 per second").arg(rate, 0, 'f', 1));
        d << detail(tr("In the kernel ring"), tr("%1 of %2 entries").arg(inRing).arg(ringSize));

        const int level = (ringSize > 0 && inRing >= ringSize / 2) ? 3 : 2;
        QString note = tr("Threshold: every failing caller in the ring is reported; the level "
                          "is raised when one caller holds half of its %1 entries. The ring is "
                          "a snapshot, so a rate taken from it is a lower bound.").arg(ringSize);
        note += QLatin1Char(' ') + from + QStringLiteral("/failed_transaction_log");
        add(level, tr("Failing binder calls"),
            tr("%1 calls into the %2 domain and the driver answers %3.")
                .arg(f.value(QStringLiteral("fromName")).toString())
                .arg(f.value(QStringLiteral("context")).toString())
                .arg(ret.isEmpty() ? tr("with an error") : ret),
            d, note, fromPid,
            QStringLiteral("fail|%1|%2|%3|%4").arg(fromPid)
                .arg(f.value(QStringLiteral("context")).toString())
                .arg(f.value(QStringLiteral("retCode")).toInt())
                .arg(f.value(QStringLiteral("errParam")).toInt()));
    }

    // 2. Thread pools that have nothing left to answer with.
    for (QHash<int, ProcStat>::const_iterator it = now.constBegin(); it != now.constEnd(); ++it) {
        const ProcStat &p = it.value();
        if (p.maxThreads <= 0)
            continue;
        QVariantList d;
        d << detail(tr("Process"), QStringLiteral("%1 (%2)").arg(procName(p.pid)).arg(p.pid))
          << detail(tr("Binder threads"), tr("%1 running, at most %2")
                        .arg(p.threads).arg(p.maxThreads))
          << detail(tr("Ready for a call"), p.readyThreads < 0 ? tr("not reported")
                                                               : QString::number(p.readyThreads),
                    p.readyThreads == 0 ? QStringLiteral("bad") : QString())
          << detail(tr("Requested, not started"), QString::number(p.requestedNow))
          << detail(tr("Queued transactions"), QString::number(p.pending));
        if (p.threads >= p.maxThreads && p.readyThreads == 0) {
            add(2, tr("Binder thread pool exhausted"),
                tr("%1 runs every binder thread it is allowed and none is free to take a "
                   "call.").arg(procName(p.pid)),
                d,
                tr("Threshold: running threads at the maximum the process registered and "
                   "ready threads at zero — both are the kernel's own counters.")
                    + QLatin1Char(' ') + from + QStringLiteral("/stats"),
                p.pid,
                QStringLiteral("pool|%1").arg(p.pid));
        } else if (p.requestedNow > 0 && p.readyThreads == 0) {
            add(1, tr("Binder thread requested"),
                tr("The driver has asked %1 for another binder thread, has not got it yet, "
                   "and meanwhile no thread of the pool is free.").arg(procName(p.pid)),
                d,
                tr("Threshold: the kernel's \"requested threads\" counter above zero and no "
                   "ready thread at the same time. The counter alone says nothing — on an "
                   "idle device several processes carry one while a thread is free.")
                    + QLatin1Char(' ') + from + QStringLiteral("/stats"),
                p.pid,
                QStringLiteral("req|%1").arg(p.pid));
        }
    }

    // 3. One-way buffer running out, and work nobody has taken.
    for (QHash<int, ProcStat>::const_iterator it = now.constBegin(); it != now.constEnd(); ++it) {
        const ProcStat &p = it.value();
        if (p.freeAsync >= 0 && p.freeAsync < 64 * 1024) {
            QVariantList d;
            d << detail(tr("Process"), QStringLiteral("%1 (%2)").arg(procName(p.pid)).arg(p.pid))
              << detail(tr("One-way buffer left"), tr("%1 bytes").arg(p.freeAsync),
                        QStringLiteral("bad"))
              << detail(tr("Buffers held"), QString::number(p.buffers));
            add(2, tr("One-way buffer nearly full"),
                tr("%1 has little room left for one-way calls; when it runs out, the driver "
                   "rejects them.").arg(procName(p.pid)),
                d,
                tr("Threshold: below 64 KiB free. That threshold is SysMetrics', not the "
                   "kernel's — the kernel reports the figure and sets no limit of its own "
                   "here.") + QLatin1Char(' ') + from + QStringLiteral("/stats"),
                p.pid,
                QStringLiteral("async|%1").arg(p.pid));
        }
        if (p.pending > 0) {
            QVariantList d;
            d << detail(tr("Process"), QStringLiteral("%1 (%2)").arg(procName(p.pid)).arg(p.pid))
              << detail(tr("Queued transactions"), QString::number(p.pending))
              << detail(tr("Binder threads"), tr("%1 running, %2 ready")
                            .arg(p.threads).arg(qMax(0, p.readyThreads)));
            add(1, tr("Binder work waiting"),
                tr("%1 has work queued that it has not taken yet.").arg(procName(p.pid)),
                d,
                tr("The kernel's \"pending transactions\" counter, reported from one upward.")
                    + QLatin1Char(' ') + from + QStringLiteral("/stats"),
                p.pid,
                QStringLiteral("pend|%1").arg(p.pid));
        }
    }

    // 4. Calls that did not come back between two refreshes.
    for (const QVariant &v : m_stuck) {
        const QVariantMap s = v.toMap();
        const int caller = s.value(QStringLiteral("fromPid")).toInt();
        const QString svc = s.value(QStringLiteral("toService")).toString();
        QVariantList d;
        d << detail(tr("Waiting"), QStringLiteral("%1 (%2)")
                        .arg(s.value(QStringLiteral("fromName")).toString()).arg(caller))
          << detail(tr("Waiting for"), QStringLiteral("%1 (%2)")
                        .arg(s.value(QStringLiteral("toName")).toString())
                        .arg(s.value(QStringLiteral("toPid")).toInt()));
        if (!svc.isEmpty())
            d << detail(tr("Service"), svc);
        d << detail(tr("Transaction"), s.value(QStringLiteral("txid")).toString())
          << detail(tr("Method code"), s.value(QStringLiteral("code")).toInt() >= 0
                        ? QStringLiteral("0x%1").arg(s.value(QStringLiteral("code")).toInt(), 0, 16)
                        : tr("not reported"))
          << detail(tr("Waiting since"), tr("%1 s")
                        .arg(s.value(QStringLiteral("seconds")).toDouble(), 0, 'f', 1),
                    QStringLiteral("bad"));
        add(3, tr("Binder call not returning"),
            tr("%1 has been waiting for %2 to answer.")
                .arg(s.value(QStringLiteral("fromName")).toString())
                .arg(svc.isEmpty() ? s.value(QStringLiteral("toName")).toString() : svc),
            d,
            tr("Reported when the same transaction is still in flight on the next refresh. "
               "A binder call normally finishes in microseconds; the figure is the time "
               "between the two readings, so it is a lower bound.")
                + QLatin1Char(' ') + from + QStringLiteral("/proc/<pid>"),
            caller,
            QStringLiteral("stuck|%1|%2").arg(caller)
                .arg(s.value(QStringLiteral("txid")).toString()));
    }

    // 5. Services that did not answer their ping during identification.
    for (const Service &s : m_registry) {
        if (s.proof != QLatin1String("no answer"))
            continue;
        QVariantList d;
        d << detail(tr("Service"), s.name)
          << detail(tr("Domain"), s.device)
          << detail(tr("Answer"), tr("none within 2 s"), QStringLiteral("bad"));
        add(3, tr("Service does not answer"),
            tr("%1 is registered but did not answer a ping.").arg(s.name),
            d,
            tr("A ping is the liveness check every binder service must answer; it performs "
               "no action inside the service. Timeout: 2 s, set by SysMetrics."),
            s.pid,
            QStringLiteral("svc|%1").arg(s.name));
    }

    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("level")).toInt()
             > b.toMap().value(QStringLiteral("level")).toInt();
    });
    m_findings = out;
}

// ------------------------------------------------------------- lookups ----

QVariantMap BinderMon::processDetail(int pid) const
{
    return m_byPid.value(pid);
}

QVariantList BinderMon::servicesOf(int pid) const
{
    QVariantList out;
    for (const Service &s : m_registry) {
        if (s.pid != pid || pid <= 0)
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("name"), s.name);
        m.insert(QStringLiteral("device"), s.device);
        m.insert(QStringLiteral("context"), s.context);
        m.insert(QStringLiteral("node"), s.node);
        m.insert(QStringLiteral("proof"), s.proof);
        out.append(m);
    }
    return out;
}

QString BinderMon::serviceLabel(int pid) const
{
    if (pid <= 0)
        return QString();
    QString first;
    int n = 0;
    for (const Service &s : m_registry) {
        if (s.pid != pid)
            continue;
        if (n == 0)
            first = s.name;
        ++n;
    }
    if (n == 0)
        return QString();
    return n == 1 ? first : tr("%1 +%2").arg(first).arg(n - 1);
}

// The kernel names a binder pool thread "binder:<pid>_<n>", where pid is the
// thread group it belongs to. Android's own pools use "Binder:<pid>_<n>" and
// "HwBinder:<pid>_<n>". All three carry the owner's pid, which is why this
// resolves rather than guesses.
QVariantMap BinderMon::describeThread(const QString &threadName) const
{
    QVariantMap out;
    const int colon = threadName.indexOf(QLatin1Char(':'));
    if (colon < 0)
        return out;
    const QString prefix = threadName.left(colon).toLower();
    if (prefix != QLatin1String("binder") && prefix != QLatin1String("hwbinder")
            && prefix != QLatin1String("vndbinder"))
        return out;
    const QString rest = threadName.mid(colon + 1);
    const QString pidPart = rest.section(QLatin1Char('_'), 0, 0);
    bool ok = false;
    const int pid = pidPart.toInt(&ok);
    if (!ok || pid <= 0)
        return out;

    out.insert(QStringLiteral("ownerPid"), pid);
    out.insert(QStringLiteral("poolIndex"), rest.section(QLatin1Char('_'), 1));
    out.insert(QStringLiteral("alive"), QFile::exists(QStringLiteral("/proc/%1").arg(pid)));
    out.insert(QStringLiteral("ownerName"), procName(pid));
    out.insert(QStringLiteral("ownerCmdline"), procCmdline(pid));
    const QVariantMap bind = m_byPid.value(pid);
    out.insert(QStringLiteral("context"), bind.value(QStringLiteral("context")));
    out.insert(QStringLiteral("binder"), bind);
    out.insert(QStringLiteral("services"), servicesOf(pid));
    return out;
}

QString BinderMon::threadOwner(const QString &threadName) const
{
    const QVariantMap d = describeThread(threadName);
    if (d.isEmpty())
        return QString();
    const QVariantList svc = d.value(QStringLiteral("services")).toList();
    const QString owner = d.value(QStringLiteral("ownerName")).toString();
    if (svc.isEmpty())
        return owner;
    if (svc.size() == 1)
        return QStringLiteral("%1 · %2").arg(owner)
                .arg(svc.first().toMap().value(QStringLiteral("name")).toString());
    return tr("%1 · %n service(s)", "", svc.size()).arg(owner);
}

// Nodes whose owning process is gone while somebody still holds a reference.
// The state file is the only place that has them, and it is expensive, so
// this runs on request and says so.
QVariantMap BinderMon::deadNodes()
{
    QVariantMap out;
    const QByteArray blob = readLog(QStringLiteral("state"));
    if (blob.isEmpty()) {
        out.insert(QStringLiteral("readable"), false);
        return out;
    }
    out.insert(QStringLiteral("readable"), true);
    out.insert(QStringLiteral("bytes"), blob.size());

    QVariantList nodes;
    bool inDead = false;
    for (const QByteArray &raw : blob.split('\n')) {
        const QString line = QString::fromLatin1(raw);
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1String("dead nodes:"))) {
            inDead = true;
            continue;
        }
        if (!inDead)
            continue;
        if (!t.startsWith(QLatin1String("node ")))
            break;               // the dead-node block ends at the first proc
        QVariantMap n;
        n.insert(QStringLiteral("node"), t.mid(5).section(QLatin1Char(':'), 0, 0).toInt());
        // The trailing "proc a b c" on a node line names the processes that
        // still hold a reference to it.
        QVariantList holders;
        const QStringList tok = t.simplified().split(QLatin1Char(' '));
        const int at = tok.indexOf(QStringLiteral("proc"));
        if (at >= 0)
            for (int i = at + 1; i < tok.size(); ++i) {
                bool ok = false;
                const int pid = tok.at(i).toInt(&ok);
                if (!ok)
                    break;
                QVariantMap h;
                h.insert(QStringLiteral("pid"), pid);
                h.insert(QStringLiteral("name"), procName(pid));
                holders.append(h);
            }
        n.insert(QStringLiteral("holders"), holders);
        nodes.append(n);
    }
    out.insert(QStringLiteral("nodes"), nodes);
    out.insert(QStringLiteral("count"), nodes.size());
    return out;
}

QVariantMap BinderMon::rawLog()
{
    QVariantMap out;
    auto lines = [this](const QString &name) {
        QStringList l;
        for (const QByteArray &raw : readLog(name).split('\n')) {
            const QString t = QString::fromLatin1(raw).trimmed();
            if (!t.isEmpty())
                l << t;
        }
        return l;
    };
    const QStringList tx = lines(QStringLiteral("transaction_log"));
    const QStringList fx = lines(QStringLiteral("failed_transaction_log"));
    out.insert(QStringLiteral("transactions"), tx);
    out.insert(QStringLiteral("failed"), fx);
    out.insert(QStringLiteral("readable"), !tx.isEmpty() || !fx.isEmpty());
    return out;
}

// node id -> owning pid. In the state file a node is printed inside the
// section of the process that owns it; the "proc" list at the end of the
// node line names the holders of references, which is a different thing.
QHash<int, int> BinderMon::nodeOwners() const
{
    QHash<int, int> out;
    const QByteArray blob = const_cast<BinderMon *>(this)->readLog(QStringLiteral("state"));
    int cur = -1;
    for (const QByteArray &raw : blob.split('\n')) {
        const QString line = QString::fromLatin1(raw);
        if (line.startsWith(QLatin1String("proc "))) {
            cur = line.mid(5).trimmed().toInt();
            continue;
        }
        if (cur > 0 && line.startsWith(QLatin1String("  node "))) {
            const int node = line.mid(7).section(QLatin1Char(':'), 0, 0).toInt();
            out.insert(node, cur);
        }
    }
    return out;
}

// ------------------------------------------------- service identification --

QStringList BinderMon::listServices(const QString &device) const
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("binder-list"), QStringList() << QStringLiteral("-d") << device);
    if (!p.waitForFinished(4000)) {
        p.kill();
        p.waitForFinished(500);
        return QStringList();
    }
    QStringList out;
    for (const QByteArray &l : p.readAllStandardOutput().split('\n')) {
        const QString t = QString::fromUtf8(l).trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

void BinderMon::identifyServices()
{
    if (m_scanning || !m_canIdentify)
        return;

    // Every binder device the config knows, plus the three standard ones, as
    // far as they exist on this device.
    QStringList devices;
    QStringList names = m_ctxProtocol.keys();
    names << QStringLiteral("binder") << QStringLiteral("vndbinder")
          << QStringLiteral("hwbinder");
    for (const QString &ctx : names) {
        const QString dev = QStringLiteral("/dev/") + ctx;
        if (QFile::exists(dev) && !devices.contains(dev))
            devices << dev;
    }

    m_scanQueue.clear();
    for (const QString &dev : devices)
        for (const QString &svc : listServices(dev))
            m_scanQueue << dev + QLatin1Char('\t') + svc;

    m_scanTotal = m_scanQueue.size();
    m_scanDone = 0;
    m_scanning = m_scanTotal > 0;
    m_scanCurrent.clear();
    // A fresh scan replaces what a previous one found, so a service that has
    // since moved to another process cannot keep its old claim.
    m_registry.clear();
    emit scanChanged();
    if (!m_scanning) {
        publishServices();
        return;
    }
    scanStep();
}

void BinderMon::cancelScan()
{
    if (!m_scanning)
        return;
    m_scanQueue.clear();
    if (m_ping) {
        m_ping->kill();
        m_ping->waitForFinished(300);
    }
    m_scanning = false;
    m_scanCurrent.clear();
    emit scanChanged();
    publishServices();
    saveServiceCache();
}

// One ping per service: note where the kernel's transaction log stands, send
// the ping, and afterwards look for the calls our own ping process made. The
// last of them went to the service itself -- the ones before it are the
// lookup at the service manager.
void BinderMon::scanStep()
{
    if (m_scanQueue.isEmpty()) {
        m_scanning = false;
        m_scanCurrent.clear();
        emit scanChanged();
        publishServices();
        saveServiceCache();
        refresh();
        return;
    }

    const QString job = m_scanQueue.takeFirst();
    m_scanDevice = job.section(QLatin1Char('\t'), 0, 0);
    m_scanName = job.section(QLatin1Char('\t'), 1);
    m_scanCurrent = m_scanName;
    emit scanChanged();

    const QList<LogEntry> before = parseLog(readLog(QStringLiteral("transaction_log")));
    m_scanLogMark = 0;
    for (const LogEntry &e : before)
        m_scanLogMark = qMax(m_scanLogMark, e.id);

    if (!m_ping) {
        m_ping = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        m_ping->setProcessEnvironment(env);
        connect(m_ping, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                this, &BinderMon::onPingFinished);
    }
    if (!m_pingGuard) {
        m_pingGuard = new QTimer(this);
        m_pingGuard->setSingleShot(true);
        connect(m_pingGuard, &QTimer::timeout, this, &BinderMon::onPingTimeout);
    }
    m_ping->start(QStringLiteral("binder-ping"),
                  QStringList() << QStringLiteral("-d") << m_scanDevice << m_scanName);
    m_pingGuard->start(2000);
}

void BinderMon::onPingTimeout()
{
    if (!m_ping)
        return;
    // A service that does not answer its ping is exactly the kind of thing
    // this page exists for, so it is recorded rather than skipped.
    Service s;
    s.name = m_scanName;
    s.device = m_scanDevice;
    s.context = m_scanDevice.section(QLatin1Char('/'), -1);
    s.proof = QStringLiteral("no answer");
    m_registry.append(s);
    m_ping->kill();
    // kill() ends in onPingFinished, which advances the queue.
}

void BinderMon::onPingFinished()
{
    if (m_pingGuard)
        m_pingGuard->stop();

    const qint64 pingPid = m_ping ? m_ping->processId() : 0;
    const bool recorded = !m_registry.isEmpty()
            && m_registry.last().name == m_scanName
            && m_registry.last().proof == QLatin1String("no answer");

    if (!recorded) {
        Service s;
        s.name = m_scanName;
        s.device = m_scanDevice;
        s.context = m_scanDevice.section(QLatin1Char('/'), -1);
        s.proof = QStringLiteral("unproven");

        // Only entries our own ping process wrote count. Its last call is the
        // one to the service; the earlier ones went to the service manager.
        const QList<LogEntry> after = parseLog(readLog(QStringLiteral("transaction_log")));
        for (const LogEntry &e : after) {
            if (e.id <= m_scanLogMark || e.kind != QLatin1String("call"))
                continue;
            if (pingPid > 0 && e.fromPid != int(pingPid))
                continue;
            s.pid = e.toPid;
            s.node = e.node;
            s.proof = QStringLiteral("ping");
        }
        m_registry.append(s);
    }

    ++m_scanDone;
    emit scanChanged();
    // Back to the event loop between pings so the page stays responsive and
    // the user can stop the scan.
    QTimer::singleShot(0, this, &BinderMon::scanStep);
}

// Re-check what a scan established, without pinging again: a service's node
// must still be owned by the same process. This is what keeps the list from
// carrying a stale pid after a service restarted.
void BinderMon::reverifyServices()
{
    if (m_registry.isEmpty())
        return;
    const QHash<int, int> owners = nodeOwners();
    if (owners.isEmpty())
        return;
    for (int i = 0; i < m_registry.size(); ++i) {
        Service &s = m_registry[i];
        if (s.node < 0)
            continue;
        const int owner = owners.value(s.node, -1);
        if (owner < 0) {
            s.pid = -1;
            s.proof = QStringLiteral("gone");
        } else if (owner != s.pid) {
            s.pid = owner;
            s.proof = QStringLiteral("state");
        }
    }
}

void BinderMon::publishServices()
{
    reverifyServices();
    QVariantList list;
    for (const Service &s : m_registry) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), s.name);
        m.insert(QStringLiteral("device"), s.device);
        m.insert(QStringLiteral("context"), s.context);
        m.insert(QStringLiteral("node"), s.node);
        m.insert(QStringLiteral("pid"), s.pid);
        m.insert(QStringLiteral("proof"), s.proof);
        m.insert(QStringLiteral("process"), s.pid > 0 ? procName(s.pid) : QString());
        m.insert(QStringLiteral("cmdline"), s.pid > 0 ? procCmdline(s.pid) : QString());
        list.append(m);
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString()
             < b.toMap().value(QStringLiteral("name")).toString();
    });
    m_services = list;
    emit servicesChanged();
}

// The mapping holds only within one boot: binder node numbers are handed out
// afresh every time, so a cache from an earlier boot would be a guess. The
// boot id is stored with it and checked on load.
QString cachePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/binder-services.txt");
}

void BinderMon::loadServiceCache()
{
    const QString path = cachePath();
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QTextStream in(&f);
    const QString boot = in.readLine();
    if (boot != m_bootId)
        return;
    while (!in.atEnd()) {
        const QStringList f2 = in.readLine().split(QLatin1Char('\t'));
        if (f2.size() < 5)
            continue;
        Service s;
        s.name = f2.at(0);
        s.device = f2.at(1);
        s.context = f2.at(2);
        s.node = f2.at(3).toInt();
        s.pid = f2.at(4).toInt();
        s.proof = f2.value(5);
        m_registry.append(s);
    }
    if (!m_registry.isEmpty())
        publishServices();
}

void BinderMon::saveServiceCache() const
{
    const QString path = cachePath();
    if (path.isEmpty() || m_registry.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream out(&f);
    out << m_bootId << '\n';
    for (const Service &s : m_registry)
        out << s.name << '\t' << s.device << '\t' << s.context << '\t'
            << s.node << '\t' << s.pid << '\t' << s.proof << '\n';
}
