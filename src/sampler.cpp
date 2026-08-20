#include "sampler.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTimer>

#include <cstdlib>
#include <dirent.h>
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

void Sampler::sampleNow()
{
    sample();
}

void Sampler::sample()
{
    if (m_paused)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 dtMs = m_prevMs > 0 ? now - m_prevMs : 0;

    SysSnap snap;
    qulonglong totalDelta = 0;
    sampleSystem(snap, totalDelta);

    QVector<ProcSample> procs;
    sampleProcesses(procs, dtMs);

    snap.processCount = procs.size();
    int threads = 0;
    for (const ProcSample &p : procs)
        threads += p.threads;
    snap.threadCount = threads;

    m_prevMs = now;
    emit systemSampled(snap);
    emit processesSampled(procs, totalDelta);
}

void Sampler::sampleSystem(SysSnap &s, qulonglong &totalDelta)
{
    // /proc/stat: aggregate + per-core busy/total deltas
    const QByteArray stat = readAll(QStringLiteral("/proc/stat"));
    QVector<QPair<qulonglong, qulonglong>> cur; // (busy, total)
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
        cur.append(qMakePair(busy, busy + idle + iow));
    }
    for (int i = 0; i < cur.size(); ++i) {
        float pct = 0.f;
        if (i < m_prevCpu.size()) {
            const qulonglong db = cur[i].first - m_prevCpu[i].first;
            const qulonglong dt = cur[i].second - m_prevCpu[i].second;
            if (dt > 0)
                pct = 100.f * db / dt;
            if (i == 0)
                totalDelta = dt;
        }
        if (i == 0)
            s.cpuPct = pct;
        else
            s.corePct.append(pct);
    }
    m_prevCpu = cur;

    for (int c = 0; c < s.corePct.size(); ++c) {
        const QByteArray f = readAll(QStringLiteral("/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq").arg(c));
        s.coreFreqKhz.append(f.trimmed().toInt());
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
    const double pctFactor = dtMs > 0 ? 100000.0 / (m_clkTck * (double)dtMs) : 0.0;

    struct dirent *de;
    while ((de = readdir(dir))) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        const int pid = atoi(de->d_name);
        const QString base = QStringLiteral("/proc/") + QLatin1String(de->d_name);
        const QByteArray stat = readAll(base + QStringLiteral("/stat"));
        const int close = stat.lastIndexOf(')');
        if (close < 0)
            continue;

        ProcSample p;
        p.pid = pid;
        const int open = stat.indexOf('(');
        p.name = QString::fromLocal8Bit(stat.mid(open + 1, close - open - 1));
        // fields after comm: 0=state 1=ppid ... 11=utime 12=stime 16=nice 17=threads 19=starttime 21=rss
        const QList<QByteArray> f = stat.mid(close + 2).split(' ');
        if (f.size() < 22)
            continue;
        p.state = f[0].isEmpty() ? '?' : f[0].at(0);
        p.ppid = f[1].toInt();
        p.jiffies = f[11].toULongLong() + f[12].toULongLong();
        p.nice = f[16].toInt();
        p.threads = f[17].toInt();
        p.startJiffies = f[19].toULongLong();
        p.rssBytes = f[21].toULongLong() * (qulonglong)sysconf(_SC_PAGESIZE);

        struct stat st;
        if (::stat(QFile::encodeName(base).constData(), &st) == 0)
            p.uid = st.st_uid;

        const auto prev = m_prevProc.constFind(pid);
        if (prev != m_prevProc.constEnd() && prev->start == p.startJiffies) {
            if (p.jiffies > prev->jiffies)
                p.cpuPct = (float)((p.jiffies - prev->jiffies) * pctFactor);
            p.cmdline = prev->cmdline;
        } else {
            const QByteArray cmd = readAll(base + QStringLiteral("/cmdline"));
            const QList<QByteArray> parts = cmd.split('\0');
            QStringList sl;
            for (const QByteArray &a : parts)
                if (!a.isEmpty())
                    sl << QString::fromLocal8Bit(a);
            p.cmdline = sl.join(QLatin1Char(' '));
        }
        p.kernelThread = p.cmdline.isEmpty();
        if (!p.kernelThread) {
            // display name: argv[0] basename unless comm is more specific
            const QString arg0 = p.cmdline.section(QLatin1Char(' '), 0, 0);
            const QString bn = arg0.section(QLatin1Char('/'), -1);
            if (!bn.isEmpty() && !bn.startsWith(p.name))
                p.name = bn;
        }

        next.insert(pid, { p.startJiffies, p.jiffies, p.name, p.cmdline });
        out.append(p);
    }
    closedir(dir);
    m_prevProc = next;
}
