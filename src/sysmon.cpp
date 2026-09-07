#include "sysmon.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFile>
#include <algorithm>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QProcess>
#include <QRegExp>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusObjectPath>

#include "chargerlog.h"
#include "rootclient.h"
#include "source.h"

#include <signal.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/klog.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const int HIST_MAX = 180;

static QString readTrim(const QString &p);

SysMon::SysMon(QObject *parent)
    : QObject(parent)
{
}

void SysMon::onSystem(const SysSnap &snap)
{
    m_s = snap;
    push(m_cpuHist, snap.cpuPct);
    push(m_memHist, snap.memTotal ? 100.0 * (snap.memTotal - snap.memAvailable) / snap.memTotal : 0);
    push(m_rxHist, snap.netRxRate);
    push(m_txHist, snap.netTxRate);
    // Discharge positive: the drain graph reads upward. The direction used to
    // come from the status word, which misplots a phone that says "Charging"
    // while the cell empties. The power is signed now, so the measurement says.
    const double drain = -snap.battPowerW;
    push(m_battHist, drain);
    emit updated();
}

void SysMon::push(QVector<double> &v, double value)
{
    v.append(value);
    if (v.size() > HIST_MAX)
        v.remove(0, v.size() - HIST_MAX);
}

QVariantList SysMon::toList(const QVector<double> &v)
{
    QVariantList l;
    l.reserve(v.size());
    for (double d : v)
        l.append(d);
    return l;
}

QVariantList SysMon::corePercents() const
{
    QVariantList l;
    for (float f : m_s.corePct)
        l.append((double)f);
    return l;
}

QVariantList SysMon::coreFreqsMhz() const
{
    QVariantList l;
    for (int k : m_s.coreFreqKhz)
        l.append(k < 0 ? SysSnap::CoreOffline : k / 1000);   // keep the offline flag intact
    return l;
}

QVariantList SysMon::thermalZones() const
{
    QVariantList l;
    for (const auto &z : m_s.thermal) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), z.name);
        m.insert(QStringLiteral("temp"), (double)z.degC);
        m.insert(QStringLiteral("corrected"), z.corrected);
        m.insert(QStringLiteral("suspect"), z.suspect);
        l.append(m);
    }
    return l;
}

QVariantList SysMon::interfaces() const
{
    QVariantList l;
    for (const auto &i : m_s.ifaces) {
        const QStringList v = i.second.split(QLatin1Char('|'));
        QVariantMap m;
        m.insert(QStringLiteral("name"), i.first);
        m.insert(QStringLiteral("rx"), v.value(0).toDouble());
        m.insert(QStringLiteral("tx"), v.value(1).toDouble());
        l.append(m);
    }
    return l;
}

void SysMon::setPaused(bool p)
{
    if (m_paused == p)
        return;
    m_paused = p;
    emit pausedChanged();
    emit pauseRequested(p);
}

void SysMon::setForeground(bool f)
{
    if (m_foreground == f)
        return;
    m_foreground = f;
    emit foregroundChanged();
}

void SysMon::setThermalWanted(bool w)
{
    if (m_thermalWanted == w)
        return;
    m_thermalWanted = w;
    emit thermalWantedChanged();
}

void SysMon::setIntervalMs(int ms)
{
    if (m_intervalMs == ms)
        return;
    m_intervalMs = ms;
    emit intervalChanged();
    emit intervalRequested(ms);
}

QVariantList SysMon::storageMounts() const
{
    QVariantList out;
    // pseudo filesystems carry no real capacity — skip them
    static const QSet<QByteArray> pseudo = {
        "proc","sysfs","cgroup","cgroup2","devtmpfs","devpts","mqueue","debugfs",
        "tracefs","securityfs","pstore","bpf","autofs","rpc_pipefs","binfmt_misc",
        "configfs","fusectl","hugetlbfs","nsfs","ramfs","sysv","fuse.lxc","efivarfs"
    };
    QFile f(QStringLiteral("/proc/self/mounts"));
    if (!f.open(QIODevice::ReadOnly))
        return out;

    // Last entry wins: /proc/self/mounts is in mount order, and what statvfs
    // measures is whatever ended up on top.
    QMap<QString, QList<QByteArray>> byMount;
    QStringList mountOrder;
    for (const QByteArray &line : f.readAll().split('\n')) {
        const QList<QByteArray> c0 = line.split(' ');
        if (c0.size() < 3)
            continue;
        QByteArray mpKey = c0[1];
        const QString key = QString::fromLocal8Bit(mpKey);
        if (!byMount.contains(key))
            mountOrder.append(key);
        byMount.insert(key, c0);
    }

    QSet<QString> seenDev;
    for (const QString &mountKey : mountOrder) {
        const QList<QByteArray> c = byMount.value(mountKey);
        const QByteArray fstype = c[2];
        if (pseudo.contains(fstype))
            continue;
        // octal-escaped mount point (\040 = space, etc.)
        QByteArray mpRaw = c[1];
        QByteArray mp;
        for (int i = 0; i < mpRaw.size(); ++i) {
            if (mpRaw[i] == '\\' && i + 3 < mpRaw.size()) {
                mp += (char)mpRaw.mid(i + 1, 3).toInt(nullptr, 8);
                i += 3;
            } else {
                mp += mpRaw[i];
            }
        }
        const QString mount = QString::fromLocal8Bit(mp);
        // One image mounted at several places is one filesystem. Listing it
        // once per mount point turned a handful of read-only system images
        // into dozens of identical bars.
        const QString dev = QString::fromLocal8Bit(c[0]);
        if (dev.startsWith(QLatin1String("/dev/")) && seenDev.contains(dev))
            continue;

        struct statvfs vfs;
        if (statvfs(mp.constData(), &vfs) != 0 || vfs.f_blocks == 0)
            continue;
        if (dev.startsWith(QLatin1String("/dev/")))
            seenDev.insert(dev);

        const double frsize = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
        const double total = (double)vfs.f_blocks * frsize;
        const double avail = (double)vfs.f_bavail * frsize;
        const double free = (double)vfs.f_bfree * frsize;
        const double used = total - free;

        QVariantMap m;
        m.insert(QStringLiteral("device"), dev);
        m.insert(QStringLiteral("mount"), mount);
        m.insert(QStringLiteral("fstype"), QString::fromLatin1(fstype));
        m.insert(QStringLiteral("readonly"), c.size() > 3 && c[3].startsWith("ro"));
        m.insert(QStringLiteral("total"), total);
        m.insert(QStringLiteral("used"), used);
        m.insert(QStringLiteral("avail"), avail);
        m.insert(QStringLiteral("pct"), total > 0 ? 100.0 * used / total : 0.0);
        out.append(m);
    }
    return out;
}

QVariantList SysMon::storageHardware() const
{
    QVariantList out;
    auto rd = [](const QString &p) {
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly))
            return QString();
        return QString::fromLatin1(f.readAll().trimmed());
    };
    // JEDEC MMCA manufacturer IDs (manfid) -> vendor
    auto mmcVendor = [](const QString &manfidHex) -> QString {
        bool ok = false;
        const int id = manfidHex.toInt(&ok, 16);
        switch (id) {
        case 0x11: return QStringLiteral("Toshiba/Kioxia");
        case 0x13: return QStringLiteral("Micron");
        case 0x15: return QStringLiteral("Samsung");
        case 0x45: return QStringLiteral("SanDisk");
        case 0x70: return QStringLiteral("Kingston");
        case 0x90: return QStringLiteral("SK hynix");
        case 0xFE: return QStringLiteral("Micron");
        case 0x2C: return QStringLiteral("Micron");
        default: return id ? QStringLiteral("manfid 0x%1").arg(id, 0, 16) : QString();
        }
    };

    const QDir blk(QStringLiteral("/sys/block"));
    for (const QString &name : blk.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const bool mmc = name.startsWith(QLatin1String("mmcblk")) && !name.contains(QLatin1Char('p'))
                         && !name.contains(QLatin1String("boot")) && !name.contains(QLatin1String("rpmb"));
        const bool sd = name.startsWith(QLatin1String("sd")) && name.size() == 3;
        if (!mmc && !sd)
            continue;
        const QString dev = QStringLiteral("/sys/block/") + name + QStringLiteral("/device/");
        QVariantMap m;
        m.insert(QStringLiteral("dev"), name);
        m.insert(QStringLiteral("size"),
                 rd(QStringLiteral("/sys/block/") + name + QStringLiteral("/size")).toDouble() * 512);

        if (mmc) {
            const QString type = rd(dev + QStringLiteral("type"));
            m.insert(QStringLiteral("bus"),
                     type.isEmpty() || type == QLatin1String("MMC") ? QStringLiteral("eMMC") : type.toUpper());
            m.insert(QStringLiteral("vendor"), mmcVendor(rd(dev + QStringLiteral("manfid"))));
            m.insert(QStringLiteral("model"), rd(dev + QStringLiteral("name")));
            m.insert(QStringLiteral("serial"), rd(dev + QStringLiteral("serial")));
            m.insert(QStringLiteral("rev"), rd(dev + QStringLiteral("fwrev")));
            m.insert(QStringLiteral("date"), rd(dev + QStringLiteral("date")));
            // host controller (the "card reader"): walk up to the sdhci/mmc platform node
            QString host = QFileInfo(QStringLiteral("/sys/block/") + name + QStringLiteral("/device")).canonicalFilePath();
            for (int i = 0; i < 8 && !host.isEmpty(); ++i) {
                const QString drv = QFileInfo(QFile::symLinkTarget(host + QStringLiteral("/driver"))).fileName();
                if (drv.contains(QLatin1String("sdhci")) || drv.contains(QLatin1String("mmc"))) {
                    m.insert(QStringLiteral("hostNode"), QFileInfo(host).fileName());
                    m.insert(QStringLiteral("hostDriver"), drv);
                    break;
                }
                host = QFileInfo(host + QStringLiteral("/..")).canonicalFilePath();
            }
        } else {
            // UFS / SCSI: identity from the SCSI inquiry
            m.insert(QStringLiteral("bus"), QStringLiteral("UFS/SCSI"));
            m.insert(QStringLiteral("vendor"), rd(dev + QStringLiteral("vendor")));
            m.insert(QStringLiteral("model"), rd(dev + QStringLiteral("model")));
            m.insert(QStringLiteral("rev"), rd(dev + QStringLiteral("rev")));
            m.insert(QStringLiteral("serial"),
                     rd(QStringLiteral("/sys/block/") + name + QStringLiteral("/device/../../unique_id")));
            // UFS controller descriptors: walk up to the *.ufshc node
            QString up = QFileInfo(dev).canonicalFilePath();
            for (int i = 0; i < 8 && !up.isEmpty(); ++i) {
                const QString dd = up + QStringLiteral("/device_descriptor/");
                if (QFileInfo::exists(dd + QStringLiteral("specification_version"))) {
                    const int spec = rd(dd + QStringLiteral("specification_version")).toInt(nullptr, 16);
                    if (spec > 0)
                        m.insert(QStringLiteral("ufsSpec"),
                                 QString::number(spec >> 8) + QLatin1Char('.') + QString::number((spec >> 4) & 0xF));
                    const int wbt = rd(dd + QStringLiteral("wb_type")).toInt(nullptr, 16);
                    const int extFeat = rd(dd + QStringLiteral("ext_feature_sup")).toInt(nullptr, 16);
                    m.insert(QStringLiteral("writeBooster"), wbt > 0 || (extFeat & 0x100));
                    m.insert(QStringLiteral("queueDepth"), rd(dd + QStringLiteral("queue_depth")).toInt(nullptr, 16));
                    m.insert(QStringLiteral("numLuns"), rd(dd + QStringLiteral("number_of_luns")).toInt(nullptr, 16));
                    m.insert(QStringLiteral("numWluns"), rd(dd + QStringLiteral("number_of_wluns")).toInt(nullptr, 16));
                    m.insert(QStringLiteral("mfrId"), rd(dd + QStringLiteral("manufacturer_id")));
                    const QString sd = up + QStringLiteral("/string_descriptors/");
                    const QString pn = rd(sd + QStringLiteral("product_name"));
                    if (!pn.isEmpty()) m.insert(QStringLiteral("model"), pn);
                    const QString mn = rd(sd + QStringLiteral("manufacturer_name"));
                    if (!mn.isEmpty()) m.insert(QStringLiteral("vendor"), mn);
                    const QString sn = rd(sd + QStringLiteral("serial_number"));
                    if (!sn.isEmpty()) m.insert(QStringLiteral("serial"), sn);
                    break;
                }
                up = QFileInfo(up + QStringLiteral("/..")).canonicalFilePath();
            }
        }

        // --- health / wear ------------------------------------------------
        // eMMC 5.1: life_time = "0xNN 0xNN" (each step = 10% used), pre_eol_info
        // 1 normal / 2 warning / 3 urgent. UFS: health_descriptor/{eol_info,
        // life_time_estimation_a,_b} found by walking up from the scsi device.
        int lifeA = -1, lifeB = -1, eol = -1;
        if (mmc) {
            const QStringList lt = rd(dev + QStringLiteral("life_time")).split(QLatin1Char(' '), QString::SkipEmptyParts);
            if (lt.size() >= 2) {
                lifeA = lt[0].toInt(nullptr, 16);
                lifeB = lt[1].toInt(nullptr, 16);
            }
            const QString pe = rd(dev + QStringLiteral("pre_eol_info"));
            if (!pe.isEmpty())
                eol = pe.toInt(nullptr, pe.startsWith(QLatin1String("0x")) ? 16 : 10);
        } else {
            QString cur = QFileInfo(dev).canonicalFilePath();
            for (int up = 0; up < 6 && !cur.isEmpty(); ++up) {
                const QString hd = cur + QStringLiteral("/health_descriptor/");
                if (QFileInfo::exists(hd + QStringLiteral("eol_info"))) {
                    lifeA = rd(hd + QStringLiteral("life_time_estimation_a")).toInt(nullptr, 16);
                    lifeB = rd(hd + QStringLiteral("life_time_estimation_b")).toInt(nullptr, 16);
                    eol = rd(hd + QStringLiteral("eol_info")).toInt(nullptr, 16);
                    break;
                }
                cur = QFileInfo(cur + QStringLiteral("/..")).canonicalFilePath();
            }
        }
        if (lifeA > 0 || lifeB > 0 || eol > 0) {
            const int worst = qMax(lifeA, lifeB);
            // Steps 0x01..0x0A are 10% bands; 0x0B is not a band but an
            // overflow -- "past the estimated lifetime", with no upper figure.
            // Rendering it as 100% would claim a measurement the chip never
            // made, so it is flagged instead of converted.
            const bool exceeded = worst >= 11;
            m.insert(QStringLiteral("lifeExceeded"), exceeded);
            if (!exceeded)
                m.insert(QStringLiteral("lifeUsedPct"), worst > 0 ? (worst - 1) * 10 : 0); // lower bound of the band
            m.insert(QStringLiteral("preEol"), eol);
            QString verdict;
            if (eol >= 3 || exceeded) verdict = QStringLiteral("urgent");
            else if (eol == 2 || worst >= 8) verdict = QStringLiteral("warning");
            else if (worst >= 1) verdict = QStringLiteral("good");
            m.insert(QStringLiteral("healthVerdict"), verdict);
        }

        out.append(m);
    }
    return out;
}

static int chanFromFreq(int mhz)
{
    if (mhz == 2484) return 14;
    if (mhz >= 2412 && mhz <= 2472) return (mhz - 2407) / 5;
    if (mhz >= 5000 && mhz <= 5900) return (mhz - 5000) / 5;
    if (mhz >= 5955) return (mhz - 5950) / 5;
    return 0;
}
static QString bandOfFreq(int mhz)
{
    if (mhz < 2500) return QStringLiteral("2.4 GHz");
    if (mhz < 5925) return QStringLiteral("5 GHz");
    return QStringLiteral("6 GHz");
}

QVariantMap SysMon::wifiDetail() const
{
    QVariantMap m;
    QString iface, phy;
    const QDir netDir(QStringLiteral("/sys/class/net"));
    for (const QString &n : netDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString b = QStringLiteral("/sys/class/net/") + n + QLatin1Char('/');
        if (QFile::exists(b + QStringLiteral("phy80211")) && !n.startsWith(QLatin1String("p2p"))) {
            iface = n;
            phy = readTrim(b + QStringLiteral("phy80211/name"));
            break;
        }
    }
    if (iface.isEmpty())
        return m;
    m.insert(QStringLiteral("iface"), iface);
    m.insert(QStringLiteral("phy"), phy);
    const QString drv = QFileInfo(QStringLiteral("/sys/class/net/") + iface + QStringLiteral("/device/driver"))
                            .symLinkTarget().section(QLatin1Char('/'), -1);
    m.insert(QStringLiteral("driver"), drv);
    QString vendor;
    // A platform driver may be called plainly "wlan" (MediaTek does), so the
    // node's own compatible string decides where it exists.
    const QString compat =
        readTrim(QStringLiteral("/sys/class/net/") + iface + QStringLiteral("/device/of_node/compatible"))
        .toLower();
    if (compat.contains(QStringLiteral("mediatek")) || compat.contains(QStringLiteral("mtk")))
        vendor = QStringLiteral("MediaTek");
    else if (compat.contains(QStringLiteral("qcom")) || compat.contains(QStringLiteral("qca")))
        vendor = QStringLiteral("Qualcomm Atheros");
    else if (compat.contains(QStringLiteral("brcm")) || compat.contains(QStringLiteral("broadcom")))
        vendor = QStringLiteral("Broadcom");
    else if (drv.contains(QStringLiteral("cnss")) || drv.contains(QStringLiteral("icnss"))
        || drv.startsWith(QStringLiteral("wlan")) || drv.contains(QStringLiteral("qca")))
        vendor = QStringLiteral("Qualcomm Atheros");
    else if (drv.contains(QStringLiteral("bcm")) || drv.contains(QStringLiteral("dhd")))
        vendor = QStringLiteral("Broadcom");
    else if (drv.startsWith(QStringLiteral("mt")))
        vendor = QStringLiteral("MediaTek");
    else if (drv.contains(QStringLiteral("iwl")))
        vendor = QStringLiteral("Intel");
    m.insert(QStringLiteral("vendor"), vendor);

    auto runIw = [](const QStringList &args) -> QString {
        QProcess p;
        p.start(QStringLiteral("/usr/sbin/iw"), args);
        if (!p.waitForFinished(2500))
            return QString();
        return QString::fromLocal8Bit(p.readAllStandardOutput());
    };

    const QString link = runIw(QStringList() << QStringLiteral("dev") << iface << QStringLiteral("link"));
    const bool connected = !link.isEmpty() && !link.contains(QStringLiteral("Not connected"));
    m.insert(QStringLiteral("connected"), connected);
    if (connected) {
        for (const QString &raw : link.split(QLatin1Char('\n'))) {
            const QString l = raw.trimmed();
            if (l.startsWith(QStringLiteral("Connected to ")))
                m.insert(QStringLiteral("bssid"), l.mid(13).section(QLatin1Char(' '), 0, 0));
            else if (l.startsWith(QStringLiteral("SSID:")))
                m.insert(QStringLiteral("ssid"), l.mid(5).trimmed());
            else if (l.startsWith(QStringLiteral("freq:"))) {
                // "freq: 5520.0" since iw 6.x -- an integer parse answers 0,
                // and 0 reads as a valid 2.4 GHz channel further down.
                const int f = qRound(l.mid(5).trimmed().toDouble());
                m.insert(QStringLiteral("freqMhz"), f);
                m.insert(QStringLiteral("channel"), chanFromFreq(f));
                m.insert(QStringLiteral("band"), bandOfFreq(f));
            } else if (l.startsWith(QStringLiteral("signal:")))
                m.insert(QStringLiteral("signalDbm"), l.mid(7).trimmed().section(QLatin1Char(' '), 0, 0).toInt());
            else if (l.startsWith(QStringLiteral("tx bitrate:")))
                m.insert(QStringLiteral("txBitrate"), l.mid(11).trimmed());
            else if (l.startsWith(QStringLiteral("rx bitrate:")))
                m.insert(QStringLiteral("rxBitrate"), l.mid(11).trimmed());
        }
    }
    const QString info = runIw(QStringList() << QStringLiteral("dev") << iface << QStringLiteral("info"));
    for (const QString &raw : info.split(QLatin1Char('\n'))) {
        const QString l = raw.trimmed();
        if (l.startsWith(QStringLiteral("txpower")))
            m.insert(QStringLiteral("txpower"), l.mid(7).trimmed());
        else if (l.startsWith(QStringLiteral("addr ")))
            m.insert(QStringLiteral("mac"), l.mid(5).trimmed());
        else if (l.startsWith(QStringLiteral("type ")))
            m.insert(QStringLiteral("mode"), l.mid(5).trimmed());
    }
    // fall back to the sysfs address if iw did not report one
    if (!m.contains(QStringLiteral("mac")))
        m.insert(QStringLiteral("mac"),
                 readTrim(QStringLiteral("/sys/class/net/") + iface + QStringLiteral("/address")));

    const QString pinfo = runIw(QStringList() << QStringLiteral("phy") << phy << QStringLiteral("info"));
    QVariantList bands;
    QVariantMap curBand;
    QVariantList chOn, chOff;
    bool ht = false, vht = false, he = false;
    QString bandName;
    auto flushBand = [&]() {
        if (!bandName.isEmpty()) {
            curBand.insert(QStringLiteral("name"), bandName);
            curBand.insert(QStringLiteral("channelsEnabled"), chOn);
            curBand.insert(QStringLiteral("channelsDisabled"), chOff);
            curBand.insert(QStringLiteral("ht"), ht);
            curBand.insert(QStringLiteral("vht"), vht);
            curBand.insert(QStringLiteral("he"), he);
            bands.append(curBand);
        }
        curBand = QVariantMap();
        chOn.clear(); chOff.clear(); ht = vht = he = false; bandName.clear();
    };
    for (const QString &raw : pinfo.split(QLatin1Char('\n'))) {
        const QString l = raw.trimmed();
        if (l.startsWith(QStringLiteral("Band "))) {
            flushBand();
            bandName = QStringLiteral("?");
        } else if (l.contains(QStringLiteral("HT20")) || l.contains(QStringLiteral("HT40"))) {
            ht = true;
        } else if (l.startsWith(QStringLiteral("VHT Capabilities")) && !l.contains(QStringLiteral("(0x00000000)"))) {
            vht = true;
        } else if (l.startsWith(QStringLiteral("HE PHY")) || l.contains(QStringLiteral("HE Iftypes"))) {
            he = true;
        } else if (l.contains(QStringLiteral(" MHz ["))) {
            const int mhz = qRound(l.section(QStringLiteral(" MHz"), 0, 0)
                                   .section(QLatin1Char('*'), -1).trimmed().toDouble());
            if (bandName == QStringLiteral("?") && mhz > 0)
                bandName = bandOfFreq(mhz);
            const int ch = chanFromFreq(mhz);
            if (l.contains(QStringLiteral("disabled")) || l.contains(QStringLiteral("no IR")))
                chOff.append(ch);
            else
                chOn.append(ch);
        }
    }
    flushBand();
    m.insert(QStringLiteral("bands"), bands);
    return m;
}

// Addresses per interface. sysfs has none of this -- it knows the hardware, not
// what the interface currently answers to -- so the list comes from getifaddrs.
static QHash<QString, QStringList> ifaceAddresses()
{
    QHash<QString, QStringList> out;
    struct ifaddrs *first = nullptr;
    if (getifaddrs(&first) != 0)
        return out;
    for (struct ifaddrs *a = first; a; a = a->ifa_next) {
        if (!a->ifa_addr || !a->ifa_name)
            continue;
        char buf[INET6_ADDRSTRLEN] = {0};
        if (a->ifa_addr->sa_family == AF_INET) {
            const auto *in = reinterpret_cast<struct sockaddr_in *>(a->ifa_addr);
            if (!inet_ntop(AF_INET, &in->sin_addr, buf, sizeof buf))
                continue;
        } else if (a->ifa_addr->sa_family == AF_INET6) {
            const auto *in6 = reinterpret_cast<struct sockaddr_in6 *>(a->ifa_addr);
            if (!inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof buf))
                continue;
        } else {
            continue;
        }
        out[QString::fromLatin1(a->ifa_name)].append(QString::fromLatin1(buf));
    }
    freeifaddrs(first);
    return out;
}

QVariantList SysMon::networkHardware() const
{
    QVariantList out;
    const QHash<QString, QStringList> addrs = ifaceAddresses();
    auto rd = [](const QString &p) {
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly))
            return QString();
        return QString::fromLatin1(f.readAll().trimmed());
    };
    const QDir netDir(QStringLiteral("/sys/class/net"));
    for (const QString &iface : netDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString base = QStringLiteral("/sys/class/net/") + iface + QLatin1Char('/');
        QVariantMap m;
        m.insert(QStringLiteral("iface"), iface);
        m.insert(QStringLiteral("mac"), rd(base + QStringLiteral("address")));
        // How long this counter has been running. The totals start when the
        // interface is created, which for built-in hardware is seconds after
        // boot but for a mobile-data interface is the moment it came up -- the
        // sysfs directory carries that creation time.
        const QDateTime born = QFileInfo(QStringLiteral("/sys/class/net/") + iface).lastModified();
        if (born.isValid()) {
            const qint64 age = born.secsTo(QDateTime::currentDateTime());
            if (age >= 0)
                m.insert(QStringLiteral("countingSec"), (double)age);
        }

        // Split by family: the v4 address is what a user recognises, the v6 ones
        // are usually several and mostly link-local.
        QStringList v4, v6;
        for (const QString &a : addrs.value(iface))
            (a.contains(QLatin1Char(':')) ? v6 : v4).append(a);
        if (!v4.isEmpty())
            m.insert(QStringLiteral("ipv4"), v4.join(QStringLiteral(", ")));
        if (!v6.isEmpty())
            m.insert(QStringLiteral("ipv6"), v6.join(QStringLiteral(", ")));
        m.insert(QStringLiteral("state"), rd(base + QStringLiteral("operstate")));
        m.insert(QStringLiteral("mtu"), rd(base + QStringLiteral("mtu")).toInt());
        m.insert(QStringLiteral("carrier"), rd(base + QStringLiteral("carrier")) == QLatin1String("1"));

        const QString speed = rd(base + QStringLiteral("speed"));
        if (!speed.isEmpty() && speed.toInt() > 0)
            m.insert(QStringLiteral("speedMbit"), speed.toInt());

        // type: loopback / wifi / cellular / ethernet
        QString kind;
        if (iface == QLatin1String("lo"))
            kind = QStringLiteral("loopback");
        else if (QFile::exists(base + QStringLiteral("wireless")) || QFile::exists(base + QStringLiteral("phy80211")))
            kind = QStringLiteral("wifi");
        else if (iface.startsWith(QLatin1String("rmnet")) || iface.startsWith(QLatin1String("wwan"))
                 || iface.startsWith(QLatin1String("ccmni")) || iface.startsWith(QLatin1String("pdp")))
            kind = QStringLiteral("cellular");
        else if (rd(base + QStringLiteral("type")) == QLatin1String("1"))
            kind = QStringLiteral("ethernet");
        else
            kind = QStringLiteral("virtual");
        m.insert(QStringLiteral("kind"), kind);

        // driver + chip identity from the backing device, if any
        const QString devLink = QFileInfo(base + QStringLiteral("device")).symLinkTarget();
        if (!devLink.isEmpty()) {
            const QString dev = base + QStringLiteral("device/");
            QString driver = QFileInfo(dev + QStringLiteral("driver")).symLinkTarget().section(QLatin1Char('/'), -1);
            if (driver.isEmpty()) {
                for (const QString &l : rd(dev + QStringLiteral("uevent")).split(QLatin1Char('\n')))
                    if (l.startsWith(QLatin1String("DRIVER=")))
                        driver = l.mid(7);
            }
            m.insert(QStringLiteral("driver"), driver);
            QString vendor = rd(dev + QStringLiteral("vendor"));
            QString model = rd(dev + QStringLiteral("device"));
            if (model.isEmpty()) {
                const QString compat = rd(dev + QStringLiteral("of_node/compatible"));
                model = compat.isEmpty() ? rd(dev + QStringLiteral("modalias"))
                                         : compat.split(QLatin1Char('\0')).value(0);
            }
            m.insert(QStringLiteral("vendor"), vendor);
            m.insert(QStringLiteral("model"), model);
        }

        m.insert(QStringLiteral("rxBytes"), rd(base + QStringLiteral("statistics/rx_bytes")).toDouble());
        m.insert(QStringLiteral("txBytes"), rd(base + QStringLiteral("statistics/tx_bytes")).toDouble());
        m.insert(QStringLiteral("rxErrors"), rd(base + QStringLiteral("statistics/rx_errors")).toInt());
        m.insert(QStringLiteral("txErrors"), rd(base + QStringLiteral("statistics/tx_errors")).toInt());
        out.append(m);
    }
    return out;
}

