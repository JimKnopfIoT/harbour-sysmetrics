// Known-issue diagnostics. Every check reads sysfs/procfs as the app user and
// reports a verdict plus the measured details behind it; fixes are linked, not
// applied. A check that does not apply to this device adds nothing.
#include "diagnostics.h"
#include "rootclient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace {

QString readTrim(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll()).trimmed();
}

// color: "" (neutral), "ok" (green — the system already has a fix/mitigation),
// "bad" (red — actually vulnerable/broken)
QVariantMap detail(const QString &label, const QString &value,
                   const QString &color = QString())
{
    QVariantMap d;
    d.insert(QStringLiteral("label"), label);
    d.insert(QStringLiteral("value"), value);
    d.insert(QStringLiteral("color"), color);
    return d;
}

QVariantMap finding(const QString &id, const QString &title, int level,
                    const QString &verdict, const QVariantList &details,
                    const QString &note = QString(),
                    const QString &fixLabel = QString(),
                    const QString &fixUrl = QString())
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), id);
    m.insert(QStringLiteral("title"), title);
    m.insert(QStringLiteral("level"), level);
    m.insert(QStringLiteral("verdict"), verdict);
    m.insert(QStringLiteral("details"), details);
    m.insert(QStringLiteral("note"), note);
    m.insert(QStringLiteral("fixLabel"), fixLabel);
    m.insert(QStringLiteral("fixUrl"), fixUrl);
    return m;
}

const QString kAdvCamUrl    = QStringLiteral("https://github.com/JimKnopfIoT/harbour-advanced-camera");
const QString kMicGainUrl   = QStringLiteral("https://github.com/JimKnopfIoT/harbour-micgain");

QString mhz(qlonglong khz) { return QString::number(khz / 1000) + QStringLiteral(" MHz"); }

struct Policy {
    QString path, cpus, governor;
    qlonglong minKhz = 0, maxKhz = 0;
};

QList<Policy> readPolicies()
{
    QList<Policy> out;
    QDir d(QStringLiteral("/sys/devices/system/cpu/cpufreq"));
    QStringList entries = d.entryList(QStringList() << QStringLiteral("policy*"), QDir::Dirs);
    entries.sort();
    for (const QString &e : entries) {
        Policy p;
        p.path = d.filePath(e);
        p.cpus = readTrim(p.path + QStringLiteral("/related_cpus"));
        p.governor = readTrim(p.path + QStringLiteral("/scaling_governor"));
        p.minKhz = readTrim(p.path + QStringLiteral("/scaling_min_freq")).toLongLong();
        p.maxKhz = readTrim(p.path + QStringLiteral("/cpuinfo_max_freq")).toLongLong();
        if (p.maxKhz > 0)
            out.append(p);
    }
    return out;
}

} // namespace

Diagnostics::Diagnostics(QObject *parent) : QObject(parent) {}

// Every finding belongs to the detail page of its subsystem; the topic key
// is what the pages filter on.
static QString topicFor(const QString &id)
{
    if (id.startsWith(QLatin1String("cpu-")) || id == QLatin1String("touch-boost")
        || id == QLatin1String("load-dstate"))
        return QStringLiteral("cpu");
    if (id.startsWith(QLatin1String("gpu-")))    return QStringLiteral("gpu");
    if (id.startsWith(QLatin1String("camera-"))) return QStringLiteral("camera");
    if (id == QLatin1String("mic-gain"))         return QStringLiteral("audio");
    if (id.startsWith(QLatin1String("bt-")))     return QStringLiteral("bluetooth");
    if (id.startsWith(QLatin1String("wlan-")))   return QStringLiteral("network");
    return QStringLiteral("system");
}

QVariantList Diagnostics::run(double cpuPct, double load1) const
{
    QVariantList out;
    checkCameraProvider(out);
    checkCpuVulnerabilities(out);
    checkCpuGovernor(out);
    checkTouchBoost(out);
    checkGpuFloor(out);
    checkFreqResidency(out);
    checkLoadVsCpu(out, cpuPct, load1);
    checkMtkHotplug(out);
    checkMicGain(out);
    checkBtAdapters(out);
    checkWlanRadio(out);
    for (int i = 0; i < out.size(); ++i) {
        QVariantMap m = out[i].toMap();
        m.insert(QStringLiteral("topic"), topicFor(m.value(QStringLiteral("id")).toString()));
        out[i] = m;
    }
    return out;
}

