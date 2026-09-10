/*
  harbour-sysmetrics — audiopolicy.cpp

  Where the volumes on this device actually come from.

  Sailfish keeps them in four different places and none of them is the one the
  settings page shows:

    ngfd/events.d/*.ini          gives each event its own volume entry
    ngfd/plugins.d/50-*.ini      binds that entry to a stored setting, or pins it
    profiled                     holds the stored setting, per profile
    PulseAudio's own databases    hold what was last applied, per role and route

  This file reads all four, without root and without changing anything, and
  says for every value which file it came from and whether a system update
  keeps it. The alarm is the case that made it necessary: its entry is bound to
  the *general* profile whatever profile is active, the platform ships it at
  100 and no part of the interface writes it — so the alarm ignores the
  ringtone slider, the volume keys and the Silent profile alike.
*/
#include "sysmon.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace {

const char *kNgfdEvents   = "/usr/share/ngfd/events.d";
const char *kNgfdPlugin   = "/usr/share/ngfd/plugins.d/50-streamrestore.ini";
const char *kProfiledDef  = "/etc/profiled/50.sailfish_default.ini";
const char *kRouteTable   = "/etc/pulse/x-maemo-route.table";
const char *kFallbackTbl  = "/etc/pulse/x-maemo-stream-restore.table";
const char *kMainVolume   = "/var/lib/nemo-pulseaudio-parameters/algs/mainvolume";
const char *kModes        = "/var/lib/nemo-pulseaudio-parameters/modes";

