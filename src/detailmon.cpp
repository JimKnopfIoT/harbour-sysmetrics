#include "detailmon.h"

#include "deviceinfo.h"
#include "netinfo.h"
#include "rootclient.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <cstdlib>

#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

// readdir-based fd listing: keeps socket/pipe/anon fds that QDir's stat-based
// type filters drop (their symlink targets are not real paths).
QStringList fdEntries(const QString &dirPath)
{
    QStringList out;
    DIR *d = opendir(QFile::encodeName(dirPath).constData());
    if (!d)
        return out;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9')
            continue;
        out << QLatin1String(e->d_name);
    }
    closedir(d);
    return out;
}

// Raw readlink: QFile::symLinkTarget prepends the dir to proc magic links
// (socket:[n], anon_inode:[..], pipe:[n]); readlink(2) returns the literal.
QString readLinkRaw(const QString &path)
{
    char buf[4096];
    const ssize_t n = ::readlink(QFile::encodeName(path).constData(), buf, sizeof(buf) - 1);
    return n < 0 ? QString() : QString::fromLocal8Bit(buf, n);
}

QByteArray statusValue(const QByteArray &status, const QByteArray &key)
{
    int i = status.indexOf(key + ':');
    while (i > 0 && status.at(i - 1) != '\n')
        i = status.indexOf(key + ':', i + 1);
    if (i < 0)
        return QByteArray();
    const int nl = status.indexOf('\n', i);
    return status.mid(i + key.size() + 1, nl - i - key.size() - 1).trimmed();
}

QVariantMap note(int level, const QString &text)
{
    QVariantMap m;
    m.insert(QStringLiteral("level"), level);
    m.insert(QStringLiteral("text"), text);
    return m;
}

} // namespace

DetailMon::DetailMon(QObject *parent)
    : QObject(parent)
{
    m_clkTck = sysconf(_SC_CLK_TCK);
    if (m_clkTck <= 0)
        m_clkTck = 100;
    m_nCores = (int)sysconf(_SC_NPROCESSORS_CONF);
    connect(&m_timer, &QTimer::timeout, this, &DetailMon::sample);
}

void DetailMon::setPid(int pid)
{
    if (m_pid == pid)
        return;
    m_pid = pid;
    emit pidChanged();
    m_prevMs = 0;
    m_prevJiffies = 0;
    m_prevTid.clear();
    m_prevQueues.clear();
    m_emaCpu = m_emaWake = m_emaWrite = -1;
    m_cpuHist.clear();
    m_tick = 0;
    if (pid > 0) {
        sampleStatic();
        sample();
        m_timer.start(1200);
    } else {
        m_timer.stop();
    }
}

QString DetailMon::procName(int pid)
{
    return QString::fromLocal8Bit(
        readAll(QStringLiteral("/proc/%1/comm").arg(pid)).trimmed());
}

void DetailMon::sampleStatic()
{
    const QString base = QStringLiteral("/proc/%1").arg(m_pid);
    QVariantMap info;

    const QByteArray cmd = readAll(base + QStringLiteral("/cmdline"));
    QStringList args;
    for (const QByteArray &a : cmd.split('\0'))
        if (!a.isEmpty())
            args << QString::fromLocal8Bit(a);
    info.insert(QStringLiteral("cmdline"), args.join(QLatin1Char(' ')));
    info.insert(QStringLiteral("exe"), QFileInfo(base + QStringLiteral("/exe")).symLinkTarget());
    info.insert(QStringLiteral("cwd"), QFileInfo(base + QStringLiteral("/cwd")).symLinkTarget());

    const QList<QByteArray> cg = readAll(base + QStringLiteral("/cgroup")).split('\n');
    info.insert(QStringLiteral("cgroup"),
                cg.isEmpty() ? QString()
                             : QString::fromLocal8Bit(cg.first()).section(QLatin1Char(':'), 2));

    struct stat st;
    if (::stat(QFile::encodeName(base).constData(), &st) == 0) {
        info.insert(QStringLiteral("uid"), (uint)st.st_uid);
        QString user = QString::number(st.st_uid);
        if (const struct passwd *pw = getpwuid(st.st_uid))
            user = QString::fromLocal8Bit(pw->pw_name);
        info.insert(QStringLiteral("user"), user);
        m_sameUser = st.st_uid == getuid();
    }
    m_info = info;
}