static QString readTrim(const QString &p)
{
    // Deliberately not QFile: a detail page reads a few hundred of these, and
    // QFile sets up an I/O engine and a buffer for every one. open/read/close
    // does the same job for a sysfs attribute at a fraction of the cost --
    // the sampler reached the same conclusion for its own hot path.
    const QByteArray path = p.toLocal8Bit();
    const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return QString();
    char buf[4096];
    int total = 0;
    ssize_t r;
    while (total < (int)sizeof(buf) - 1
           && (r = ::read(fd, buf + total, sizeof(buf) - 1 - total)) > 0)
        total += (int)r;
    ::close(fd);
    return QString::fromLatin1(QByteArray(buf, total).trimmed());
}

static QString hwDevice();

// What the maker states for the cell of this phone, in mAh, where this app
// knows the model. A catalogue figure, not a reading -- and the only source
// left where the kernel's own design capacity is demonstrably not this cell.
static double ratedCapacityMah()
{
    const QString model = hwDevice();
    if (model == QLatin1String("jp2601"))
        return 5450.0;
    return 0;
}

// A gauge's own full-charge figure, in mAh, or 0 where none is readable.
// The unit is a vendor decision, so only a reading that can be a phone battery
// is accepted: µAh here, 0.1 mAh there, plain mAh on the older debug node.
static double gaugeFullChargeMah(const QDir &psy, const QString &batteryDir, QString *from = nullptr)
{
    auto asMah = [](double v) -> double {
        if (v / 1000.0 >= 500 && v / 1000.0 <= 20000) return v / 1000.0;
        if (v / 10.0 >= 500 && v / 10.0 <= 20000) return v / 10.0;
        if (v >= 500 && v <= 20000) return v;
        return 0;
    };
    for (const QString &e : psy.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString g = psy.filePath(e) + QLatin1Char('/');
        if (g == batteryDir)
            continue;
        QString raw = readTrim(g + QStringLiteral("charge_full"));
        if (raw.isEmpty()) raw = readTrim(g + QStringLiteral("energy_full"));
        if (raw.isEmpty()) raw = readTrim(g + QStringLiteral("charge_full_design"));
        if (raw.isEmpty()) raw = readTrim(g + QStringLiteral("energy_full_design"));
        const double mah = asMah(raw.toDouble());
        if (mah > 0) {
            if (from) *from = e;
            return mah;
        }
    }
    const double mah = asMah(readTrim(
        QStringLiteral("/sys/devices/platform/battery_meter/FG_g_fg_dbg_bat_qmax")).toDouble());
    if (mah > 0 && from)
        *from = QStringLiteral("battery_meter");
    return mah;
}

QVariantMap SysMon::batteryHardware() const
{
    QVariantMap m;
    QString bat;
    const QDir psy(QStringLiteral("/sys/class/power_supply"));
    if (psy.exists(QStringLiteral("battery")))
        bat = psy.filePath(QStringLiteral("battery"));
    else
        for (const QString &e : psy.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (readTrim(psy.filePath(e) + QStringLiteral("/type")) == QLatin1String("Battery")) {
                bat = psy.filePath(e);
                break;
            }
    if (bat.isEmpty())
        return m;
    const QString b = bat + QLatin1Char('/');
    m.insert(QStringLiteral("supply"), QFileInfo(bat).fileName());
    m.insert(QStringLiteral("manufacturer"), readTrim(b + QStringLiteral("manufacturer")));
    m.insert(QStringLiteral("model"), readTrim(b + QStringLiteral("model_name")));
    m.insert(QStringLiteral("serial"), readTrim(b + QStringLiteral("serial_number")));
    m.insert(QStringLiteral("technology"), readTrim(b + QStringLiteral("technology")));
    m.insert(QStringLiteral("health"), readTrim(b + QStringLiteral("health")));
    if (m_s.battCycles >= 0)
        m.insert(QStringLiteral("cycles"), m_s.battCycles);

    // Qualcomm's gauge keeps far more than the one cycle figure it advertises.
    // These live under bms/ next to the battery node; every one of them is
    // optional, so each is gated on being readable and sane.
    {
        const QString g = QStringLiteral("/sys/class/power_supply/bms/");
        const QString age = readTrim(g + QStringLiteral("batt_age_level"));
        if (!age.isEmpty())
            m.insert(QStringLiteral("ageLevel"), age.toInt());
        // Three resistances on three scales, per the Qualcomm QG driver:
        // RESISTANCE_NOW is the last measured ESR in mOhm, RESISTANCE is the
        // stored value converted to microOhm (SDAM keeps it in mOhm and the
        // driver multiplies by 1000), RESISTANCE_ID is the battery
        // identification resistor in Ohm and says nothing about wear.
        const QString rNow = readTrim(g + QStringLiteral("resistance_now"));
        if (!rNow.isEmpty() && rNow.toDouble() > 0)
            m.insert(QStringLiteral("esrMilliOhm"), rNow.toDouble());
        const QString rBase = readTrim(g + QStringLiteral("resistance"));
        if (!rBase.isEmpty() && rBase.toDouble() > 0)
            m.insert(QStringLiteral("storedMilliOhm"), rBase.toDouble() / 1000.0);
        const QString rId = readTrim(g + QStringLiteral("resistance_id"));
        if (!rId.isEmpty() && rId.toDouble() > 0)
            m.insert(QStringLiteral("batteryIdOhm"), rId.toDouble());
        // Whether the gauge ever measured a capacity of its own. All zero means
        // charge_full comes from the battery profile, not from a measurement --
        // which decides whether the health figure means anything.
        bool haveLearn = false;
        qulonglong learn = 0;
        for (const char *k : { "learning_counter", "learning_trial_counter",
                               "full_counter", "recharge_counter" }) {
            const QString v = readTrim(g + QLatin1String(k));
            if (v.isEmpty())
                continue;
            haveLearn = true;
            learn += v.toULongLong();
        }
        if (haveLearn) {
            m.insert(QStringLiteral("learnEvents"), (double)learn);
            // Only the first of the four is a completed learning pass; a trial
            // is an attempt and the other two count charges.
            m.insert(QStringLiteral("learnPasses"),
                     readTrim(g + QStringLiteral("learning_counter")).toDouble());
            m.insert(QStringLiteral("learnTrials"),
                     readTrim(g + QStringLiteral("learning_trial_counter")).toDouble());
        }
        const QString prof = readTrim(g + QStringLiteral("battery_type"));
        if (!prof.isEmpty())
            m.insert(QStringLiteral("profileId"), prof);
    }
    // The capacities come from the sampler, which resolved once where this
    // phone keeps them. Reading them again here would be a second path to the
    // same number, and two paths are how a page ends up contradicting itself.
    m.insert(QStringLiteral("designCapacity"), m_s.battChargeDesign);
    m.insert(QStringLiteral("fullCapacity"), m_s.battChargeFull);

    Source designV;
    designV.add(b + QStringLiteral("voltage_max_design"), 1e-6, 3.0, 6.0);
    m.insert(QStringLiteral("voltageDesign"), designV.value(0));
    // What the charger actually charges this cell to. Not the design voltage,
    // and on gauges that publish no design voltage it is the only one there is.
    Source target;
    target.add(b + QStringLiteral("constant_charge_voltage"), 1e-6, 3.0, 6.0)
          .add(b + QStringLiteral("voltage_max"), 1e-6, 3.0, 6.0);
    m.insert(QStringLiteral("chargeTargetVolt"), target.value(0));
    m.insert(QStringLiteral("chargeType"), readTrim(b + QStringLiteral("charge_type")));

    // A second opinion on the pack. A gauge beside the battery node keeps its
    // own full-charge figure, and it need not agree with the class value -- on
    // one phone the class says 3760 mAh where the gauge divides by 5584. The
    // unit of that node is a vendor decision (µAh here, 0.1 mAh there), so only
    // a reading that can be a phone battery at all is accepted, and it is shown
    // beside the class figure rather than replacing it.
    QString gaugeFrom;
    const double gaugeMah = gaugeFullChargeMah(psy, b, &gaugeFrom);
    if (gaugeMah > 0) {
        m.insert(QStringLiteral("gaugeFullMah"), gaugeMah);
        m.insert(QStringLiteral("gaugeSupply"), gaugeFrom);
    }
    return m;
}

// The typec class marks the active entry in brackets: "source [sink]".
static QString typecActive(const QString &v)
{
    const int a = v.indexOf(QLatin1Char('['));
    const int b = v.indexOf(QLatin1Char(']'));
    return (a >= 0 && b > a) ? v.mid(a + 1, b - a - 1) : v;
}

static QString chargerProtocol(const QString &t)
{
    if (t.contains(QLatin1String("PD"))) return QStringLiteral("USB Power Delivery");
    if (t.contains(QLatin1String("HVDCP_3"))) return QStringLiteral("Quick Charge 3.x");
    if (t.contains(QLatin1String("HVDCP"))) return QStringLiteral("Quick Charge (HVDCP)");
    if (t.contains(QLatin1String("DCP"))) return QStringLiteral("Dedicated charger (5 V)");
    if (t.contains(QLatin1String("CDP"))) return QStringLiteral("Charging port (CDP, 1.5 A)");
    if (t.contains(QLatin1String("SDP")) || t == QLatin1String("USB")) return QStringLiteral("Standard USB (0.5–0.9 A)");
    if (t.contains(QLatin1String("Wireless"))) return QStringLiteral("Wireless");
    if (t.contains(QLatin1String("FLOAT"))) return QStringLiteral("Unknown / floating");
    return t;
}

// USB-PD source capabilities from the Qualcomm usbpd class. The raw PDOs are
// world-readable (0444), so this needs no privileges. Bit layout per the PD
// specification, cross-checked against the driver's own macros.
//
// The negotiated spec revision in the message header only encodes 1.0/2.0/3.0
// — PD 3.1 and 3.2 deliberately leave it at 3.0. What the source actually
// implements is therefore derived from the capabilities it offers:
// a PPS APDO means 3.0 or later, the EPR-Mode-Capable bit (bit 23 of the
// vSafe5V fixed PDO, reserved before 3.1) means 3.1, an SPR-AVS APDO means 3.2.
static void pdSourceCaps(QVariantMap &m)
{
    const QString pd = QStringLiteral("/sys/class/usbpd/usbpd0/");
    if (!QFileInfo::exists(pd))
        return;

    QVariantList pdos;
    double maxW = 0;
    bool pps = false, eprCapable = false, sprAvs = false, eprAvs = false;

    for (int i = 1; i <= 7; ++i) {
        bool ok = false;
        const quint32 raw = readTrim(pd + QStringLiteral("pdo%1").arg(i)).toUInt(&ok, 16);
        if (!ok || raw == 0)
            continue;
        QVariantMap p;
        p.insert(QStringLiteral("index"), i);
        p.insert(QStringLiteral("raw"), QStringLiteral("%1").arg(raw, 8, 16, QLatin1Char('0')));
        switch ((raw >> 30) & 3) {
        case 0: {   // fixed supply
            const double v = ((raw >> 10) & 0x3FF) * 0.05;
            const double a = (raw & 0x3FF) * 0.01;
            p.insert(QStringLiteral("kind"), QStringLiteral("fixed"));
            p.insert(QStringLiteral("voltage"), v);
            p.insert(QStringLiteral("current"), a);
            maxW = qMax(maxW, v * a);
            if (i == 1) {
                eprCapable = ((raw >> 23) & 1) != 0;
                p.insert(QStringLiteral("externallyPowered"), ((raw >> 27) & 1) != 0);
                p.insert(QStringLiteral("usbComm"), ((raw >> 26) & 1) != 0);
                p.insert(QStringLiteral("dualRolePower"), ((raw >> 29) & 1) != 0);
            }
            break;
        }
        case 1: {   // battery supply
            const double vmax = ((raw >> 20) & 0x3FF) * 0.05;
            const double vmin = ((raw >> 10) & 0x3FF) * 0.05;
            const double w = (raw & 0x3FF) * 0.25;
            p.insert(QStringLiteral("kind"), QStringLiteral("battery"));
            p.insert(QStringLiteral("voltageMin"), vmin);
            p.insert(QStringLiteral("voltageMax"), vmax);
            p.insert(QStringLiteral("power"), w);
            maxW = qMax(maxW, w);
            break;
        }
        case 2: {   // variable supply
            const double vmax = ((raw >> 20) & 0x3FF) * 0.05;
            const double vmin = ((raw >> 10) & 0x3FF) * 0.05;
            const double a = (raw & 0x3FF) * 0.01;
            p.insert(QStringLiteral("kind"), QStringLiteral("variable"));
            p.insert(QStringLiteral("voltageMin"), vmin);
            p.insert(QStringLiteral("voltageMax"), vmax);
            p.insert(QStringLiteral("current"), a);
            maxW = qMax(maxW, vmax * a);
            break;
        }
        default: {  // augmented (APDO)
            const int sub = (raw >> 28) & 3;
            if (sub == 0) {
                pps = true;
                const double vmax = ((raw >> 17) & 0xFF) * 0.1;
                const double vmin = ((raw >> 8) & 0xFF) * 0.1;
                const double a = (raw & 0x7F) * 0.05;
                p.insert(QStringLiteral("kind"), QStringLiteral("pps"));
                p.insert(QStringLiteral("voltageMin"), vmin);
                p.insert(QStringLiteral("voltageMax"), vmax);
                p.insert(QStringLiteral("current"), a);
                maxW = qMax(maxW, vmax * a);
            } else {
                // AVS objects: recorded, not decoded — no device to verify the
                // field layout against, and a wrong number is worse than none.
                eprAvs = eprAvs || sub == 1;
                sprAvs = sprAvs || sub == 2;
                p.insert(QStringLiteral("kind"), sub == 1 ? QStringLiteral("eprAvs")
                                                          : QStringLiteral("sprAvs"));
            }
            break;
        }
        }
        pdos.append(p);
    }

    if (pdos.isEmpty())
        return;

    m.insert(QStringLiteral("pdos"), pdos);
    m.insert(QStringLiteral("pdMaxPower"), maxW);
    m.insert(QStringLiteral("pdPps"), pps);
    m.insert(QStringLiteral("pdEpr"), eprCapable || eprAvs);
    m.insert(QStringLiteral("pdAvs"), sprAvs || eprAvs);
    // Lower bound, never an assertion: absent extensions mean the source did
    // not offer them here, not that it cannot do them.
    m.insert(QStringLiteral("pdSpecFloor"), sprAvs ? QStringLiteral("3.2")
                                          : (eprCapable || eprAvs) ? QStringLiteral("3.1")
                                          : pps ? QStringLiteral("3.0")
                                                : QString());

    m.insert(QStringLiteral("pdContract"), readTrim(pd + QStringLiteral("contract")));
    bool ok = false;
    const quint32 rdo = readTrim(pd + QStringLiteral("rdo")).toUInt(&ok, 16);
    if (ok && rdo != 0)
        m.insert(QStringLiteral("pdRequestedObject"), int((rdo >> 28) & 0xF));
    const double ppsV = readTrim(pd + QStringLiteral("pps_current_voltage")).toDouble() / 1e6;
    const double ppsA = readTrim(pd + QStringLiteral("pps_requested_current")).toDouble() / 1e3;
    if (ppsV > 0) {
        m.insert(QStringLiteral("ppsVoltage"), ppsV);
        m.insert(QStringLiteral("ppsCurrent"), ppsA);
    }
}

// Source capabilities on MediaTek, which has no usbpd class. The port
// controller prints them decoded in caps_info: "type vmin vmax oper" per line
// (0 fixed, 1 battery, 2 variable, 3 augmented), mV and mA, under a header per
// table; selected_cap is the position in remote_src_cap, counted from one.
// The raw PDOs' flags -- EPR bit, AVS subtype, RDO -- are not in it, so nothing
// derived from those is claimed here.
static void pdSourceCapsTcpc(QVariantMap &m)
{
    const QDir cls(QStringLiteral("/sys/class/tcpc"));
    if (!cls.exists())
        return;
    QString port;
    for (const QString &e : cls.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFileInfo::exists(cls.filePath(e) + QStringLiteral("/caps_info"))) {
            port = cls.filePath(e) + QLatin1Char('/');
            break;
        }
    }
    if (port.isEmpty())
        return;

    QFile f(port + QStringLiteral("caps_info"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));

    QVariantList pdos;
    QString section;
    int selected = 0;
    double maxW = 0;
    bool pps = false;

    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1String("selected_cap"))) {
            selected = line.section(QLatin1Char('='), 1).trimmed().toInt();
            continue;
        }
        if (line.contains(QLatin1String("_cap("))) {
            section = line.left(line.indexOf(QLatin1Char('(')));
            continue;
        }
        const QStringList fld = line.split(QRegExp(QStringLiteral("\\s+")), QString::SkipEmptyParts);
        if (fld.size() != 4)
            continue;
        const int type = fld.at(0).toInt();
        const double vmin = fld.at(1).toDouble() / 1000.0;
        const double vmax = fld.at(2).toDouble() / 1000.0;
        const double oper = fld.at(3).toDouble() / 1000.0;

        QVariantMap p;
        switch (type) {
        case 1:     // battery: the last field is power, not current
            p.insert(QStringLiteral("kind"), QStringLiteral("battery"));
            p.insert(QStringLiteral("voltageMin"), vmin);
            p.insert(QStringLiteral("voltageMax"), vmax);
            p.insert(QStringLiteral("power"), oper);
            break;
        case 2:
            p.insert(QStringLiteral("kind"), QStringLiteral("variable"));
            p.insert(QStringLiteral("voltageMin"), vmin);
            p.insert(QStringLiteral("voltageMax"), vmax);
            p.insert(QStringLiteral("current"), oper);
            break;
        case 3:
            // Augmented with a range: PPS. The subtype is not named here, so an
            // AVS object would be indistinguishable.
            p.insert(QStringLiteral("kind"), QStringLiteral("pps"));
            p.insert(QStringLiteral("voltageMin"), vmin);
            p.insert(QStringLiteral("voltageMax"), vmax);
            p.insert(QStringLiteral("current"), oper);
            break;
        default:
            p.insert(QStringLiteral("kind"), QStringLiteral("fixed"));
            p.insert(QStringLiteral("voltage"), vmax);
            p.insert(QStringLiteral("current"), oper);
            break;
        }

        if (section != QLatin1String("remote_src_cap"))
            continue;
        p.insert(QStringLiteral("index"), pdos.size() + 1);
        if (type == 3)
            pps = true;
        maxW = qMax(maxW, (type == 1 ? oper : vmax * oper));
        pdos.append(p);
    }

    if (pdos.isEmpty())
        return;

    m.insert(QStringLiteral("pdos"), pdos);
    m.insert(QStringLiteral("pdMaxPower"), maxW);
    m.insert(QStringLiteral("pdPps"), pps);
    // This reader knows less than the raw PDOs; the page words it accordingly.
    m.insert(QStringLiteral("pdCapsFrom"), QStringLiteral("tcpc"));
    if (selected > 0 && selected <= pdos.size())
        m.insert(QStringLiteral("pdRequestedObject"), selected);
    // Policy engine ready is the explicit contract.
    if (readTrim(port + QStringLiteral("pe_ready")) == QLatin1String("yes"))
        m.insert(QStringLiteral("pdContract"), QStringLiteral("explicit"));
}

// One reading of wakeup_sources, unranked: two pages ask it different
// questions. The header decides the columns -- their order has changed.
// The sysfs class: same counters as debugfs, world-readable, and it carries
// the one debugfs has no column name for -- how long suspend was prevented.
static QVariantList wakeupFromClass()
{
    QVariantList out;
    const QDir wcls(QStringLiteral("/sys/class/wakeup"));
    if (!wcls.exists())
        return out;
    for (const QString &e : wcls.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString d = wcls.filePath(e) + QLatin1Char('/');
        const QString name = readTrim(d + QStringLiteral("name"));
        if (name.isEmpty() || name == QLatin1String("deleted"))
            continue;   // the kernel's aggregate of destroyed sources
        QVariantMap w;
        w.insert(QStringLiteral("name"), name);
        w.insert(QStringLiteral("count"), readTrim(d + QStringLiteral("wakeup_count")).toDouble());
        w.insert(QStringLiteral("activeCount"), readTrim(d + QStringLiteral("active_count")).toDouble());
        w.insert(QStringLiteral("heldSec"), readTrim(d + QStringLiteral("total_time_ms")).toDouble() / 1000.0);
        w.insert(QStringLiteral("longestSec"), readTrim(d + QStringLiteral("max_time_ms")).toDouble() / 1000.0);
        w.insert(QStringLiteral("preventSec"),
                 readTrim(d + QStringLiteral("prevent_suspend_time_ms")).toDouble() / 1000.0);
        out.append(w);
    }
    return out;
}

// The debugfs table, for kernels without that class. debugfs is 0700 though
// the file is not, so: unprivileged read first, then the root helper.
// The header decides the columns -- their order has changed between kernels.
static QVariantList wakeupFromDebugfs()
{
    QVariantList out;
    const QString path = QStringLiteral("/sys/kernel/debug/wakeup_sources");
    QByteArray raw;
    QFile ws(path);
    if (ws.open(QIODevice::ReadOnly))
        raw = ws.readAll();
    if (raw.isEmpty() && RootClient::instance()->active())
        raw = RootClient::instance()->readFile(path);
    if (raw.isEmpty())
        return out;

    const QList<QByteArray> lines = raw.split('\n');
    const QList<QByteArray> head = lines.value(0).simplified().split(' ');
    if (head.isEmpty())
        return out;
    const int cActive = head.indexOf(QByteArray("active_count"));
    const int cWakeup = head.indexOf(QByteArray("wakeup_count"));
    const int cTotal  = head.indexOf(QByteArray("total_time"));
    const int cMax    = head.indexOf(QByteArray("max_time"));
    const int cPrev   = head.indexOf(QByteArray("prevent_suspend_time"));

    for (int i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> f = lines.at(i).simplified().split(' ');
        if (f.size() < head.size())
            continue;
        // A source name may contain spaces; the numeric columns are anchored
        // at the end, so any surplus fields belong to the name.
        const int extra = f.size() - head.size();
        QByteArray nameBytes = f.value(0);
        for (int e = 1; e <= extra; ++e)
            nameBytes += ' ' + f.value(e);
        if (nameBytes == "deleted")
            continue;
        const auto col = [&](int c) -> qulonglong {
            return c < 0 ? 0 : f.value(c + extra).toULongLong();
        };
        QVariantMap w;
        w.insert(QStringLiteral("name"), QString::fromLatin1(nameBytes));
        w.insert(QStringLiteral("count"), (double)col(cWakeup));
        w.insert(QStringLiteral("activeCount"), (double)col(cActive));
        w.insert(QStringLiteral("heldSec"), col(cTotal) / 1000.0);
        w.insert(QStringLiteral("longestSec"), col(cMax) / 1000.0);
        if (cPrev >= 0)
            w.insert(QStringLiteral("preventSec"), col(cPrev) / 1000.0);
        out.append(w);
    }
    return out;
}

// One reading, unranked: two pages ask it different questions.
QVariantList SysMon::readWakeupSources()
{
    const QVariantList fromClass = wakeupFromClass();
    return fromClass.isEmpty() ? wakeupFromDebugfs() : fromClass;
}

// Ranked by how long each source held the system awake. Unlike the attributed
// milliamps this models nothing: the kernel counts it itself.
QVariantList SysMon::wakeupSources() const
{
    QVariantList out;
    const QVariantList all = readWakeupSources();
    bool havePrevent = false;
    for (const QVariant &v : all)
        if (v.toMap().value(QStringLiteral("preventSec")).toDouble() > 0) {
            havePrevent = true;
            break;
        }
    const QString key = havePrevent ? QStringLiteral("preventSec") : QStringLiteral("heldSec");
    for (const QVariant &v : all)
        if (v.toMap().value(key).toDouble() > 0)
            out.append(v);
    std::sort(out.begin(), out.end(), [key](const QVariant &a, const QVariant &b) {
        return a.toMap().value(key).toDouble()
             > b.toMap().value(key).toDouble();
    });
    return out;
}