// Hardware CVE class (Spectre, Meltdown, …): the kernel self-reports each
// known speculative-execution vulnerability. The sysfs list is generic and
// mostly x86 — entries this CPU cannot be affected by ("Not affected") are
// noise on ARM, so only issues that actually touch this CPU are listed:
// green = mitigated by the kernel, red = affected with no fix in this build.
void Diagnostics::checkCpuVulnerabilities(QVariantList &out) const
{
    const QDir d(QStringLiteral("/sys/devices/system/cpu/vulnerabilities"));
    if (!d.exists()) {
        // The reporting interface arrived in kernel 4.15. On older kernels
        // silence would read as "all clear" — say the honest thing instead:
        // status unknown, and kernels this old predate the fixes anyway.
        const QString kern = readTrim(QStringLiteral("/proc/sys/kernel/osrelease"));
        QVariantList det;
        det.append(detail(tr("Kernel"), kern));
        out.append(finding(QStringLiteral("cpu-vulns"),
            tr("CPU vulnerability status unknown"), 2,
            tr("This kernel predates the reporting interface (4.15) — it cannot say whether Spectre-class issues are mitigated."),
            det,
            tr("Kernels this old were released before the fixes existed; out-of-order cores (e.g. Cortex-A72) are affected by Spectre v1/v2 and almost certainly run unmitigated.")));
        return;
    }
    QStringList files = d.entryList(QDir::Files);
    files.sort();
    QVariantList det;
    int vulnerable = 0, mitigated = 0, notAffected = 0;
    for (const QString &f : files) {
        const QString v = readTrim(d.filePath(f));
        if (v.startsWith(QLatin1String("Not affected"))) { ++notAffected; continue; }
        QString color;
        if (v.startsWith(QLatin1String("Mitigation"))) { ++mitigated; color = QStringLiteral("ok"); }
        else if (v.startsWith(QLatin1String("Vulnerable"))) { ++vulnerable; color = QStringLiteral("bad"); }
        det.append(detail(f, v, color));
    }
    // Nothing on the list touches this CPU: no finding at all. Entries that
    // cannot affect this architecture are never listed — the glossary's
    // Diagnosis section explains those classes instead.
    Q_UNUSED(notAffected)
    if (det.isEmpty())
        return;
    if (vulnerable > 0) {
        out.append(finding(QStringLiteral("cpu-vulns"),
            tr("CPU hardware vulnerabilities"), 3,
            tr("This CPU is affected by %1 known issues — %2 unmitigated (red), %3 mitigated.")
                .arg(vulnerable + mitigated).arg(vulnerable).arg(mitigated),
            det,
            tr("Spectre/Meltdown-class issues. An unmitigated entry means this kernel build ships no fix for it — only a kernel update can change that. Issue classes that cannot affect this architecture are not listed; the glossary explains them.")));
    } else {
        out.append(finding(QStringLiteral("cpu-vulns"),
            tr("CPU hardware vulnerabilities"), 0,
            tr("This CPU is affected by %1 known issues — all carry a kernel mitigation (green).")
                .arg(mitigated),
            det,
            tr("Spectre/Meltdown-class issues, self-reported by the kernel. Green = the fix is already in place. Issue classes that cannot affect this architecture are not listed; the glossary explains them.")));
    }
}

