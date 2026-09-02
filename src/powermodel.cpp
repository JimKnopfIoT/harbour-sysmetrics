#include "powermodel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>

#include <algorithm>

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll().trimmed();
}

// "0-3", "0-5", "0,2-3" -- the kernel writes cpu masks as ranges.
QVector<int> parseCpuList(const QByteArray &s)
{
    QVector<int> out;
    for (const QByteArray &part : s.split(',')) {
        const int dash = part.indexOf('-');
        if (dash > 0) {
            const int a = part.left(dash).toInt();
            const int b = part.mid(dash + 1).toInt();
            for (int i = a; i <= b && i - a < 64; ++i)
                out.append(i);
        } else if (!part.isEmpty()) {
            out.append(part.toInt());
        }
    }
    return out;
}

} // namespace

PowerModel::PowerModel(QObject *parent)
    : QObject(parent)
{
    readWeights();
    load();
}

void PowerModel::readWeights()
{
    // Kernels up to 5.x name a state cs:<freq> in milliwatts, 6.x ps:<freq> in
    // microwatts. The unit cancels in the scale; only the ratios matter.
    const QDir em(QStringLiteral("/sys/kernel/debug/energy_model"));
    for (const QString &pd : em.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString base = em.filePath(pd);
        Domain d;
        d.cpus = parseCpuList(readAll(base + QStringLiteral("/cpus")));
        if (d.cpus.isEmpty())
            continue;
        const QDir dir(base);
        for (const QString &st : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!st.startsWith(QLatin1String("ps:")) && !st.startsWith(QLatin1String("cs:")))
                continue;
            m_unitMilliWatt = st.startsWith(QLatin1String("ps:")) ? 0.001 : 1.0;
            const int khz = readAll(dir.filePath(st) + QStringLiteral("/frequency")).toInt();
            const double pw = readAll(dir.filePath(st) + QStringLiteral("/power")).toDouble();
            if (khz > 0 && pw > 0)
                d.states.append({khz, pw});
        }
        if (d.states.isEmpty())
            continue;
        std::sort(d.states.begin(), d.states.end(),
                  [](const QPair<int, double> &a, const QPair<int, double> &b) {
                      return a.first < b.first;
                  });
        m_domains.append(d);
    }
    if (!m_domains.isEmpty()) {
        m_basis = EnergyModel;
        return;
    }

    // No energy model: the scheduler's capacity is a device figure too, though
    // it says what a core does per second, not what that costs.
    QVector<int> caps;
    bool any = false;
    for (int c = 0; c < 64; ++c) {
        const QString p = QStringLiteral("/sys/devices/system/cpu/cpu%1/cpu_capacity").arg(c);
        if (!QFile::exists(p))
            break;
        caps.append(readAll(p).toInt());
        any = true;
    }
    if (any) {
        for (int c = 0; c < caps.size(); ++c) {
            Domain d;
            d.cpus.append(c);
            d.flat = caps.at(c);
            m_domains.append(d);
        }
        m_basis = CpuCapacity;
        return;
    }

    // Last resort: frequency gets the direction right, the magnitude wrong.
    for (int c = 0; c < 64; ++c) {
        const QString p = QStringLiteral("/sys/devices/system/cpu/cpu%1/cpufreq/cpuinfo_max_freq").arg(c);
        if (!QFile::exists(p))
            break;
        Domain d;
        d.cpus.append(c);
        d.flat = readAll(p).toInt() / 1000.0;
        m_domains.append(d);
        m_basis = Frequency;
    }
}

double PowerModel::coreWeight(int cpu, int freqKhz) const
{
    for (const Domain &d : m_domains) {
        if (!d.cpus.contains(cpu))
            continue;
        if (d.states.isEmpty())
            return d.flat;
        // Clamp: the model says nothing outside the governor's own range.
        if (freqKhz <= d.states.first().first)
            return d.states.first().second;
        if (freqKhz >= d.states.last().first)
            return d.states.last().second;
        for (int i = 1; i < d.states.size(); ++i) {
            if (freqKhz > d.states.at(i).first)
                continue;
            const auto &lo = d.states.at(i - 1);
            const auto &hi = d.states.at(i);
            const double t = double(freqKhz - lo.first) / double(hi.first - lo.first);
            return lo.second + t * (hi.second - lo.second);
        }
        return d.states.last().second;
    }
    return 0;
}

double PowerModel::observe(const SysSnap &snap)
{
    double loadUnits = 0;
    for (int c = 0; c < snap.corePct.size() && c < snap.coreFreqKhz.size(); ++c) {
        const float pct = snap.corePct.at(c);
        const int khz = snap.coreFreqKhz.at(c);
        if (pct <= 0 || khz <= 0)          // parked cores carry CoreOffline
            continue;
        loadUnits += (pct / 100.0) * coreWeight(c, khz);
    }

    // Only off the charger: on the cable the gauge measures the cell. The sign
    // decides, not the status -- older MediaTek gauges say "Not charging".
    const bool charging = snap.battStatus == QLatin1String("Charging")
                          || snap.battStatus == QLatin1String("Full");
    const double mA = -snap.battCurrentA * 1000.0;      // drain positive
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    m_totalMilliAmp = (!charging && mA > 0) ? mA : -1;
    m_cpuMilliAmp = toMilliAmp(loadUnits, snap.battVoltageV);

    if (!charging && mA > 0) {
        m_hist.append({now, loadUnits, mA});
        if (m_hist.size() > 600)
            m_hist.remove(0, m_hist.size() - 600);
        recompute();
    } else {
        // Discard rather than bridge: a pair across the unplug would compare
        // two different circuits.
        m_hist.clear();
    }
    return loadUnits;
}