QVariantMap SysMon::chargerDetail() const
{
    QVariantMap m;
    const QString batStatus = readTrim(QStringLiteral("/sys/class/power_supply/battery/status"));
    const bool charging = batStatus == QLatin1String("Charging") || batStatus == QLatin1String("Full");

    // Find the active input supply. Qualcomm: usb/pc_port/dc. MediaTek: primary_chg/
    // mtk-master-charger/…. Scan all supplies for an online/charging USB/Mains input
    // rather than a fixed list, so it works across chipsets.
    QString src;
    const QDir psy(QStringLiteral("/sys/class/power_supply"));
    const QStringList prefer = { QStringLiteral("usb"), QStringLiteral("primary_chg"),
        QStringLiteral("pc_port"), QStringLiteral("mtk-master-charger"), QStringLiteral("main") };
    QStringList order = prefer;
    for (const QString &e : psy.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (!order.contains(e))
            order.append(e);
    for (const QString &s : order) {
        const QString base = QStringLiteral("/sys/class/power_supply/") + s + QLatin1Char('/');
        if (!QFileInfo::exists(base))
            continue;
        const QString type = readTrim(base + QStringLiteral("type"));
        const bool isInput = type.contains(QLatin1String("USB")) || type == QLatin1String("Mains")
                             || s.startsWith(QLatin1String("primary_chg")) || s.contains(QLatin1String("charger"));
        if (!isInput)
            continue;
        if (readTrim(base + QStringLiteral("online")) == QLatin1String("1")
            || readTrim(base + QStringLiteral("status")) == QLatin1String("Charging")
            || (charging && readTrim(base + QStringLiteral("present")) == QLatin1String("1"))) {
            src = s;
            break;
        }
    }

    m.insert(QStringLiteral("online"), !src.isEmpty() || charging);
    m.insert(QStringLiteral("charging"), charging);
    m.insert(QStringLiteral("status"), batStatus);
    // Charging with no input supply matched is a real state (the battery says
    // so, no node owns up to it). The rows still render, so they get an answer.
    m.insert(QStringLiteral("source"), QString());
    m.insert(QStringLiteral("typeRaw"), QString());
    m.insert(QStringLiteral("protocol"), QString());
    if (src.isEmpty())
        return m;

    const QString b = QStringLiteral("/sys/class/power_supply/") + src + QLatin1Char('/');
    QString type = readTrim(b + QStringLiteral("real_type"));
    if (type.isEmpty())
        type = readTrim(b + QStringLiteral("type"));
    m.insert(QStringLiteral("source"), src);
    m.insert(QStringLiteral("typeRaw"), type);
    m.insert(QStringLiteral("protocol"), chargerProtocol(type));
    Source inputV;
    inputV.add(b + QStringLiteral("voltage_now"), 1e-6, 3.0, 30.0)   // class: µV
          .add(b + QStringLiteral("voltage_now"), 1e-3, 3.0, 30.0)   // vendor: mV
          .add(QStringLiteral("/sys/devices/platform/charger/ADC_Charger_Voltage"),
               1e-3, 3.0, 30.0)                                      // MediaTek ADC
          .add(QStringLiteral("/sys/class/power_supply/battery/ChargerVoltage"),
               1e-3, 3.0, 30.0);                                     // legacy MediaTek
    m.insert(QStringLiteral("inputVoltage"), inputV.value(0));
    m.insert(QStringLiteral("inputVoltageFrom"), inputV.from());

    Source inputVMax;
    inputVMax.add(b + QStringLiteral("voltage_max"), 1e-6, 3.0, 30.0)
             .add(b + QStringLiteral("voltage_max"), 1e-3, 3.0, 30.0);
    m.insert(QStringLiteral("inputVoltageMax"), inputVMax.value(0));

    // current_max is the input limit on Qualcomm; on the MediaTek charger it
    // mirrors the battery-side ICC. Where input_current_limit exists it decides.
    Source inputIMax;
    inputIMax.add(b + QStringLiteral("input_current_limit"), 1e-6, 0.05, 10.0)
             .add(b + QStringLiteral("current_max"), 1e-6, 0.05, 10.0);
    m.insert(QStringLiteral("inputCurrentMax"), inputIMax.value(0));
    const QString pd = readTrim(b + QStringLiteral("pd_active"));
    if (!pd.isEmpty()) {
        m.insert(QStringLiteral("pdActive"), pd.toInt() > 0);
        if (pd.toInt() == 2)
            m.insert(QStringLiteral("pdPps"), true);
        m.insert(QStringLiteral("pdCurrentMax"), readTrim(b + QStringLiteral("pd_current_max")).toDouble() / 1e6);
    }
    // actual rate into the battery
    const QString bat = QStringLiteral("/sys/class/power_supply/battery/");
    const double ci = readTrim(bat + QStringLiteral("current_now")).toDouble() / 1e6;
    const double bv = readTrim(bat + QStringLiteral("voltage_now")).toDouble() / 1e6;
    m.insert(QStringLiteral("chargeType"), readTrim(bat + QStringLiteral("charge_type")));
    // Which stage carries the charge. charge_type is a Qualcomm field and
    // absent here, but every stage keeps its own status -- read, not derived.
    QVariantList stages;
    const QDir psyDir(QStringLiteral("/sys/class/power_supply"));
    for (const QString &n : psyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (n == QLatin1String("battery"))
            continue;
        if (readTrim(psyDir.filePath(n) + QStringLiteral("/status"))
                != QLatin1String("Charging"))
            continue;
        QVariantMap st;
        st.insert(QStringLiteral("name"), n);
        const QString low = n.toLower();
        st.insert(QStringLiteral("pump"), low.contains(QLatin1String("dvchg"))
                                          || low.contains(QLatin1String("div")));
        stages.append(st);
    }
    if (!stages.isEmpty())
        m.insert(QStringLiteral("chargeStages"), stages);
    // Signed: where it runs the other way, that is the fact worth having.
    m.insert(QStringLiteral("chargeCurrent"), ci);
    m.insert(QStringLiteral("batteryVoltage"), bv);
    m.insert(QStringLiteral("chargePower"), ci * bv);

    // Type-C negotiated roles / partner / cable (from the tcpm/typec class)
    const QString tc = QStringLiteral("/sys/class/typec/port0/");
    if (QFileInfo::exists(tc)) {
        m.insert(QStringLiteral("typecPowerRole"), typecActive(readTrim(tc + QStringLiteral("power_role"))));
        m.insert(QStringLiteral("typecDataRole"), typecActive(readTrim(tc + QStringLiteral("data_role"))));
        // current the port/cable advertises via CC: "default", "1.5A", "3.0A"
        const QString opMode = readTrim(tc + QStringLiteral("power_operation_mode"));
        m.insert(QStringLiteral("typecCurrent"), opMode);
        // No pd_active on this chipset, but the port knows whether a contract
        // stands. Marked as coming from the port, because the voltage and
        // current of that contract are not readable here.
        if (!m.contains(QStringLiteral("pdActive"))
                && opMode == QLatin1String("usb_power_delivery")) {
            m.insert(QStringLiteral("pdActive"), true);
            m.insert(QStringLiteral("pdFromPort"), true);
        }
        // Qualcomm writes the bare major ("3"), mainline writes "3.0".
        QString rev = readTrim(tc + QStringLiteral("usb_power_delivery_revision"));
        if (!rev.isEmpty() && !rev.contains(QLatin1Char('.')))
            rev += QStringLiteral(".0");
        m.insert(QStringLiteral("pdRevision"), rev);
        m.insert(QStringLiteral("typecRevision"), readTrim(tc + QStringLiteral("usb_typec_revision")));
        m.insert(QStringLiteral("vconn"), readTrim(tc + QStringLiteral("vconn_source")));
        const QString pn = QStringLiteral("/sys/class/typec/port0-partner/");
        if (QFileInfo::exists(pn)) {
            m.insert(QStringLiteral("partnerPd"),
                     readTrim(pn + QStringLiteral("supports_usb_power_delivery")) == QLatin1String("yes"));
            m.insert(QStringLiteral("partnerAccessory"), readTrim(pn + QStringLiteral("accessory_mode")));
        }
        // Cable e-marker identity — only present on platforms with the mainline
        // tcpm driver; absent on this Qualcomm stack. Shown when available.
        const QString cbl = QStringLiteral("/sys/class/typec/port0-cable/");
        if (QFileInfo::exists(cbl)) {
            m.insert(QStringLiteral("cablePresent"), true);
            m.insert(QStringLiteral("cablePlugType"), readTrim(cbl + QStringLiteral("plug_type")));
            m.insert(QStringLiteral("cableType"), readTrim(cbl + QStringLiteral("type")));
            const QString cid = cbl + QStringLiteral("identity/");
            if (QFileInfo::exists(cid)) {
                m.insert(QStringLiteral("cableVid"), readTrim(cid + QStringLiteral("id_header")));
                m.insert(QStringLiteral("cableProduct"), readTrim(cid + QStringLiteral("product")));
            }
        }
    }
    pdSourceCaps(m);
    if (!m.contains(QStringLiteral("pdos")))
        pdSourceCapsTcpc(m);
    return m;
}

// The negotiation out of the kernel ring buffer. Readable unprivileged where
// dmesg_restrict is 0; where it is not, klogctl fails and the page falls back
// to the root helper.
QStringList SysMon::chargerLog() const
{
    const int len = klogctl(10 /*SIZE_BUFFER*/, nullptr, 0);
    if (len <= 0)
        return QStringList();
    QByteArray buf(len + 1, 0);
    const int n = klogctl(3 /*READ_ALL*/, buf.data(), len);
    if (n <= 0)
        return QStringList();
    buf.truncate(n);
    return chargerLogLines(buf);
}

// ---------------------------------------------------------------------------
// The vendor charging path -- everything the charger exports below the
// standard power-supply class.
//
// On MediaTek platforms the charger driver keeps a directory of its own with
// ADC readings, the negotiated adapter type and its throttling flags, and the
// battery framework adds a small procfs command directory next to it. All of
// it is world-readable, so this works with the root helper switched off.
//
// Units are deliberately not normalised. Vendor nodes mix them -- on the Jolla
// Phone (2026) ADC_Charging_Current counts milliamps while battery/current_now
// in the directory beside it counts microamps -- and nothing in sysfs says
// which one a node uses. Only the two ADC readings are converted, because
// their scale was checked against the battery node at the same operating
// point; everything else is passed through exactly as the kernel wrote it.
// ---------------------------------------------------------------------------

// Attributes that must not be read, because reading them is not a read.
// A few vendor nodes do work in their show() handler: the DRAM driver's
// binning_test runs a memory test, MediaTek's gpu_loading sleeps 100 ms inside
// the kernel, the Wi-Fi firmware nodes each cost a blocking round trip to the
// chip, and opening the camera's pdaf_type issues a sensor feature call. An
// exhaustive dump has to know where to stop, and this is the list.
static bool sysfsUnsafe(const QString &name)
{
    static const QStringList never = { QStringLiteral("binning_test"),
                                       QStringLiteral("gpu_loading"),
                                       QStringLiteral("efuse_dump"),
                                       QStringLiteral("mcr"),
                                       QStringLiteral("roam_param"),
                                       QStringLiteral("pdaf_type"),
                                       // The flash LED controller latches its
                                       // faults and clears them when the flag
                                       // register is read -- the datasheet
                                       // names that read as the way to re-arm
                                       // a chip that has shut itself off. A
                                       // 0444 file whose read is a write.
                                       QStringLiteral("flash_fault"),
                                       QStringLiteral("brightness"),
                                       QStringLiteral("flash_brightness"),
                                       QStringLiteral("flash_strobe"),
                                       // Vendor forks add raw register windows
                                       // under these names.
                                       QStringLiteral("reg"),
                                       QStringLiteral("registers") };
    return never.contains(name);
}

// Attribute count without reading any: on I2C stages the reads are the cost.
static int sysfsAttributeCount(const QString &dir);

static QVariantList sysfsAttributes(const QString &dir)
{
    static const QStringList skip = { QStringLiteral("uevent"),
                                      QStringLiteral("modalias"),
                                      QStringLiteral("driver_override") };
    QVariantList out;
    const QFileInfoList files =
        QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : files) {
        if (skip.contains(fi.fileName()) || sysfsUnsafe(fi.fileName()) || !fi.isReadable())
            continue;
        QString v = readTrim(fi.absoluteFilePath());
        v.replace(QLatin1Char('\n'), QStringLiteral("  ·  "));
        if (v.size() > 200)
            v = v.left(200) + QStringLiteral(" …");
        QVariantMap a;
        a.insert(QStringLiteral("name"), fi.fileName());
        a.insert(QStringLiteral("value"), v);
        out.append(a);
    }
    return out;
}

static int sysfsAttributeCount(const QString &dir)
{
    static const QStringList skip = { QStringLiteral("uevent"),
                                      QStringLiteral("modalias"),
                                      QStringLiteral("driver_override") };
    int n = 0;
    const QFileInfoList files =
        QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : files)
        if (!skip.contains(fi.fileName()) && !sysfsUnsafe(fi.fileName()) && fi.isReadable())
            ++n;
    return n;
}

QVariantMap SysMon::chargingPath() const
{
    QVariantMap m;

    const QString mtk = QStringLiteral("/sys/devices/platform/charger/");
    if (QFileInfo::exists(mtk)) {
        m.insert(QStringLiteral("vendor"), QStringLiteral("MediaTek"));
        m.insert(QStringLiteral("path"), mtk);
        // The two ADC readings are the only figures here with an established
        // scale: millivolt on the bus, milliamp into the battery.
        // Converted only inside a window the quantity can physically occupy.
        // The unit of a vendor node is an assumption, and an assumption that is
        // wrong by a factor of a thousand produces a number that still looks
        // like an answer. Outside the window the raw figure is passed through
        // with its unit called unknown, which is the honest failure.
        const double mv = readTrim(mtk + QStringLiteral("ADC_Charger_Voltage")).toDouble();
        if (mv >= 3000 && mv <= 30000)          // 3 V … 30 V, expressed in mV
            m.insert(QStringLiteral("vbus"), mv / 1000.0);
        else if (mv > 0)
            m.insert(QStringLiteral("vbusRaw"), mv);
        const double ma = readTrim(mtk + QStringLiteral("ADC_Charging_Current")).toDouble();
        // Negative is discharge, and the node reports it while unplugged --
        // an earlier window that started at zero threw a real reading away.
        if (ma >= -20000 && ma <= 20000)        // ±20 A, expressed in mA
            m.insert(QStringLiteral("adcCurrent"), ma / 1000.0);
        else if (ma != 0)
            m.insert(QStringLiteral("adcCurrentRaw"), ma);

        m.insert(QStringLiteral("chargingMode"), readTrim(mtk + QStringLiteral("Charging_mode")));
        m.insert(QStringLiteral("adapterType"), readTrim(mtk + QStringLiteral("ta_type")));
        m.insert(QStringLiteral("chargerType"), readTrim(mtk + QStringLiteral("chr_type")));
        m.insert(QStringLiteral("pumpExpress"), readTrim(mtk + QStringLiteral("Pump_Express")));
        m.insert(QStringLiteral("highVoltage"), readTrim(mtk + QStringLiteral("High_voltage_chg_enable")));
        m.insert(QStringLiteral("rustDetect"), readTrim(mtk + QStringLiteral("Rust_detect")));
        m.insert(QStringLiteral("throttleFlag"), readTrim(mtk + QStringLiteral("Thermal_throttle")));
        m.insert(QStringLiteral("swJeita"), readTrim(mtk + QStringLiteral("sw_jeita")));
        const double ovp = readTrim(mtk + QStringLiteral("sw_ovp_threshold")).toDouble();
        if (ovp >= 3e6 && ovp <= 30e6)          // 3 V … 30 V, expressed in µV
            m.insert(QStringLiteral("ovpVolt"), ovp / 1e6);
        else if (ovp > 0)
            m.insert(QStringLiteral("ovpRaw"), ovp);
        m.insert(QStringLiteral("powerPath"), readTrim(mtk + QStringLiteral("enable_power_path")));
        m.insert(QStringLiteral("smartCharging"), readTrim(mtk + QStringLiteral("enable_sc")));
        // The smart-charging schedule: hold at a target percentage, resume in
        // time for a start hour. Its current limit is in milliamp and its
        // over-voltage threshold in microvolt, in the same directory.
        m.insert(QStringLiteral("scTargetSoc"), readTrim(mtk + QStringLiteral("sc_tuisoc")));
        m.insert(QStringLiteral("scCurrentLimit"), readTrim(mtk + QStringLiteral("sc_ibat_limit")));
        m.insert(QStringLiteral("scStart"), readTrim(mtk + QStringLiteral("sc_stime")));
        m.insert(QStringLiteral("scEnd"), readTrim(mtk + QStringLiteral("sc_etime")));
        m.insert(QStringLiteral("fastChargeIndicator"), readTrim(mtk + QStringLiteral("fast_chg_indicator")));
        m.insert(QStringLiteral("safetyTimer"), readTrim(QStringLiteral("/proc/mtk_battery_cmd/en_safety_timer")));
        Source setCv;
        setCv.add(QStringLiteral("/proc/mtk_battery_cmd/set_cv"), 1e-6, 3.0, 6.0);
        m.insert(QStringLiteral("setCvVolt"), setCv.value(0));
    }

    // Which charging protocols this kernel implements at all. PD, PPS, Pump
    // Express and UFCS each ship as their own module, so the module list says
    // what the hardware can negotiate -- independently of what is plugged in
    // right now. Names are the drivers' own; nothing here is interpreted.
    QFile mods(QStringLiteral("/proc/modules"));
    if (mods.open(QIODevice::ReadOnly)) {
        QStringList names;
        while (!mods.atEnd()) {
            const QString n =
                QString::fromLatin1(mods.readLine().split(' ').value(0)).trimmed();
            if (n.isEmpty())
                continue;
            const QString l = n.toLower();
            if (l.contains(QLatin1String("charg")) || l.contains(QLatin1String("chg"))
                || l.contains(QLatin1String("ufcs")) || l.contains(QLatin1String("tcpc"))
                || l.contains(QLatin1String("pep")) || l.contains(QLatin1String("_pd_"))
                || l.contains(QLatin1String("adapter")) || l.contains(QLatin1String("gauge"))
                || l.contains(QLatin1String("battery")))
                names.append(n);
        }
        names.sort();
        if (!names.isEmpty())
            m.insert(QStringLiteral("modules"), names);
    }
    return m;
}

// Every power-supply node with every attribute it exports. On a Qualcomm
// device that is battery, bms, usb and dc; on the MediaTek platform of the
// Jolla Phone (2026) it is ten nodes, because every stage of the charging
// chain -- master, slave, and two divider stages each in a plain and a
// high-voltage variant -- registers separately, and the fuel gauge exports one
// file per register. The list is complete rather than curated: which node
// carries the interesting figure differs per device, and an attribute this app
// has never heard of is still worth seeing.
QVariantList SysMon::powerSupplyDump(bool withAttrs) const
{
    QVariantList out;
    const QDir psy(QStringLiteral("/sys/class/power_supply"));
    for (const QString &name : psy.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString base = psy.filePath(name) + QLatin1Char('/');
        QVariantMap s;
        s.insert(QStringLiteral("name"), name);
        s.insert(QStringLiteral("type"), readTrim(base + QStringLiteral("type")));
        s.insert(QStringLiteral("online"), readTrim(base + QStringLiteral("online")));
        s.insert(QStringLiteral("present"), readTrim(base + QStringLiteral("present")));
        s.insert(QStringLiteral("status"), readTrim(base + QStringLiteral("status")));
        s.insert(QStringLiteral("manufacturer"), readTrim(base + QStringLiteral("manufacturer")));
        s.insert(QStringLiteral("model"), readTrim(base + QStringLiteral("model_name")));
        // The driver behind the node names the charger IC, where the supply
        // name only says which stage of the chain it is.
        const QString drv = QFileInfo(base + QStringLiteral("device/driver")).symLinkTarget();
        if (!drv.isEmpty())
            s.insert(QStringLiteral("driver"), QFileInfo(drv).fileName());
        // "[Unknown] SDP DCP CDP PD PD_PPS": the bracketed entry is the one in
        // force, the rest is what this stage is able to negotiate.
        const QString ut = readTrim(base + QStringLiteral("usb_type"));
        if (!ut.isEmpty()) {
            QStringList all = ut.split(QLatin1Char(' '), QString::SkipEmptyParts);
            for (QString &t : all)
                if (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']'))) {
                    t = t.mid(1, t.size() - 2);
                    s.insert(QStringLiteral("usbTypeActive"), t);
                }
            s.insert(QStringLiteral("usbTypes"), all.join(QStringLiteral("  ")));
        }
        // Seconds on a divider topology, so the page asks only when opened.
        if (withAttrs)
            s.insert(QStringLiteral("attrs"), sysfsAttributes(psy.filePath(name)));
        else
            s.insert(QStringLiteral("attrCount"), sysfsAttributeCount(psy.filePath(name)));
        out.append(s);
    }
    return out;
}

// One node's registers, read when its dump is opened. The name comes from QML,
// so it stays a single directory entry under the class.
QVariantList SysMon::powerSupplyAttrs(const QString &name) const
{
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.startsWith(QLatin1Char('.')))
        return QVariantList();
    const QString d = QStringLiteral("/sys/class/power_supply/") + name;
    if (!QFileInfo(d).isDir())
        return QVariantList();
    return sysfsAttributes(d);
}

// The thermal framework in full: every zone the kernel registers, its trip
// points, and the cooling devices -- plus which zone each cooling device is
// bound to. The live card on the overview shows only zones that carry a
// temperature; this is the complete register, including the ones it drops and
// the reason each one was dropped.
// Two values in MediaTek's thermal interface are not measurements at all, and
// both look entirely plausible if taken as numbers.
//
// 666666666 is the driver's way of saying no limit is set. Printed as a power
// budget it reads as 666 kW; printed as a frequency, as 666 GHz.
//
// -274000 millidegrees is "no sensor" -- deliberately below absolute zero, so
// that nothing can mistake it for a reading. Printed as a temperature it reads
// as -274 °C, which is exactly the kind of figure that ends up in a bug report.
//
// Both were seen on the Jolla Phone (2026) the first time this code ran on it.
static bool thermalSentinel(double v)
{
    return v == 666666666.0 || v <= -273150.0;
}

// A comma- or space-separated list of millidegrees, as degrees. The vendor's
// own separators are inconsistent -- min_throttle_freq mixes commas and a
// space in one line -- so both are accepted.
static QString fmtThermalList(const QString &raw, double divisor, const QString &unit,
                              const QString &sentinelWord)
{
    if (raw.isEmpty())
        return QString();
    QStringList out;
    int lastNumber = -1;
    const QStringList parts = raw.split(QRegExp(QStringLiteral("[,\\s]+")),
                                        QString::SkipEmptyParts);
    for (const QString &p : parts) {
        bool ok = false;
        const double v = p.trimmed().toDouble(&ok);
        if (!ok)
            return raw;                 // not a number list after all
        if (thermalSentinel(v)) {
            out << sentinelWord;
        } else {
            lastNumber = out.size();
            out << QString::number(v / divisor, 'f', divisor >= 1000 ? 1 : 0);
        }
    }
    if (out.isEmpty())
        return QString();
    // The unit goes on the last figure, not on the end of the line: a list
    // whose final entry is a sentinel would otherwise read "no limit K".
    // Non-breaking space, so a wrap cannot leave the unit starting a line.
    if (!unit.isEmpty() && lastNumber >= 0)
        out[lastNumber] += QChar(0x00A0) + unit;
    return out.join(QStringLiteral(", "));
}