void DetailMon::sample()
{
    const QString base = QStringLiteral("/proc/%1").arg(m_pid);
    const QByteArray stat = readAll(base + QStringLiteral("/stat"));
    if (stat.isEmpty()) {
        m_alive = false;
        m_timer.stop();
        emit updated();
        return;
    }
    m_alive = true;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 dtMs = m_prevMs > 0 ? now - m_prevMs : 0;
    m_prevMs = now;

    // ---- stat / status --------------------------------------------------
    const int close = stat.lastIndexOf(')');
    const int open = stat.indexOf('(');
    const QList<QByteArray> f = stat.mid(close + 2).split(' ');
    if (f.size() < 22)
        return;

    m_info.insert(QStringLiteral("name"), QString::fromLocal8Bit(stat.mid(open + 1, close - open - 1)));
    m_info.insert(QStringLiteral("state"), QString(QChar::fromLatin1(f[0].at(0))));
    const int ppid = f[1].toInt();
    m_info.insert(QStringLiteral("ppid"), ppid);
    m_info.insert(QStringLiteral("ppidName"), procName(ppid));
    m_info.insert(QStringLiteral("prio"), f[15].toInt());
    m_info.insert(QStringLiteral("nice"), f[16].toInt());
    m_info.insert(QStringLiteral("threadCount"), f[17].toInt());
    const double uptime = readAll(QStringLiteral("/proc/uptime")).split(' ').value(0).toDouble();
    m_info.insert(QStringLiteral("ageSec"), (int)(uptime - f[19].toULongLong() / (double)m_clkTck));
    m_info.insert(QStringLiteral("oomScore"),
                  readAll(base + QStringLiteral("/oom_score")).trimmed().toInt());

    const QByteArray status = readAll(base + QStringLiteral("/status"));
    m_info.insert(QStringLiteral("cpusAllowed"),
                  QString::fromLatin1(statusValue(status, "Cpus_allowed_list")));

    // ---- cpu -------------------------------------------------------------
    double pct = 0, sharePct = 0;
    sampleCpu(dtMs, pct, sharePct);
    const qulonglong jiff = f[11].toULongLong() + f[12].toULongLong();
    m_cpu.insert(QStringLiteral("timeSec"), jiff / (double)m_clkTck);
    m_prevJiffies = jiff;

    const qulonglong vctx = statusValue(status, "voluntary_ctxt_switches").toULongLong();
    const qulonglong nvctx = statusValue(status, "nonvoluntary_ctxt_switches").toULongLong();
    double wakeRate = 0;
    if (dtMs > 0) {
        wakeRate = (vctx - m_prevVctx + nvctx - m_prevNvctx) * 1000.0 / dtMs;
        m_cpu.insert(QStringLiteral("wakeupsPerSec"), wakeRate);
        m_cpu.insert(QStringLiteral("vctxPerSec"), (vctx - m_prevVctx) * 1000.0 / dtMs);
        m_cpu.insert(QStringLiteral("nvctxPerSec"), (nvctx - m_prevNvctx) * 1000.0 / dtMs);
    }
    m_prevVctx = vctx;
    m_prevNvctx = nvctx;

    const qulonglong minflt = f[6].toULongLong(), majflt = f[8].toULongLong();
    if (dtMs > 0) {
        m_cpu.insert(QStringLiteral("minfltPerSec"), (minflt - m_prevMinflt) * 1000.0 / dtMs);
        m_cpu.insert(QStringLiteral("majfltPerSec"), (majflt - m_prevMajflt) * 1000.0 / dtMs);
    }
    m_prevMinflt = minflt;
    m_prevMajflt = majflt;

    m_cpuHist.append(pct);
    if (m_cpuHist.size() > 180)
        m_cpuHist.removeFirst();

    // ---- memory ----------------------------------------------------------
    m_mem.insert(QStringLiteral("rss"), statusValue(status, "VmRSS").split(' ').value(0).toDouble() * 1024);
    m_mem.insert(QStringLiteral("vmsize"), statusValue(status, "VmSize").split(' ').value(0).toDouble() * 1024);
    m_mem.insert(QStringLiteral("data"), statusValue(status, "VmData").split(' ').value(0).toDouble() * 1024);
    const QByteArray rollup = readAll(base + QStringLiteral("/smaps_rollup"));
    if (!rollup.isEmpty()) {
        const double pss = statusValue(rollup, "Pss").split(' ').value(0).toDouble() * 1024;
        const double priv = (statusValue(rollup, "Private_Clean").split(' ').value(0).toDouble()
                             + statusValue(rollup, "Private_Dirty").split(' ').value(0).toDouble()) * 1024;
        m_mem.insert(QStringLiteral("pss"), pss);
        m_mem.insert(QStringLiteral("uss"), priv);
        m_mem.insert(QStringLiteral("swap"), statusValue(rollup, "Swap").split(' ').value(0).toDouble() * 1024);
    }

    // ---- io ----------------------------------------------------------------
    const QByteArray io = readAll(base + QStringLiteral("/io"));
    double wrRate = 0;
    if (!io.isEmpty()) {
        const qulonglong rd = statusValue(io, "read_bytes").toULongLong();
        const qulonglong wr = statusValue(io, "write_bytes").toULongLong();
        if (dtMs > 0) {
            m_io.insert(QStringLiteral("readRate"), (rd - m_prevRd) * 1000.0 / dtMs);
            wrRate = (wr - m_prevWr) * 1000.0 / dtMs;
            m_io.insert(QStringLiteral("writeRate"), wrRate);
        }
        m_io.insert(QStringLiteral("readTotal"), (double)rd);
        m_io.insert(QStringLiteral("writeTotal"), (double)wr);
        m_prevRd = rd;
        m_prevWr = wr;
    }

    // ---- energy ------------------------------------------------------------
    const QString bat = QStringLiteral("/sys/class/power_supply/battery");
    const double curA = readAll(bat + QStringLiteral("/current_now")).trimmed().toLongLong() / 1e6;
    const double voltV = readAll(bat + QStringLiteral("/voltage_now")).trimmed().toLongLong() / 1e6;
    const QByteArray bstat = readAll(bat + QStringLiteral("/status")).trimmed();
    const double sysW = qAbs(curA) * voltV;
    m_energy.insert(QStringLiteral("systemW"), sysW);
    m_energy.insert(QStringLiteral("discharging"), bstat == "Discharging");
    // crude attribution: CPU-share fraction of measured system power
    m_energy.insert(QStringLiteral("estimateW"), sysW * sharePct / 100.0);
    m_energy.insert(QStringLiteral("sharePct"), sharePct);

    // ---- fds, sockets, devices (each tick), watchers (every 4th) -----------
    sampleFds();
    if (m_tick % 4 == 0)
        sampleWatchers();
    ++m_tick;

    // EMAs for the assessment
    const double a = 0.3;
    m_emaCpu = m_emaCpu < 0 ? pct : (1 - a) * m_emaCpu + a * pct;
    if (dtMs > 0) {
        m_emaWake = m_emaWake < 0 ? wakeRate : (1 - a) * m_emaWake + a * wakeRate;
        m_emaWrite = m_emaWrite < 0 ? wrRate : (1 - a) * m_emaWrite + a * wrRate;
    }
    assess();
    emit updated();
}