void PowerModel::recompute()
{
    // From changes, never levels. Measured on the Jolla Phone (2026): the gauge
    // follows a step in 1.2 s; single readings gave 67 mA and 644 mA at one load.
    if (m_hist.size() < kSmooth * 3)
        return;

    QVector<Obs> sm;
    sm.reserve(m_hist.size());
    for (int i = kSmooth - 1; i < m_hist.size(); ++i) {
        double l = 0, a = 0;
        for (int k = i - kSmooth + 1; k <= i; ++k) {
            l += m_hist.at(k).load;
            a += m_hist.at(k).mA;
        }
        sm.append({m_hist.at(i).ms, l / kSmooth, a / kSmooth});
    }

    // The load must move enough to dominate the gauge's noise: a tenth of the
    // device's biggest core, in model units, so the threshold scales with it.
    double biggest = 0;
    for (const Domain &d : m_domains)
        biggest = qMax(biggest, d.states.isEmpty() ? d.flat : d.states.last().second);
    const double minStep = biggest * 0.10;
    if (minStep <= 0)
        return;

    m_ratios.clear();
    for (int i = 0; i < sm.size(); ++i) {
        for (int j = i + 1; j < sm.size(); ++j) {
            const qint64 dt = sm.at(j).ms - sm.at(i).ms;
            if (dt < kPairMinMs)
                continue;
            if (dt > kPairMaxMs)
                break;                      // too far apart: the base has drifted
            const double dl = sm.at(j).load - sm.at(i).load;
            if (qAbs(dl) < minStep)
                continue;
            const double r = (sm.at(j).mA - sm.at(i).mA) / dl;
            // Negative ratios stay: dropping them leaves only the pairs whose
            // noise pointed the expected way, overstating the scale by 40 %.
            if (qAbs(r) < 1e6)
                m_ratios.append(r);
        }
    }
    if (m_ratios.size() < kMinPairs) {
        m_ratios.clear();
        return;
    }

    std::sort(m_ratios.begin(), m_ratios.end());
    const int n = m_ratios.size();
    const double median = m_ratios.at(n / 2);
    const double q1 = m_ratios.at(n / 4);
    const double q3 = m_ratios.at((3 * n) / 4);
    const double spread = median > 0 ? (q3 - q1) / 2.0 / median : -1;

    // The gate: a scale this uncertain differed by a factor of two on one
    // device, so the page shows weighted shares instead of milliamps.
    if (median <= 0 || spread < 0 || spread > kMaxSpread) {
        m_scale = -1;
        m_spread = spread;
        return;
    }
    m_scale = median;
    m_spread = spread;
    m_calibratedMs = QDateTime::currentMSecsSinceEpoch();
    save();
}

// The energy model does not only rank the clusters, it states what a core draws
// at each state -- so a share of CPU time converts to power with no calibration.
double PowerModel::toMilliWatt(double modelLoad) const
{
    return m_unitMilliWatt > 0 ? modelLoad * m_unitMilliWatt : -1;
}

// A floor, not the whole cost: the model counts the cores' dynamic power and
// leaves out leakage, L3, memory controller and the loss in the regulator.
// Measured against a load step on the Jolla Phone (2026), the true figure came
// out about twice this one -- so it is quoted as what it is and not scaled by a
// constant from one device. Where the gauge has yielded a scale precise enough,
// that is used instead, because it was measured on the device in hand.
double PowerModel::toMilliAmp(double modelLoad, double volts) const
{
    if (m_scale > 0)
        return m_scale * modelLoad;
    const double mW = toMilliWatt(modelLoad);
    return (mW >= 0 && volts > 0.1) ? mW / volts : -1;
}

void PowerModel::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("powermodel"));
    // Tied to the basis: a scale learned against the energy model means
    // nothing without it.
    if (s.value(QStringLiteral("basis"), -1).toInt() == (int)m_basis) {
        m_scale = s.value(QStringLiteral("scale"), -1).toDouble();
        m_spread = s.value(QStringLiteral("spread"), -1).toDouble();
        m_calibratedMs = s.value(QStringLiteral("calibratedMs"), 0).toLongLong();
    }
    s.endGroup();
}

void PowerModel::save() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("powermodel"));
    s.setValue(QStringLiteral("basis"), (int)m_basis);
    s.setValue(QStringLiteral("scale"), m_scale);
    s.setValue(QStringLiteral("spread"), m_spread);
    s.setValue(QStringLiteral("calibratedMs"), m_calibratedMs);
    s.endGroup();
}

void PowerModel::onSystem(const SysSnap &snap)
{
    observe(snap);
    emit changed();
}

QString PowerModel::basisText() const
{
    switch (m_basis) {
    case EnergyModel: return QStringLiteral("energy model");
    case CpuCapacity: return QStringLiteral("cpu capacity");
    case Frequency:   return QStringLiteral("frequency");
    default:          return QString();
    }
}

int PowerModel::calibratedAgoSec() const
{
    if (m_calibratedMs <= 0)
        return -1;
    return int((QDateTime::currentMSecsSinceEpoch() - m_calibratedMs) / 1000);
}