QVariantMap SysMon::thermalDetail() const
{
    QVariantMap m;
    const QDir tdir(QStringLiteral("/sys/class/thermal"));
    const QStringList zoneDirs =
        tdir.entryList(QStringList() << QStringLiteral("thermal_zone*"), QDir::Dirs, QDir::Name);

    // cooling_deviceN -> the zones that bind it, from the cdevN symlinks the
    // kernel creates when a zone gets a cooling device attached. A cooling
    // device nothing binds cannot be driven by the kernel's governor at all.
    QHash<QString, QStringList> boundBy;
    QVariantList zones;
    int bindings = 0;

    for (const QString &z : zoneDirs) {
        const QString base = tdir.filePath(z) + QLatin1Char('/');
        QVariantMap zm;
        zm.insert(QStringLiteral("node"), z);
        const QString name = readTrim(base + QStringLiteral("type"));
        zm.insert(QStringLiteral("name"), name);
        const QString raw = readTrim(base + QStringLiteral("temp"));
        zm.insert(QStringLiteral("raw"), raw);
        zm.insert(QStringLiteral("policy"), readTrim(base + QStringLiteral("policy")));
        zm.insert(QStringLiteral("mode"), readTrim(base + QStringLiteral("mode")));

        // The same rule the live card applies, but here the dropped zones stay
        // on the page with the reason attached instead of vanishing.
        const int milli = raw.toInt();
        QString skip;
        if (raw.isEmpty())
            skip = QStringLiteral("empty");
        else if (name.contains(QStringLiteral("-vbat-lvl")) || name.contains(QStringLiteral("-ibat-lvl"))
                 || name.contains(QStringLiteral("-vph-lvl")) || name.contains(QStringLiteral("-bcl-lvl"))
                 || (name == QLatin1String("soc") && milli <= 100))
            skip = QStringLiteral("watchdog");
        else if (milli <= 0)
            skip = QStringLiteral("zero");
        else if (milli > 150000)
            skip = QStringLiteral("range");
        else
            zm.insert(QStringLiteral("tempC"), milli / 1000.0);
        if (!skip.isEmpty())
            zm.insert(QStringLiteral("skipped"), skip);

        QVariantList trips;
        for (int i = 0; i < 16; ++i) {
            const QString t = readTrim(base + QStringLiteral("trip_point_%1_temp").arg(i));
            if (t.isEmpty())
                continue;
            QVariantMap tm;
            tm.insert(QStringLiteral("index"), i);
            tm.insert(QStringLiteral("kind"), readTrim(base + QStringLiteral("trip_point_%1_type").arg(i)));
            tm.insert(QStringLiteral("tempC"), t.toDouble() / 1000.0);
            const QString h = readTrim(base + QStringLiteral("trip_point_%1_hyst").arg(i));
            if (!h.isEmpty())
                tm.insert(QStringLiteral("hystC"), h.toDouble() / 1000.0);
            trips.append(tm);
        }
        if (!trips.isEmpty())
            zm.insert(QStringLiteral("trips"), trips);

        QStringList cdevs;
        for (const QString &c : QDir(base).entryList(QStringList() << QStringLiteral("cdev*"),
                                                     QDir::Dirs | QDir::System)) {
            if (c.endsWith(QStringLiteral("_trip_point")) || c.endsWith(QStringLiteral("_weight")))
                continue;
            const QString target = QFileInfo(base + c).symLinkTarget();
            if (target.isEmpty())
                continue;
            const QString cd = QFileInfo(target).fileName();
            cdevs.append(cd);
            boundBy[cd].append(name.isEmpty() ? z : name);
            ++bindings;
        }
        if (!cdevs.isEmpty())
            zm.insert(QStringLiteral("cdevs"), cdevs.join(QStringLiteral(", ")));
        zones.append(zm);
    }

    // Same comparison the live card makes, for the same reason: a node that
    // carries millivolts arrives as a number in the temperature range, and the
    // only thing that gives it away is sitting far below every other sensor in
    // the same handset. Marked here rather than removed -- this is the page
    // that exists to show what the kernel registered.
    {
        QVector<double> t;
        for (const QVariant &v : zones) {
            const QVariantMap z = v.toMap();
            if (z.contains(QStringLiteral("tempC")))
                t.append(z.value(QStringLiteral("tempC")).toDouble());
        }
        if (t.size() >= 5) {
            std::sort(t.begin(), t.end());
            const double median = t.at(t.size() / 2);
            // Thresholds as in the sampler, and for the same reason: the
            // legitimate spread across one board reaches 45 K under load, so
            // only a near-freezing zone on a warm board counts as suspect.
            if (median > 25.0)
                for (int i = 0; i < zones.size(); ++i) {
                    QVariantMap z = zones[i].toMap();
                    const double zt = z.value(QStringLiteral("tempC")).toDouble();
                    if (z.contains(QStringLiteral("tempC")) && zt < 10.0
                        && median - zt > 25.0) {
                        z.insert(QStringLiteral("suspect"), true);
                        zones[i] = z;
                    }
                }
        }
    }

    QVariantList cooling;
    for (const QString &c : tdir.entryList(QStringList() << QStringLiteral("cooling_device*"),
                                           QDir::Dirs, QDir::Name)) {
        const QString base = tdir.filePath(c) + QLatin1Char('/');
        QVariantMap cm;
        cm.insert(QStringLiteral("node"), c);
        cm.insert(QStringLiteral("type"), readTrim(base + QStringLiteral("type")));
        cm.insert(QStringLiteral("cur"), readTrim(base + QStringLiteral("cur_state")).toInt());
        cm.insert(QStringLiteral("max"), readTrim(base + QStringLiteral("max_state")).toInt());
        const QStringList b = boundBy.value(c);
        if (!b.isEmpty())
            cm.insert(QStringLiteral("boundTo"), b.join(QStringLiteral(", ")));
        cooling.append(cm);
    }

    // MediaTek's thermal interface answers the one question the zone list
    // cannot. The zones give temperatures, the trip points give intentions,
    // and these flags give the state of the limiter itself: whether the SoC is
    // being held back at this moment, and against which junction target.
    const QString ki = QStringLiteral("/sys/kernel/thermal/");
    if (QFileInfo::exists(ki)) {
        QVariantMap lim;
        const QString noLimit = QStringLiteral("no limit");
        const QString noSensor = QStringLiteral("no sensor");
        for (const char *k : { "is_cpu_limit", "is_gpu_limit", "is_apu_limit" }) {
            const QString v = readTrim(ki + QLatin1String(k));
            if (!v.isEmpty())
                lim.insert(QString::fromLatin1(k) == QLatin1String("is_cpu_limit")
                               ? QStringLiteral("cpuLimited")
                           : QString::fromLatin1(k) == QLatin1String("is_gpu_limit")
                               ? QStringLiteral("gpuLimited")
                               : QStringLiteral("apuLimited"), v);
        }
        // Junction targets and the skin target are millidegrees; the power
        // budget is milliwatt with the no-limit sentinel; the headroom is
        // whole kelvin below target, one per core plus the board.
        // The sentinel words are shown to the reader, so they go through the
        // translations like every other word in the app.
        struct Conv { const char *key, *file; double div; const char *unit; const char *word; };
        static const Conv conv[] = {
            { "junctionTarget", "ttj",               1000, "°C", QT_TRANSLATE_NOOP("SysMon", "no limit") },
            { "junctionMax",    "max_ttj",           1000, "°C", QT_TRANSLATE_NOOP("SysMon", "no limit") },
            { "junctionMin",    "min_ttj",           1000, "°C", QT_TRANSLATE_NOOP("SysMon", "no limit") },
            { "skinTarget",     "target_tpcb",       1000, "°C", QT_TRANSLATE_NOOP("SysMon", "no sensor") },
            { "cpuTemps",       "cpu_temp",          1000, "°C", QT_TRANSLATE_NOOP("SysMon", "no sensor") },
            { "skinTemp",       "vtskin_temp",       1000, "°C", QT_TRANSLATE_NOOP("SysMon", "no sensor") },
            { "headroom",       "headroom_info",        1, "K",  QT_TRANSLATE_NOOP("SysMon", "no limit") },
            { "powerBudget",    "power_budget",         1, "mW", QT_TRANSLATE_NOOP("SysMon", "no limit") },
            { "dsuCeiling",     "dsu_ceiling_freq",  1000, "MHz", QT_TRANSLATE_NOOP("SysMon", "no limit") },
            { "throttleFloor",  "min_throttle_freq", 1000, "MHz", QT_TRANSLATE_NOOP("SysMon", "no limit") },
            { nullptr, nullptr, 0, nullptr, nullptr }
        };
        for (int i = 0; conv[i].key; ++i) {
            // fromUtf8, not QLatin1String: the unit column carries "°C", and
            // the degree sign is two bytes in this file. Read as Latin-1 they
            // become two characters and the app prints "Â°C".
            const QString v = fmtThermalList(readTrim(ki + QLatin1String(conv[i].file)),
                                             conv[i].div, QString::fromUtf8(conv[i].unit),
                                             SysMon::tr(conv[i].word));
            if (!v.isEmpty())
                lim.insert(QString::fromLatin1(conv[i].key), v);
        }
        // "temperature, capped clock, current clock" -- millidegrees and two
        // kilohertz figures in one line.
        const QStringList gi = readTrim(ki + QStringLiteral("gpu_info"))
                               .split(QLatin1Char(','), QString::SkipEmptyParts);
        if (gi.size() >= 3) {
            lim.insert(QStringLiteral("gpuTemp"),
                       QStringLiteral("%1 °C").arg(gi.at(0).toDouble() / 1000.0, 0, 'f', 1));
            lim.insert(QStringLiteral("gpuClock"),
                       QStringLiteral("%1 / %2 MHz").arg(gi.at(2).toDouble() / 1000.0, 0, 'f', 0)
                                                    .arg(gi.at(1).toDouble() / 1000.0, 0, 'f', 0));
        }
        const QString bt = readTrim(ki + QStringLiteral("bat_type"));
        if (!bt.isEmpty())
            lim.insert(QStringLiteral("batteryType"), bt);
        if (!lim.isEmpty())
            m.insert(QStringLiteral("limits"), lim);
    }

    m.insert(QStringLiteral("zones"), zones);
    m.insert(QStringLiteral("cooling"), cooling);
    m.insert(QStringLiteral("bindings"), bindings);
    return m;
}

// ---------------------------------------------------------------------------
// Raw nodes: everything a subsystem exports, not only the parts this app has a
// label for.
//
// Every detail page above is curated -- each row is an attribute whose meaning
// was established before it was shown. That leaves out whatever the vendor
// added, and on a MediaTek platform that is most of it: a procfs file per
// camera sensor slot, the Mali driver's own counters, the Wi-Fi firmware's
// parameter list, a charger stage per silicon block. None of it is privileged,
// all of it is world-readable, and the only reason it never appeared here is
// that nobody wrote a name for it.
//
// So these dump the directories a subsystem owns, attribute by attribute,
// exactly as the kernel exports them, with no interpretation at all. Discovery
// goes through the kernel's own class enumeration wherever there is one, so
// the same code finds the Qualcomm nodes on one device and the MediaTek ones
// on the next without carrying a device list.
// ---------------------------------------------------------------------------

static QString taintWords(const QString &flags);

// Binary multiples with the prefixes that mean binary multiples. Everything
// here divides by 1024, so the unit has to say KiB and not kB -- the storage
// page already explains the gap between the two, and it cannot explain it in
// units that pretend the gap is not there.
static QString humanBytes(double b)
{
    if (b < 0)
        b = 0;
    if (b >= 1073741824.0)
        return QString::number(b / 1073741824.0, 'f', 2) + QStringLiteral(" GiB");
    if (b >= 1048576.0)
        return QString::number(b / 1048576.0, 'f', 1) + QStringLiteral(" MiB");
    if (b >= 1024.0)
        return QString::number(b / 1024.0, 'f', 0) + QStringLiteral(" KiB");
    return QString::number(b, 'f', 0) + QStringLiteral(" B");
}

// A raw figure with no unit says nothing: 12288 is a number, 12 KiB is a
// statement. A few sysfs attributes carry a unit the kernel itself defines --
// the same on every device, every driver, documented in the ABI -- and those
// get read out in words. The rest keeps the driver's own figure untouched,
// because on a vendor node the same key counts microamps on one chip and
// milliamps on the next, and a unit guessed from a name would be a claim this
// app cannot back.
//
// Nothing is lost either way: where there is a reading it takes the value
// column and the kernel's own figure moves to the right of the row.
static QString rawReading(const QString &dir, const QString &name, const QString &value)
{
    const bool isModule  = dir.startsWith(QLatin1String("/sys/module/"));
    const bool isNet     = dir.startsWith(QLatin1String("/sys/class/net/"));
    const bool isThermal = dir.startsWith(QLatin1String("/sys/class/thermal/"));

    if (isModule && name == QLatin1String("taint"))
        return taintWords(value);

    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok)
        return QString();

    // Module section sizes: bytes, kernel/module.c.
    if (isModule && (name == QLatin1String("coresize") || name == QLatin1String("initsize")))
        return humanBytes(v);
    // Interface counters and the MTU: bytes, Documentation/ABI/testing/sysfs-class-net.
    if (isNet && (name == QLatin1String("rx_bytes") || name == QLatin1String("tx_bytes")))
        return humanBytes(v);
    // Not humanBytes here: an MTU of 1500 is read as 1500, and "1 KiB" would
    // be a worse answer than the figure it replaced.
    if (isNet && name == QLatin1String("mtu"))
        return QString::number(v, 'f', 0) + QStringLiteral(" B");
    if (isNet && name == QLatin1String("speed") && v > 0)
        return QString::number(v, 'f', 0) + QStringLiteral(" Mbit/s");
    // Thermal zones: millidegrees, sysfs-class-thermal.
    if (isThermal && name == QLatin1String("temp"))
        return QString::number(v / 1000.0, 'f', 1) + QStringLiteral(" °C");
    return QString();
}

static void addRawDir(QVariantList &out, const QString &title, const QString &dir)
{
    if (!QFileInfo::exists(dir))
        return;
    QVariantList attrs = sysfsAttributes(dir);
    if (attrs.isEmpty())
        return;
    for (int i = 0; i < attrs.size(); ++i) {
        QVariantMap a = attrs[i].toMap();
        const QString r = rawReading(dir, a.value(QStringLiteral("name")).toString(),
                                     a.value(QStringLiteral("value")).toString());
        if (r.isEmpty())
            continue;
        a.insert(QStringLiteral("reading"), r);
        attrs[i] = a;
    }
    QVariantMap g;
    g.insert(QStringLiteral("title"), title);
    g.insert(QStringLiteral("path"), dir);
    g.insert(QStringLiteral("attrs"), attrs);
    out.append(g);
}

// A plain file instead of a directory of attributes: one row per line, so a
// procfs table stays a table instead of collapsing into one long value.
static void addRawFile(QVariantList &out, const QString &title, const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QVariantList attrs;
    while (!f.atEnd() && attrs.size() < 400) {
        QString line = QString::fromLatin1(f.readLine()).trimmed();
        if (line.isEmpty())
            continue;
        if (line.size() > 200)
            line = line.left(200) + QStringLiteral(" …");
        QString key;
        int sep = line.indexOf(QLatin1Char(':'));
        if (sep < 0 || sep > 40)
            sep = line.indexOf(QLatin1Char('='));
        if (sep > 0 && sep <= 40) {
            key = line.left(sep).trimmed();
            line = line.mid(sep + 1).trimmed();
        }
        QVariantMap a;
        a.insert(QStringLiteral("name"), key);
        a.insert(QStringLiteral("value"), line);
        attrs.append(a);
    }
    if (attrs.isEmpty())
        return;
    QVariantMap g;
    g.insert(QStringLiteral("title"), title);
    g.insert(QStringLiteral("path"), path);
    g.insert(QStringLiteral("attrs"), attrs);
    out.append(g);
}

// A file whose content identifies the device rather than describing it. Only
// the value is dropped -- the key stays, so the reader sees that something was
// held back rather than that nothing was there.
static void addRawFileRedacted(QVariantList &out, const QString &title, const QString &path)
{
    QVariantList before = out;
    addRawFile(out, title, path);
    if (out.size() == before.size())
        return;
    QVariantMap g = out.last().toMap();
    QVariantList attrs = g.value(QStringLiteral("attrs")).toList();
    for (int i = 0; i < attrs.size(); ++i) {
        QVariantMap a = attrs[i].toMap();
        QString v = a.value(QStringLiteral("value")).toString();
        static const QStringList secret = { QStringLiteral("serialno"),
                                            QStringLiteral("uuid"),
                                            QStringLiteral("imei"),
                                            QStringLiteral("androidboot.un"),
                                            QStringLiteral("root_hash") };
        QStringList toks = v.split(QLatin1Char(' '), QString::SkipEmptyParts);
        bool touched = false;
        for (QString &t : toks) {
            for (const QString &k : secret)
                if (t.contains(k, Qt::CaseInsensitive) && t.contains(QLatin1Char('='))) {
                    t = t.section(QLatin1Char('='), 0, 0) + QStringLiteral("=…");
                    touched = true;
                    break;
                }
        }
        if (!touched)
            continue;
        a.insert(QStringLiteral("value"), toks.join(QStringLiteral(" ")));
        attrs[i] = a;
    }
    g.insert(QStringLiteral("attrs"), attrs);
    out[out.size() - 1] = g;
}

// Every member of a kernel class, optionally one level deeper (net/eth0/
// statistics rather than net/eth0). The class is the discovery mechanism:
// whatever the device registered is what gets dumped.
static void addRawClass(QVariantList &out, const QString &cls, const QString &sub = QString())
{
    const QDir d(QStringLiteral("/sys/class/") + cls);
    for (const QString &e : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString path = sub.isEmpty() ? d.filePath(e)
                                           : d.filePath(e) + QLatin1Char('/') + sub;
        addRawDir(out, cls + QLatin1Char('/') + e + (sub.isEmpty() ? QString()
                                                                  : QLatin1Char('/') + sub), path);
    }
}

// Module parameters. On vendor kernels these carry the tuning the driver was
// built with -- firmware paths, feature switches, buffer sizes -- and they are
// the only place some of it is visible at all.
static void addRawModules(QVariantList &out, const QStringList &patterns)
{
    const QDir d(QStringLiteral("/sys/module"));
    for (const QString &m : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        bool hit = false;
        for (const QString &p : patterns)
            if (m.contains(p, Qt::CaseInsensitive)) {
                hit = true;
                break;
            }
        if (!hit)
            continue;
        addRawDir(out, QStringLiteral("module ") + m, d.filePath(m));
        addRawDir(out, QStringLiteral("module ") + m + QStringLiteral(" — parameters"),
                  d.filePath(m) + QStringLiteral("/parameters"));
    }
}

// A device-tree property. Strings there are NUL-terminated and a property may
// hold several of them in a row, which is how a node lists its compatible
// entries from most to least specific.
static QString dtString(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QByteArray b = f.read(4096);
    while (b.endsWith('\0'))
        b.chop(1);
    b.replace('\0', ", ");
    return QString::fromLatin1(b).trimmed();
}

// The device tree is the phone's parts list. Every chip the kernel binds a
// driver to appears as a node with a "compatible" string, and that string is
// the vendor's own name for the part -- which is why this finds hardware no
// class enumeration knows about: a fingerprint sensor on SPI, an audio
// amplifier on I2C, the PMICs, the touch controller. It is world-readable,
// costs nothing, and is the same walk on every device.
//
// filter is a space-separated list of substrings; a node matches when its name
// or its compatible string contains any of them. Empty means everything.
// ---------------------------------------------------------------------------
// Catalogue figures for the SoC, keyed on the device-tree compatible string.
//
// Nothing in here is measured, and that is the point of keeping it separate.
// The kernel names the part and stops: a device tree says "mediatek,MT6858"
// and nothing else, while the figures a reader actually wants -- process node,
// how many cores of which design, what the memory controller accepts -- exist
// only in the vendor's own publication. So they are carried here, each with
// the source that published it, and where the vendor published nothing the row
// says "not published" rather than borrowing a number from a spec database.
//
// Two sources are distinguished, because they are not worth the same:
// "vendor" is the chip maker's own product page, "third party" is a database
// or the press. Chip makers do not publish their MT/SM part numbers alongside
// the marketing name, so the very link between the two is third-party -- which
// is why the row that makes it says so.
// ---------------------------------------------------------------------------

static void dramGrade(QVariantMap &m);

// The adaptation's own name for this device, from /etc/hw-release. It is what
// distinguishes one port from another where the device tree only names the SoC.
static QString hwDevice()
{
    QFile hw(QStringLiteral("/etc/hw-release"));
    if (hw.open(QIODevice::ReadOnly))
        for (const QByteArray &l : hw.readAll().split('\n'))
            if (l.startsWith("MER_HA_DEVICE="))
                return QString::fromUtf8(l.mid(14).trimmed());
    return QString();
}

struct SocSpec { const char *key; const char *value; const char *source; };

// Every loaded kernel module with its size and who holds it. On a vendor
// kernel the module list is the closest thing to a driver inventory: it names
// the silicon blocks that got a driver at all, and the dependency column shows
// which of them lean on which. Ordinary users may read /proc/modules; only the
// load addresses are withheld from them, and those are not wanted here.
// ---------------------------------------------------------------------------
// Firmware. Not one version but a dozen of them, because a phone is a dozen
// computers: the kernel, a module per silicon block, a blob per radio, and a
// controller in the storage, the charger, every USB device and the modem, each
// with a release of its own that nobody ever collects in one place.
//
// Everything below is read without privileges. Where a version genuinely does
// not exist -- and for several of these it does not, because the vendor never
// exported it -- the row says so instead of leaving a blank that reads like
// zero.
// ---------------------------------------------------------------------------

// The driver-info ioctl. It is the only way to ask a network driver what
// firmware it loaded: no sysfs node carries it, because the string comes back
// from the device rather than from the kernel. Unprivileged by design -- this
// is what "ethtool -i" does before it needs any capability.
// The expansion ROM version is a late addition to struct ethtool_drvinfo: the
// 5.1 headers carry it, the 5.0 and 4.6 ones do not, and this app is built
// against all three. Detected rather than pinned to a version macro -- what
// decides is the header in the build target, not the kernel on the device.
// The first overload only exists where the field does; where it does not, the
// second one wins and the row is simply absent.
template <typename T>
static auto eromVersion(const T &d, int) -> decltype(QString::fromLatin1(d.erom_version))
{
    return QString::fromLatin1(d.erom_version).trimmed();
}
template <typename T>
static QString eromVersion(const T &, long)
{
    return QString();
}

static QVariantMap netDriverInfo(const QString &iface)
{
    QVariantMap m;
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return m;
    struct ethtool_drvinfo di;
    memset(&di, 0, sizeof(di));
    di.cmd = ETHTOOL_GDRVINFO;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface.toLatin1().constData(), IFNAMSIZ - 1);
    ifr.ifr_data = reinterpret_cast<char *>(&di);
    const bool ok = ::ioctl(fd, SIOCETHTOOL, &ifr) == 0;
    ::close(fd);
    if (!ok)
        return m;
    auto put = [&m](const char *key, const char *val) {
        const QString s = QString::fromLatin1(val).trimmed();
        if (!s.isEmpty())
            m.insert(QString::fromLatin1(key), s);
    };
    put("driver", di.driver);
    put("driverVersion", di.version);
    put("firmware", di.fw_version);
    put("bus", di.bus_info);
    const QString erom = eromVersion(di, 0);
    if (!erom.isEmpty())
        m.insert(QStringLiteral("rom"), erom);
    return m;
}

// Module taint letters, as the kernel documents them. A module carrying one of
// these is not broken -- most drivers on a phone carry O and E, because a
// vendor kernel is built out of tree and rarely signed -- but it is worth
// naming, because it says what the kernel knows about where the code came from.
static QString taintWords(const QString &flags)
{
    QStringList out;
    for (const QChar c : flags) {
        switch (c.toLatin1()) {
        case 'P': out << QStringLiteral("proprietary"); break;
        case 'O': out << QStringLiteral("out of tree"); break;
        case 'E': out << QStringLiteral("unsigned"); break;
        case 'F': out << QStringLiteral("force loaded"); break;
        case 'C': out << QStringLiteral("staging"); break;
        case 'X': out << QStringLiteral("externally built"); break;
        default: break;
        }
    }
    return out.join(QStringLiteral(", "));
}

// part selects one section, because the pages ask separately and the blob
// inventory walks a few thousand files -- no reason to pay for it when a page
// only wants the USB release numbers. An empty part returns everything.
QVariantMap SysMon::firmwareDetail(const QString &part) const
{
    QVariantMap m;
    const bool all = part.isEmpty();

    // ---- one module at a time -------------------------------------------
    // A module may carry a version string, a source hash, or neither. The
    // Mali driver's release lives here and nowhere else reachable; most others
    // answer with a build hash, which still tells two builds apart.
    if (all || part == QLatin1String("modules")) {
        QVariantList mods;
        const QDir d(QStringLiteral("/sys/module"));
        for (const QString &name : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString base = d.filePath(name) + QLatin1Char('/');
            const QString ver = readTrim(base + QStringLiteral("version"));
            const QString src = readTrim(base + QStringLiteral("srcversion"));
            const QString taint = readTrim(base + QStringLiteral("taint"));
            if (ver.isEmpty() && src.isEmpty() && taint.isEmpty())
                continue;   // built in, or nothing to say
            QVariantMap e;
            e.insert(QStringLiteral("name"), name);
            e.insert(QStringLiteral("version"), ver);
            e.insert(QStringLiteral("srcversion"), src);
            e.insert(QStringLiteral("taint"), taint);
            e.insert(QStringLiteral("taintWords"), taintWords(taint));
            e.insert(QStringLiteral("sizeKb"),
                     readTrim(base + QStringLiteral("coresize")).toDouble() / 1024.0);
            mods.append(e);
        }
        m.insert(QStringLiteral("modules"), mods);
    }

    // ---- network interfaces ---------------------------------------------
    if (all || part == QLatin1String("net")) {
        QVariantList ifs;
        const QDir nd(QStringLiteral("/sys/class/net"));
        for (const QString &n : nd.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QVariantMap e = netDriverInfo(n);
            if (e.isEmpty())
                continue;
            e.insert(QStringLiteral("name"), n);
            ifs.append(e);
        }
        m.insert(QStringLiteral("interfaces"), ifs);
    }

    // ---- USB devices ------------------------------------------------------
    // bcdDevice is the device release number: the closest thing a USB device
    // has to a firmware version, and it is right there in sysfs.
    if (all || part == QLatin1String("usb")) {
        QVariantList devs;
        const QDir ud(QStringLiteral("/sys/bus/usb/devices"));
        for (const QString &n : ud.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString base = ud.filePath(n) + QLatin1Char('/');
            const QString vid = readTrim(base + QStringLiteral("idVendor"));
            if (vid.isEmpty())
                continue;   // an interface, not a device
            QVariantMap e;
            e.insert(QStringLiteral("port"), n);
            e.insert(QStringLiteral("vendorId"), vid);
            e.insert(QStringLiteral("productId"), readTrim(base + QStringLiteral("idProduct")));
            e.insert(QStringLiteral("manufacturer"), readTrim(base + QStringLiteral("manufacturer")));
            e.insert(QStringLiteral("product"), readTrim(base + QStringLiteral("product")));
            e.insert(QStringLiteral("release"), readTrim(base + QStringLiteral("bcdDevice")));
            e.insert(QStringLiteral("usbVersion"), readTrim(base + QStringLiteral("version")));
            e.insert(QStringLiteral("speed"), readTrim(base + QStringLiteral("speed")));
            devs.append(e);
        }
        m.insert(QStringLiteral("usb"), devs);
    }

    // ---- storage controllers ---------------------------------------------
    // UFS and eMMC both keep a firmware revision, under different names.
    if (all || part == QLatin1String("storage")) {
        QVariantList st;
        const QDir bd(QStringLiteral("/sys/class/block"));
        for (const QString &n : bd.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (n.startsWith(QStringLiteral("loop")) || n.startsWith(QStringLiteral("ram"))
                || n.startsWith(QStringLiteral("zram")) || n.startsWith(QStringLiteral("dm-"))
                || n.contains(QLatin1Char('p')) )
                continue;
            const QString base = bd.filePath(n) + QStringLiteral("/device/");
            const QString rev = readTrim(base + QStringLiteral("rev"));
            const QString fw = readTrim(base + QStringLiteral("fwrev"));
            if (rev.isEmpty() && fw.isEmpty())
                continue;
            QVariantMap e;
            e.insert(QStringLiteral("name"), n);
            e.insert(QStringLiteral("vendor"), readTrim(base + QStringLiteral("vendor")));
            e.insert(QStringLiteral("model"), readTrim(base + QStringLiteral("model"))
                     + readTrim(base + QStringLiteral("name")));
            e.insert(QStringLiteral("firmware"), rev.isEmpty() ? fw : rev);
            e.insert(QStringLiteral("hardware"), readTrim(base + QStringLiteral("hwrev")));
            e.insert(QStringLiteral("manufacturerId"), readTrim(base + QStringLiteral("manfid")));
            e.insert(QStringLiteral("oemId"), readTrim(base + QStringLiteral("oemid")));
            e.insert(QStringLiteral("date"), readTrim(base + QStringLiteral("date")));
            st.append(e);
        }
        m.insert(QStringLiteral("storage"), st);
    }

    // ---- input devices ----------------------------------------------------
    // The touch controller, the fingerprint reader and the buttons each report
    // a bus, a vendor, a product and a version through the input core -- the
    // only place several of them are versioned at all.
    if (all || part == QLatin1String("input")) {
        QVariantList inputs;
        QFile f(QStringLiteral("/proc/bus/input/devices"));
        if (f.open(QIODevice::ReadOnly)) {
            QVariantMap cur;
            while (!f.atEnd()) {
                const QString line = QString::fromLatin1(f.readLine()).trimmed();
                if (line.startsWith(QLatin1String("I:"))) {
                    cur.clear();
                    for (const QString &kv : line.mid(2).split(QLatin1Char(' '),
                                                              QString::SkipEmptyParts)) {
                        const int eq = kv.indexOf(QLatin1Char('='));
                        if (eq > 0)
                            cur.insert(kv.left(eq).toLower(), kv.mid(eq + 1));
                    }
                } else if (line.startsWith(QLatin1String("N: Name="))) {
                    cur.insert(QStringLiteral("name"),
                               line.mid(8).remove(QLatin1Char('"')));
                } else if (line.isEmpty() && cur.contains(QStringLiteral("name"))) {
                    inputs.append(cur);
                    cur.clear();
                }
            }
            if (cur.contains(QStringLiteral("name")))
                inputs.append(cur);
        }
        m.insert(QStringLiteral("input"), inputs);
    }

    // ---- the blobs on disk ------------------------------------------------
    // What the kernel would load into a radio or a DSP. The file names carry
    // the chip family, and the dates say when the vendor last touched them.
    if (all || part == QLatin1String("blobs")) {
        QVariantList blobs;
        qint64 total = 0;
        int count = 0;
        for (const QString &root : { QStringLiteral("/lib/firmware"),
                                     QStringLiteral("/vendor/firmware"),
                                     QStringLiteral("/etc/firmware"),
                                     QStringLiteral("/odm/firmware"),
                                     QStringLiteral("/vendor/firmware_mnt/image") }) {
            if (!QFileInfo::exists(root))
                continue;
            QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext() && count < 3000) {
                const QFileInfo fi(it.next());
                ++count;
                total += fi.size();
                if (blobs.size() >= 800)
                    continue;
                QVariantMap e;
                e.insert(QStringLiteral("name"), fi.absoluteFilePath().mid(root.size() + 1));
                e.insert(QStringLiteral("root"), root);
                e.insert(QStringLiteral("bytes"), (double)fi.size());
                e.insert(QStringLiteral("date"), fi.lastModified().toString(Qt::ISODate));
                blobs.append(e);
            }
        }
        std::sort(blobs.begin(), blobs.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("name")).toString()
                 < b.toMap().value(QStringLiteral("name")).toString();
        });
        m.insert(QStringLiteral("blobs"), blobs);
        m.insert(QStringLiteral("blobCount"), count);
        m.insert(QStringLiteral("blobBytes"), (double)total);
    }

    // ---- how hard the kernel is to attack from here ----------------------
    // Switches an ordinary process may read, each of which decides whether
    // some class of local attack is available at all. They are shown as they
    // stand, with the safer value named beside them; none of this is a verdict
    // about the device.
    if (all || part == QLatin1String("system")) {
        QVariantList hard;
        // Each switch carries the direction that makes it stricter, because
        // "different from the recommendation" is not the same as "weaker".
        // kptr_restrict=2 hides kernel pointers from everyone and 1 only from
        // unprivileged readers; perf_event_paranoid=3 forbids what 2 allows.
        // Reporting either of those as something to improve would be telling
        // the reader to loosen a setting that is already tighter than asked.
        // cmp: '>' = at least this, '<' = at most this, 0 = no recommendation.
        const struct { const char *path, *label, *safe; char cmp; } sw[] = {
            { "/proc/sys/kernel/kptr_restrict",          "Kernel pointers hidden",      "1", '>' },
            { "/proc/sys/kernel/dmesg_restrict",         "Kernel log restricted",       "1", '>' },
            { "/proc/sys/kernel/perf_event_paranoid",    "Performance counters",        "2", '>' },
            { "/proc/sys/kernel/yama/ptrace_scope",      "Debugging other processes",   "1", '>' },
            { "/proc/sys/kernel/randomize_va_space",     "Address space randomised",    "2", '>' },
            { "/proc/sys/kernel/unprivileged_bpf_disabled", "Unprivileged BPF blocked", "1", '>' },
            { "/proc/sys/user/max_user_namespaces",      "User namespaces",             "",  0 },
            { "/proc/sys/fs/protected_symlinks",         "Symlink protection",          "1", '>' },
            { "/proc/sys/fs/protected_hardlinks",        "Hardlink protection",         "1", '>' },
            { "/proc/sys/fs/suid_dumpable",              "Core dumps of setuid programs", "0", '<' },
            { nullptr, nullptr, nullptr, 0 }
        };
        for (int i = 0; sw[i].path; ++i) {
            const QString v = readTrim(QLatin1String(sw[i].path));
            if (v.isEmpty())
                continue;
            QVariantMap e;
            e.insert(QStringLiteral("label"), QString::fromLatin1(sw[i].label));
            e.insert(QStringLiteral("value"), v);
            e.insert(QStringLiteral("safe"), QString::fromLatin1(sw[i].safe));
            // The leaf name, not the whole path: the directory is the same for
            // nearly all of them and the column is narrow.
            e.insert(QStringLiteral("path"),
                     QString::fromLatin1(sw[i].path).section(QLatin1Char('/'), -1));
            if (sw[i].cmp) {
                const double val = v.toDouble();
                const double safe = QString::fromLatin1(sw[i].safe).toDouble();
                e.insert(QStringLiteral("weaker"),
                         sw[i].cmp == '>' ? val < safe : val > safe);
            }
            hard.append(e);
        }
        const QString lock = readTrim(QStringLiteral("/sys/kernel/security/lockdown"));
        if (!lock.isEmpty())
            m.insert(QStringLiteral("lockdown"), lock);
        const QString se = readTrim(QStringLiteral("/sys/fs/selinux/enforce"));
        if (!se.isEmpty())
            m.insert(QStringLiteral("selinux"), se == QLatin1String("1")
                     ? QStringLiteral("enforcing") : QStringLiteral("permissive"));
        m.insert(QStringLiteral("hardening"), hard);
        m.insert(QStringLiteral("tainted"), readTrim(QStringLiteral("/proc/sys/kernel/tainted")));
    }

    // ---- the bootloader's own account ------------------------------------
    // The kernel command line is where the bootloader records what it was and
    // what it verified. Only the version-bearing keys are lifted out.
    if (all || part == QLatin1String("system")) {
        QVariantList boot;
        const QString cmd = readTrim(QStringLiteral("/proc/cmdline"));
        for (const QString &tok : cmd.split(QLatin1Char(' '), QString::SkipEmptyParts)) {
            if (!tok.startsWith(QLatin1String("androidboot.")))
                continue;
            const int eq = tok.indexOf(QLatin1Char('='));
            if (eq < 0)
                continue;
            const QString k = tok.mid(12, eq - 12);
            if (k != QLatin1String("bootloader") && k != QLatin1String("baseband")
                && k != QLatin1String("hardware") && k != QLatin1String("verifiedbootstate")
                && k != QLatin1String("veritymode") && k != QLatin1String("vbmeta.avb_version")
                && k != QLatin1String("boot_devices") && k != QLatin1String("product.hardware.sku")
                && k != QLatin1String("dtbo_idx") && k != QLatin1String("serialno"))
                continue;
            QVariantMap e;
            e.insert(QStringLiteral("key"), k);
            e.insert(QStringLiteral("value"), tok.mid(eq + 1));
            boot.append(e);
        }
        m.insert(QStringLiteral("boot"), boot);
    }

    return m;
}