void DetailMon::sampleCpu(qint64 dtMs, double &pct, double &sharePct)
{
    // system totals + per-core busy
    const QByteArray sysStat = readAll(QStringLiteral("/proc/stat"));
    qulonglong busyAll = 0, totalAll = 0;
    QVector<qulonglong> coreBusy;
    for (const QByteArray &line : sysStat.split('\n')) {
        if (!line.startsWith("cpu"))
            continue;
        const QList<QByteArray> f = line.simplified().split(' ');
        if (f.size() < 8)
            continue;
        const qulonglong busy = f[1].toULongLong() + f[2].toULongLong() + f[3].toULongLong()
                              + f[6].toULongLong() + f[7].toULongLong();
        const qulonglong total = busy + f[4].toULongLong() + f[5].toULongLong();
        if (f[0] == "cpu") {
            busyAll = busy;
            totalAll = total;
        } else {
            coreBusy.append(busy);
        }
    }

    // per-thread deltas attributed to the core the thread last ran on
    QVector<double> procCore(coreBusy.size(), 0.0);
    QVariantList threads;
    QHash<int, qulonglong> tidNow;
    qulonglong deltaSum = 0;
    QSet<int> coresTouched;
    const QDir taskDir(QStringLiteral("/proc/%1/task").arg(m_pid));
    const double pctFactor = dtMs > 0 ? 100000.0 / (m_clkTck * (double)dtMs) : 0.0;
    for (const QString &tidStr : taskDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const int tid = tidStr.toInt();
        const QByteArray ts = readAll(taskDir.filePath(tidStr) + QStringLiteral("/stat"));
        const int close = ts.lastIndexOf(')');
        if (close < 0)
            continue;
        const int open = ts.indexOf('(');
        const QList<QByteArray> tf = ts.mid(close + 2).split(' ');
        if (tf.size() < 37)
            continue;
        const qulonglong j = tf[11].toULongLong() + tf[12].toULongLong();
        const int core = tf[36].toInt();
        tidNow.insert(tid, j);
        qulonglong d = 0;
        const auto prev = m_prevTid.constFind(tid);
        if (prev != m_prevTid.constEnd() && j >= prev.value())
            d = j - prev.value();
        deltaSum += d;
        if (core >= 0 && core < procCore.size()) {
            procCore[core] += d * pctFactor;
            if (d > 0)
                coresTouched.insert(core);
        }
        QVariantMap t;
        t.insert(QStringLiteral("tid"), tid);
        t.insert(QStringLiteral("name"), QString::fromLocal8Bit(ts.mid(open + 1, close - open - 1)));
        t.insert(QStringLiteral("cpu"), d * pctFactor);
        t.insert(QStringLiteral("core"), core);
        threads.append(t);
    }
    m_prevTid = tidNow;
    m_threads = threads;

    pct = deltaSum * pctFactor;
    m_cpu.insert(QStringLiteral("pct"), pct);
    m_cpu.insert(QStringLiteral("coresUsed"), coresTouched.size());
    m_cpu.insert(QStringLiteral("coreCount"), coreBusy.size());

    QVariantList perCore, sysPerCore;
    for (int c = 0; c < coreBusy.size(); ++c) {
        perCore.append(procCore.value(c));
        double sysPct = 0;
        if (c < m_prevCoreBusy.size() && dtMs > 0)
            sysPct = (coreBusy[c] - m_prevCoreBusy[c]) * pctFactor;
        sysPerCore.append(sysPct);
    }
    m_cpu.insert(QStringLiteral("perCore"), perCore);
    m_cpu.insert(QStringLiteral("sysPerCore"), sysPerCore);
    m_prevCoreBusy = coreBusy;

    const qulonglong dTotal = totalAll - m_prevSysTotal;
    const qulonglong dBusy = busyAll - m_prevSysBusy;
    if (m_prevSysTotal > 0 && dBusy > 0)
        sharePct = 100.0 * deltaSum / dBusy;
    if (m_prevSysTotal > 0 && dTotal > 0)
        m_cpu.insert(QStringLiteral("sysPct"), 100.0 * dBusy / dTotal);
    m_cpu.insert(QStringLiteral("shareOfBusyPct"), sharePct);
    m_prevSysBusy = busyAll;
    m_prevSysTotal = totalAll;
}

