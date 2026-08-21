#include "sampler.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// out-of-line definition: QVector::fill() binds a reference, which ODR-uses it
const int SysSnap::CoreOffline;

namespace {

// /proc and /sys are read a few hundred times per tick. QFile sets up an engine
// and buffering for each of those; open/read/close does the same job far cheaper.
int readRaw(const char *path, char *buf, int cap)
{
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    int total = 0;
    ssize_t r;
    while (total < cap - 1 && (r = ::read(fd, buf + total, cap - 1 - total)) > 0)
        total += (int)r;
    ::close(fd);
    buf[total] = '\0';
    return total;
}

QByteArray readAll(const QString &path)
{
    const QByteArray p = QFile::encodeName(path);
    const int fd = ::open(p.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return QByteArray();
    QByteArray out;
    char chunk[4096];
    ssize_t r;
    while ((r = ::read(fd, chunk, sizeof(chunk))) > 0)
        out.append(chunk, (int)r);
    ::close(fd);
    return out;
}

qulonglong keyValue(const QByteArray &text, const QByteArray &key)
{
    // "Key:   12345 kB" style lookup (meminfo, status).
    int i = text.indexOf(key);
    while (i > 0 && text.at(i - 1) != '\n')
        i = text.indexOf(key, i + 1);
    if (i < 0)
        return 0;
    i += key.size();
    while (i < text.size() && (text.at(i) == ':' || text.at(i) == ' ' || text.at(i) == '\t'))
        ++i;
    qulonglong v = 0;
    while (i < text.size() && text.at(i) >= '0' && text.at(i) <= '9')
        v = v * 10 + (text.at(i++) - '0');
    return v;
}

int cpuPresentCount()
{
    // /sys/.../cpu/present lists every CPU the kernel knows, online or parked,
    // as "0-7" or "0-3,6-7". /proc/stat only ever shows the online ones.
    const QByteArray s = readAll(QStringLiteral("/sys/devices/system/cpu/present")).trimmed();
    int count = 0;
    for (const QByteArray &part : s.split(',')) {
        if (part.isEmpty())
            continue;
        const int dash = part.indexOf('-');
        if (dash < 0) {
            ++count;
        } else {
            const int a = part.left(dash).toInt();
            const int b = part.mid(dash + 1).toInt();
            if (b >= a)
                count += b - a + 1;
        }
    }
    return count;
}

} // namespace

Sampler::Sampler(QObject *parent)
    : QObject(parent)
{
    m_clkTck = sysconf(_SC_CLK_TCK);
    if (m_clkTck <= 0)
        m_clkTck = 100;
}

void Sampler::start()
{
    if (m_timer)
        return;
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Sampler::sample);
    m_timer->start(m_intervalMs);
    sample();
}

void Sampler::setIntervalMs(int ms)
{
    m_intervalMs = qBound(250, ms, 10000);
    if (m_timer)
        m_timer->start(m_intervalMs);
}

void Sampler::setPaused(bool paused)
{
    m_paused = paused;
}

void Sampler::setProcessesEnabled(bool on)
{
    if (m_procsEnabled == on)
        return;
    m_procsEnabled = on;
    if (on)
        sample();     // the list is visible again: refresh it now, not in 3s
}

void Sampler::sampleNow()
{
    sample();
}

void Sampler::sample()
{
    if (m_paused)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    SysSnap snap;
    qulonglong totalDelta = 0;
    sampleSystem(snap, totalDelta);

    QVector<ProcSample> procs;
    if (m_procsEnabled) {
        // measured against the last walk, not the last tick: after a spell in the
        // background those are minutes apart and a tick-sized divisor would
        // inflate every percentage
        sampleProcesses(procs, m_prevProcMs > 0 ? now - m_prevProcMs : 0);
        m_prevProcMs = now;
        m_lastProcCount = procs.size();
        m_lastThreadCount = 0;
        for (const ProcSample &p : procs)
            m_lastThreadCount += p.threads;
    }
    snap.processCount = m_lastProcCount;
    const int threads = m_lastThreadCount;
    snap.threadCount = threads;

    m_prevMs = now;
    emit systemSampled(snap);
    // never emit an empty list while skipping: the model would read that as
    // "every process exited" and clear itself
    if (m_procsEnabled)
        emit processesSampled(procs, totalDelta);
}