QVariantList SysMon::kernelModules() const
{
    QVariantList out;
    QFile f(QStringLiteral("/proc/modules"));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    while (!f.atEnd()) {
        const QStringList c =
            QString::fromLatin1(f.readLine()).trimmed().split(QLatin1Char(' '));
        if (c.size() < 4)
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("name"), c.at(0));
        m.insert(QStringLiteral("sizeKb"), c.at(1).toDouble() / 1024.0);
        m.insert(QStringLiteral("used"), c.at(2).toInt());
        // "-" where nothing depends on it; otherwise a comma-separated list
        // with a trailing comma the kernel leaves in.
        QString by = c.at(3);
        if (by == QLatin1String("-"))
            by.clear();
        while (by.endsWith(QLatin1Char(',')))
            by.chop(1);
        m.insert(QStringLiteral("usedBy"), by.replace(QLatin1Char(','), QStringLiteral(", ")));
        out.append(m);
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString()
             < b.toMap().value(QStringLiteral("name")).toString();
    });
    return out;
}

// ---------------------------------------------------------------------------
// Catalogue figures for the device itself, as opposed to its SoC.
//
// A phone carries parts the kernel never names. The camera sensors sit behind
// a vendor HAL that Sailfish does not run; the panel reports a resolution and
// not a part; the memory package is a board decision the datasheet of the SoC
// cannot know. Where the maker of the device has stated such a figure, it is
// carried here with the maker named -- and it stays beside the hardware it
// belongs to, so the camera parts are on the camera page and nowhere else.
//
// Every row says where it came from. "the maker" is the device vendor's own
// statement, "observed" is something read off a device of this model, and a
// figure nobody published is absent rather than guessed.
// ---------------------------------------------------------------------------

struct DevSpec { const char *part; const char *key; const char *value; const char *source; };

QVariantMap SysMon::deviceCatalogue(const QString &part) const
{
    QVariantMap m;
    const QString model = hwDevice();
    if (model.isEmpty())
        return m;

    QVariantList rows;
    QString name;

    if (model == QLatin1String("jp2601")) {
        name = QStringLiteral("Jolla Phone (2026)");
        static const DevSpec jp2601[] = {
            { "camera", "Rear, main", "Sony IMX766, 50 MP, autofocus", "the maker" },
            { "camera", "Rear, ultra-wide", "Sony IMX214, 13 MP, autofocus", "the maker" },
            { "camera", "Front", "Sony IMX616, 32 MP, fixed focus", "the maker" },
            { "camera", "Flash", "Awinic AW36515, two channels", "observed" },
            { "display", "Panel", "6.36 inch AMOLED, 1032 × 2272", "observed" },
            { "memory", "Package", "LPDDR4X, single package shared with the storage, SK hynix", "the maker" },
            { "storage", "Package", "UFS 2.2, single package shared with the memory, SK hynix", "the maker" },
            { "battery", "Cell", "5450 mAh, user-replaceable, three-pin (plus, minus, thermistor)", "the maker" },
            { "battery", "Charging", "45 W over USB Power Delivery with programmable supply", "the maker" },
            { "audio", "Codec", "MT6369, inside the PMIC rather than a separate part", "observed" },
            { "radio", "LTE bands", "FDD 1–8, 12, 17–20, 25, 26, 28AB, 66  ·  TDD 34, 38–41", "the maker" },
            { "radio", "5G bands", "n1, n2, n3, n5, n7, n8, n12, n20, n26, n28, n38, n40, n41, n66, n77, n78 — sub-6 only", "the maker" },
            { "radio", "Wireless", "Wi-Fi 6, Bluetooth 5.4, NFC; combined radio with an A-die 6631 companion", "the maker" },
            { "body", "Size and weight", "157 × 74 × 9.7 mm, 208 g", "the maker" },
            { "body", "Fingerprint reader", "in the power key", "the maker" },
            { "body", "Assembly", "Salo, Finland", "the maker" },
            { "expansion", "Accessory connector", "seven pogo pins at 2.90 mm: 5 V in, 5 V out, ground, I3C clock and data, identify, interrupt", "the maker" },
            { "expansion", "Accessory bus", "I3C in single-data-rate mode, up to 12.5 Mbit/s; the bus runs at 1.8 V in the SoC and is shifted to 3.3 V at the pins", "the maker" },
            { "expansion", "Accessory identity", "an EEPROM at I2C address 0x50, starting with the four bytes 4A 54 4F 48 and a CRC-32 over a CBOR record", "the maker" },
            { nullptr, nullptr, nullptr, nullptr }
        };
        for (const DevSpec *p = jp2601; p->key; ++p) {
            if (!part.isEmpty() && part != QLatin1String(p->part))
                continue;
            QVariantMap r;
            r.insert(QStringLiteral("part"), QString::fromUtf8(p->part));
            r.insert(QStringLiteral("k"), QString::fromUtf8(p->key));
            r.insert(QStringLiteral("v"), QString::fromUtf8(p->value));
            r.insert(QStringLiteral("src"), QString::fromUtf8(p->source));
            rows.append(r);
        }
    }

    if (rows.isEmpty())
        return m;

    // The one figure worth setting against the device in front of us. The cell
    // is sold as one capacity and the gauge reports another; both are on the
    // battery page already, and saying so is better than letting a reader
    // discover the gap and assume one of them is a bug in this app.
    if (part.isEmpty() || part == QLatin1String("battery")) {
        const double design =
            readTrim(QStringLiteral("/sys/class/power_supply/battery/charge_full_design"))
            .toDouble() / 1000.0;
        if (design > 0)
            m.insert(QStringLiteral("batteryDesignMah"), design);
        if (model == QLatin1String("jp2601"))
            m.insert(QStringLiteral("batteryRatedMah"), 5450.0);
    }

    m.insert(QStringLiteral("device"), name);
    m.insert(QStringLiteral("model"), model);
    m.insert(QStringLiteral("rows"), rows);
    return m;
}

QVariantMap SysMon::socCatalogue() const
{
    QVariantMap m;
    QString compat;
    {
        QFile f(QStringLiteral("/proc/device-tree/compatible"));
        if (f.open(QIODevice::ReadOnly))
            compat = QString::fromLatin1(f.readAll().replace('\0', ' ')).trimmed();
    }
    if (compat.isEmpty())
        return m;

    QVariantList rows;
    QString part, name;

    if (compat.contains(QLatin1String("MT6858"), Qt::CaseInsensitive)) {
        part = QStringLiteral("MT6858");
        name = QStringLiteral("MediaTek Dimensity 7100");
        static const SocSpec mt6858[] = {
            { "Marketing name", "Dimensity 7100", "third party" },
            { "Process", "6 nm", "vendor" },
            { "Foundry", "TSMC", "third party" },
            { "CPU", "4× Cortex-A78 up to 2.4 GHz  +  4× Cortex-A55 up to 2.0 GHz", "vendor" },
            { "Caches, DSU", "", "not published" },
            { "GPU", "Arm Mali-G610 MC2 (2 shader cores, Valhall 3rd gen)", "vendor" },
            { "GPU clock", "1000 MHz", "third party" },
            { "Memory", "LPDDR5 up to 5500 Mbps, or LPDDR4X up to 4266 Mbps", "vendor" },
            { "Storage", "UFS 3.1", "vendor" },
            { "Camera", "up to 200 MP; HDR video (DCG/DAG), multi-frame noise reduction,"
                        " hardware face detection", "vendor" },
            { "ISP name, concurrent sensors", "", "not published" },
            { "NPU / APU", "", "not published" },
            { "Modem", "5G 3GPP Release 16, sub-6 GHz only (no mmWave), SA and NSA,"
                       " up to 3.3 Gbit/s down, NR DL 2CC / 140 MHz, 256QAM, VoNR,"
                       " dual 5G SIM", "vendor" },
            { "LTE category", "", "not published" },
            { "Wi-Fi", "Wi-Fi 6 (802.11a/b/g/n/ac/ax), 1T1R", "vendor" },
            { "Bluetooth", "5.4, including Long Range", "vendor" },
            { "Satellite navigation", "dual band — GPS L1CA+L5, BeiDou B1I+B2a, GLONASS L1OF,"
                                      " Galileo E1+E5a, QZSS L1CB, NavIC L5+N1", "vendor" },
            { "Display controller", "up to 1200 × 2600 at up to 120 Hz, 10 bit,"
                                    " HDR10 / HDR10+ / HLG / HDR Vivid", "vendor" },
            { "Video codecs", "", "not published" },
            { "Charging", "45 W integrated, UFCS", "vendor" },
            { "Announced", "31 December 2025", "third party" },
            { nullptr, nullptr, nullptr }
        };
        for (const SocSpec *p = mt6858; p->key; ++p) {
            QVariantMap r;
            r.insert(QStringLiteral("k"), QString::fromUtf8(p->key));
            r.insert(QStringLiteral("v"), QString::fromUtf8(p->value));
            r.insert(QStringLiteral("src"), QString::fromUtf8(p->source));
            rows.append(r);
        }
        m.insert(QStringLiteral("note"),
                 QStringLiteral("MediaTek does not publish its MT part numbers next to the "
                                "marketing name, so the step from mediatek,MT6858 to "
                                "Dimensity 7100 rests on third-party databases — several of "
                                "them, none contradicting the others, and all consistent with "
                                "the core layout this device reports."));
    }

    if (rows.isEmpty())
        return m;

    // The catalogue lists two memory standards for this part because the SoC
    // accepts either; which one is soldered to this board is not in any
    // datasheet. The DRAM governor's own ceiling settles it.
    QVariantMap dram;
    dramGrade(dram);
    const double dramHz =
        readTrim(QStringLiteral("/sys/class/devfreq/mtk-dvfsrc-devfreq/max_freq")).toDouble();
    QString fitted;
    if (dram.contains(QStringLiteral("dramType"))) {
        // The controller says it outright; no inference needed.
        fitted = dram.value(QStringLiteral("dramType")).toString();
        if (dram.contains(QStringLiteral("dramRate")))
            fitted += QStringLiteral(", %1 MB/s")
                      .arg(dram.value(QStringLiteral("dramRate")).toDouble(), 0, 'f', 0);
    } else if (dramHz > 0) {
        // No controller node: the DRAM governor's ceiling still narrows it to
        // one of the grades the catalogue lists, which is worth saying as long
        // as it is called what it is -- a ceiling, not a nameplate.
        const double mts = dramHz / 1e6;
        if (mts >= 800 && mts <= 20000)
            fitted = QStringLiteral("%1 MT/s ceiling").arg(mts, 0, 'f', 0);
    }
    if (!fitted.isEmpty()) {
        QVariantMap r;
        r.insert(QStringLiteral("k"), QStringLiteral("Memory fitted here"));
        r.insert(QStringLiteral("v"), fitted);
        r.insert(QStringLiteral("src"), QStringLiteral("this device"));
        rows.append(r);
    }

    m.insert(QStringLiteral("part"), part);
    m.insert(QStringLiteral("name"), name);
    m.insert(QStringLiteral("rows"), rows);
    return m;
}

QVariantList SysMon::deviceTreeParts(const QString &filter) const
{
    QVariantList out;
    const QString root = QStringLiteral("/sys/firmware/devicetree/base");
    if (!QFileInfo::exists(root))
        return out;
    const QStringList keys = filter.split(QLatin1Char(' '), QString::SkipEmptyParts);

    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && out.size() < 4000) {
        const QString dir = it.next();
        const QString comp = dtString(dir + QStringLiteral("/compatible"));
        if (comp.isEmpty())
            continue;
        const QString name = QFileInfo(dir).fileName();
        if (!keys.isEmpty()) {
            bool hit = false;
            for (const QString &k : keys)
                if (name.contains(k, Qt::CaseInsensitive) || comp.contains(k, Qt::CaseInsensitive)) {
                    hit = true;
                    break;
                }
            if (!hit)
                continue;
        }
        QVariantMap p;
        p.insert(QStringLiteral("node"), name);
        p.insert(QStringLiteral("path"), dir.mid(root.size()));
        p.insert(QStringLiteral("compatible"), comp);
        // A node the board file switched off: the silicon exists in the SoC,
        // this device does not wire it up.
        const QString st = dtString(dir + QStringLiteral("/status"));
        if (!st.isEmpty())
            p.insert(QStringLiteral("status"), st);
        out.append(p);
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("path")).toString()
             < b.toMap().value(QStringLiteral("path")).toString();
    });
    return out;
}

// A bus with one directory per attached device. The device's own name or
// modalias is the part the driver bound to, so this enumerates the chips on
// the board rather than the classes they were sorted into.
static void addRawBus(QVariantList &out, const QString &title, const QString &busDir,
                      const QString &attr)
{
    const QDir d(busDir);
    QVariantList attrs;
    for (const QString &e : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (attrs.size() >= 500)
            break;
        const QString v = readTrim(d.filePath(e) + QLatin1Char('/') + attr);
        if (v.isEmpty())
            continue;
        QVariantMap a;
        a.insert(QStringLiteral("name"), e);
        a.insert(QStringLiteral("value"), v);
        attrs.append(a);
    }
    if (attrs.isEmpty())
        return;
    QVariantMap g;
    g.insert(QStringLiteral("title"), title);
    g.insert(QStringLiteral("path"), busDir);
    g.insert(QStringLiteral("attrs"), attrs);
    out.append(g);
}

QVariantList SysMon::rawNodes(const QString &topic) const
{
    QVariantList out;

    if (topic == QLatin1String("cpu")) {
        const QDir pol(QStringLiteral("/sys/devices/system/cpu/cpufreq"));
        for (const QString &p : pol.entryList(QStringList() << QStringLiteral("policy*"),
                                              QDir::Dirs, QDir::Name))
            addRawDir(out, QStringLiteral("cpufreq/") + p, pol.filePath(p));
        for (int i = 0; i < 32; ++i) {
            const QString base = QStringLiteral("/sys/devices/system/cpu/cpu%1/").arg(i);
            if (!QFileInfo::exists(base))
                break;
            addRawDir(out, QStringLiteral("cpu%1/topology").arg(i), base + QStringLiteral("topology"));
        }
        addRawDir(out, QStringLiteral("cpu/vulnerabilities"),
                  QStringLiteral("/sys/devices/system/cpu/vulnerabilities"));
        addRawFile(out, QStringLiteral("/proc/cpuinfo"), QStringLiteral("/proc/cpuinfo"));
        addRawFile(out, QStringLiteral("/proc/pressure/cpu"), QStringLiteral("/proc/pressure/cpu"));
        addRawModules(out, QStringList() << QStringLiteral("cm_mgr") << QStringLiteral("cpufreq")
                                         << QStringLiteral("task_turbo") << QStringLiteral("scheduler")
                                         << QStringLiteral("core_ctl"));
    } else if (topic == QLatin1String("gfx")) {
        addRawDir(out, QStringLiteral("mali0"), QStringLiteral("/sys/class/misc/mali0/device"));
        addRawClass(out, QStringLiteral("devfreq"));
        addRawDir(out, QStringLiteral("kgsl-3d0"), QStringLiteral("/sys/class/kgsl/kgsl-3d0"));
        addRawDir(out, QStringLiteral("drm/card0"), QStringLiteral("/sys/class/drm/card0/device"));
        addRawModules(out, QStringList() << QStringLiteral("mali") << QStringLiteral("gpufreq")
                                         << QStringLiteral("ged") << QStringLiteral("kgsl")
                                         << QStringLiteral("drm"));
    } else if (topic == QLatin1String("camera")) {
        // MediaTek creates one procfs file per sensor slot, and they look like
        // an inventory without being one: all eight print the same single
        // global, which stays empty until a privileged debug write fills it.
        // Listed because they exist and cost nothing, not because they answer.
        const QDir drv(QStringLiteral("/proc/driver"));
        for (const QString &f : drv.entryList(QStringList() << QStringLiteral("camsensor*"),
                                              QDir::Files, QDir::Name))
            addRawFile(out, QStringLiteral("/proc/driver/") + f, drv.filePath(f));
        addRawClass(out, QStringLiteral("video4linux"));
        // The flash and its driver, named without touching a register: the
        // class directory and the I2C client both carry the part name, while
        // the attributes beside them do not survive being read.
        addRawBus(out, QStringLiteral("leds"), QStringLiteral("/sys/class/leds"),
                  QStringLiteral("device/name"));
        addRawBus(out, QStringLiteral("leds — device tree"), QStringLiteral("/sys/class/leds"),
                  QStringLiteral("device/of_node/compatible"));
        addRawModules(out, QStringList() << QStringLiteral("imgsensor") << QStringLiteral("camera")
                                         << QStringLiteral("seninf") << QStringLiteral("flashlight"));
    } else if (topic == QLatin1String("net")) {
        addRawClass(out, QStringLiteral("net"));
        addRawClass(out, QStringLiteral("net"), QStringLiteral("statistics"));
        addRawFile(out, QStringLiteral("/proc/net/wireless"), QStringLiteral("/proc/net/wireless"));
        addRawModules(out, QStringList() << QStringLiteral("wlan") << QStringLiteral("cfg80211")
                                         << QStringLiteral("mac80211") << QStringLiteral("conninfra")
                                         << QStringLiteral("connfem"));
    } else if (topic == QLatin1String("bt")) {
        addRawClass(out, QStringLiteral("bluetooth"));
        addRawModules(out, QStringList() << QStringLiteral("bluetooth") << QStringLiteral("btmtk")
                                         << QStringLiteral("bt_drv") << QStringLiteral("hci"));
    } else if (topic == QLatin1String("storage")) {
        const QDir blk(QStringLiteral("/sys/class/block"));
        for (const QString &b : blk.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (b.startsWith(QStringLiteral("loop")) || b.startsWith(QStringLiteral("ram"))
                || b.startsWith(QStringLiteral("zram")) || b.startsWith(QStringLiteral("dm-")))
                continue;
            addRawDir(out, QStringLiteral("block/") + b, blk.filePath(b) + QStringLiteral("/device"));
        }
        addRawClass(out, QStringLiteral("scsi_device"), QStringLiteral("device"));
        addRawFile(out, QStringLiteral("/proc/pressure/io"), QStringLiteral("/proc/pressure/io"));
        addRawModules(out, QStringList() << QStringLiteral("ufs") << QStringLiteral("mmc")
                                         << QStringLiteral("blocktag"));
    } else if (topic == QLatin1String("mem")) {
        addRawFile(out, QStringLiteral("/proc/meminfo"), QStringLiteral("/proc/meminfo"));
        addRawFile(out, QStringLiteral("/proc/vmstat"), QStringLiteral("/proc/vmstat"));
        addRawFile(out, QStringLiteral("/proc/pressure/memory"), QStringLiteral("/proc/pressure/memory"));
        addRawDir(out, QStringLiteral("mm/transparent_hugepage"),
                  QStringLiteral("/sys/kernel/mm/transparent_hugepage"));
        addRawDir(out, QStringLiteral("mm/ksm"), QStringLiteral("/sys/kernel/mm/ksm"));
        // The DRAM controller names the memory grade outright, which no other
        // node on the device does.
        addRawDir(out, QStringLiteral("dramc_drv"),
                  QStringLiteral("/sys/bus/platform/drivers/dramc_drv"));
        addRawDir(out, QStringLiteral("helio-dvfsrc"),
                  QStringLiteral("/sys/kernel/helio-dvfsrc"));
        addRawModules(out, QStringList() << QStringLiteral("dramc") << QStringLiteral("emi")
                                         << QStringLiteral("zram"));
    } else if (topic == QLatin1String("audio")) {
        addRawClass(out, QStringLiteral("sound"));
        addRawFile(out, QStringLiteral("/proc/asound/cards"), QStringLiteral("/proc/asound/cards"));
        addRawFile(out, QStringLiteral("/proc/asound/pcm"), QStringLiteral("/proc/asound/pcm"));
        addRawModules(out, QStringList() << QStringLiteral("snd_soc") << QStringLiteral("spk_amp")
                                         << QStringLiteral("audiodsp"));
    } else if (topic == QLatin1String("usb")) {
        addRawClass(out, QStringLiteral("typec"));
        addRawClass(out, QStringLiteral("udc"));
        addRawClass(out, QStringLiteral("usb_role"));
        addRawClass(out, QStringLiteral("usbpd"));
        addRawModules(out, QStringList() << QStringLiteral("tcpc") << QStringLiteral("usb")
                                         << QStringLiteral("extcon"));
    } else if (topic == QLatin1String("modem")) {
        addRawClass(out, QStringLiteral("ccci_node"));
        addRawModules(out, QStringList() << QStringLiteral("ccci") << QStringLiteral("md_power")
                                         << QStringLiteral("modem"));
    } else if (topic == QLatin1String("thermal")) {
        addRawClass(out, QStringLiteral("thermal"));
        // MediaTek's thermal interface hangs off the kernel object rather than
        // a class, so no enumeration finds it. It is the only place that says
        // whether the SoC is being limited right now.
        addRawDir(out, QStringLiteral("/sys/kernel/thermal"),
                  QStringLiteral("/sys/kernel/thermal"));
        addRawDir(out, QStringLiteral("/sys/kernel/thermal_trace"),
                  QStringLiteral("/sys/kernel/thermal_trace"));
        addRawDir(out, QStringLiteral("/sys/kernel/charger_cooler"),
                  QStringLiteral("/sys/kernel/charger_cooler"));
        addRawModules(out, QStringList() << QStringLiteral("thermal") << QStringLiteral("throttling")
                                         << QStringLiteral("cooler") << QStringLiteral("cooling"));
    } else if (topic == QLatin1String("battery")) {
        addRawDir(out, QStringLiteral("platform/charger"),
                  QStringLiteral("/sys/devices/platform/charger"));
        addRawDir(out, QStringLiteral("/proc/mtk_battery_cmd"),
                  QStringLiteral("/proc/mtk_battery_cmd"));
        addRawModules(out, QStringList() << QStringLiteral("charg") << QStringLiteral("gauge")
                                         << QStringLiteral("battery") << QStringLiteral("ufcs")
                                         << QStringLiteral("adapter"));
    } else if (topic == QLatin1String("device")) {
        // The board itself: what the bootloader was told, which chips sit on
        // which bus, and which of them asked the kernel for an interrupt.
        addRawFile(out, QStringLiteral("/proc/cmdline"), QStringLiteral("/proc/cmdline"));
        addRawFile(out, QStringLiteral("/proc/bus/input/devices"),
                   QStringLiteral("/proc/bus/input/devices"));
        addRawBus(out, QStringLiteral("i2c devices"), QStringLiteral("/sys/bus/i2c/devices"),
                  QStringLiteral("name"));
        addRawBus(out, QStringLiteral("spi devices"), QStringLiteral("/sys/bus/spi/devices"),
                  QStringLiteral("modalias"));
        addRawBus(out, QStringLiteral("platform devices"),
                  QStringLiteral("/sys/bus/platform/devices"), QStringLiteral("modalias"));
        addRawFile(out, QStringLiteral("/proc/interrupts"), QStringLiteral("/proc/interrupts"));
        addRawFile(out, QStringLiteral("/proc/devices"), QStringLiteral("/proc/devices"));
        addRawFile(out, QStringLiteral("/proc/bootprof"), QStringLiteral("/proc/bootprof"));
        addRawFile(out, QStringLiteral("/proc/misc"), QStringLiteral("/proc/misc"));
        addRawDir(out, QStringLiteral("soc0"), QStringLiteral("/sys/devices/soc0"));
    } else if (topic == QLatin1String("sensors")) {
        addRawClass(out, QStringLiteral("sensors"));
        addRawClass(out, QStringLiteral("iio:device0"));
        const QDir iio(QStringLiteral("/sys/bus/iio/devices"));
        for (const QString &e : iio.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            addRawDir(out, QStringLiteral("iio/") + e, iio.filePath(e));
        addRawModules(out, QStringList() << QStringLiteral("sensor") << QStringLiteral("accel")
                                         << QStringLiteral("gyro") << QStringLiteral("als"));
    }
    return out;
}

// The memory grade, from the DRAM controller rather than from a datasheet.
// A SoC usually accepts two or three LPDDR generations and the device tree
// does not say which one the board carries; this driver does, as an enum of
// its own that it also prints in words nowhere. The mapping is the driver's.
static void dramGrade(QVariantMap &m)
{
    const QString dc = QStringLiteral("/sys/bus/platform/drivers/dramc_drv/");
    const QString dt = readTrim(dc + QStringLiteral("dram_type"));
    if (!dt.isEmpty()) {
        bool ok = false;
        const int id = dt.section(QLatin1Char('='), 1).trimmed().toInt(&ok);
        QString name;
        if (ok) {
            switch (id) {
            case 5: name = QStringLiteral("LPDDR4"); break;
            case 6: name = QStringLiteral("LPDDR4X"); break;
            case 7: name = QStringLiteral("LPDDR4P"); break;
            case 8: name = QStringLiteral("LPDDR5"); break;
            case 9: name = QStringLiteral("LPDDR5X"); break;
            default: break;
            }
        }
        if (!name.isEmpty())
            m.insert(QStringLiteral("dramType"), name);
        m.insert(QStringLiteral("dramTypeRaw"), dt);
    }
    const QString dr = readTrim(dc + QStringLiteral("dram_data_rate"));
    if (!dr.isEmpty()) {
        const double rate = dr.section(QLatin1Char('='), 1).trimmed().toDouble();
        // This is what the memory runs at this second -- the governor drops it
        // to a low step when nothing is asking. The nameplate is the ceiling
        // below, and showing one without the other invites the reader to take
        // an idle figure for the rating.
        if (rate >= 100 && rate <= 20000)
            m.insert(QStringLiteral("dramRate"), rate);
        else if (rate > 0)
            m.insert(QStringLiteral("dramRateRaw"), rate);
    }
    const double ceil =
        readTrim(QStringLiteral("/sys/class/devfreq/mtk-dvfsrc-devfreq/max_freq")).toDouble();
    if (ceil > 0 && ceil / 1e6 >= 800 && ceil / 1e6 <= 20000)
        m.insert(QStringLiteral("dramRateMax"), ceil / 1e6);
    const QString mr = readTrim(dc + QStringLiteral("mr"));
    if (!mr.isEmpty())
        m.insert(QStringLiteral("dramModeRegisters"), mr);
}

QVariantMap SysMon::memoryDetail() const
{
    QVariantMap m;
    dramGrade(m);
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly))
        return m;
    QVariantList rows;
    for (const QByteArray &line : f.readAll().split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QString key = QString::fromLatin1(line.left(colon).trimmed());
        const QByteArray val = line.mid(colon + 1).trimmed();
        // value is "12345 kB" or a bare number
        const qulonglong kb = val.split(' ').value(0).toULongLong();
        QVariantMap r;
        r.insert(QStringLiteral("key"), key);
        r.insert(QStringLiteral("bytes"), (double)kb * 1024.0);
        rows.append(r);
    }
    m.insert(QStringLiteral("rows"), rows);

    // DDR type from the bootloader-populated device-tree property (big-endian).
    // The LPDDR manufacturer (JEDEC MR5) is read into SMEM by the bootloader but
    // is not surfaced to userspace on this platform.
    QFile dt(QStringLiteral("/sys/firmware/devicetree/base/memory/ddr_device_type"));
    if (dt.open(QIODevice::ReadOnly)) {
        const QByteArray b = dt.read(4);
        if (b.size() == 4) {
            const quint32 code = (quint8(b[0]) << 24) | (quint8(b[1]) << 16)
                               | (quint8(b[2]) << 8) | quint8(b[3]);
            m.insert(QStringLiteral("ddrTypeCode"), code);
            static const QHash<quint32, QString> names = {
                {0, QStringLiteral("LPDDR1")}, {1, QStringLiteral("LPDDR2")},
                {2, QStringLiteral("PCDDR2")}, {3, QStringLiteral("PCDDR3")},
                {4, QStringLiteral("LPDDR3")}, {6, QStringLiteral("LPDDR4")},
                {7, QStringLiteral("LPDDR4X")}, {8, QStringLiteral("LPDDR5")},
                {9, QStringLiteral("LPDDR5X")} };
            const QString nm = names.value(code);
            if (!nm.isEmpty())
                m.insert(QStringLiteral("ddrType"), nm);
        }
    }
    // physical memory regions the kernel sees (#address-cells=2, #size-cells=2
    // => 16 bytes/entry, big-endian). This is the address map, not the die layout.
    QFile reg(QStringLiteral("/sys/firmware/devicetree/base/memory/reg"));
    if (reg.open(QIODevice::ReadOnly)) {
        const QByteArray r = reg.readAll();
        QVariantList regions;
        auto be64 = [](const QByteArray &d, int o) {
            quint64 v = 0;
            for (int i = 0; i < 8; ++i) v = (v << 8) | quint8(d[o + i]);
            return v;
        };
        for (int o = 0; o + 16 <= r.size(); o += 16) {
            QVariantMap e;
            e.insert(QStringLiteral("base"), (double)be64(r, o));
            e.insert(QStringLiteral("size"), (double)be64(r, o + 8));
            regions.append(e);
        }
        if (!regions.isEmpty())
            m.insert(QStringLiteral("regions"), regions);
    }
    return m;
}