void DetailMon::sampleFds()
{
    QVariantList files, devices, sockets;
    QHash<QString, int> anonCount;
    const QHash<quint64, SockInfo> socks = NetInfo::snapshot();
    QHash<quint64, QPair<qulonglong, qulonglong>> queuesNow;

    // Uniform fd list from local procfs, or from the root helper when the
    // target's fd dir is unreadable (foreign uid) and the helper is active.
    struct FdEnt { int fd; QString target; int flags; qulonglong pos; };
    QVector<FdEnt> entries;

    const QString fdDir = QStringLiteral("/proc/%1/fd").arg(m_pid);
    const QStringList fds = fdEntries(fdDir);
    bool viaRoot = false;

    // Local attempt first. Many Sailfish apps are ptrace-restricted even for the
    // same user: the fd directory lists names but readlink() on the targets is
    // denied, so we get names without targets. Fall back to the root helper
    // whenever the local scan produced no usable targets (foreign uid OR
    // same-uid-but-unreadable), not only when the directory is empty.
    for (const QString &fd : fds) {
        const QString target = readLinkRaw(fdDir + QLatin1Char('/') + fd);
        if (target.isEmpty())
            continue;
        const QByteArray fdinfo = readAll(QStringLiteral("/proc/%1/fdinfo/%2").arg(m_pid).arg(fd));
        entries.append({ fd.toInt(), target,
                         statusValue(fdinfo, "flags").toInt(nullptr, 8),
                         statusValue(fdinfo, "pos").toULongLong() });
    }

    if (entries.isEmpty() && RootClient::instance()->active()) {
        viaRoot = true;
        for (const QString &row : RootClient::instance()->fdDump(m_pid)) {
            const QStringList c = row.split(QLatin1Char('|'));
            if (c.size() < 4)
                continue;
            entries.append({ c[0].toInt(), c[1], c[2].toInt(nullptr, 8), c[3].toULongLong() });
        }
        m_info.insert(QStringLiteral("source"), QStringLiteral("root"));
    } else {
        m_info.insert(QStringLiteral("source"),
                      entries.isEmpty() ? QStringLiteral("restricted")
                      : m_sameUser ? QStringLiteral("self") : QStringLiteral("limited"));
    }
    m_info.insert(QStringLiteral("fdCount"), entries.size());

    for (const FdEnt &e : entries) {
        const int fdNum = e.fd;
        const QString &target = e.target;

        if (target.startsWith(QLatin1String("socket:["))) {
            const quint64 inode = target.mid(8, target.size() - 9).toULongLong();
            QVariantMap s;
            s.insert(QStringLiteral("fd"), fdNum);
            const auto it = socks.constFind(inode);
            if (it != socks.constEnd()) {
                s.insert(QStringLiteral("proto"), it->proto);
                s.insert(QStringLiteral("local"), it->local);
                s.insert(QStringLiteral("remote"), it->remote);
                s.insert(QStringLiteral("state"), it->state);
                const auto prevQ = m_prevQueues.constFind(inode);
                const bool changed = prevQ != m_prevQueues.constEnd()
                    && (prevQ->first != it->txq || prevQ->second != it->rxq);
                const bool fresh = prevQ == m_prevQueues.constEnd() && m_tick > 0;
                s.insert(QStringLiteral("active"), changed || fresh
                         || it->txq > 0 || it->rxq > 0);
                queuesNow.insert(inode, qMakePair(it->txq, it->rxq));
            } else {
                // inode absent from all tables: AF_BLUETOOTH or exotic family
                s.insert(QStringLiteral("proto"), QStringLiteral("other"));
                s.insert(QStringLiteral("active"), false);
            }
            sockets.append(s);
            continue;
        }
        if (target.startsWith(QLatin1String("anon_inode:"))) {
            QString kind = target.mid(11);
            kind.remove(QLatin1Char('['));
            kind.remove(QLatin1Char(']'));
            anonCount[kind]++;
            continue;
        }
        if (target.startsWith(QLatin1String("pipe:"))) {
            anonCount[QStringLiteral("pipe")]++;
            continue;
        }

        const int acc = e.flags & 3; // O_ACCMODE
        const QString mode = acc == 0 ? QStringLiteral("r")
                           : acc == 1 ? QStringLiteral("w") : QStringLiteral("rw");

        if (target.startsWith(QLatin1String("/dev/"))) {
            QVariantMap d = DeviceInfo::describe(target);
            d.insert(QStringLiteral("fd"), fdNum);
            d.insert(QStringLiteral("mode"), mode);
            devices.append(d);
            continue;
        }

        QVariantMap fm;
        fm.insert(QStringLiteral("fd"), fdNum);
        QString path = target;
        const bool deleted = path.endsWith(QLatin1String(" (deleted)"));
        if (deleted)
            path.chop(10);
        fm.insert(QStringLiteral("path"), path);
        fm.insert(QStringLiteral("deleted"), deleted);
        fm.insert(QStringLiteral("mode"), mode);
        fm.insert(QStringLiteral("pos"), (double)e.pos);
        fm.insert(QStringLiteral("size"), viaRoot ? -1.0 : (double)QFileInfo(path).size());
        files.append(fm);
    }

    QVariantList anon;
    for (auto it = anonCount.constBegin(); it != anonCount.constEnd(); ++it) {
        QVariantMap m;
        m.insert(QStringLiteral("kind"), it.key());
        m.insert(QStringLiteral("count"), it.value());
        anon.append(m);
    }
    m_info.insert(QStringLiteral("anonInodes"), anon);
    int timerfds = 0;
    for (auto it = anonCount.constBegin(); it != anonCount.constEnd(); ++it)
        if (it.key().contains(QLatin1String("timerfd")))
            timerfds += it.value();
    m_info.insert(QStringLiteral("timerfdCount"), timerfds);

    m_files = files;
    m_devices = devices;
    m_sockets = sockets;
    m_prevQueues = queuesNow;
}