void Sampler::sampleSystem(SysSnap &s, qulonglong &totalDelta)
{
    // /proc/stat: aggregate + per-core busy/total deltas
    // Cores are keyed by their real CPU number, never by line position. SoCs that
    // hotplug (MediaTek in particular) drop a parked core's line from /proc/stat
    // entirely, so position N stops meaning cpuN -- subtracting the stored
    // counters of a different core underflows the unsigned delta and yields
    // impossible readings such as 134%.
    const QByteArray stat = readAll(QStringLiteral("/proc/stat"));
    QHash<int, QPair<qulonglong, qulonglong>> cur;   // cpu number -> (busy, total)
    QPair<qulonglong, qulonglong> curAll(0, 0);
    bool haveAll = false;

    for (const QByteArray &line : stat.split('\n')) {
        if (line.startsWith("procs_running")) {
            s.runnable = line.mid(14).trimmed().toInt();
            continue;
        }
        if (!line.startsWith("cpu"))
            continue;
        const QList<QByteArray> f = line.simplified().split(' ');
        if (f.size() < 8)
            continue;
        qulonglong user = f[1].toULongLong(), nice = f[2].toULongLong(),
                   sys = f[3].toULongLong(), idle = f[4].toULongLong(),
                   iow = f[5].toULongLong(), irq = f[6].toULongLong(),
                   sirq = f[7].toULongLong();
        qulonglong steal = f.size() > 8 ? f[8].toULongLong() : 0;
        const qulonglong busy = user + nice + sys + irq + sirq + steal;
        const QPair<qulonglong, qulonglong> v(busy, busy + idle + iow);

        const QByteArray id = f[0].mid(3);           // empty on the aggregate line
        if (id.isEmpty()) {
            curAll = v;
            haveAll = true;
        } else {
            cur.insert(id.toInt(), v);
        }
    }

    if (haveAll && m_haveAll
        && curAll.first >= m_prevAll.first && curAll.second >= m_prevAll.second) {
        const qulonglong dt = curAll.second - m_prevAll.second;
        if (dt > 0) {
            s.cpuPct = qBound(0.f, 100.f * (curAll.first - m_prevAll.first) / dt, 100.f);
            totalDelta = dt;
        }
    }
    m_prevAll = curAll;
    m_haveAll = haveAll;

    if (m_cpuCount <= 0)
        m_cpuCount = cpuPresentCount();
    for (auto it = cur.constBegin(); it != cur.constEnd(); ++it)
        m_cpuCount = qMax(m_cpuCount, it.key() + 1);   // fallback if 'present' was unreadable

    s.corePct.fill(SysSnap::CoreOffline, m_cpuCount);
    s.coreFreqKhz.fill(SysSnap::CoreOffline, m_cpuCount);
    for (int c = 0; c < m_cpuCount; ++c) {
        const auto now = cur.constFind(c);
        if (now == cur.constEnd()) {
            // parked: leave it flagged offline and forget its counters, which
            // restart from zero if the core is brought back
            m_prevCore.remove(c);
            continue;
        }
        float pct = 0.f;
        const auto prev = m_prevCore.constFind(c);
        if (prev != m_prevCore.constEnd()
            && now->first >= prev->first && now->second >= prev->second) {
            const qulonglong dt = now->second - prev->second;
            if (dt > 0)
                pct = qBound(0.f, 100.f * (now->first - prev->first) / dt, 100.f);
        }
        s.corePct[c] = pct;
        m_prevCore.insert(c, now.value());
        s.coreFreqKhz[c] = readAll(QStringLiteral("/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq")
                                   .arg(c)).trimmed().toInt();
    }

    const QByteArray mem = readAll(QStringLiteral("/proc/meminfo"));
    s.memTotal = keyValue(mem, "MemTotal") * 1024;
    s.memAvailable = keyValue(mem, "MemAvailable") * 1024;
    s.swapTotal = keyValue(mem, "SwapTotal") * 1024;
    s.swapFree = keyValue(mem, "SwapFree") * 1024;
    s.cached = keyValue(mem, "Cached") * 1024;
    s.buffers = keyValue(mem, "Buffers") * 1024;

    const QList<QByteArray> load = readAll(QStringLiteral("/proc/loadavg")).split(' ');
    if (load.size() >= 3) {
        s.load1 = load[0].toDouble();
        s.load5 = load[1].toDouble();
        s.load15 = load[2].toDouble();
    }
    s.uptimeSec = (qlonglong)readAll(QStringLiteral("/proc/uptime")).split(' ').value(0).toDouble();

    // network totals over all non-lo interfaces
    qulonglong rx = 0, tx = 0;
    for (const QByteArray &line : readAll(QStringLiteral("/proc/net/dev")).split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray name = line.left(colon).trimmed();
        if (name == "lo")
            continue;
        const QList<QByteArray> f = line.mid(colon + 1).simplified().split(' ');
        if (f.size() < 10)
            continue;
        const qulonglong irx = f[0].toULongLong();
        const qulonglong itx = f[8].toULongLong();
        rx += irx;
        tx += itx;
        if (irx || itx)
            s.ifaces.append(qMakePair(QString::fromLatin1(name),
                                      QStringLiteral("%1|%2").arg(irx).arg(itx)));
    }
    s.netRxTotal = rx;
    s.netTxTotal = tx;

    // whole-disk sector counters (mmcblkN, sdX, no partitions)
    qulonglong rd = 0, wr = 0;
    for (const QByteArray &line : readAll(QStringLiteral("/proc/diskstats")).split('\n')) {
        const QList<QByteArray> f = line.simplified().split(' ');
        if (f.size() < 10)
            continue;
        const QByteArray name = f[2];
        const bool mmc = name.startsWith("mmcblk") && !name.contains('p');
        const bool sd = name.startsWith("sd") && name.size() == 3;
        if (!mmc && !sd)
            continue;
        rd += f[5].toULongLong() * 512;
        wr += f[9].toULongLong() * 512;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 dt = m_prevMs > 0 ? now - m_prevMs : 0;
    if (dt > 0) {
        s.netRxRate = (rx - m_prevRx) * 1000.0 / dt;
        s.netTxRate = (tx - m_prevTx) * 1000.0 / dt;
        s.diskReadRate = (rd - m_prevDiskRd) * 1000.0 / dt;
        s.diskWriteRate = (wr - m_prevDiskWr) * 1000.0 / dt;
    }
    m_prevRx = rx;
    m_prevTx = tx;
    m_prevDiskRd = rd;
    m_prevDiskWr = wr;

    const QDir tdir(QStringLiteral("/sys/class/thermal"));
    for (const QString &zone : tdir.entryList(QStringList() << QStringLiteral("thermal_zone*"), QDir::Dirs)) {
        const QString base = tdir.filePath(zone);
        const int milli = readAll(base + QStringLiteral("/temp")).trimmed().toInt();
        if (milli <= 0 || milli > 150000)
            continue;
        const QString type = QString::fromLatin1(readAll(base + QStringLiteral("/type")).trimmed());
        s.thermal.append(qMakePair(type, milli / 1000.f));
    }

    // battery: prefer "battery", else first type=Battery supply
    QString bat;
    const QDir psy(QStringLiteral("/sys/class/power_supply"));
    if (psy.exists(QStringLiteral("battery")))
        bat = psy.filePath(QStringLiteral("battery"));
    else
        for (const QString &e : psy.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (readAll(psy.filePath(e) + QStringLiteral("/type")).trimmed() == "Battery") {
                bat = psy.filePath(e);
                break;
            }
    if (!bat.isEmpty()) {
        s.battCapacity = readAll(bat + QStringLiteral("/capacity")).trimmed().toInt();
        // Standard sysfs first; older MediaTek exposes only CamelCase legacy
        // names in different units (mA/mV instead of µA/µV).
        const QByteArray cur = readAll(bat + QStringLiteral("/current_now")).trimmed();
        s.battCurrentA = cur.isEmpty()
            ? readAll(bat + QStringLiteral("/BatteryAverageCurrent")).trimmed().toLongLong() / 1e3
            : cur.toLongLong() / 1e6;
        const QByteArray vol = readAll(bat + QStringLiteral("/voltage_now")).trimmed();
        s.battVoltageV = vol.isEmpty()
            ? readAll(bat + QStringLiteral("/batt_vol")).trimmed().toLongLong() / 1e3
            : vol.toLongLong() / 1e6;
        QByteArray t = readAll(bat + QStringLiteral("/temp")).trimmed();
        if (t.isEmpty()) t = readAll(bat + QStringLiteral("/batt_temp")).trimmed();
        s.battTempC = t.toInt() / 10.0;
        s.battStatus = QString::fromLatin1(readAll(bat + QStringLiteral("/status")).trimmed());
        s.battPowerW = qAbs(s.battCurrentA) * s.battVoltageV;
        qulonglong full = readAll(bat + QStringLiteral("/charge_full")).trimmed().toULongLong();
        qulonglong design = readAll(bat + QStringLiteral("/charge_full_design")).trimmed().toULongLong();
        // some drivers only expose energy_* (µWh) instead of charge_* (µAh)
        if (!full)
            full = readAll(bat + QStringLiteral("/energy_full")).trimmed().toULongLong();
        if (!design)
            design = readAll(bat + QStringLiteral("/energy_full_design")).trimmed().toULongLong();
        s.battChargeFull = full;
        s.battChargeDesign = design;
        // prefer the gauge's own state-of-health; fall back to full/design ratio
        int soh = readAll(bat + QStringLiteral("/soh")).trimmed().toInt();
        if (soh <= 0)
            soh = readAll(QStringLiteral("/sys/class/power_supply/bms/soh")).trimmed().toInt();
        if (soh > 0 && soh <= 100) {
            s.battHealthPct = soh;
            s.battHealthFromGauge = true;
        } else if (full && design) {
            s.battHealthPct = (int)(100 * full / design);
            s.battHealthFromGauge = false;
        }
        s.battCycles = readAll(bat + QStringLiteral("/cycle_count")).trimmed().toInt();
        s.battTech = QString::fromLatin1(readAll(bat + QStringLiteral("/technology")).trimmed());
        s.battModel = QString::fromLatin1(readAll(bat + QStringLiteral("/model_name")).trimmed());
        s.battHealthReport = QString::fromLatin1(readAll(bat + QStringLiteral("/health")).trimmed());
    }

    const QList<QByteArray> ver = readAll(QStringLiteral("/proc/version")).split(' ');
    if (ver.size() >= 3)
        s.kernel = QString::fromLatin1(ver[0] + ' ' + ver[2]);
}

void Sampler::sampleProcesses(QVector<ProcSample> &out, qint64 dtMs)
{
    DIR *dir = opendir("/proc");
    if (!dir)
        return;

    QHash<int, PrevProc> next;
    next.reserve(m_prevProc.size() + 32);
    out.reserve(m_prevProc.size() + 32);
    const double pctFactor = dtMs > 0 ? 100000.0 / (m_clkTck * (double)dtMs) : 0.0;
    const qulonglong pageSize = (qulonglong)sysconf(_SC_PAGESIZE);

    char path[64];
    char buf[4096];

    struct dirent *de;
    while ((de = readdir(dir))) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        const int pid = atoi(de->d_name);

        snprintf(path, sizeof(path), "/proc/%s/stat", de->d_name);
        if (readRaw(path, buf, sizeof(buf)) <= 0)
            continue;

        // comm may itself contain parentheses, so the last ')' closes it
        char *rparen = strrchr(buf, ')');
        if (!rparen)
            continue;

        // fields after comm: 0=state 1=ppid ... 11=utime 12=stime 16=nice
        //                    17=threads 19=starttime 21=rss
        const char *fld[22];
        int nf = 0;
        for (char *c = rparen + 2; *c && nf < 22; ) {
            fld[nf++] = c;
            while (*c && *c != ' ')
                ++c;
            while (*c == ' ')
                ++c;
        }
        if (nf < 22)
            continue;

        ProcSample p;
        p.pid = pid;
        p.state = fld[0][0];
        p.ppid = (int)strtol(fld[1], nullptr, 10);
        p.jiffies = strtoull(fld[11], nullptr, 10) + strtoull(fld[12], nullptr, 10);
        p.nice = (int)strtol(fld[16], nullptr, 10);
        p.threads = (int)strtol(fld[17], nullptr, 10);
        p.startJiffies = strtoull(fld[19], nullptr, 10);
        p.rssBytes = strtoull(fld[21], nullptr, 10) * pageSize;

        const auto prev = m_prevProc.constFind(pid);
        if (prev != m_prevProc.constEnd() && prev->start == p.startJiffies) {
            // Known process: name, cmdline and uid cannot change while it lives,
            // so reuse them. That skips a /proc/<pid>/cmdline read, a stat() and
            // the whole argv[0] derivation for every process on every tick.
            if (p.jiffies > prev->jiffies)
                p.cpuPct = (float)((p.jiffies - prev->jiffies) * pctFactor);
            p.name = prev->name;
            p.cmdline = prev->cmdline;
            p.uid = prev->uid;
            p.kernelThread = prev->kernelThread;
        } else {
            const char *lparen = strchr(buf, '(');
            if (lparen && rparen > lparen)
                p.name = QString::fromLocal8Bit(lparen + 1, (int)(rparen - lparen - 1));

            snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
            const int n = readRaw(path, buf, sizeof(buf));   // buf is free again
            QStringList sl;
            for (int i = 0; i < n; ) {
                const int len = (int)strnlen(buf + i, n - i);
                if (len > 0)
                    sl << QString::fromLocal8Bit(buf + i, len);
                i += len + 1;
            }
            p.cmdline = sl.join(QLatin1Char(' '));
            p.kernelThread = p.cmdline.isEmpty();
            if (!p.kernelThread) {
                // display name: argv[0] basename unless comm is more specific.
                // The kernel caps comm at 15 chars, so "harbour-sysmetr" is
                // really "harbour-sysmetrics"; when argv[0] merely continues
                // comm it is the same name untruncated, when it differs
                // outright, comm stays.
                const QString bn = sl.value(0).section(QLatin1Char('/'), -1);
                if (!bn.isEmpty() && bn != p.name)
                    p.name = bn;
            }

            snprintf(path, sizeof(path), "/proc/%s", de->d_name);
            struct stat st;
            if (::stat(path, &st) == 0)
                p.uid = st.st_uid;
        }

        next.insert(pid, { p.startJiffies, p.jiffies, p.name, p.cmdline,
                           p.uid, p.kernelThread });
        out.append(p);
    }
    closedir(dir);
    m_prevProc = next;
}