QVariantMap SysMon::cpuDetail() const
{
    QVariantMap m;
    // ARM implementer / part -> readable core name
    auto coreName = [](const QString &impl, const QString &part) -> QString {
        const int p = part.toInt(nullptr, 16);
        if (impl == QLatin1String("0x41")) { // ARM
            switch (p) {
            case 0xd03: return QStringLiteral("Cortex-A53");
            case 0xd05: return QStringLiteral("Cortex-A55");
            case 0xd07: return QStringLiteral("Cortex-A57");
            case 0xd08: return QStringLiteral("Cortex-A72");
            case 0xd09: return QStringLiteral("Cortex-A73");
            case 0xd0a: return QStringLiteral("Cortex-A75");
            case 0xd0b: return QStringLiteral("Cortex-A76");
            case 0xd0d: return QStringLiteral("Cortex-A77");
            case 0xd41: return QStringLiteral("Cortex-A78");
            case 0xd44: return QStringLiteral("Cortex-X1");
            case 0xd46: return QStringLiteral("Cortex-A510");
            case 0xd47: return QStringLiteral("Cortex-A710");
            case 0xd48: return QStringLiteral("Cortex-X2");
            case 0xd4d: return QStringLiteral("Cortex-A715");
            default: return QStringLiteral("ARM part 0x%1").arg(p, 0, 16);
            }
        }
        if (impl == QLatin1String("0x51")) return QStringLiteral("Qualcomm Kryo");
        return QString();
    };

    QFile f(QStringLiteral("/proc/cpuinfo"));
    if (!f.open(QIODevice::ReadOnly))
        return m;
    QVariantList cores;
    QVariantMap cur;
    QString flags, impl, part, variant, arch, hw;
    for (const QByteArray &lineB : f.readAll().split('\n')) {
        const QString line = QString::fromLatin1(lineB);
        const int colon = line.indexOf(':');
        if (colon < 0) {
            if (!cur.isEmpty()) { cores.append(cur); cur.clear(); }
            continue;
        }
        const QString k = line.left(colon).trimmed();
        const QString v = line.mid(colon + 1).trimmed();
        if (k == QLatin1String("processor")) cur.insert(QStringLiteral("id"), v.toInt());
        else if (k == QLatin1String("CPU implementer")) { impl = v; cur.insert(QStringLiteral("impl"), v); }
        else if (k == QLatin1String("CPU part")) { part = v; cur.insert(QStringLiteral("part"), v); }
        else if (k == QLatin1String("CPU variant")) variant = v;
        else if (k == QLatin1String("CPU architecture")) arch = v;
        else if (k == QLatin1String("Features")) flags = v;
        else if (k == QLatin1String("Hardware")) hw = v;
        else if (k == QLatin1String("model name")) cur.insert(QStringLiteral("model"), v);
        if (k == QLatin1String("CPU part"))
            cur.insert(QStringLiteral("name"), coreName(impl, v));
    }
    if (!cur.isEmpty()) cores.append(cur);

    m.insert(QStringLiteral("cores"), cores);
    m.insert(QStringLiteral("count"), cores.size());
    m.insert(QStringLiteral("features"), flags);
    m.insert(QStringLiteral("architecture"), arch);
    m.insert(QStringLiteral("hardware"), hw);
    m.insert(QStringLiteral("machine"),
             readTrim(QStringLiteral("/sys/firmware/devicetree/base/model")).remove(QChar('\0')));
    m.insert(QStringLiteral("socName"),
             readTrim(QStringLiteral("/sys/firmware/devicetree/base/compatible")).remove(QChar('\0')));
    // cache sizes of cpu0
    QVariantList caches;
    const QDir cdir(QStringLiteral("/sys/devices/system/cpu/cpu0/cache"));
    for (const QString &idx : cdir.entryList(QStringList() << QStringLiteral("index*"), QDir::Dirs)) {
        const QString cp = cdir.filePath(idx) + QLatin1Char('/');
        QVariantMap c;
        c.insert(QStringLiteral("level"), readTrim(cp + QStringLiteral("level")));
        c.insert(QStringLiteral("type"), readTrim(cp + QStringLiteral("type")));
        c.insert(QStringLiteral("size"), readTrim(cp + QStringLiteral("size")));
        caches.append(c);
    }
    m.insert(QStringLiteral("caches"), caches);

    // cpufreq capabilities of cpu0: current vs available (for grayed unused)
    const QString cf0 = QStringLiteral("/sys/devices/system/cpu/cpu0/cpufreq/");
    m.insert(QStringLiteral("governor"), readTrim(cf0 + QStringLiteral("scaling_governor")));
    QVariantList govs;
    for (const QByteArray &g : readTrim(cf0 + QStringLiteral("scaling_available_governors")).toLatin1().split(' '))
        if (!g.isEmpty())
            govs.append(QString::fromLatin1(g));
    m.insert(QStringLiteral("availGovernors"), govs);
    QVariantList freqs;
    for (const QByteArray &fr : readTrim(cf0 + QStringLiteral("scaling_available_frequencies")).toLatin1().split(' '))
        if (!fr.isEmpty())
            freqs.append(fr.toInt() / 1000);
    m.insert(QStringLiteral("availFreqsMhz"), freqs);

    // device / OS / kernel identity
    auto osField = [](const QString &file, const QByteArray &key) -> QString {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            return QString();
        for (const QByteArray &l : f.readAll().split('\n')) {
            if (l.startsWith(key + '=')) {
                QByteArray v = l.mid(key.size() + 1).trimmed();
                if (v.startsWith('"') && v.endsWith('"'))
                    v = v.mid(1, v.size() - 2);
                return QString::fromUtf8(v);
            }
        }
        return QString();
    };
    m.insert(QStringLiteral("kernel"), readTrim(QStringLiteral("/proc/sys/kernel/osrelease")));
    m.insert(QStringLiteral("kernelVersion"), readTrim(QStringLiteral("/proc/sys/kernel/version")));
    m.insert(QStringLiteral("os"), osField(QStringLiteral("/etc/os-release"), "PRETTY_NAME"));
    m.insert(QStringLiteral("osVersion"), osField(QStringLiteral("/etc/os-release"), "VERSION_ID"));
    // Marketing name and hardware model from the adaptation's own release file.
    QString hwName = osField(QStringLiteral("/etc/hw-release"), "NAME");
    if (hwName.isEmpty())
        hwName = osField(QStringLiteral("/etc/hw-release"), "PRETTY_NAME");
    if (hwName.isEmpty())
        hwName = osField(QStringLiteral("/etc/hw-release"), "MER_HA_DEVICE");
    // Some adaptations write the vendor into NAME and then repeat it in the
    // product: the Jolla Phone (2026) ships NAME="Jolla Jolla Phone". Collapse
    // a word that immediately repeats itself rather than showing the stutter.
    {
        QStringList w = hwName.split(QLatin1Char(' '), QString::SkipEmptyParts);
        for (int i = w.size() - 1; i > 0; --i)
            if (w.at(i).compare(w.at(i - 1), Qt::CaseInsensitive) == 0)
                w.removeAt(i);
        hwName = w.join(QLatin1Char(' '));
    }
    m.insert(QStringLiteral("deviceName"), hwName);
    // HW_DEVICE_MODEL is optional and the Jolla Phone (2026) omits it. The
    // adaptation's own device code identifies the port just as well, and it is
    // the name every other file on the device uses for it.
    QString hwModel = osField(QStringLiteral("/etc/hw-release"), "HW_DEVICE_MODEL");
    if (hwModel.isEmpty())
        hwModel = osField(QStringLiteral("/etc/hw-release"), "MER_HA_DEVICE");
    if (hwModel.isEmpty())
        hwModel = osField(QStringLiteral("/etc/hw-release"), "ID");
    m.insert(QStringLiteral("deviceModel"), hwModel);
    m.insert(QStringLiteral("deviceVendor"),
             osField(QStringLiteral("/etc/hw-release"), "MER_HA_VENDOR"));
    m.insert(QStringLiteral("hwVersion"), osField(QStringLiteral("/etc/hw-release"), "VERSION_ID"));

    // Android base under libhybris: version/patch level of the vendor blobs.
    // Property names moved between Android generations, so try both spellings
    // in each build.prop the port ships.
    auto prop = [](const QStringList &files, const QStringList &keys) -> QString {
        for (const QString &file : files) {
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QList<QByteArray> lines = f.readAll().split('\n');
            for (const QString &key : keys)
                for (const QByteArray &l : lines)
                    if (l.startsWith(key.toLatin1() + '='))
                        return QString::fromUtf8(l.mid(key.size() + 1).trimmed());
        }
        return QString();
    };
    const QStringList propFiles = {
        QStringLiteral("/vendor/build.prop"), QStringLiteral("/odm/etc/build.prop"),
        QStringLiteral("/system/build.prop"), QStringLiteral("/vendor/odm/etc/build.prop") };
    m.insert(QStringLiteral("androidVersion"),
             prop(propFiles, { QStringLiteral("ro.vendor.build.version.release"),
                               QStringLiteral("ro.build.version.release") }));
    m.insert(QStringLiteral("androidPatch"),
             prop(propFiles, { QStringLiteral("ro.vendor.build.security_patch"),
                               QStringLiteral("ro.build.version.security_patch") }));
    m.insert(QStringLiteral("androidBuild"),
             prop(propFiles, { QStringLiteral("ro.vendor.build.id"),
                               QStringLiteral("ro.build.id") }));
    m.insert(QStringLiteral("androidFingerprint"),
             prop(propFiles, { QStringLiteral("ro.vendor.build.fingerprint"),
                               QStringLiteral("ro.build.fingerprint") }));

    // SoC identity straight from the device tree root ("qcom,lagoon",
    // "mediatek,MT6797", …) — names the IC independent of marketing names.
    {
        QFile f(QStringLiteral("/proc/device-tree/compatible"));
        if (f.open(QIODevice::ReadOnly)) {
            QStringList parts;
            for (const QByteArray &p : f.readAll().split('\0'))
                if (!p.isEmpty()) parts << QString::fromLatin1(p);
            m.insert(QStringLiteral("socCompatible"), parts.join(QStringLiteral(", ")));
        }
    }

    // Qualcomm exposes the SoC identity in /sys/devices/soc0. soc_id maps to
    // the part number Qualcomm's security bulletins (and thus CVE texts) use;
    // only verified ids are mapped, everything else falls back to
    // family + machine as reported.
    {
        const QString fam = readTrim(QStringLiteral("/sys/devices/soc0/family"));
        const QString mach = readTrim(QStringLiteral("/sys/devices/soc0/machine"));
        const int socId = readTrim(QStringLiteral("/sys/devices/soc0/soc_id")).toInt();
        QString model;
        switch (socId) {
        case 434: model = QStringLiteral("SM6350 (Snapdragon 690)"); break;   // lagoon
        case 394: model = QStringLiteral("SM6125 (Snapdragon 665)"); break;   // trinket
        default: break;
        }
        if (model.isEmpty() && !fam.isEmpty() && !mach.isEmpty())
            model = fam + QLatin1Char(' ') + mach;
        if (!model.isEmpty())
            m.insert(QStringLiteral("socModel"), model);
        if (socId > 0)
            m.insert(QStringLiteral("socId"), socId);
    }
    return m;
}

QVariantMap SysMon::graphicsDetail() const
{
    QVariantMap m;
    // The kernel driver's release name. kbase puts it in the module's version
    // attribute and nowhere else an ordinary user can reach -- the mali0
    // device directory has no version node at all, and the ioctl that would
    // answer needs the GPU opened.
    {
        const QDir mods(QStringLiteral("/sys/module"));
        for (const QString &mod : mods.entryList(QStringList() << QStringLiteral("mali_kbase*"),
                                                 QDir::Dirs, QDir::Name)) {
            const QString v = readTrim(mods.filePath(mod) + QStringLiteral("/version"));
            if (v.isEmpty())
                continue;
            m.insert(QStringLiteral("gpuDriverModule"), mod);
            m.insert(QStringLiteral("gpuDriverRelease"), v);
            break;
        }
    }
    // GPU: Adreno (kgsl) first, then a generic devfreq gpu node
    const QString kgsl = QStringLiteral("/sys/class/kgsl/kgsl-3d0/");
    QString gpuModel = readTrim(kgsl + QStringLiteral("gpu_model"));
    double curHz = readTrim(kgsl + QStringLiteral("gpuclk")).toDouble();
    double maxHz = readTrim(kgsl + QStringLiteral("max_gpuclk")).toDouble();
    int busy = -1;
    const QString bp = readTrim(kgsl + QStringLiteral("gpu_busy_percentage"));
    if (!bp.isEmpty())
        busy = bp.split(QLatin1Char(' ')).value(0).remove(QLatin1Char('%')).toInt();

    // Mali (MediaTek & Co.): the kbase driver names the exact core here
    if (gpuModel.isEmpty())
        gpuModel = readTrim(QStringLiteral("/sys/class/misc/mali0/device/gpuinfo"));
    if (gpuModel.isEmpty()) {
        // generic: find a devfreq node whose name mentions gpu
        const QDir df(QStringLiteral("/sys/class/devfreq"));
        for (const QString &e : df.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString nm = readTrim(df.filePath(e) + QStringLiteral("/device/of_node/compatible"));
            if (e.contains(QStringLiteral("gpu"), Qt::CaseInsensitive)
                || e.contains(QStringLiteral("mali"), Qt::CaseInsensitive) || nm.contains(QStringLiteral("mali"))) {
                if (curHz <= 0) curHz = readTrim(df.filePath(e) + QStringLiteral("/cur_freq")).toDouble();
                if (maxHz <= 0) maxHz = readTrim(df.filePath(e) + QStringLiteral("/max_freq")).toDouble();
                if (gpuModel.isEmpty()) gpuModel = nm.isEmpty() ? e : nm;
                break;
            }
        }
    }
    if (gpuModel.isEmpty()) {
        // ARM Mali (e.g. MediaTek): model from the mali misc device
        const QString gi = readTrim(QStringLiteral("/sys/class/misc/mali0/device/gpuinfo"));
        if (!gi.isEmpty()) {
            const QStringList p = gi.split(QLatin1Char(' '), QString::SkipEmptyParts);
            QString model = p.value(0);
            if (p.size() >= 3 && p.value(2) == QLatin1String("cores"))
                model += QStringLiteral(" \u00B7 ") + p.value(1) + QStringLiteral(" cores");
            else if (p.size() >= 2 && p.value(1).startsWith(QLatin1String("MP")))
                model += QLatin1Char(' ') + p.value(1);
            gpuModel = model;
        }
    }
    // MediaTek Mali: clock and utilisation live in /proc (not devfreq/kgsl)
    QString gpuDriver;
    if (QFileInfo::exists(QStringLiteral("/sys/module/mali_kbase")))
        gpuDriver = QStringLiteral("mali_kbase");
    else if (QFileInfo::exists(QStringLiteral("/sys/module/mali")))
        gpuDriver = QStringLiteral("mali");
    auto slurp = [](const QString &p) -> QString {
        QFile f(p);
        return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()) : QString();
    };
    auto numAfter = [](const QString &hay, const QString &key) -> qulonglong {
        const int i = hay.indexOf(key);
        if (i < 0) return 0;
        int j = i + key.size();
        while (j < hay.size() && !hay[j].isDigit()) ++j;
        int k = j;
        while (k < hay.size() && hay[k].isDigit()) ++k;
        return hay.mid(j, k - j).toULongLong();
    };
    if (curHz <= 0) {
        const qulonglong khz = numAfter(slurp(QStringLiteral("/proc/gpufreq/gpufreq_var_dump")),
                                        QStringLiteral("g_cur_gpu_freq"));
        if (khz > 0) curHz = (double)khz * 1000.0;               // kHz -> Hz
    }
    if (maxHz <= 0) {
        const qulonglong khz = numAfter(slurp(QStringLiteral("/proc/gpufreq/gpufreq_opp_dump")),
                                        QStringLiteral("freq ="));
        if (khz > 0) maxHz = (double)khz * 1000.0;
    }
    if (curHz <= 0) {                                            // newer MTK GED (Hz)
        const QString gf = readTrim(QStringLiteral("/sys/kernel/ged/hal/current_freqency"));
        if (!gf.isEmpty()) curHz = gf.toDouble();
    }
    if (busy < 0) {
        // /proc/mali/utilization: "gpu/cljs0/cljs1=67/0/0, ..."
        const QString u = slurp(QStringLiteral("/proc/mali/utilization"));
        const int eq = u.indexOf(QLatin1Char('='));
        if (eq >= 0) {
            int j = eq + 1, k = j;
            while (k < u.size() && u[k].isDigit()) ++k;
            if (k > j) busy = u.mid(j, k - j).toInt();
        }
    }
    if (busy < 0) {
        const QString gu = readTrim(QStringLiteral("/sys/kernel/ged/hal/gpu_utilization"));
        if (!gu.isEmpty()) busy = gu.split(QLatin1Char(' ')).value(0).toInt();
    }

    m.insert(QStringLiteral("gpuModel"), gpuModel);
    if (!gpuDriver.isEmpty()) m.insert(QStringLiteral("gpuDriver"), gpuDriver);
    if (curHz > 0) m.insert(QStringLiteral("gpuCurMhz"), (int)(curHz / 1e6));
    if (maxHz > 0) m.insert(QStringLiteral("gpuMaxMhz"), (int)(maxHz / 1e6));
    if (busy >= 0) m.insert(QStringLiteral("gpuBusy"), busy);
    m.insert(QStringLiteral("renderer"),
             readTrim(QStringLiteral("/sys/class/drm/card0/device/uevent")).contains(QStringLiteral("DRIVER="))
                 ? QString() : QString());

    // Displays via DRM connectors
    QVariantList displays;
    const QDir drm(QStringLiteral("/sys/class/drm"));
    for (const QString &conn : drm.entryList(QStringList() << QStringLiteral("card*-*"), QDir::Dirs)) {
        const QString cp = drm.filePath(conn) + QLatin1Char('/');
        const QString status = readTrim(cp + QStringLiteral("status"));
        if (status != QLatin1String("connected"))
            continue;
        QVariantMap d;
        d.insert(QStringLiteral("connector"), conn.section(QLatin1Char('-'), 1));
        d.insert(QStringLiteral("status"), status);
        const QString modes = readTrim(cp + QStringLiteral("modes"));
        d.insert(QStringLiteral("resolution"), modes.split(QLatin1Char('\n')).value(0));
        d.insert(QStringLiteral("enabled"), readTrim(cp + QStringLiteral("enabled")));
        displays.append(d);
    }
    if (displays.isEmpty()) {
        // no DRM connector (older MediaTek): fall back to the framebuffer
        const QString modes = readTrim(QStringLiteral("/sys/class/graphics/fb0/modes"));
        QString res;
        for (int i = 0; i + 1 < modes.size(); ++i) {
            if (modes[i].isDigit()) {
                int a = i; while (a < modes.size() && modes[a].isDigit()) ++a;
                if (a < modes.size() && modes[a] == QLatin1Char('x')) {
                    int b = a + 1; while (b < modes.size() && modes[b].isDigit()) ++b;
                    res = modes.mid(i, b - i);
                    break;
                }
            }
        }
        if (!res.isEmpty()) {
            QVariantMap d;
            d.insert(QStringLiteral("connector"), QStringLiteral("fb0"));
            d.insert(QStringLiteral("status"), QStringLiteral("connected"));
            d.insert(QStringLiteral("resolution"), res);
            displays.append(d);
        }
    }
    m.insert(QStringLiteral("displays"), displays);
    m.insert(QStringLiteral("driver"),
             QFileInfo(QStringLiteral("/sys/class/drm/card0/device/driver")).symLinkTarget().section(QLatin1Char('/'), -1));
    return m;
}