// Vendor init on some Qualcomm ports leaves the big cluster's governor in
// "powersave", pinning it to its floor under load. (The second flavour — a
// dead schedutil instance — cannot be told apart from a healthy one without
// generating load, so it is covered by the residency check instead.)
void Diagnostics::checkCpuGovernor(QVariantList &out) const
{
    const QList<Policy> pols = readPolicies();
    if (pols.isEmpty())
        return;
    QVariantList det;
    bool stuck = false, aggressive = false;
    for (const Policy &p : pols) {
        det.append(detail(tr("CPUs %1").arg(p.cpus),
                          p.governor + QStringLiteral(" · ") + mhz(p.minKhz)
                          + QStringLiteral(" – ") + mhz(p.maxKhz)));
        if (p.governor == QLatin1String("powersave"))
            stuck = true;
        // schedutil tunables: how the governor is biased, not just that it runs
        const QString su = p.path + QStringLiteral("/schedutil/");
        const QString up = readTrim(su + QStringLiteral("up_rate_limit_us"));
        if (!up.isEmpty()) {
            const QString down = readTrim(su + QStringLiteral("down_rate_limit_us"));
            const QString hiLoad = readTrim(su + QStringLiteral("hispeed_load"));
            const QString hiFreq = readTrim(su + QStringLiteral("hispeed_freq"));
            const QString pl = readTrim(su + QStringLiteral("pl"));
            QString v = tr("ramp-up limit %1 µs · ramp-down %2 µs").arg(up).arg(down);
            if (!hiLoad.isEmpty() && hiFreq.toLongLong() > 0)
                v += tr(" · from %1% load jump to %2").arg(hiLoad).arg(mhz(hiFreq.toLongLong()));
            if (pl == QLatin1String("1"))
                v += tr(" · predictive load on");
            det.append(detail(tr("Tuning CPUs %1").arg(p.cpus), v));
            if (up == QLatin1String("0") || pl == QLatin1String("1"))
                aggressive = true;
        }
    }
    if (stuck) {
        out.append(finding(QStringLiteral("cpu-governor"),
            tr("CPU governor stuck in powersave"), 3,
            tr("A cluster is pinned to its lowest frequency — known vendor-init bug on Qualcomm ports."),
            det,
            tr("Fix, technically: rewriting the governor (echo schedutil > scaling_governor) re-creates its instance and clears the stuck state. The write does not survive a reboot, so a persistent repair needs a small boot-time service — that is the whole job of a Qualcomm tuning patch, and with this information it can just as well be written oneself.")));
    } else if (aggressive) {
        out.append(finding(QStringLiteral("cpu-governor"),
            tr("CPU frequency governor"), 0,
            tr("Healthy, tuned for responsiveness: a 0 µs ramp-up limit lets any load spike raise the clock immediately; predictive load holds it high."),
            det,
            tr("Optimizable? Toward energy, not speed: raising up_rate_limit_us (hundreds to thousands of µs) smooths out micro-spikes, a higher hispeed_load or pl=0 reduces overshoot — each step trades touch latency for battery. The knobs live under cpufreq/policyN/schedutil/ and reset at boot. Whether the vendor's trade is balanced depends on use: for a snappy UI it is; for standby-heavy use there is headroom. A Qualcomm tuning patch would not help here — its governor repair targets a broken state this device does not have.")));
    } else {
        out.append(finding(QStringLiteral("cpu-governor"),
            tr("CPU frequency governor"), 0,
            tr("Governors are healthy — no known misconfiguration."), det));
    }
}

// Long-term frequency residency: how the clusters actually spent their time.
// Mostly-at-maximum is not a performance problem but flags energy headroom;
// it also catches the dead-schedutil flavour (mostly-at-minimum despite load).
void Diagnostics::checkFreqResidency(QVariantList &out) const
{
    const QList<Policy> pols = readPolicies();
    QVariantList det;
    double worstTop = 0;
    bool floorStuck = false, haveStats = false;
    for (const Policy &p : pols) {
        const QString stats = readTrim(p.path + QStringLiteral("/stats/time_in_state"));
        if (stats.isEmpty())
            continue;
        qlonglong total = 0, top = 0, atMin = 0;
        qlonglong topFreq = 0;
        const QStringList lines = stats.split(QLatin1Char('\n'), QString::SkipEmptyParts);
        for (const QString &ln : lines) {
            const QStringList f = ln.split(QLatin1Char(' '), QString::SkipEmptyParts);
            if (f.size() != 2) continue;
            const qlonglong freq = f[0].toLongLong(), t = f[1].toLongLong();
            total += t;
            if (freq > topFreq) { topFreq = freq; top = t; }
            if (freq <= p.minKhz) atMin += t;
        }
        if (total <= 0) continue;
        haveStats = true;
        const double topShare = 100.0 * top / total;
        const double minShare = 100.0 * atMin / total;
        if (topShare > worstTop) worstTop = topShare;
        if (minShare > 90.0) floorStuck = true;
        det.append(detail(tr("CPUs %1").arg(p.cpus),
                          tr("%1% at %2 (max) · %3% at floor")
                              .arg(topShare, 0, 'f', 0).arg(mhz(topFreq))
                              .arg(minShare, 0, 'f', 0)));
    }
    if (!haveStats)
        return;
    if (floorStuck) {
        out.append(finding(QStringLiteral("cpu-residency"),
            tr("CPU rarely leaves its minimum frequency"), 2,
            tr("A cluster spends >90% of its time at the floor — matches the dead-governor bug if the device also feels slow."),
            det,
            tr("Rewriting the governor re-creates its instance and usually clears this — see the governor finding for the mechanism.")));
    } else if (worstTop >= 50.0) {
        out.append(finding(QStringLiteral("cpu-residency"),
            tr("CPU frequency skews high"), 1,
            tr("Performance: fine — energy: headroom. A cluster spends %1% of its accounted time at maximum frequency.").arg(worstTop, 0, 'f', 0),
            det,
            tr("Aggressive schedutil tuning (instant ramp-up) keeps clocks high. Costs battery, not speed; no known fix packaged yet.")));
    } else {
        out.append(finding(QStringLiteral("cpu-residency"),
            tr("CPU frequency residency"), 0,
            tr("Time-at-frequency distribution looks balanced."), det));
    }
}