QString slurp(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

QString runC(const QString &prog, const QStringList &args, int ms = 4000)
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.remove(QStringLiteral("LANGUAGE"));
    p.setProcessEnvironment(env);
    p.start(prog, args);
    if (!p.waitForFinished(ms)) {
        p.kill();
        p.waitForFinished(500);
        return QString();
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

// ---- PulseAudio's two databases -------------------------------------------
// Both are the "simple" format: u32 key length, key, u32 data length, data.
// The stream database carries a tagstruct, the route database a plain struct
// with the padding the compiler left in. Decoded rather than guessed: the
// entries 50-streamrestore.ini pins by hand come out as the numbers that file
// states (100, 40, 0, 50, 50), which is what makes the readings trustworthy.

quint32 leU32(const QByteArray &b, int off)
{
    return (quint8)b.at(off) | ((quint32)(quint8)b.at(off + 1) << 8)
         | ((quint32)(quint8)b.at(off + 2) << 16) | ((quint32)(quint8)b.at(off + 3) << 24);
}

quint32 beU32(const QByteArray &b, int off)
{
    return ((quint32)(quint8)b.at(off) << 24) | ((quint32)(quint8)b.at(off + 1) << 16)
         | ((quint32)(quint8)b.at(off + 2) << 8) | (quint8)b.at(off + 3);
}

QList<QPair<QString, QByteArray> > readSimpleDb(const QString &path)
{
    QList<QPair<QString, QByteArray> > out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QByteArray b = f.readAll();
    int off = 0;
    while (off + 4 <= b.size()) {
        const quint32 klen = leU32(b, off);
        if (klen == 0 || klen > 200 || off + 4 + (int)klen + 4 > b.size())
            break;
        const QString key = QString::fromUtf8(b.mid(off + 4, klen));
        off += 4 + klen;
        const quint32 dlen = leU32(b, off);
        if (off + 4 + (int)dlen > b.size())
            break;
        out.append(qMakePair(key, b.mid(off + 4, dlen)));
        off += 4 + dlen;
    }
    return out;
}

double tagVolume(const QByteArray &d, bool *ok)
{
    *ok = false;
    int i = 0;
    while (i < d.size()) {
        const quint8 t = (quint8)d.at(i);
        if (t == 0x42) i += 2;                                   // u8
        else if (t == 0x30 || t == 0x31) i += 1;                 // boolean
        else if (t == 0x6d) {                                    // channel map
            if (i + 1 >= d.size()) return 0;
            i += 2 + (quint8)d.at(i + 1);
        } else if (t == 0x76) {                                  // cvolume
            if (i + 1 >= d.size()) return 0;
            const int n = (quint8)d.at(i + 1);
            if (n < 1 || i + 2 + 4 * n > d.size()) return 0;
            double sum = 0;
            for (int c = 0; c < n; ++c)
                sum += beU32(d, i + 2 + 4 * c);
            *ok = true;
            return sum / n / 65536.0 * 100.0;
        } else if (t == 0x74) {                                  // string
            const int nul = d.indexOf('\0', i + 1);
            if (nul < 0) return 0;
            i = nul + 1;
        } else if (t == 0x4e) i += 1;                            // empty string
        else if (t == 0x4c) i += 5;                              // u32
        else return 0;
    }
    return 0;
}

double rawVolume(const QByteArray &d, bool *ok)
{
    *ok = false;
    if (d.size() < 12 || leU32(d, 0) != 4)
        return 0;
    const int n = (quint8)d.at(4);
    if (n < 1 || n > 8 || d.size() < 8 + 4 * n)
        return 0;
    double sum = 0;
    for (int c = 0; c < n; ++c) {
        const quint32 v = leU32(d, 8 + 4 * c);
        if (v > 0x20000)
            return 0;
        sum += v;
    }
    *ok = true;
    return sum / n / 65536.0 * 100.0;
}

QString pulseDb(const QString &suffix)
{
    const QDir d(QDir::homePath() + QStringLiteral("/.config/pulse"));
    const QStringList m = d.entryList(QStringList() << (QStringLiteral("*") + suffix), QDir::Files);
    return m.isEmpty() ? QString() : d.filePath(m.first());
}

// ---- profiled --------------------------------------------------------------

QString profiledValue(const QString &profile, const QString &key)
{
    QDBusInterface iface(QStringLiteral("com.nokia.profiled"),
                         QStringLiteral("/com/nokia/profiled"),
                         QStringLiteral("com.nokia.profiled"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return QString();
    const QDBusReply<QString> r = iface.call(QStringLiteral("get_value"), profile, key);
    return r.isValid() ? r.value() : QString();
}

// ---- what a system update does to a file -----------------------------------

QVariantMap fileFate(const QString &path)
{
    QVariantMap m;
    m.insert(QStringLiteral("path"), path);
    m.insert(QStringLiteral("exists"), QFileInfo::exists(path));

    const QString home = QDir::homePath();
    if (!home.isEmpty() && path.startsWith(home + QLatin1Char('/'))) {
        m.insert(QStringLiteral("owner"), QString());
        m.insert(QStringLiteral("fate"), QStringLiteral("user"));
        return m;
    }
    const QString pkg = runC(QStringLiteral("rpm"), QStringList() << QStringLiteral("-qf") << path);
    if (pkg.isEmpty() || pkg.contains(QStringLiteral("not owned"))) {
        m.insert(QStringLiteral("owner"), QString());
        m.insert(QStringLiteral("fate"), QStringLiteral("unowned"));
        return m;
    }
    const QString name = pkg.split(QLatin1Char('\n')).first().trimmed();
    m.insert(QStringLiteral("owner"), name);
    const QString conf = runC(QStringLiteral("rpm"), QStringList() << QStringLiteral("-qc") << name);
    m.insert(QStringLiteral("fate"), conf.split(QLatin1Char('\n')).contains(path)
                                         ? QStringLiteral("config") : QStringLiteral("replaced"));
    return m;
}

// ---- the step tables -------------------------------------------------------

QVariantMap curveOf(const QString &raw)
{
    QVariantMap m;
    QList<int> steps;
    const QStringList parts = raw.split(QLatin1Char(','), QString::SkipEmptyParts);
    for (const QString &p : parts) {
        const int c = p.indexOf(QLatin1Char(':'));
        if (c < 0)
            continue;
        bool ok = false;
        const int mb = p.mid(c + 1).trimmed().toInt(&ok);
        if (ok)
            steps.append(mb);
    }
    m.insert(QStringLiteral("count"), steps.size());
    if (steps.isEmpty())
        return m;
    int lo = steps.first(), hi = steps.first(), gap = 0, gapAt = -1;
    for (int i = 0; i < steps.size(); ++i) {
        lo = qMin(lo, steps.at(i));
        hi = qMax(hi, steps.at(i));
        if (i > 0) {
            const int d = qAbs(steps.at(i) - steps.at(i - 1));
            if (d > gap) { gap = d; gapAt = i - 1; }
        }
    }
    m.insert(QStringLiteral("lowDb"), lo / 100.0);
    m.insert(QStringLiteral("highDb"), hi / 100.0);
    m.insert(QStringLiteral("gapDb"), gap / 100.0);
    m.insert(QStringLiteral("gapAt"), gapAt);
    return m;
}

} // namespace

QVariantMap SysMon::audioPolicy() const
{
    QVariantMap out;

    // --- the bindings the feedback daemon declares ---------------------------
    // role.<entry> = profile.<which>.<key>   binds an entry to a stored setting
    // set.<entry>  = <number>                pins one outright
    QVariantList bindings, pinned;
    QHash<QString, QString> boundKey, boundScope;
    {
        const QStringList lines = slurp(QLatin1String(kNgfdPlugin)).split(QLatin1Char('\n'));
        const QRegularExpression roleRe(
            QStringLiteral("^role\\.([^=\\s]+)\\s*=\\s*profile\\.(current|general)\\.(\\S+)"));
        const QRegularExpression setRe(QStringLiteral("^set\\.([^=\\s]+)\\s*=\\s*(\\d+)"));
        for (const QString &raw : lines) {
            const QString l = raw.trimmed();
            const QRegularExpressionMatch rm = roleRe.match(l);
            if (rm.hasMatch()) {
                const QString entry = rm.captured(1), scope = rm.captured(2), key = rm.captured(3);
                boundKey.insert(entry, key);
                boundScope.insert(entry, scope);
                QVariantMap b;
                b.insert(QStringLiteral("entry"), entry);
                b.insert(QStringLiteral("scope"), scope);
                b.insert(QStringLiteral("key"), key);
                b.insert(QStringLiteral("value"), profiledValue(scope == QLatin1String("general")
                                                                    ? QStringLiteral("general")
                                                                    : currentProfile(), key));
                bindings.append(b);
                continue;
            }
            const QRegularExpressionMatch sm = setRe.match(l);
            if (sm.hasMatch()) {
                QVariantMap b;
                b.insert(QStringLiteral("entry"), sm.captured(1));
                b.insert(QStringLiteral("value"), sm.captured(2).toInt());
                pinned.append(b);
            }
        }
    }
    out.insert(QStringLiteral("bindings"), bindings);
    out.insert(QStringLiteral("pinned"), pinned);

    // --- what PulseAudio last applied ---------------------------------------
    QVariantList named, perRoute;
    const QString streamFile = pulseDb(QStringLiteral("-stream-volumes.simple"));
    const QString routeFile = pulseDb(QStringLiteral("-x-maemo-route-volumes.simple"));

    for (const auto &e : readSimpleDb(streamFile)) {
        if (!e.first.startsWith(QStringLiteral("x-")))
            continue;
        bool ok = false;
        const double pc = tagVolume(e.second, &ok);
        if (!ok)
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("entry"), e.first);
        m.insert(QStringLiteral("percent"), pc);
        m.insert(QStringLiteral("key"), boundKey.value(e.first));
        m.insert(QStringLiteral("scope"), boundScope.value(e.first));
        named.append(m);
    }
    const QString rolePrefix = QStringLiteral("sink-input-by-media-role:");
    for (const auto &e : readSimpleDb(routeFile)) {
        if (!e.first.startsWith(rolePrefix))
            continue;
        const QString rest = e.first.mid(rolePrefix.size());
        const int c = rest.indexOf(QLatin1Char(':'));
        if (c < 0)
            continue;
        bool ok = false;
        const double pc = rawVolume(e.second, &ok);
        if (!ok)
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("role"), rest.left(c));
        m.insert(QStringLiteral("route"), rest.mid(c + 1));
        m.insert(QStringLiteral("percent"), pc);
        perRoute.append(m);
    }
    out.insert(QStringLiteral("named"), named);
    out.insert(QStringLiteral("perRoute"), perRoute);

    // --- the alarm, end to end ----------------------------------------------
    {
        QVariantMap alarm;
        const QString clock = slurp(QString(QLatin1String(kNgfdEvents)) + QStringLiteral("/clock.ini"));
        const QRegularExpression idRe(
            QStringLiteral("sound\\.stream\\.module-stream-restore\\.id\\s*=\\s*(\\S+)"));
        const QRegularExpression roleRe(QStringLiteral("sound\\.stream\\.media\\.role\\s*=\\s*(\\S+)"));
        const QRegularExpression fadeRe(QStringLiteral("sound\\.fade-in\\s*=\\s*(\\S+)"));
        const QRegularExpressionMatch im = idRe.match(clock), rm = roleRe.match(clock),
                                      fm = fadeRe.match(clock);
        const QString entry = im.hasMatch() ? im.captured(1) : QString();
        alarm.insert(QStringLiteral("entry"), entry);
        alarm.insert(QStringLiteral("mediaRole"), rm.hasMatch() ? rm.captured(1) : QString());
        alarm.insert(QStringLiteral("fadeIn"), fm.hasMatch() ? fm.captured(1) : QString());
        alarm.insert(QStringLiteral("key"), boundKey.value(entry));
        alarm.insert(QStringLiteral("scope"), boundScope.value(entry));
        alarm.insert(QStringLiteral("value"),
                     profiledValue(QStringLiteral("general"), boundKey.value(entry)));
        alarm.insert(QStringLiteral("ringValue"),
                     profiledValue(currentProfile(), QStringLiteral("ringing.alert.volume")));
        out.insert(QStringLiteral("alarm"), alarm);
    }

    // --- the fallback each route starts from --------------------------------
    QVariantList fallbacks;
    for (const QString &raw : slurp(QLatin1String(kRouteTable)).split(QLatin1Char('\n'))) {
        const QString l = raw.trimmed();
        if (l.isEmpty())
            continue;
        const int sp = l.lastIndexOf(QLatin1Char(' '));
        if (sp < 0)
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("entry"), l.left(sp).trimmed());
        m.insert(QStringLiteral("db"), l.mid(sp + 1).trimmed().toInt());
        fallbacks.append(m);
    }
    out.insert(QStringLiteral("fallbacks"), fallbacks);

    // --- the step tables, and which routes share one ------------------------
    QVariantList tables;
    {
        QHash<QString, QStringList> users;
        const QDir modes = QDir(QString::fromLatin1(kModes));
        for (const QString &mode : modes.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QFileInfo link(modes.filePath(mode) + QStringLiteral("/mainvolume"));
            if (link.exists())
                users[QFileInfo(link.canonicalFilePath()).fileName()].append(mode);
        }
        const QDir algs = QDir(QString::fromLatin1(kMainVolume));
        const QRegularExpression re(
            QStringLiteral("x-nemo\\.mainvolume\\.([a-z0-9-]+)\\s*=\\s*\"([^\"]*)\""));
        for (const QString &name : algs.entryList(QDir::Files, QDir::Name)) {
            const QString text = slurp(algs.filePath(name));
            if (text.isEmpty())
                continue;
            QVariantMap t;
            t.insert(QStringLiteral("name"), name);
            t.insert(QStringLiteral("modes"), users.value(name));
            QVariantMap curves;
            QRegularExpressionMatchIterator it = re.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                if (m.captured(1) == QLatin1String("high-volume-step"))
                    t.insert(QStringLiteral("warnStep"), m.captured(2));
                else
                    curves.insert(m.captured(1), curveOf(m.captured(2)));
            }
            t.insert(QStringLiteral("curves"), curves);
            tables.append(t);
        }
    }
    out.insert(QStringLiteral("tables"), tables);

    // --- Bluetooth: who is holding the volume -------------------------------
    {
        QVariantMap bt;
        const QString sinks = runC(QStringLiteral("pactl"),
                                   QStringList() << QStringLiteral("list") << QStringLiteral("sinks"));
        QString name, vol;
        bool found = false;
        for (const QString &raw : sinks.split(QLatin1Char('\n'))) {
            const QString l = raw.trimmed();
            if (l.startsWith(QStringLiteral("Sink #"))) {
                if (found)
                    break;
                name.clear(); vol.clear();
            } else if (l.startsWith(QStringLiteral("Name:"))) {
                name = l.mid(5).trimmed();
                found = name.contains(QStringLiteral("bluez_sink"));
            } else if (found && l.startsWith(QStringLiteral("Volume:")) && vol.isEmpty()) {
                const int pc = l.indexOf(QLatin1Char('%'));
                if (pc > 0) {
                    int st = pc - 1;
                    while (st > 0 && (l.at(st).isDigit() || l.at(st) == QLatin1Char(' ')))
                        --st;
                    vol = l.mid(st + 1, pc - st - 1).trimmed();
                }
            }
        }
        bt.insert(QStringLiteral("present"), found);
        bt.insert(QStringLiteral("sink"), name);
        bt.insert(QStringLiteral("percent"), vol.toInt());

        // The module argument decides whether the number above is applied here
        // or handed to the headphones.
        const QString mods = runC(QStringLiteral("pactl"),
                                  QStringList() << QStringLiteral("list") << QStringLiteral("modules"));
        bt.insert(QStringLiteral("avrcpAbsolute"),
                  mods.contains(QStringLiteral("avrcp_absolute_volume=1")));
        out.insert(QStringLiteral("bluetooth"), bt);
    }

    // --- every file involved, and what an update does to it -----------------
    QVariantList files;
    const QStringList paths = QStringList()
        << (QString(QLatin1String(kNgfdEvents)) + QStringLiteral("/clock.ini"))
        << (QString(QLatin1String(kNgfdEvents)) + QStringLiteral("/ringtone.ini"))
        << QLatin1String(kNgfdPlugin)
        << QLatin1String(kProfiledDef)
        << (QDir::homePath() + QStringLiteral("/.config/profiled/custom.ini"))
        << streamFile << routeFile
        << QLatin1String(kRouteTable) << QLatin1String(kFallbackTbl);
    for (const QString &p : paths)
        if (!p.isEmpty())
            files.append(fileFate(p));
    {
        const QDir algs = QDir(QString::fromLatin1(kMainVolume));
        const QStringList names = algs.entryList(QDir::Files, QDir::Name);
        if (!names.isEmpty())
            files.append(fileFate(algs.filePath(names.first())));
    }
    out.insert(QStringLiteral("files"), files);

    return out;
}

QString SysMon::currentProfile() const
{
    QDBusInterface iface(QStringLiteral("com.nokia.profiled"),
                         QStringLiteral("/com/nokia/profiled"),
                         QStringLiteral("com.nokia.profiled"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return QStringLiteral("general");
    const QDBusReply<QString> r = iface.call(QStringLiteral("get_profile"));
    return r.isValid() && !r.value().isEmpty() ? r.value() : QStringLiteral("general");
}