QVariantMap SysMon::audioDetail() const
{
    QVariantMap m;
    auto readAll = [](const QString &p) {
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        return f.readAll();
    };

    // sound cards
    QVariantList cards;
    QString firstCard;
    for (const QByteArray &line : readAll(QStringLiteral("/proc/asound/cards")).split('\n')) {
        // " 0 [xyz  ]: driver - Longname"
        const int lb = line.indexOf('[');
        const int dash = line.indexOf(" - ");
        if (lb < 0 || dash < 0)
            continue;
        const QString idx = QString::fromLatin1(line.left(lb).trimmed());
        const QString longName = QString::fromLatin1(line.mid(dash + 3).trimmed());
        if (firstCard.isEmpty())
            firstCard = idx;
        QVariantMap c;
        c.insert(QStringLiteral("index"), idx);
        c.insert(QStringLiteral("name"), longName);
        cards.append(c);
    }
    m.insert(QStringLiteral("cards"), cards);

    // codec chip name(s) from card0 codec files
    QVariantList codecs;
    const QDir cdir(QStringLiteral("/proc/asound/card") + (firstCard.isEmpty() ? QStringLiteral("0") : firstCard));
    for (const QString &e : cdir.entryList(QStringList() << QStringLiteral("codec#*"), QDir::Files)) {
        for (const QByteArray &l : readAll(cdir.filePath(e)).split('\n')) {
            if (l.startsWith("Codec:")) {
                codecs.append(QString::fromLatin1(l.mid(6).trimmed()));
                break;
            }
        }
    }
    // some codecs live under /sys/kernel/debug or expose via component; also try id
    m.insert(QStringLiteral("codecs"), codecs);

    // jack / headset state: Android switch h2w or a jack sysfs
    int jack = -1;
    const QByteArray h2w = readAll(QStringLiteral("/sys/class/switch/h2w/state")).trimmed();
    if (!h2w.isEmpty())
        jack = h2w.toInt();
    m.insert(QStringLiteral("jackState"), jack); // -1 unknown, 0 out, 1 headset, 2 headphone

    // active playback / capture streams
    bool playing = false, capturing = false;
    const QDir base(QStringLiteral("/proc/asound"));
    for (const QString &card : base.entryList(QStringList() << QStringLiteral("card*"), QDir::Dirs)) {
        const QDir cd(base.filePath(card));
        for (const QString &pcm : cd.entryList(QStringList() << QStringLiteral("pcm*"), QDir::Dirs)) {
            const bool isPlay = pcm.endsWith(QLatin1Char('p'));
            const QDir pd(cd.filePath(pcm));
            for (const QString &sub : pd.entryList(QStringList() << QStringLiteral("sub*"), QDir::Dirs)) {
                const QByteArray st = readAll(pd.filePath(sub) + QStringLiteral("/status"));
                if (st.contains("state: RUNNING")) {
                    if (isPlay) playing = true; else capturing = true;
                }
            }
        }
    }
    m.insert(QStringLiteral("playing"), playing);
    m.insert(QStringLiteral("capturing"), capturing);
    return m;
}

static QString usbClassName(const QString &hex)
{
    const int c = hex.toInt(nullptr, 16);
    switch (c) {
    case 0x00: return QStringLiteral("(per interface)");
    case 0x01: return QStringLiteral("Audio");
    case 0x02: return QStringLiteral("Communications (CDC)");
    case 0x03: return QStringLiteral("HID");
    case 0x05: return QStringLiteral("Physical");
    case 0x06: return QStringLiteral("Image");
    case 0x07: return QStringLiteral("Printer");
    case 0x08: return QStringLiteral("Mass storage");
    case 0x09: return QStringLiteral("Hub");
    case 0x0a: return QStringLiteral("CDC data");
    case 0x0b: return QStringLiteral("Smart card");
    case 0x0e: return QStringLiteral("Video");
    case 0x0f: return QStringLiteral("Personal healthcare");
    case 0xe0: return QStringLiteral("Wireless (BT/…)");
    case 0xef: return QStringLiteral("Miscellaneous");
    case 0xff: return QStringLiteral("Vendor specific");
    default: return QStringLiteral("class 0x%1").arg(c, 2, 16, QLatin1Char('0'));
    }
}
static QString usbSpeedName(const QString &s)
{
    if (s == QLatin1String("1.5")) return QStringLiteral("USB 1.0 · Low · 1.5 Mbps");
    if (s == QLatin1String("12")) return QStringLiteral("USB 1.1 · Full · 12 Mbps");
    if (s == QLatin1String("480")) return QStringLiteral("USB 2.0 · High · 480 Mbps");
    if (s == QLatin1String("5000")) return QStringLiteral("USB 3.0 · Super · 5 Gbps");
    if (s == QLatin1String("10000")) return QStringLiteral("USB 3.1 · Super+ · 10 Gbps");
    return s.isEmpty() ? QString() : s + QStringLiteral(" Mbps");
}

QVariantMap SysMon::usbDetail() const
{
    QVariantMap out;
    // usb.ids for human-readable vendor/product names
    QByteArray ids;
    {
        QFile f(QStringLiteral("/usr/share/hwdata/usb.ids"));
        if (f.open(QIODevice::ReadOnly))
            ids = f.readAll();
    }
    auto lookup = [&ids](const QString &vid, const QString &pid) -> QPair<QString, QString> {
        QString vn, pn;
        if (ids.isEmpty())
            return qMakePair(vn, pn);
        const QByteArray vkey = '\n' + vid.toLower().toLatin1() + "  ";
        int v = ids.indexOf(vkey);
        if (v < 0)
            return qMakePair(vn, pn);
        int vEnd = ids.indexOf('\n', v + 1);
        vn = QString::fromUtf8(ids.mid(v + vkey.size(), vEnd - v - vkey.size())).trimmed();
        // products are indented with a tab until the next vendor (non-tab) line
        const QByteArray pkey = '\t' + pid.toLower().toLatin1() + "  ";
        int p = ids.indexOf(pkey, vEnd);
        // ensure p is still within this vendor block: no non-tab, non-comment line between
        if (p >= 0) {
            int nextVendor = p;
            // walk back to confirm contiguity is unnecessary; accept first match after vendor
            int pEnd = ids.indexOf('\n', p + 1);
            pn = QString::fromUtf8(ids.mid(p + pkey.size(), pEnd - p - pkey.size())).trimmed();
            Q_UNUSED(nextVendor)
        }
        return qMakePair(vn, pn);
    };

    auto rd = [](const QString &p) {
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly))
            return QString();
        return QString::fromUtf8(f.readAll().trimmed());
    };

    // block-device -> mount point, for cross-referencing USB storage
    QHash<QString, QString> mountByDev;
    {
        QFile mf(QStringLiteral("/proc/self/mounts"));
        if (mf.open(QIODevice::ReadOnly))
            for (const QByteArray &line : mf.readAll().split('\n')) {
                const QList<QByteArray> c = line.split(' ');
                if (c.size() >= 2 && c[0].startsWith("/dev/"))
                    mountByDev.insert(QString::fromLatin1(c[0]).section(QLatin1Char('/'), -1),
                                      QString::fromLocal8Bit(c[1]));
            }
    }
    // find the /dev nodes a USB device provides, by matching each class entry's
    // backing device up the sysfs tree to this USB device's canonical path.
    auto nodesFor = [&](const QString &usbCanon) -> QVariantList {
        QVariantList nodes;
        struct Cls { const char *cls; const char *devPrefix; };
        static const Cls classes[] = {
            {"tty", "/dev/"}, {"block", "/dev/"}, {"net", ""},
            {"hidraw", "/dev/"}, {"video4linux", "/dev/"}, {"input", "/dev/input/"}
        };
        for (const Cls &c : classes) {
            const QDir cd(QStringLiteral("/sys/class/") + QLatin1String(c.cls));
            for (const QString &name : cd.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString link = QFileInfo(cd.filePath(name) + QStringLiteral("/device")).canonicalFilePath();
                if (link.isEmpty() || !link.startsWith(usbCanon))
                    continue;
                QVariantMap n;
                n.insert(QStringLiteral("subsystem"), QLatin1String(c.cls));
                n.insert(QStringLiteral("name"), name);
                if (*c.devPrefix)
                    n.insert(QStringLiteral("node"), QLatin1String(c.devPrefix) + name);
                if (qstrcmp(c.cls, "block") == 0 && mountByDev.contains(name))
                    n.insert(QStringLiteral("mount"), mountByDev.value(name));
                nodes.append(n);
            }
        }
        return nodes;
    };

    QVariantList controllers, devices;
    const QDir bus(QStringLiteral("/sys/bus/usb/devices"));
    QStringList entries = bus.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    entries.sort();
    for (const QString &e : entries) {
        const QString d = bus.filePath(e) + QLatin1Char('/');
        const QString vid = rd(d + QStringLiteral("idVendor"));
        if (vid.isEmpty())
            continue;
        const QString pid = rd(d + QStringLiteral("idProduct"));
        const QString cls = rd(d + QStringLiteral("bDeviceClass"));
        const bool rootHub = e.startsWith(QLatin1String("usb"));

        QVariantMap m;
        m.insert(QStringLiteral("addr"), e);
        m.insert(QStringLiteral("vid"), vid);
        m.insert(QStringLiteral("pid"), pid);
        m.insert(QStringLiteral("idPair"), vid + QLatin1Char(':') + pid);
        m.insert(QStringLiteral("manufacturer"), rd(d + QStringLiteral("manufacturer")));
        m.insert(QStringLiteral("product"), rd(d + QStringLiteral("product")));
        m.insert(QStringLiteral("serial"), rd(d + QStringLiteral("serial")));
        m.insert(QStringLiteral("class"), usbClassName(cls));
        m.insert(QStringLiteral("speed"), usbSpeedName(rd(d + QStringLiteral("speed"))));
        m.insert(QStringLiteral("maxPower"), rd(d + QStringLiteral("bMaxPower")));
        m.insert(QStringLiteral("version"), rd(d + QStringLiteral("version")).trimmed());
        m.insert(QStringLiteral("busnum"), rd(d + QStringLiteral("busnum")));
        m.insert(QStringLiteral("devnum"), rd(d + QStringLiteral("devnum")));
        const QPair<QString, QString> names = lookup(vid, pid);
        m.insert(QStringLiteral("vendorName"), names.first);
        m.insert(QStringLiteral("productName"), names.second);
        m.insert(QStringLiteral("driver"),
                 QFileInfo(d + QStringLiteral("driver")).symLinkTarget().section(QLatin1Char('/'), -1));
        m.insert(QStringLiteral("nodes"), nodesFor(QFileInfo(bus.filePath(e)).canonicalFilePath()));

        if (rootHub)
            controllers.append(m);
        else
            devices.append(m);
    }
    out.insert(QStringLiteral("controllers"), controllers);
    out.insert(QStringLiteral("devices"), devices);

    // Host/device controller identity: the UDC names the IP core (dwc3 =
    // Synopsys DesignWare USB3, musb = Mentor, mtu3 = MediaTek), its DT
    // compatible names the SoC integration, Type-C adds the current roles.
    QVariantMap ctl;
    const QDir udc(QStringLiteral("/sys/class/udc"));
    const QStringList udcs = udc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (!udcs.isEmpty()) {
        const QString u = udcs.first();
        ctl.insert(QStringLiteral("name"), u);
        ctl.insert(QStringLiteral("maxSpeed"), readTrim(udc.filePath(u) + QStringLiteral("/maximum_speed")));
        const QString cur = readTrim(udc.filePath(u) + QStringLiteral("/current_speed"));
        if (!cur.isEmpty() && cur != QLatin1String("UNKNOWN"))
            ctl.insert(QStringLiteral("curSpeed"), cur);
        QFile cf(udc.filePath(u) + QStringLiteral("/device/of_node/compatible"));
        if (cf.open(QIODevice::ReadOnly)) {
            QStringList parts;
            for (const QByteArray &p : cf.readAll().split('\0'))
                if (!p.isEmpty()) parts << QString::fromLatin1(p);
            ctl.insert(QStringLiteral("compatible"), parts.join(QStringLiteral(", ")));
        }
    }
    const QDir tc(QStringLiteral("/sys/class/typec"));
    const QStringList ports = tc.entryList(QStringList() << QStringLiteral("port*"), QDir::Dirs);
    if (!ports.isEmpty()) {
        const QString p = tc.filePath(ports.first());
        ctl.insert(QStringLiteral("powerRole"), readTrim(p + QStringLiteral("/power_role")));
        ctl.insert(QStringLiteral("dataRole"), readTrim(p + QStringLiteral("/data_role")));
    }
    out.insert(QStringLiteral("controller"), ctl);
    return out;
}

// What the kernel registered in the V4L2 class: capture nodes and sub-devices,
// counted by what their names say they are. Qualcomm registers its sensors,
// EEPROMs and flash units here; MediaTek registers only the flash.
static void collectV4l2(QVariantList &captureNodes, QVariantList &subdevs,
                        int &sensors, int &eeproms, int &flashes,
                        bool &isp, bool &cpas)
{
    const QDir d(QStringLiteral("/sys/class/video4linux"));
    QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    entries.sort();
    for (const QString &e : entries) {
        const QString name = readTrim(d.filePath(e) + QStringLiteral("/name"));
        if (e.startsWith(QLatin1String("video"))) {
            QVariantMap n;
            n.insert(QStringLiteral("name"), e);
            n.insert(QStringLiteral("node"), QStringLiteral("/dev/") + e);
            n.insert(QStringLiteral("label"), name);
            captureNodes.append(n);
            continue;
        }
        const QString ln = name.toLower();
        if (ln.contains(QLatin1String("sensor"))) ++sensors;
        else if (ln.contains(QLatin1String("eeprom"))) ++eeproms;
        else if (ln.contains(QLatin1String("flash")) || ln.contains(QLatin1String("led"))) ++flashes;
        else if (ln.contains(QLatin1String("isp"))) isp = true;
        else if (ln.contains(QLatin1String("cpas"))) cpas = true;
        QVariantMap sd;
        sd.insert(QStringLiteral("name"), e);
        sd.insert(QStringLiteral("label"), name);
        subdevs.append(sd);
    }
}

// Qualcomm: the part numbers are in the vendor camera modules, because CAMX
// exposes no V4L2 capabilities. "…sensormodule.imx766_wide.bin".
static QVariantList collectQualcommSensors()
{
    QVariantList cameras;
    QStringList seen;
    const QStringList dirs = {
        QStringLiteral("/vendor/lib64/camera"), QStringLiteral("/vendor/lib/camera"),
        QStringLiteral("/odm/lib64/camera"),    QStringLiteral("/odm/lib/camera") };
    for (const QString &cd : dirs) {
        QDir dir(cd);
        if (!dir.exists())
            continue;
        const QStringList mods = dir.entryList(
            QStringList() << QStringLiteral("*sensormodule*.bin"), QDir::Files);
        for (const QString &f : mods) {
            const int a = f.indexOf(QLatin1String("sensormodule."));
            if (a < 0)
                continue;
            QString tag = f.mid(a + 13);
            if (tag.endsWith(QLatin1String(".bin")))
                tag.chop(4);
            const int us = tag.indexOf(QLatin1Char('_'));
            const QString model = us < 0 ? tag : tag.left(us);
            const QString role  = us < 0 ? QString() : tag.mid(us + 1);
            const QString key = model + QLatin1Char('/') + role;
            if (seen.contains(key))
                continue;
            seen.append(key);
            QVariantMap c;
            c.insert(QStringLiteral("model"), model);
            if (!role.isEmpty())
                c.insert(QStringLiteral("role"), role);
            cameras.append(c);
        }
    }
    return cameras;
}

// MediaTek: the driver names its sensors in procfs, one block each --
// "CAM[0]:imx766_mipi_raw;" followed by the frame it grabs per mode.
static QVariantList collectMediatekSensors()
{
    QVariantList cameras;
    const QString info = readTrim(QStringLiteral("/proc/driver/camera_info"));
    QString pending;
    for (const QString &ln : info.split(QLatin1Char('\n'))) {
        const QString l = ln.trimmed();
        if (l.startsWith(QLatin1String("CAM[")) && l.contains(QLatin1Char(':'))) {
            pending = l.section(QLatin1Char(':'), 1).section(QLatin1Char(';'), 0, 0).trimmed();
            if (pending.endsWith(QLatin1String("_mipi_raw")))
                pending.chop(9);
        } else if (!pending.isEmpty() && l.startsWith(QLatin1String("Cap:"))) {
            const QStringList n = l.section(QLatin1Char('='), 1).split(QLatin1Char(','));
            const int w = n.value(0).trimmed().toInt();
            const int h = n.value(1).trimmed().toInt();
            QVariantMap c;
            c.insert(QStringLiteral("model"), pending);
            if (w > 0 && h > 0) {
                c.insert(QStringLiteral("width"), w);
                c.insert(QStringLiteral("height"), h);
            }
            cameras.append(c);
            pending.clear();
        }
    }
    return cameras;
}

// The same stack keeps its EEPROMs and processing engines as plain character
// devices, so counting the V4L2 class alone reported zero of each.
static void collectMediatekNodes(int &eeproms, bool &isp, QStringList &engines)
{
    const QDir dev(QStringLiteral("/dev"));
    if (eeproms == 0)
        eeproms = dev.entryList(QStringList() << QStringLiteral("camera_eeprom*"),
                                QDir::System | QDir::Files).size();
    if (!isp)
        isp = QFileInfo::exists(QStringLiteral("/dev/camera-isp"));
    engines = dev.entryList(QStringList() << QStringLiteral("camera-*"),
                            QDir::System | QDir::Files);
    engines.sort();
}

QVariantMap SysMon::cameraDetail() const
{
    QVariantMap m;
    QVariantList captureNodes, subdevs;
    int sensors = 0, eeproms = 0, flashes = 0;
    bool isp = false, cpas = false;
    collectV4l2(captureNodes, subdevs, sensors, eeproms, flashes, isp, cpas);

    // Which stack this phone runs decides where the sensors are named at all.
    const bool mtkCam = QFileInfo::exists(QStringLiteral("/proc/driver/camsensor"))
                     || QFileInfo::exists(QStringLiteral("/sys/bus/platform/drivers/seninf"))
                     || !QDir(QStringLiteral("/sys/module"))
                             .entryList(QStringList() << QStringLiteral("imgsensor*"), QDir::Dirs).isEmpty();

    QVariantList cameras = collectQualcommSensors();
    if (cameras.isEmpty() && mtkCam)
        cameras = collectMediatekSensors();

    if (mtkCam) {
        QStringList engines;
        collectMediatekNodes(eeproms, isp, engines);
        if (sensors == 0)
            sensors = cameras.size();
        if (!engines.isEmpty())
            m.insert(QStringLiteral("engines"), engines);
        m.insert(QStringLiteral("platform"), QStringLiteral("mediatek"));
    } else if (cpas) {
        m.insert(QStringLiteral("platform"), QStringLiteral("qualcomm"));
    } else {
        m.insert(QStringLiteral("platform"), QString());
    }

    m.insert(QStringLiteral("cameras"), cameras);
    m.insert(QStringLiteral("captureNodes"), captureNodes);
    m.insert(QStringLiteral("subdevs"), subdevs);
    m.insert(QStringLiteral("sensors"), sensors);
    m.insert(QStringLiteral("eeproms"), eeproms);
    m.insert(QStringLiteral("flashes"), flashes);
    m.insert(QStringLiteral("isp"), isp);
    m.insert(QStringLiteral("cpas"), cpas);
    return m;
}