void DetailMon::sampleWatchers()
{
    // who traces us, who holds handles into our procfs
    QVariantMap watch;
    const QByteArray status = readAll(QStringLiteral("/proc/%1/status").arg(m_pid));
    const int tracer = statusValue(status, "TracerPid").toInt();
    watch.insert(QStringLiteral("tracerPid"), tracer);
    if (tracer > 0)
        watch.insert(QStringLiteral("tracerName"), procName(tracer));

    QVariantList watchers;
    const QString needle = QStringLiteral("/proc/%1/").arg(m_pid);

    if (RootClient::instance()->active()) {
        for (const QString &row : RootClient::instance()->watcherScan(m_pid)) {
            const QStringList c = row.split(QLatin1Char('|'));
            if (c.size() < 3 || c[0].toInt() == getpid())
                continue;
            QVariantMap w;
            w.insert(QStringLiteral("pid"), c[0].toInt());
            w.insert(QStringLiteral("name"), c[1]);
            w.insert(QStringLiteral("what"), c[2]);
            watchers.append(w);
        }
        watch.insert(QStringLiteral("watchers"), watchers);
        watch.insert(QStringLiteral("complete"), true);
        m_watch = watch;
        return;
    }

    DIR *proc = opendir("/proc");
    if (proc) {
        struct dirent *de;
        while ((de = readdir(proc))) {
            if (de->d_name[0] < '0' || de->d_name[0] > '9')
                continue;
            const int opid = atoi(de->d_name);
            if (opid == m_pid || opid == getpid())
                continue;
            const QString fdDir = QStringLiteral("/proc/%1/fd").arg(opid);
            QStringList hits;
            for (const QString &fd : fdEntries(fdDir)) {
                const QString t = readLinkRaw(fdDir + QLatin1Char('/') + fd);
                if (t.startsWith(needle))
                    hits << t.mid(needle.size());
            }
            if (!hits.isEmpty()) {
                QVariantMap w;
                w.insert(QStringLiteral("pid"), opid);
                w.insert(QStringLiteral("name"), procName(opid));
                w.insert(QStringLiteral("what"), hits.join(QStringLiteral(", ")));
                watchers.append(w);
            }
        }
        closedir(proc);
    }
    watch.insert(QStringLiteral("watchers"), watchers);
    // fd dirs of foreign-uid processes are unreadable without root
    watch.insert(QStringLiteral("complete"), getuid() == 0);
    m_watch = watch;
}