// Qualcomm's per-touch CPU boost. Android configures it; Sailfish ships it
// zeroed. The sysfs location moved between kernel generations: module_param
// (4.14, e.g. Xperia 10 II) vs kobject under the cpu subsystem (4.19, e.g.
// Xperia 10 III) — probe both.
void Diagnostics::checkTouchBoost(QVariantList &out) const
{
    QString base;
    for (const QString &p : { QStringLiteral("/sys/devices/system/cpu/cpu_boost"),
                              QStringLiteral("/sys/module/cpu_boost/parameters") })
        if (QFileInfo::exists(p + QStringLiteral("/input_boost_freq"))) { base = p; break; }
    if (base.isEmpty())
        return; // no CAF cpu_boost driver — not applicable
    const QString freq = readTrim(base + QStringLiteral("/input_boost_freq"));
    const QString ms   = readTrim(base + QStringLiteral("/input_boost_ms"));
    const bool active = QRegularExpression(QStringLiteral(":[1-9]")).match(freq).hasMatch()
                     || QRegularExpression(QStringLiteral("^[1-9]")).match(freq).hasMatch();
    QVariantList det;
    det.append(detail(tr("Interface"), base));
    det.append(detail(QStringLiteral("input_boost_freq"), freq));
    det.append(detail(QStringLiteral("input_boost_ms"), ms));
    if (active) {
        out.append(finding(QStringLiteral("touch-boost"),
            tr("Per-touch CPU boost"), 0,
            tr("Active — touches raise the CPU floor as on Android."), det));
    } else {
        out.append(finding(QStringLiteral("touch-boost"),
            tr("Per-touch CPU boost disabled"), 1,
            tr("The kernel's touch boost exists but is zeroed — Android uses it, Sailfish leaves it off."),
            det,
            tr("Enabled by writing one cpu:kHz pair per CPU to input_boost_freq and a hold time (40–100 ms is the useful range) to input_boost_ms. The values reset at boot, so persistence needs a boot-time service — typical territory of a Qualcomm tuning patch, and just as writable by hand. Caveat: the interface moved between kernel generations (module parameters on 4.14, a cpu-subsystem kobject on 4.19), so any such service must probe both paths.")));
    }
}