// Radio chipset identity: everything the kernel exposes about the WLAN/BT
// combo chip — driver, device-tree node, firmware identity, firmware files.
// Sources vary per SoC; every field simply stays empty when absent. All of
// this is offline; the debugfs firmware identity additionally falls back to
// the root helper when the direct read is not permitted.
QVariantMap SysMon::wirelessDetail() const
{
    QVariantMap m;

    // --- WLAN ---------------------------------------------------------
    const QString wlanDev = QStringLiteral("/sys/class/net/wlan0/device");
    m.insert(QStringLiteral("wlanAddress"), readTrim(QStringLiteral("/sys/class/net/wlan0/address")));
    const QFileInfo drv(wlanDev + QStringLiteral("/driver"));
    if (drv.exists())
        m.insert(QStringLiteral("wlanDriver"), QFileInfo(drv.symLinkTarget()).fileName());
    const QFileInfo devNode(wlanDev);
    if (devNode.exists())
        m.insert(QStringLiteral("wlanDevice"), QFileInfo(devNode.symLinkTarget()).fileName());

    // device-tree: wifi and bt nodes name the chip on ARM SoCs
    auto dtCompatible = [](const QString &dir) -> QString {
        QFile f(dir + QStringLiteral("/compatible"));
        if (!f.open(QIODevice::ReadOnly))
            return QString();
        // NUL-separated list of compatible strings
        const QList<QByteArray> parts = f.readAll().split('\0');
        QStringList out;
        for (const QByteArray &p : parts)
            if (!p.isEmpty()) out << QString::fromLatin1(p);
        return out.join(QStringLiteral(", "));
    };
    // Qualcomm puts the radio nodes under /soc; MediaTek at the tree root
    // (btif@…, consys@…) — scan both.
    for (const QString &base : { QStringLiteral("/proc/device-tree/soc"),
                                 QStringLiteral("/proc/device-tree") }) {
        const QDir soc(base);
        for (const QString &e : soc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString le = e.toLower();
            if ((le.contains(QLatin1String("wifi")) || le.contains(QLatin1String("wlan"))
                 || le.startsWith(QLatin1String("consys")))
                && !le.contains(QLatin1String("smmu"))
                && !m.contains(QStringLiteral("wlanDtNode"))) {
                m.insert(QStringLiteral("wlanDtNode"), e);
                m.insert(QStringLiteral("wlanDtCompatible"), dtCompatible(soc.filePath(e)));
            }
            if ((le.startsWith(QLatin1String("bt")) || le.contains(QLatin1String("bluetooth"))
                 || le.contains(QLatin1String("wcn")))
                && !le.contains(QLatin1String("wifi"))
                && !m.contains(QStringLiteral("btDtNode"))) {
                const QString compat = dtCompatible(soc.filePath(e));
                if (compat.contains(QLatin1String("coresight"))
                        || compat.contains(QLatin1String("dummy")))
                    continue;   // btm0 and friends are trace sources
                m.insert(QStringLiteral("btDtNode"), e);
                m.insert(QStringLiteral("btDtCompatible"), compat);
            }
        }
    }

    // firmware identity from the Qualcomm cnss/icnss driver (root-only
    // debugfs; falls back to the root helper, else the fields stay empty)
    for (const QString &p : { QStringLiteral("/sys/kernel/debug/icnss/stats"),
                              QStringLiteral("/sys/kernel/debug/cnss/stats") }) {
        QString stats = readTrim(p);
        if (stats.isEmpty() && RootClient::instance()->active())
            stats = QString::fromUtf8(RootClient::instance()->readFile(p)).trimmed();
        if (stats.isEmpty())
            continue;
        for (const QString &ln : stats.split(QLatin1Char('\n'))) {
            const QString t = ln.trimmed();
            if (t.startsWith(QLatin1String("Firmware Version")))
                m.insert(QStringLiteral("wlanFwVersion"),
                         t.section(QLatin1Char(':'), 1).trimmed());
            else if (t.contains(QLatin1String("IMAGE_VERSION_STRING")))
                m.insert(QStringLiteral("wlanFwBuild"),
                         t.section(QLatin1Char('='), 1).trimmed());
        }
        break;
    }

    // shipped firmware blobs (names only — they identify the chip family);
    // Qualcomm patterns plus MediaTek (WIFI_RAM_CODE_*, WMT_*, /etc/firmware)
    QStringList fwFiles;
    QMap<QString, QString> fwWhere;      // name -> directory it was found in
    for (const QString &d : { QStringLiteral("/vendor/firmware"),
                              QStringLiteral("/lib/firmware"),
                              QStringLiteral("/etc/firmware"),
                              QStringLiteral("/vendor/firmware_mnt/image") }) {
        for (const QString &f : QDir(d).entryList(
                 QStringList() << QStringLiteral("*wlan*") << QStringLiteral("*wcn*")
                               << QStringLiteral("bdwlan*") << QStringLiteral("*bt*fw*")
                               << QStringLiteral("WIFI_RAM_CODE*") << QStringLiteral("BT_RAM_CODE*")
                               << QStringLiteral("WMT_*") << QStringLiteral("mt66*"),
                 QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
            if (!fwFiles.contains(f)) { fwFiles << f; fwWhere.insert(f, d); }
    }
    fwFiles.sort();
    // One entry per file rather than one long line: a dozen blob names run
    // together are unreadable, and the directory is worth naming beside each.
    QVariantList fwList;
    for (const QString &f : fwFiles) {
        QVariantMap e;
        e.insert(QStringLiteral("name"), f);
        e.insert(QStringLiteral("dir"), fwWhere.value(f));
        fwList.append(e);
    }
    m.insert(QStringLiteral("firmwareFileList"), fwList);

    // --- Bluetooth ----------------------------------------------------
    QStringList adapters;
    const QDir btd(QStringLiteral("/sys/class/bluetooth"));
    for (const QString &e : btd.entryList(QStringList() << QStringLiteral("hci*"), QDir::Dirs))
        adapters << e;
    adapters.sort();
    m.insert(QStringLiteral("btAdapters"), adapters.join(QStringLiteral(", ")));

    QStringList rfk;
    const QDir rf(QStringLiteral("/sys/class/rfkill"));
    for (const QString &e : rf.entryList(QStringList() << QStringLiteral("rfkill*"), QDir::Dirs)) {
        const QString type = readTrim(rf.filePath(e) + QStringLiteral("/type"));
        if (type != QLatin1String("bluetooth") && type != QLatin1String("wlan"))
            continue;
        const bool blocked = readTrim(rf.filePath(e) + QStringLiteral("/soft")) == QLatin1String("1")
                          || readTrim(rf.filePath(e) + QStringLiteral("/hard")) == QLatin1String("1");
        const QString who = readTrim(rf.filePath(e) + QStringLiteral("/name"));
        rfk << type + (who.isEmpty() ? QString() : QStringLiteral(" (") + who + QLatin1Char(')'))
               + (blocked ? QStringLiteral(": blocked") : QStringLiteral(": ok"));
    }
    m.insert(QStringLiteral("rfkill"), rfk.join(QStringLiteral(" · ")));
    return m;
}

// Whether it is worth asking at all. Cheap, so a page can offer the HAL list
// without paying for it: halServices() below runs a helper per binder domain.
bool SysMon::halBinderPresent() const
{
    return QFileInfo::exists(QStringLiteral("/dev/binder"))
        || QFileInfo::exists(QStringLiteral("/dev/hwbinder"));
}

// Is a given service manager running? Its absence is the whole reason this
// code has to look: querying a binder domain whose manager is gone does not
// fail, it waits for an answer that never comes. Measured on the Jolla phone
// of 2026 -- /dev/hwbinder exists and two processes hold it open, but
// hwservicemanager is not started at all, and the query hangs until killed.
// /proc/<pid>/cmdline is world-readable, so this needs no privilege.
static bool managerRunning(const QByteArray &exe)
{
    const QDir proc(QStringLiteral("/proc"));
    for (const QString &e : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isPid = false;
        e.toInt(&isPid);
        if (!isPid)
            continue;
        QFile f(QStringLiteral("/proc/") + e + QStringLiteral("/cmdline"));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray first = f.readAll().split('\0').value(0);
        if (first == exe || first.endsWith('/' + exe))
            return true;
    }
    return false;
}

// The registered Android services of the hardware adaptation, per binder
// domain. There are three, and which one carries the HALs depends on the age
// of the Android base underneath: HIDL on /dev/hwbinder is what a port of the
// Android 9-12 era uses, AIDL on /dev/binder is what replaced it -- Android 13
// deprecated HIDL and 14 dropped hwservicemanager outright. Asking only one of
// them therefore answers richly on one device and not at all on the next, so
// all three are asked and each says for itself who serves it.
//
// None of this has anything to do with Android App Support: these are the
// adaptation's own services and they are registered whether or not any
// Android container is installed or running.
QVariantMap SysMon::halServices() const
{
    struct Domain { const char *path, *manager, *protocol; };
    static const Domain domains[] = {
        { "/dev/binder",    "servicemanager",    "AIDL" },
        { "/dev/vndbinder", "vndservicemanager", "AIDL (vendor)" },
        { "/dev/hwbinder",  "hwservicemanager",  "HIDL" },
        { nullptr, nullptr, nullptr }
    };

    QVariantMap m;
    QVariantList domainList, services;
    for (int i = 0; domains[i].path; ++i) {
        const QString path = QString::fromLatin1(domains[i].path);
        if (!QFileInfo::exists(path))
            continue;
        QVariantMap d;
        d.insert(QStringLiteral("path"), path);
        d.insert(QStringLiteral("protocol"), QString::fromLatin1(domains[i].protocol));
        d.insert(QStringLiteral("manager"), QString::fromLatin1(domains[i].manager));

        const bool live = managerRunning(QByteArray(domains[i].manager));
        d.insert(QStringLiteral("running"), live);
        int found = 0;
        if (live) {
            QProcess p;
            p.start(QStringLiteral("binder-list"), QStringList() << QStringLiteral("-d") << path);
            if (p.waitForFinished(1500)) {
                QStringList lines;
                for (const QByteArray &l : p.readAllStandardOutput().split('\n')) {
                    const QString t = QString::fromUtf8(l).trimmed();
                    if (!t.isEmpty())
                        lines << t;
                }
                lines.sort();
                for (const QString &t : lines) {
                    QVariantMap sv;
                    sv.insert(QStringLiteral("name"), t);
                    sv.insert(QStringLiteral("protocol"), QString::fromLatin1(domains[i].protocol));
                    services.append(sv);
                    ++found;
                }
            } else {
                p.kill();
                p.waitForFinished(500);
                d.insert(QStringLiteral("timedOut"), true);
            }
        }
        d.insert(QStringLiteral("count"), found);
        domainList.append(d);
    }
    m.insert(QStringLiteral("domains"), domainList);
    m.insert(QStringLiteral("services"), services);
    return m;
}

// Bug-report log info, unprivileged part: installed packages and running
// processes matching the term. Journal/dmesg excerpts come from the root
// helper (logGrep) — the QML page combines both.
QString SysMon::bugReportInfo(const QString &term) const
{
    const QString low = term.trimmed().toLower();
    if (low.isEmpty())
        return QString();
    QString out;

    QProcess rpm;
    rpm.start(QStringLiteral("rpm"), QStringList() << QStringLiteral("-qa"));
    if (rpm.waitForFinished(15000)) {
        QStringList hits;
        for (const QByteArray &l : rpm.readAllStandardOutput().split('\n'))
            if (!l.isEmpty() && l.toLower().contains(low.toUtf8()))
                hits << QString::fromUtf8(l);
        hits.sort();
        out += QStringLiteral("== installed packages matching \"%1\" ==\n").arg(term.trimmed());
        out += hits.isEmpty() ? QStringLiteral("(none)\n") : hits.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    out += QStringLiteral("\n== running processes matching \"%1\" ==\n").arg(term.trimmed());
    bool anyProc = false;
    const QDir proc(QStringLiteral("/proc"));
    for (const QString &pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool numeric = false;
        pid.toInt(&numeric);
        if (!numeric)
            continue;
        const QString base = QStringLiteral("/proc/") + pid;
        QString cmdline = readTrim(base + QStringLiteral("/cmdline")).replace(QLatin1Char('\0'), QLatin1Char(' '));
        const QString comm = readTrim(base + QStringLiteral("/comm"));
        if (!comm.toLower().contains(low) && !cmdline.toLower().contains(low))
            continue;
        QString state, rss;
        for (const QString &l : readTrim(base + QStringLiteral("/status")).split(QLatin1Char('\n'))) {
            if (l.startsWith(QLatin1String("State:"))) state = l.mid(6).trimmed();
            else if (l.startsWith(QLatin1String("VmRSS:"))) rss = l.mid(6).trimmed();
        }
        out += pid + QStringLiteral("  ") + comm
             + (state.isEmpty() ? QString() : QStringLiteral("  [") + state + QLatin1Char(']'))
             + (rss.isEmpty() ? QString() : QStringLiteral("  ") + rss)
             + (cmdline.isEmpty() ? QString() : QStringLiteral("\n    ") + cmdline.left(160))
             + QLatin1Char('\n');
        anyProc = true;
    }
    if (!anyProc)
        out += QStringLiteral("(none)\n");
    return out;
}

static QVariantMap ofonoProps(const QString &path, const QString &iface)
{
    QDBusInterface i(QStringLiteral("org.ofono"), path,
                     QStringLiteral("org.ofono.") + iface, QDBusConnection::systemBus());
    if (!i.isValid()) return QVariantMap();
    QDBusReply<QVariantMap> r = i.call(QStringLiteral("GetProperties"));
    return r.isValid() ? r.value() : QVariantMap();
}

// first internet APN from ConnectionManager contexts (a(oa{sv}))
static QString ofonoApn(const QString &path)
{
    QDBusInterface i(QStringLiteral("org.ofono"), path,
                     QStringLiteral("org.ofono.ConnectionManager"), QDBusConnection::systemBus());
    if (!i.isValid()) return QString();
    QDBusMessage r = i.call(QStringLiteral("GetContexts"));
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) return QString();
    const QDBusArgument arg = r.arguments().first().value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        arg.beginStructure();
        QDBusObjectPath op; QVariantMap p;
        arg >> op >> p;
        arg.endStructure();
        if (p.value(QStringLiteral("Type")).toString() == QLatin1String("internet")) {
            const QString apn = p.value(QStringLiteral("AccessPointName")).toString();
            if (!apn.isEmpty()) { arg.endArray(); return apn; }
        }
    }
    arg.endArray();
    return QString();
}

QVariantMap SysMon::modemDetail() const
{
    QVariantMap out;
    QVariantList modems;
    QDBusInterface mgr(QStringLiteral("org.ofono"), QStringLiteral("/"),
                       QStringLiteral("org.ofono.Manager"), QDBusConnection::systemBus());
    if (!mgr.isValid()) { out.insert(QStringLiteral("present"), false); return out; }
    QDBusMessage r = mgr.call(QStringLiteral("GetModems"));
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) {
        out.insert(QStringLiteral("present"), false);
        return out;
    }
    const QDBusArgument arg = r.arguments().first().value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        arg.beginStructure();
        QDBusObjectPath op; QVariantMap mp;
        arg >> op >> mp;
        arg.endStructure();
        const QString path = op.path();

        QVariantMap md;
        md.insert(QStringLiteral("path"), path);
        md.insert(QStringLiteral("online"),  mp.value(QStringLiteral("Online")).toBool());
        md.insert(QStringLiteral("powered"), mp.value(QStringLiteral("Powered")).toBool());
        md.insert(QStringLiteral("manufacturer"), mp.value(QStringLiteral("Manufacturer")).toString());
        md.insert(QStringLiteral("model"),        mp.value(QStringLiteral("Model")).toString());
        md.insert(QStringLiteral("revision"),     mp.value(QStringLiteral("Revision")).toString());
        md.insert(QStringLiteral("serial"),       mp.value(QStringLiteral("Serial")).toString());
        md.insert(QStringLiteral("type"),         mp.value(QStringLiteral("Type")).toString());

        const QVariantMap sim = ofonoProps(path, QStringLiteral("SimManager"));
        if (!sim.isEmpty()) {
            QVariantMap s;
            s.insert(QStringLiteral("present"), sim.value(QStringLiteral("Present")).toBool());
            s.insert(QStringLiteral("spn"),     sim.value(QStringLiteral("ServiceProviderName")).toString());
            s.insert(QStringLiteral("imsi"),    sim.value(QStringLiteral("SubscriberIdentity")).toString());
            s.insert(QStringLiteral("iccid"),   sim.value(QStringLiteral("CardIdentifier")).toString());
            s.insert(QStringLiteral("mcc"),     sim.value(QStringLiteral("MobileCountryCode")).toString());
            s.insert(QStringLiteral("mnc"),     sim.value(QStringLiteral("MobileNetworkCode")).toString());
            s.insert(QStringLiteral("pin"),     sim.value(QStringLiteral("PinRequired")).toString());
            const QVariant nums = sim.value(QStringLiteral("SubscriberNumbers"));
            const QStringList nl = nums.toStringList();
            if (!nl.isEmpty()) s.insert(QStringLiteral("number"), nl.first());
            md.insert(QStringLiteral("sim"), s);
        }

        const QVariantMap net = ofonoProps(path, QStringLiteral("NetworkRegistration"));
        if (!net.isEmpty()) {
            QVariantMap n;
            n.insert(QStringLiteral("status"),   net.value(QStringLiteral("Status")).toString());
            n.insert(QStringLiteral("name"),     net.value(QStringLiteral("Name")).toString());
            n.insert(QStringLiteral("tech"),     net.value(QStringLiteral("Technology")).toString());
            n.insert(QStringLiteral("mcc"),      net.value(QStringLiteral("MobileCountryCode")).toString());
            n.insert(QStringLiteral("mnc"),      net.value(QStringLiteral("MobileNetworkCode")).toString());
            n.insert(QStringLiteral("mode"),     net.value(QStringLiteral("Mode")).toString());
            n.insert(QStringLiteral("cellId"),   net.value(QStringLiteral("CellId")).toUInt());
            n.insert(QStringLiteral("lac"),      net.value(QStringLiteral("LocationAreaCode")).toUInt());
            n.insert(QStringLiteral("strength"), net.value(QStringLiteral("Strength")).toInt());
            md.insert(QStringLiteral("network"), n);
        }

        const QVariantMap cm = ofonoProps(path, QStringLiteral("ConnectionManager"));
        if (!cm.isEmpty()) {
            QVariantMap c;
            c.insert(QStringLiteral("attached"), cm.value(QStringLiteral("Attached")).toBool());
            c.insert(QStringLiteral("roaming"),  cm.value(QStringLiteral("RoamingAllowed")).toBool());
            c.insert(QStringLiteral("apn"),      ofonoApn(path));
            md.insert(QStringLiteral("data"), c);
        }

        modems.append(md);
    }
    arg.endArray();
    out.insert(QStringLiteral("present"), !modems.isEmpty());
    out.insert(QStringLiteral("modems"), modems);
    return out;
}

QVariantMap SysMon::audioStreams() const
{
    QVariantMap out;
    auto runPactl = [](const QStringList &args) -> QString {
        QProcess p;
        p.start(QStringLiteral("pactl"), args);
        if (!p.waitForFinished(2500))
            return QString();
        return QString::fromUtf8(p.readAllStandardOutput());
    };
    auto parse = [](const QString &text, const QString &kind) -> QVariantList {
        QVariantList list;
        QVariantMap cur;
        bool have = false;
        for (const QString &raw : text.split(QLatin1Char('\n'))) {
            if (raw.startsWith(kind + QStringLiteral(" #"))) {
                if (have) list.append(cur);
                cur = QVariantMap();
                have = true;
                continue;
            }
            const QString l = raw.trimmed();
            if (l.startsWith(QStringLiteral("Name:")))
                cur.insert(QStringLiteral("name"), l.mid(5).trimmed());
            else if (l.startsWith(QStringLiteral("Description:")))
                cur.insert(QStringLiteral("description"), l.mid(12).trimmed());
            else if (l.startsWith(QStringLiteral("Driver:")))
                cur.insert(QStringLiteral("driver"), l.mid(7).trimmed());
            else if (l.startsWith(QStringLiteral("State:")))
                cur.insert(QStringLiteral("state"), l.mid(6).trimmed());
            else if (l.startsWith(QStringLiteral("Mute:")))
                cur.insert(QStringLiteral("mute"), l.mid(5).trimmed() == QLatin1String("yes"));
            else if (l.startsWith(QStringLiteral("Sample Specification:")))
                cur.insert(QStringLiteral("spec"), l.mid(21).trimmed());
            else if (l.startsWith(QStringLiteral("Volume:")) && !cur.contains(QStringLiteral("volume"))) {
                // "Volume: front-left: 42598 /  65% / -9.29 dB, ..."
                const int pc = l.indexOf(QLatin1Char('%'));
                if (pc > 0) {
                    int st = pc - 1;
                    while (st > 0 && (l[st].isDigit() || l[st] == QLatin1Char(' '))) --st;
                    cur.insert(QStringLiteral("volume"), l.mid(st + 1, pc - st - 1).trimmed().toInt());
                }
            }
        }
        if (have) list.append(cur);
        return list;
    };
    out.insert(QStringLiteral("sinks"), parse(runPactl(QStringList() << QStringLiteral("list") << QStringLiteral("sinks")), QStringLiteral("Sink")));
    out.insert(QStringLiteral("sources"), parse(runPactl(QStringList() << QStringLiteral("list") << QStringLiteral("sources")), QStringLiteral("Source")));
    return out;
}

bool SysMon::sendSignal(int pid, int sig)
{
    if (::kill(pid, sig) == 0)
        return true;
    if (RootClient::instance()->active())
        return RootClient::instance()->sendSignal(pid, sig);
    return false;
}

bool SysMon::setNice(int pid, int nice)
{
    if (::setpriority(PRIO_PROCESS, pid, nice) == 0)
        return true;
    if (RootClient::instance()->active())
        return RootClient::instance()->setNice(pid, nice);
    return false;
}

// The power-supply class defines these words; the driver only picks one.
// Anything a vendor invented is passed through untouched.
QString SysMon::psyWord(const QString &raw) const
{
    const QString k = raw.trimmed();
    const QString l = k.toLower();
    if (l.isEmpty())                                 return k;
    if (l.contains(QLatin1String("discharging")))    return tr("discharging");
    if (l == QLatin1String("charging"))              return tr("charging");
    if (l == QLatin1String("not charging"))          return tr("not charging");
    if (l == QLatin1String("full"))                  return tr("full");
    if (l == QLatin1String("unknown"))               return tr("unknown");
    if (l == QLatin1String("good"))                  return tr("good");
    if (l == QLatin1String("overheat"))              return tr("overheat");
    if (l == QLatin1String("dead"))                  return tr("dead");
    if (l == QLatin1String("over voltage"))          return tr("over voltage");
    if (l == QLatin1String("unspecified failure"))   return tr("unspecified failure");
    if (l == QLatin1String("cold"))                  return tr("cold");
    if (l == QLatin1String("cool"))                  return tr("cool");
    if (l == QLatin1String("warm"))                  return tr("warm");
    if (l == QLatin1String("hot"))                   return tr("hot");
    if (l == QLatin1String("watchdog timer expire")) return tr("watchdog timer expired");
    if (l == QLatin1String("safety timer expire"))   return tr("safety timer expired");
    if (l == QLatin1String("calibration required"))  return tr("calibration required");
    if (l == QLatin1String("fast"))                  return tr("fast charge");
    if (l == QLatin1String("taper"))                 return tr("taper (constant voltage)");
    if (l == QLatin1String("trickle"))               return tr("trickle");
    if (l == QLatin1String("n/a"))                   return tr("not applicable");
    return k;
}

double SysMon::battGaugeFullMah() const
{
    if (m_gaugeFullMah < 0) {
        const QDir psy(QStringLiteral("/sys/class/power_supply"));
        m_gaugeFullMah = gaugeFullChargeMah(psy,
                             psy.filePath(QStringLiteral("battery")) + QLatin1Char('/'));
    }
    return m_gaugeFullMah;
}

double SysMon::battFullMah() const
{
    const double gauge = battGaugeFullMah();
    const double driver = m_s.battChargeFull;
    return battCapacityDisputed() ? gauge : (driver > 0 ? driver : gauge);
}

// The design capacity, or 0 where no source survives. The driver's figure is
// dropped rather than shown once the gauge contradicts it; the maker's is the
// only stand-in, and it is labelled as such wherever it is displayed.
double SysMon::battDesignMah() const
{
    const double driver = m_s.battChargeDesign;
    if (driver > 0 && !battCapacityDisputed())
        return driver;
    return ratedCapacityMah();
}

bool SysMon::battDesignFromMaker() const
{
    const double driver = m_s.battChargeDesign;
    return (driver <= 0 || battCapacityDisputed()) && ratedCapacityMah() > 0;
}

double SysMon::battChargeNowMah() const
{
    const double full = battFullMah();
    if (full <= 0 || m_s.battCapacity < 0)
        return 0;
    return full * m_s.battCapacity / 100.0;
}

bool SysMon::battCapacityDisputed() const
{
    // Not cached: the sampler fills the design capacity after the first
    // binding has already asked, and a cached "no" would then stand for good.
    const double design = m_s.battChargeDesign;
    const double gauge = battGaugeFullMah();
    return design > 0 && gauge > 0 && qAbs(gauge - design) > 0.15 * design;
}

QString SysMon::battQuality() const
{
    // State-of-health from full/design capacity, tempered by cycle count and
    // the driver's own health flag. Heuristic — driver data varies by device.
    const int soh = m_s.battHealthPct;
    const int cyc = m_s.battCycles;
    const QString drv = m_s.battHealthReport;
    if (!drv.isEmpty() && drv != QLatin1String("Good") && drv != QLatin1String("Unknown"))
        return psyWord(drv);   // driver reports Overheat/Cold/Dead/Over voltage etc.

    // The wording below is our reading of the number, not something the battery
    // reports: the thresholds are ours and are spelled out in the glossary. Say
    // where the number came from, so a verdict built on a calculated SoH is not
    // mistaken for one the gauge stands behind.
    QString base;
    if (soh >= 90)      base = tr("as new");
    else if (soh >= 80) base = tr("good");
    else if (soh >= 65) base = tr("aged");
    else if (soh >= 50) base = tr("worn");
    else if (soh >= 0)  base = tr("poor — consider replacement");
    else if (cyc >= 0) {
        // No SoH at all: this is the weakest case, a guess from the cycle count
        // alone, and it has to say so.
        QString c;
        if (cyc < 300)       c = tr("good");
        else if (cyc < 600)  c = tr("aged");
        else if (cyc < 1000) c = tr("worn");
        else                 c = tr("poor");
        return c;
    } else {
        return tr("unknown");
    }
    // The basis and the cycle count used to be appended here, which wrapped the
    // overview row onto a second line. The card gets the verdict alone; the
    // detail page states what it rests on.
    return base;
}

// What the quality verdict above rests on. Kept apart from the verdict so the
// overview can stay to one line while the detail page can be explicit.
QString SysMon::battQualityBasis() const
{
    const QString drv = m_s.battHealthReport;
    if (!drv.isEmpty() && drv != QLatin1String("Good") && drv != QLatin1String("Unknown"))
        return tr("reported by the driver");
    if (m_s.battHealthCatalogue)
        return m_s.battCycles >= 0
            ? tr("no state of health: full and design capacity are the same profile figure — the word above rests on the cycle count alone")
            : tr("no state of health: full and design capacity are the same profile figure");
    if (m_s.battHealthPct >= 0)
        return m_s.battHealthFromGauge
                ? tr("state of health from the gauge")
                : tr("state of health calculated from full ÷ design capacity");
    if (m_s.battCycles >= 0)
        return tr("no state of health available — estimated from the cycle count alone");
    return QString();
}

QString SysMon::fmtBytes(double b) const
{
    return humanBytes(b);
}

QString SysMon::fmtRate(double bps) const
{
    return fmtBytes(bps) + QStringLiteral("/s");
}

QString SysMon::fmtDuration(int sec) const
{
    const int d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60;
    if (d > 0)
        return QStringLiteral("%1d %2h %3m").arg(d).arg(h).arg(m);
    if (h > 0)
        return QStringLiteral("%1h %2m").arg(h).arg(m);
    return QStringLiteral("%1m %2s").arg(m).arg(sec % 60);
}

// ---- accumulated counters -------------------------------------------------
// Everything here is a tally the kernel keeps anyway; the app reads it once, on
// demand, and stores nothing. That is the whole point of the section: figures
// that would otherwise need a logging daemon are already lying around.
//
// Screen-on time is MCE's own bookkeeping. MCE holds the mce_display_on
// wakelock for exactly as long as the display is up, and the kernel sums the
// hold time in wakeup_sources. total_time already contains the hold in
// progress (verified against a blank/unblank cycle on Xperia 10 III, SFOS
// 5.1.0.11: the column tracks wall clock 1:1 while the screen is on and freezes
// while it is off), so it is read as-is.
QVariantMap SysMon::sinceBootDetail() const
{
    QVariantMap m;

    // Uptime vs. awake time: CLOCK_BOOTTIME keeps running across suspend,
    // CLOCK_MONOTONIC stops in it, so the gap between them is deep sleep.
    struct timespec ts;
    double upSec = -1, awakeSec = -1;
    if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0)
        upSec = ts.tv_sec + ts.tv_nsec / 1e9;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        awakeSec = ts.tv_sec + ts.tv_nsec / 1e9;
    if (upSec <= 0)
        upSec = readTrim(QStringLiteral("/proc/uptime")).split(QLatin1Char(' ')).value(0).toDouble();
    if (upSec > 0)
        m.insert(QStringLiteral("uptimeSec"), upSec);
    // Only claim a deep-sleep figure where the two clocks actually diverge --
    // on a kernel that ticks both in suspend the difference is noise.
    if (awakeSec > 0 && upSec > awakeSec + 1.0)
        m.insert(QStringLiteral("awakeSec"), awakeSec);

    // --- wakeup sources: screen-on time, and who keeps waking the phone -----
    const QVariantList sources = readWakeupSources();
    QVariantList wakers;
    for (const QVariant &v : sources) {
        const QVariantMap w = v.toMap();
        if (w.value(QStringLiteral("name")).toString() == QLatin1String("mce_display_on")) {
            m.insert(QStringLiteral("screenSec"), w.value(QStringLiteral("heldSec")));
            m.insert(QStringLiteral("screenCycles"), w.value(QStringLiteral("activeCount")).toInt());
            m.insert(QStringLiteral("screenLongestSec"), w.value(QStringLiteral("longestSec")));
        }
        if (w.value(QStringLiteral("count")).toDouble() > 0)
            wakers.append(w);
    }
    // Rank by wake count: the question this answers is who interrupts sleep,
    // not who holds the CPU longest once awake.
    std::sort(wakers.begin(), wakers.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("count")).toDouble()
             > b.toMap().value(QStringLiteral("count")).toDouble();
    });
    if (!wakers.isEmpty())
        m.insert(QStringLiteral("wakers"), wakers);

    // --- suspend attempts ---------------------------------------------------
    // Moved out of debugfs into /sys/power in newer kernels; try both.
    QVariantMap sus;
    static const char *susKeys[] = { "success", "fail", "failed_freeze", "failed_prepare",
                                     "failed_suspend", "failed_suspend_late",
                                     "failed_suspend_noirq", "failed_resume",
                                     "failed_resume_early", "failed_resume_noirq",
                                     "last_failed_dev", "last_failed_errno",
                                     "last_failed_step", nullptr };
    for (const QString &base : { QStringLiteral("/sys/power/suspend_stats/"),
                                 QStringLiteral("/sys/kernel/debug/suspend_stats/") }) {
        if (!QFileInfo::exists(base))
            continue;
        for (int k = 0; susKeys[k]; ++k) {
            const QString v = readTrim(base + QLatin1String(susKeys[k]));
            if (!v.isEmpty())
                sus.insert(QString::fromLatin1(susKeys[k]), v);
        }
        break;
    }
    if (!sus.isEmpty())
        m.insert(QStringLiteral("suspend"), sus);

    // --- CPU time budget ----------------------------------------------------
    // /proc/stat's first line is cumulative jiffies per state since boot.
    const long tck = sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK) : 100;
    QFile st(QStringLiteral("/proc/stat"));
    if (st.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> f = st.readLine().simplified().split(' ');
        static const char *names[] = { "user", "nice", "system", "idle",
                                       "iowait", "irq", "softirq", "steal", nullptr };
        QVariantMap cpu;
        double total = 0;
        for (int i = 0; names[i]; ++i) {
            if (f.size() <= i + 1)
                break;
            const double s = f.value(i + 1).toULongLong() / (double)tck;
            cpu.insert(QString::fromLatin1(names[i]), s);
            total += s;
        }
        if (total > 0) {
            cpu.insert(QStringLiteral("total"), total);
            m.insert(QStringLiteral("cpu"), cpu);
        }
    }

    // --- memory pressure over the whole uptime ------------------------------
    QFile vm(QStringLiteral("/proc/vmstat"));
    if (vm.open(QIODevice::ReadOnly)) {
        QVariantMap out;
        static const QSet<QByteArray> want = { "pswpin", "pswpout", "pgmajfault",
                                               "oom_kill", "pgfault" };
        for (const QByteArray &line : vm.readAll().split('\n')) {
            const int sp = line.indexOf(' ');
            if (sp < 0)
                continue;
            const QByteArray key = line.left(sp);
            if (want.contains(key))
                out.insert(QString::fromLatin1(key), line.mid(sp + 1).trimmed().toDouble());
        }
        if (!out.isEmpty())
            m.insert(QStringLiteral("vm"), out);
    }

    // --- bytes moved to and from storage ------------------------------------
    QFile ds(QStringLiteral("/proc/diskstats"));
    if (ds.open(QIODevice::ReadOnly)) {
        QVariantList disks;
        for (const QByteArray &line : ds.readAll().split('\n')) {
            const QList<QByteArray> f = line.simplified().split(' ');
            if (f.size() < 10)
                continue;
            const QString name = QString::fromLatin1(f.value(2));
            // Whole devices only: partitions repeat the same traffic, and the
            // virtual ones (zram, loop) are not what "written to flash" means.
            if (!QFileInfo::exists(QStringLiteral("/sys/block/") + name)
                    || name.startsWith(QLatin1String("loop"))
                    || name.startsWith(QLatin1String("zram"))
                    || name.startsWith(QLatin1String("dm-"))   // repeats its slave's traffic
                    || name.startsWith(QLatin1String("ram")))
                continue;
            const double rd = f.value(5).toULongLong() * 512.0;
            const double wr = f.value(9).toULongLong() * 512.0;
            if (rd + wr <= 0)
                continue;
            QVariantMap d;
            d.insert(QStringLiteral("name"), name);
            d.insert(QStringLiteral("readBytes"), rd);
            d.insert(QStringLiteral("writeBytes"), wr);
            disks.append(d);
        }
        if (!disks.isEmpty())
            m.insert(QStringLiteral("disks"), disks);
    }

    return m;
}