void DetailMon::assess()
{
    QVariantList notes;
    const int tracer = m_watch.value(QStringLiteral("tracerPid")).toInt();
    if (tracer > 0)
        notes.append(note(3, tr("Process is being traced by PID %1 (%2) — memory readable by the tracer")
                             .arg(tracer).arg(m_watch.value(QStringLiteral("tracerName")).toString())));
    if (!m_watch.value(QStringLiteral("watchers")).toList().isEmpty())
        notes.append(note(2, tr("Other processes hold open handles into this process's procfs")));

    if (m_emaCpu >= 50)
        notes.append(note(3, tr("Sustained high CPU load (%1 %)").arg(m_emaCpu, 0, 'f', 1)));
    else if (m_emaCpu >= 10)
        notes.append(note(2, tr("Elevated CPU load (%1 %)").arg(m_emaCpu, 0, 'f', 1)));
    else if (m_emaCpu >= 0 && m_emaCpu < 2)
        notes.append(note(0, tr("CPU load inconspicuous")));

    if (m_emaWake >= 300)
        notes.append(note(3, tr("Very high wakeup rate (%1/s) — prevents CPU sleep, drains battery").arg((int)m_emaWake)));
    else if (m_emaWake >= 50)
        notes.append(note(2, tr("Elevated wakeup rate (%1/s)").arg((int)m_emaWake)));

    const int timerfds = m_info.value(QStringLiteral("timerfdCount")).toInt();
    if (timerfds >= 5)
        notes.append(note(1, tr("%1 timer fds — many periodic timers").arg(timerfds)));

    if (m_cpu.value(QStringLiteral("majfltPerSec")).toDouble() > 10)
        notes.append(note(2, tr("Frequent major page faults — memory pressure or heavy mmap I/O")));

    if (m_emaWrite > 1048576)
        notes.append(note(2, tr("Sustained writes (%1/s) — flash wear and energy cost")
                             .arg(QString::number(m_emaWrite / 1048576, 'f', 1) + QStringLiteral(" MB"))));

    const double swap = m_mem.value(QStringLiteral("swap")).toDouble();
    if (swap > 52428800)
        notes.append(note(1, tr("%1 MB swapped out").arg((int)(swap / 1048576))));

    if (m_energy.value(QStringLiteral("discharging")).toBool()
        && m_energy.value(QStringLiteral("estimateW")).toDouble() > 0.3)
        notes.append(note(2, tr("Estimated power share %1 mW — noticeable battery drain")
                             .arg((int)(m_energy.value(QStringLiteral("estimateW")).toDouble() * 1000))));

    m_notes = notes;
}