// Adreno GPU minimum power level. Idling at the slowest bin means a ramp on
// every repaint. Note for coarse frequency tables: the patch's default
// "85% of max" floor can resolve to the top bin — a permanent full-speed pin.
void Diagnostics::checkGpuFloor(QVariantList &out) const
{
    const QString base = QStringLiteral("/sys/class/kgsl/kgsl-3d0");
    const QString nStr = readTrim(base + QStringLiteral("/num_pwrlevels"));
    if (nStr.isEmpty())
        return; // no kgsl — not applicable
    const int num = nStr.toInt();
    const int minLvl = readTrim(base + QStringLiteral("/min_pwrlevel")).toInt();
    const QString freqs = readTrim(base + QStringLiteral("/gpu_available_frequencies"));
    QVariantList det;
    QStringList table;
    qlonglong maxHz = 0;
    const QStringList fl = freqs.split(QLatin1Char(' '), QString::SkipEmptyParts);
    for (const QString &f : fl) {
        const qlonglong hz = f.toLongLong();
        if (hz > maxHz) maxHz = hz;
        table << QString::number(hz / 1000000) + QStringLiteral(" MHz");
    }
    det.append(detail(tr("Frequency bins"), table.join(QStringLiteral(" / "))));
    det.append(detail(tr("Power level floor"), tr("%1 of %2 (0 = fastest)").arg(minLvl).arg(num)));
    if (num > 1 && minLvl >= num - 1) {
        out.append(finding(QStringLiteral("gpu-floor"),
            tr("GPU idles at its slowest bin"), 1,
            tr("The GPU parks at %1 and must ramp on every repaint — costs responsiveness, not correctness.")
                .arg(fl.isEmpty() ? QString() : table.last()),
            det,
            tr("min_pwrlevel is an index into the frequency table above (0 = fastest) and is writable at runtime, but resets at boot. Raising the floor removes the ramp latency at a battery cost — on a coarse table the next-faster bin is a big step, so pick the level from the actual table, not a percentage. Persistence again means a boot service plus a udev rule for device re-adds; the kind of thing a Qualcomm tuning patch bundles.")));
    } else {
        out.append(finding(QStringLiteral("gpu-floor"),
            tr("GPU power floor"), 0,
            tr("The minimum power level is already raised."), det));
    }
}

// High load average with an idle CPU is a hybris classic: tasks stuck in
// uninterruptible sleep (D state) count into loadavg without using any CPU.
void Diagnostics::checkLoadVsCpu(QVariantList &out, double cpuPct, double load1) const
{
    if (load1 < 1.0 || cpuPct >= 20.0)
        return; // nothing to explain
    QStringList dTasks;
    QDir proc(QStringLiteral("/proc"));
    const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool numeric = false;
        pid.toInt(&numeric);
        if (!numeric) continue;
        const QString stat = readTrim(QStringLiteral("/proc/") + pid + QStringLiteral("/stat"));
        const int close = stat.lastIndexOf(QLatin1Char(')'));
        if (close < 0 || close + 2 >= stat.size()) continue;
        if (stat.at(close + 2) == QLatin1Char('D')) {
            const int open = stat.indexOf(QLatin1Char('('));
            if (dTasks.size() < 6)
                dTasks << stat.mid(open + 1, close - open - 1);
            else
                { dTasks << QStringLiteral("…"); break; }
        }
    }
    QVariantList det;
    det.append(detail(tr("Load average (1 min)"), QString::number(load1, 'f', 2)));
    det.append(detail(tr("CPU usage"), QString::number(cpuPct, 'f', 1) + QStringLiteral("%")));
    if (!dTasks.isEmpty())
        det.append(detail(tr("D-state tasks"), dTasks.join(QStringLiteral(", "))));
    out.append(finding(QStringLiteral("load-dstate"),
        tr("High load, idle CPU"), 1,
        dTasks.isEmpty()
            ? tr("Load average is high while the CPU is idle — usually tasks in uninterruptible sleep, common on hybris ports.")
            : tr("The load average comes from tasks in uninterruptible sleep (D state), not from CPU work."),
        det,
        tr("Not a performance problem: D-state tasks inflate the load number without using the CPU. Typical hybris/vendor-driver artifact.")));
}

