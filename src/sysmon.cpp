#include "sysmon.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusObjectPath>

#include "rootclient.h"

#include <signal.h>
#include <sys/resource.h>
#include <sys/statvfs.h>

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
    // discharge positive, charge negative: drain graph reads upward
    const double drain = snap.battStatus == QLatin1String("Discharging") ? snap.battPowerW : -snap.battPowerW;
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
        l.append(k / 1000);
    return l;
}

QVariantList SysMon::thermalZones() const
{
    QVariantList l;
    for (const auto &z : m_s.thermal) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), z.first);
        m.insert(QStringLiteral("temp"), (double)z.second);
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

    QSet<QString> seenMp;
    for (const QByteArray &line : f.readAll().split('\n')) {
        const QList<QByteArray> c = line.split(' ');
        if (c.size() < 3)
            continue;
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
        if (seenMp.contains(mount))
            continue;

        struct statvfs vfs;
        if (statvfs(mp.constData(), &vfs) != 0 || vfs.f_blocks == 0)
            continue;
        seenMp.insert(mount);

        const double frsize = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
        const double total = (double)vfs.f_blocks * frsize;
        const double avail = (double)vfs.f_bavail * frsize;
        const double free = (double)vfs.f_bfree * frsize;
        const double used = total - free;

        QVariantMap m;
        m.insert(QStringLiteral("device"), QString::fromLocal8Bit(c[0]));
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
        const bool mmc = name.startsWith(QLatin1String("mmcblk")) && !name.contains(QLatin1Char('p'));
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
            m.insert(QStringLiteral("bus"), type.isEmpty() ? QStringLiteral("eMMC") : type.toUpper());
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
                    m.insert(QStringLiteral("writeBooster"), wbt > 0 || (rd(dd + QStringLiteral("ufs_features")).toInt(nullptr,16) & 0x1));
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
            m.insert(QStringLiteral("lifeUsedPct"), worst > 0 ? (worst - 1) * 10 : 0); // lower bound of 10% band
            m.insert(QStringLiteral("preEol"), eol);
            QString verdict;
            if (eol >= 3) verdict = QStringLiteral("urgent");
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
    if (drv.contains(QStringLiteral("cnss")) || drv.contains(QStringLiteral("icnss"))
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
                const int f = l.mid(5).trimmed().toInt();
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
            const int mhz = l.section(QStringLiteral(" MHz"), 0, 0).section(QLatin1Char('*'), -1).trimmed().toInt();
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

QVariantList SysMon::networkHardware() const
{
    QVariantList out;
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
            if (model.isEmpty())
                model = rd(dev + QStringLiteral("modalias"));
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
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromLatin1(f.readAll().trimmed());
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
    m.insert(QStringLiteral("cycles"), readTrim(b + QStringLiteral("cycle_count")));
    double designUah = readTrim(b + QStringLiteral("charge_full_design")).toDouble();
    double fullUah = readTrim(b + QStringLiteral("charge_full")).toDouble();
    QString capUnit = QStringLiteral("mAh");
    if (designUah <= 0) { // energy-reporting gauge (µWh)
        designUah = readTrim(b + QStringLiteral("energy_full_design")).toDouble();
        fullUah = readTrim(b + QStringLiteral("energy_full")).toDouble();
        capUnit = QStringLiteral("mWh");
    }
    m.insert(QStringLiteral("designCapacity"), designUah / 1000.0);
    m.insert(QStringLiteral("fullCapacity"), fullUah / 1000.0);
    m.insert(QStringLiteral("capacityUnit"), capUnit);
    m.insert(QStringLiteral("voltageDesign"),
             readTrim(b + QStringLiteral("voltage_max_design")).toDouble() / 1e6);
    m.insert(QStringLiteral("chargeType"), readTrim(b + QStringLiteral("charge_type")));
    return m;
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
    if (src.isEmpty())
        return m;

    const QString b = QStringLiteral("/sys/class/power_supply/") + src + QLatin1Char('/');
    QString type = readTrim(b + QStringLiteral("real_type"));
    if (type.isEmpty())
        type = readTrim(b + QStringLiteral("type"));
    m.insert(QStringLiteral("source"), src);
    m.insert(QStringLiteral("typeRaw"), type);
    m.insert(QStringLiteral("protocol"), chargerProtocol(type));
    m.insert(QStringLiteral("inputVoltage"), readTrim(b + QStringLiteral("voltage_now")).toDouble() / 1e6);
    m.insert(QStringLiteral("inputVoltageMax"), readTrim(b + QStringLiteral("voltage_max")).toDouble() / 1e6);
    m.insert(QStringLiteral("inputCurrentMax"), readTrim(b + QStringLiteral("current_max")).toDouble() / 1e6);
    const QString pd = readTrim(b + QStringLiteral("pd_active"));
    if (!pd.isEmpty()) {
        m.insert(QStringLiteral("pdActive"), pd.toInt() > 0);
        m.insert(QStringLiteral("pdCurrentMax"), readTrim(b + QStringLiteral("pd_current_max")).toDouble() / 1e6);
    }
    // actual rate into the battery
    const QString bat = QStringLiteral("/sys/class/power_supply/battery/");
    const double ci = readTrim(bat + QStringLiteral("current_now")).toDouble() / 1e6;
    const double bv = readTrim(bat + QStringLiteral("voltage_now")).toDouble() / 1e6;
    m.insert(QStringLiteral("chargeType"), readTrim(bat + QStringLiteral("charge_type")));
    m.insert(QStringLiteral("chargeCurrent"), qAbs(ci));
    m.insert(QStringLiteral("batteryVoltage"), bv);
    m.insert(QStringLiteral("chargePower"), qAbs(ci) * bv);

    // Type-C negotiated roles / partner / cable (from the tcpm/typec class)
    const QString tc = QStringLiteral("/sys/class/typec/port0/");
    if (QFileInfo::exists(tc)) {
        m.insert(QStringLiteral("typecPowerRole"), readTrim(tc + QStringLiteral("power_role")));
        m.insert(QStringLiteral("typecDataRole"), readTrim(tc + QStringLiteral("data_role")));
        // current the port/cable advertises via CC: "default", "1.5A", "3.0A"
        m.insert(QStringLiteral("typecCurrent"), readTrim(tc + QStringLiteral("power_operation_mode")));
        m.insert(QStringLiteral("pdRevision"), readTrim(tc + QStringLiteral("usb_power_delivery_revision")));
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
    return m;
}

QVariantMap SysMon::memoryDetail() const
{
    QVariantMap m;
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
    // Jolla marketing name + hw model from the hardware adaptation release
    QString hwName = osField(QStringLiteral("/etc/hw-release"), "NAME");
    if (hwName.isEmpty())
        hwName = osField(QStringLiteral("/etc/hw-release"), "MER_HA_DEVICE");
    m.insert(QStringLiteral("deviceName"), hwName);
    m.insert(QStringLiteral("deviceModel"),
             osField(QStringLiteral("/etc/hw-release"), "HW_DEVICE_MODEL"));
    m.insert(QStringLiteral("hwVersion"), osField(QStringLiteral("/etc/hw-release"), "VERSION_ID"));
    return m;
}

QVariantMap SysMon::graphicsDetail() const
{
    QVariantMap m;
    // GPU: Adreno (kgsl) first, then a generic devfreq gpu node
    const QString kgsl = QStringLiteral("/sys/class/kgsl/kgsl-3d0/");
    QString gpuModel = readTrim(kgsl + QStringLiteral("gpu_model"));
    double curHz = readTrim(kgsl + QStringLiteral("gpuclk")).toDouble();
    double maxHz = readTrim(kgsl + QStringLiteral("max_gpuclk")).toDouble();
    int busy = -1;
    const QString bp = readTrim(kgsl + QStringLiteral("gpu_busy_percentage"));
    if (!bp.isEmpty())
        busy = bp.split(QLatin1Char(' ')).value(0).remove(QLatin1Char('%')).toInt();

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
    m.insert(QStringLiteral("gpuModel"), gpuModel);
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
    return out;
}

QVariantMap SysMon::cameraDetail() const
{
    QVariantMap m;
    QVariantList captureNodes, subdevs;
    int sensors = 0, eeproms = 0, flashes = 0;
    bool isp = false, cpas = false;
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
        } else {
            const QString ln = name.toLower();
            if (ln.contains(QLatin1String("sensor"))) ++sensors;
            else if (ln.contains(QLatin1String("eeprom"))) ++eeproms;
            else if (ln.contains(QLatin1String("flash"))) ++flashes;
            else if (ln.contains(QLatin1String("isp"))) isp = true;
            else if (ln.contains(QLatin1String("cpas"))) cpas = true;
            QVariantMap sd;
            sd.insert(QStringLiteral("name"), e);
            sd.insert(QStringLiteral("label"), name);
            subdevs.append(sd);
        }
    }
    // sensor models from vendor camera modules (CAMX exposes no V4L2 caps)
    QVariantList cameras;
    QStringList seenCam;
    const QStringList camDirs = {
        QStringLiteral("/vendor/lib64/camera"), QStringLiteral("/vendor/lib/camera"),
        QStringLiteral("/odm/lib64/camera"),    QStringLiteral("/odm/lib/camera") };
    for (const QString &cd : camDirs) {
        QDir dir(cd);
        if (!dir.exists()) continue;
        const QStringList mods = dir.entryList(QStringList() << QStringLiteral("*sensormodule*.bin"), QDir::Files);
        for (const QString &f : mods) {
            const int a = f.indexOf(QLatin1String("sensormodule."));
            if (a < 0) continue;
            QString tag = f.mid(a + 13);
            if (tag.endsWith(QLatin1String(".bin"))) tag.chop(4);
            const int us = tag.indexOf(QLatin1Char('_'));
            const QString model = us < 0 ? tag : tag.left(us);
            const QString role  = us < 0 ? QString() : tag.mid(us + 1);
            const QString key = model + QLatin1Char('/') + role;
            if (seenCam.contains(key)) continue;
            seenCam.append(key);
            QString maker;
            if (model.startsWith(QLatin1String("imx"))) maker = QStringLiteral("Sony");
            else if (model.startsWith(QLatin1String("ov"))) maker = QStringLiteral("OmniVision");
            else if (model.startsWith(QLatin1String("s5k"))) maker = QStringLiteral("Samsung");
            else if (model.startsWith(QLatin1String("hi"))) maker = QStringLiteral("SK Hynix");
            else if (model.startsWith(QLatin1String("gc"))) maker = QStringLiteral("GalaxyCore");
            QVariantMap c;
            c.insert(QStringLiteral("model"), model);
            c.insert(QStringLiteral("maker"), maker);
            c.insert(QStringLiteral("role"),  role);
            cameras.append(c);
        }
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

QString SysMon::battQuality() const
{
    // State-of-health from full/design capacity, tempered by cycle count and
    // the driver's own health flag. Heuristic — driver data varies by device.
    const int soh = m_s.battHealthPct;
    const int cyc = m_s.battCycles;
    const QString drv = m_s.battHealthReport;
    if (!drv.isEmpty() && drv != QLatin1String("Good") && drv != QLatin1String("Unknown"))
        return drv;   // driver reports Overheat/Cold/Dead/Over voltage etc.

    QString base;
    if (soh >= 90)      base = tr("as new");
    else if (soh >= 80) base = tr("good");
    else if (soh >= 65) base = tr("aged");
    else if (soh >= 50) base = tr("worn");
    else if (soh >= 0)  base = tr("poor — consider replacement");
    else if (cyc >= 0) {
        // no SoH available: fall back to cycle count alone
        if (cyc < 300)      return tr("good (%1 cycles)").arg(cyc);
        if (cyc < 600)      return tr("aged (%1 cycles)").arg(cyc);
        if (cyc < 1000)     return tr("worn (%1 cycles)").arg(cyc);
        return tr("poor (%1 cycles)").arg(cyc);
    } else {
        return tr("unknown");
    }
    if (cyc > 0)
        base += tr(" · %1 cycles").arg(cyc);
    return base;
}

QString SysMon::fmtBytes(double b) const
{
    if (b < 0)
        b = 0;
    if (b >= 1073741824.0)
        return QString::number(b / 1073741824.0, 'f', 2) + QStringLiteral(" GB");
    if (b >= 1048576.0)
        return QString::number(b / 1048576.0, 'f', 1) + QStringLiteral(" MB");
    if (b >= 1024.0)
        return QString::number(b / 1024.0, 'f', 0) + QStringLiteral(" kB");
    return QString::number(b, 'f', 0) + QStringLiteral(" B");
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