// Xperia 10 III camera-provider crash: CamX dlopens libswregistrationalgo.so
// from /odm/lib64 after a video recording; the port ships without it and the
// Android camera service segfaults. The swregistration component node is the
// applicability gate — only stacks that load it are affected.
void Diagnostics::checkCameraProvider(QVariantList &out) const
{
    QString node;
    for (const QString &d : { QStringLiteral("/odm/lib64/camera/components"),
                              QStringLiteral("/vendor/lib64/camera/components") }) {
        const QString p = d + QStringLiteral("/com.qti.node.swregistration.so");
        if (QFileInfo::exists(p)) { node = p; break; }
    }
    if (node.isEmpty())
        return; // camera stack does not use swregistration — not affected
    const QString lib = QStringLiteral("/odm/lib64/libswregistrationalgo.so");
    const bool present = QFileInfo::exists(lib)
                      || QFileInfo::exists(QStringLiteral("/data/odmlib/upper/libswregistrationalgo.so"));
    QVariantList det;
    det.append(detail(tr("CamX component"), node));
    det.append(detail(QStringLiteral("libswregistrationalgo.so"),
                      present ? tr("present") : tr("missing")));
    if (present) {
        out.append(finding(QStringLiteral("camera-provider"),
            tr("Camera provider crash"), 0,
            tr("The known fix is in place — libswregistrationalgo.so is available to the camera HAL."), det,
            tr("The fix documentation lives in the AdvancedCam fork's README — published on GitHub only, not on OpenRepos."),
            tr("AdvancedCam (GitHub only)"), kAdvCamUrl));
    } else {
        out.append(finding(QStringLiteral("camera-provider"),
            tr("Camera provider will crash after video recording"), 3,
            tr("The camera HAL dlopens libswregistrationalgo.so, which this port does not ship — stopping a recording kills the Android camera service."),
            det,
            tr("Known since 2022 (sonyxperiadev bug #761). The library must be extracted from the device's own Android firmware — it is proprietary and cannot be redistributed. The AdvancedCam fork's README documents the extraction and a persistent bind-mount; the fork is published on GitHub only, not on OpenRepos."),
            tr("AdvancedCam (GitHub only)"), kAdvCamUrl));
    }
}

// MediaTek core hotplug (HPS): MTK parks whole cores at idle instead of the
// Qualcomm-style always-online clusters. Parked cores at idle are healthy;
// disabled hotplug with cores stuck offline is the actual failure mode.
void Diagnostics::checkMtkHotplug(QVariantList &out) const
{
    const QString en = readTrim(QStringLiteral("/proc/hps/enabled"));
    if (en.isEmpty())
        return; // no MTK HPS — not applicable
    int total = 0, offline = 0;
    const QDir d(QStringLiteral("/sys/devices/system/cpu"));
    for (const QString &c : d.entryList(QStringList() << QStringLiteral("cpu[0-9]*"), QDir::Dirs)) {
        ++total;
        const QString on = readTrim(d.filePath(c) + QStringLiteral("/online"));
        if (on == QLatin1String("0")) ++offline;
    }
    QVariantList det;
    det.append(detail(QStringLiteral("HPS"),
                      en.startsWith(QLatin1Char('1')) ? tr("enabled") : tr("disabled"),
                      en.startsWith(QLatin1Char('1')) ? QStringLiteral("ok") : QStringLiteral("bad")));
    det.append(detail(tr("Cores"), tr("%1 of %2 online").arg(total - offline).arg(total)));
    if (en.startsWith(QLatin1Char('1'))) {
        out.append(finding(QStringLiteral("cpu-mtk-hotplug"),
            tr("MediaTek core hotplug"), 0,
            offline > 0
                ? tr("Active — %1 cores are parked at idle and come online under load.").arg(offline)
                : tr("Active — all cores currently online."),
            det,
            tr("MTK parks whole cores instead of only lowering frequencies. Offline cores at idle are normal here, not a defect.")));
    } else if (offline > 0) {
        out.append(finding(QStringLiteral("cpu-mtk-hotplug"),
            tr("CPU cores stuck offline"), 3,
            tr("Hotplug is disabled and %1 of %2 cores are offline — they will not come back under load.")
                .arg(offline).arg(total),
            det,
            tr("Re-enabling HPS (or a reboot) brings the cores back; without it the device runs on a fraction of its CPU.")));
    }
}

// Hybris BT ports (bluebinder) sometimes end up with a second, dead hci
// adapter and a soft-blocked rfkill switch next to the live one. Harmless
// for pairing but confusing for apps that pick the wrong adapter.
void Diagnostics::checkBtAdapters(QVariantList &out) const
{
    const QDir d(QStringLiteral("/sys/class/bluetooth"));
    if (!d.exists())
        return;
    QStringList adapters = d.entryList(QStringList() << QStringLiteral("hci*"), QDir::Dirs);
    adapters.sort();
    if (adapters.isEmpty())
        return;
    int btSoftBlocked = 0, btSwitches = 0;
    QVariantList det;
    det.append(detail(tr("Adapters"), adapters.join(QStringLiteral(", "))));
    const QDir rf(QStringLiteral("/sys/class/rfkill"));
    for (const QString &e : rf.entryList(QStringList() << QStringLiteral("rfkill*"), QDir::Dirs)) {
        if (readTrim(rf.filePath(e) + QStringLiteral("/type")) != QLatin1String("bluetooth"))
            continue;
        ++btSwitches;
        const bool soft = readTrim(rf.filePath(e) + QStringLiteral("/soft")) == QLatin1String("1");
        const bool hard = readTrim(rf.filePath(e) + QStringLiteral("/hard")) == QLatin1String("1");
        if (soft) ++btSoftBlocked;
        det.append(detail(e, (soft || hard) ? tr("blocked") : tr("unblocked"),
                          (soft || hard) ? QStringLiteral("bad") : QStringLiteral("ok")));
    }
    const bool doubled = adapters.size() > 1;
    if (doubled || (btSoftBlocked > 0 && btSoftBlocked < btSwitches)) {
        out.append(finding(QStringLiteral("bt-adapters"),
            tr("Duplicate Bluetooth adapter"), 1,
            tr("%1 hci adapters and %2 of %3 BT rfkill switches blocked — known bluebinder artifact on hybris ports.")
                .arg(adapters.size()).arg(btSoftBlocked).arg(btSwitches),
            det,
            tr("The blocked twin is a leftover of the Android BT HAL bridge (bluebinder). Pairing works via the live adapter; apps that enumerate adapters may pick the dead one.")));
    } else if (btSoftBlocked == btSwitches && btSwitches > 0) {
        out.append(finding(QStringLiteral("bt-adapters"),
            tr("Bluetooth blocked"), 1,
            tr("All Bluetooth rfkill switches are blocked."), det));
    } else {
        out.append(finding(QStringLiteral("bt-adapters"),
            tr("Bluetooth adapter"), 0,
            tr("One live adapter, rfkill unblocked."), det));
    }
}

// WLAN radio health: regulatory domain (world domain \"00\" cripples 5 GHz),
// firmware crash counters where the driver exposes them, power-save state.
void Diagnostics::checkWlanRadio(QVariantList &out) const
{
    if (!QFileInfo::exists(QStringLiteral("/sys/class/net/wlan0")))
        return;
    QVariantList det;
    int level = 0;
    QString verdict;

    const QFileInfo drv(QStringLiteral("/sys/class/net/wlan0/device/driver"));
    if (drv.exists())
        det.append(detail(tr("Driver"), QFileInfo(drv.symLinkTarget()).fileName()));

    // regulatory domain via iw (a hard package dependency of this app)
    QProcess iw;
    iw.start(QStringLiteral("iw"), QStringList() << QStringLiteral("reg") << QStringLiteral("get"));
    if (iw.waitForFinished(2000)) {
        const QRegularExpressionMatch m = QRegularExpression(
            QStringLiteral("country (\\w+):")).match(QString::fromUtf8(iw.readAllStandardOutput()));
        if (m.hasMatch()) {
            const QString cc = m.captured(1);
            const bool world = cc == QLatin1String("00") || cc == QLatin1String("98")
                            || cc == QLatin1String("99");
            det.append(detail(tr("Regulatory domain"), cc,
                              world ? QStringLiteral("bad") : QStringLiteral("ok")));
            if (world) {
                level = qMax(level, 1);
                verdict = tr("World regulatory domain — 5 GHz channels are restricted until a country is set.");
            }
        }
    }

    // firmware crash counters (icnss/cnss debugfs; usually root-only — try
    // the root helper as fallback, otherwise say what root mode would add)
    bool fwRowAdded = false;
    for (const QString &p : { QStringLiteral("/sys/kernel/debug/icnss/stats"),
                              QStringLiteral("/sys/kernel/debug/cnss/stats") }) {
        QString stats = readTrim(p);
        if (stats.isEmpty() && RootClient::instance()->active())
            stats = QString::fromUtf8(RootClient::instance()->readFile(p)).trimmed();
        if (stats.isEmpty())
            continue;
        qlonglong crashes = 0;
        for (const QString &ln : stats.split(QLatin1Char('\n'))) {
            if (!ln.contains(QLatin1String("crash"), Qt::CaseInsensitive))
                continue;
            const int colon = ln.lastIndexOf(QLatin1Char(':'));
            if (colon >= 0)
                crashes += ln.mid(colon + 1).trimmed().toLongLong();
        }
        det.append(detail(tr("Firmware crashes"), QString::number(crashes),
                          crashes > 0 ? QStringLiteral("bad") : QStringLiteral("ok")));
        fwRowAdded = true;
        if (crashes > 0) {
            level = qMax(level, 2);
            verdict = tr("The WLAN firmware has crashed %1 time(s) since boot.").arg(crashes);
        }
        break;
    }
    if (!fwRowAdded && QFileInfo::exists(QStringLiteral("/sys/kernel/debug/icnss")))
        det.append(detail(tr("Firmware crashes"), tr("root mode shows the counter here")));

    if (det.isEmpty())
        return;
    if (verdict.isEmpty())
        verdict = tr("Regulatory domain set, no firmware crashes recorded.");
    out.append(finding(QStringLiteral("wlan-radio"),
        tr("WLAN radio"), level, verdict, det,
        level > 0 && verdict.contains(QLatin1String("5 GHz"))
            ? tr("The domain comes from ConnMan/wpa_supplicant; connecting to a local AP usually sets it. A permanently unset domain keeps DFS and upper 5 GHz channels unusable.")
            : QString()));
}

// Camcorder input gain of the Android audio HAL. The gain value sits inside
// the vendor blob and is not readable from software, so this stays precise:
// a confirmed statement only on device models where it was actually verified
// (Xperia 10 III), an honest "cannot be read" everywhere else.
void Diagnostics::checkMicGain(QVariantList &out) const
{
    QString hal;
    for (const QString &d : { QStringLiteral("/vendor/lib64/hw"), QStringLiteral("/odm/lib64/hw"),
                              QStringLiteral("/vendor/lib/hw"),  QStringLiteral("/odm/lib/hw") }) {
        const QStringList libs = QDir(d).entryList(
            QStringList() << QStringLiteral("audio.primary.*.so"), QDir::Files);
        if (!libs.isEmpty()) { hal = d + QLatin1Char('/') + libs.first(); break; }
    }
    if (hal.isEmpty())
        return; // no Android audio HAL — not applicable

    // /etc/hw-release names the adaptation's device model
    QString model;
    QFile hw(QStringLiteral("/etc/hw-release"));
    if (hw.open(QIODevice::ReadOnly))
        for (const QByteArray &l : hw.readAll().split('\n'))
            if (l.startsWith("MER_HA_DEVICE=")) { model = QString::fromUtf8(l.mid(14).trimmed()); break; }
    const bool confirmed = model == QLatin1String("xqbt52"); // Xperia 10 III

    QVariantList det;
    det.append(detail(tr("Audio HAL"), hal));
    if (!model.isEmpty())
        det.append(detail(tr("Device"), model));
    if (confirmed) {
        out.append(finding(QStringLiteral("mic-gain"),
            tr("Video recordings far too quiet"), 1,
            tr("Confirmed on this device model (Xperia 10 III): the audio HAL's camcorder input path applies too little capture gain."),
            det,
            tr("Verified on the Xperia 10 III up to Sailfish OS 5.1.0.11. The gain value lives inside the vendor blob and is not adjustable there; raising the PulseAudio record-stream volume during recording compensates — the AdvancedCam fork does this per recording (GitHub only), harbour-micgain system-wide (OpenRepos and GitHub)."),
            tr("References"), kAdvCamUrl + QStringLiteral("\n") + kMicGainUrl));
    } else {
        out.append(finding(QStringLiteral("mic-gain"),
            tr("Camcorder input gain unknown"), 1,
            tr("This port records through an Android audio HAL; whether its camcorder input gain is adequate cannot be read from software."),
            det,
            tr("Confirmed too low only on the Xperia 10 III so far. For this device the only test is listening to a video recording; if it is too quiet, raising the PulseAudio record-stream volume compensates — harbour-micgain does this system-wide (available on OpenRepos and GitHub)."),
            tr("harbour-micgain (OpenRepos & GitHub)"), kMicGainUrl));
    }
}
