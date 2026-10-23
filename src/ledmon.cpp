#include "ledmon.h"

#include "rootclient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const QString kClass = QStringLiteral("/sys/class/leds/");

QString readSmall(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromLatin1(f.read(4096)).trimmed();
}

int readInt(const QString &path, int fallback = -1)
{
    bool ok = false;
    const int v = readSmall(path).toInt(&ok);
    return ok ? v : fallback;
}

bool writeInt(const QString &path, int value)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(QByteArray::number(value)) > 0;
}

// "rw-rw-rw- system:system"
QString permOf(const QString &path)
{
    struct stat st;
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return QString();
    QString p;
    const char rwx[] = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i)
        p += (st.st_mode & (0400 >> i)) ? QLatin1Char(rwx[i]) : QLatin1Char('-');
    const QFileInfo fi(path);
    return p + QLatin1Char(' ') + fi.owner() + QLatin1Char(':') + fi.group();
}

bool nameOk(const QString &name)
{
    return !name.isEmpty() && !name.contains(QLatin1Char('/')) && name != QLatin1String(".")
           && name != QLatin1String("..") && QFileInfo(kClass + name).isDir();
}

int kindRank(const QString &kind)
{
    static const QStringList order = { QStringLiteral("indicator"), QStringLiteral("backlight"),
                                       QStringLiteral("vibrator"), QStringLiteral("flash"),
                                       QStringLiteral("other") };
    return order.indexOf(kind);
}

} // namespace

QString Leds::driverOf(const QString &name)
{
    return QFileInfo(QFileInfo(kClass + name + QStringLiteral("/device/driver"))
                         .canonicalFilePath()).fileName();
}

QString Leds::kindOf(const QString &name, const QString &driver)
{
    const QString n = name.toLower();
    const QString d = driver.toLower();
    if (n.contains(QLatin1String("vibrator")))
        return QStringLiteral("vibrator");
    if (n.contains(QLatin1String("backlight")) || n.startsWith(QLatin1String("lcd")))
        return QStringLiteral("backlight");
    if (n.contains(QLatin1String("flash")) || n.contains(QLatin1String("torch"))
        || n.contains(QLatin1String("switch")) || d.contains(QLatin1String("flash")))
        return QStringLiteral("flash");
    // Names follow "device:colour:function" or are a bare colour.
    static const QStringList indicator = {
        QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"),
        QStringLiteral("white"), QStringLiteral("amber"), QStringLiteral("yellow"),
        QStringLiteral("orange"), QStringLiteral("rgb"), QStringLiteral("indicator"),
        QStringLiteral("status")
    };
    for (const QString &part : n.split(QRegExp(QStringLiteral("[:_\\-]")), QString::SkipEmptyParts))
        if (indicator.contains(part))
            return QStringLiteral("indicator");
    return QStringLiteral("other");
}

QString Leds::currentTrigger(const QString &name)
{
    const QString t = readSmall(kClass + name + QStringLiteral("/trigger"));
    const int a = t.indexOf(QLatin1Char('['));
    const int b = t.indexOf(QLatin1Char(']'), a + 1);
    return a >= 0 && b > a ? t.mid(a + 1, b - a - 1) : QString();
}

QString Leds::patternAttr(const QString &name)
{
    for (const char *a : { "blink", "breath" })
        if (QFileInfo::exists(kClass + name + QLatin1Char('/') + QLatin1String(a)))
            return QLatin1String(a);
    return QString();
}

bool Leds::helperWrite(const QString &name, const QString &attr, int value)
{
    if (!nameOk(name) || kindOf(name, driverOf(name)) != QLatin1String("indicator"))
        return false;
    const QString trig = currentTrigger(name);
    if (!trig.isEmpty() && trig != QLatin1String("none"))
        return false;
    if (attr == QLatin1String("brightness")) {
        const int max = readInt(kClass + name + QStringLiteral("/max_brightness"));
        if (value < 0 || value > max)
            return false;
    } else if (attr.isEmpty() || attr != patternAttr(name) || (value != 0 && value != 1)) {
        return false;
    }
    return writeInt(kClass + name + QLatin1Char('/') + attr, value);
}

LedMon::LedMon(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &LedMon::next);
    m_vibTimer.setInterval(400);
    connect(&m_vibTimer, &QTimer::timeout, this, &LedMon::pulse);
    refresh();
}

LedMon::~LedMon()
{
    if (m_running)
        restore();
    if (m_vibrating)
        endVibration(true);
}

void LedMon::refresh()
{
    if (m_running)
        return;
    m_leds.clear();
    m_max.clear();
    m_indicators = 0;
    m_vib.clear();
    m_vibWritable = false;
    QList<QVariantMap> rows;
    const QStringList names = QDir(kClass).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        const QString dir = kClass + name + QLatin1Char('/');
        const QString driver = Leds::driverOf(name);
        const QString kind = Leds::kindOf(name, driver);
        QVariantMap m;
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("kind"), kind);
        m.insert(QStringLiteral("driver"), driver);
        QString compat = readSmall(dir + QStringLiteral("device/of_node/compatible"));
        compat.replace(QChar(0), QLatin1Char(' '));
        m.insert(QStringLiteral("compatible"), compat.trimmed());
        m.insert(QStringLiteral("device"),
                 QFileInfo(QFileInfo(dir + QStringLiteral("device")).canonicalFilePath()).fileName());
        const int max = readInt(dir + QStringLiteral("max_brightness"));
        m_max.insert(name, max);
        m.insert(QStringLiteral("max"), max);
        // brightness only where reading it is known to be just a read
        const bool readable = kind == QLatin1String("indicator") || kind == QLatin1String("backlight");
        m.insert(QStringLiteral("brightness"),
                 readable ? readInt(dir + QStringLiteral("brightness")) : -1);
        const QString trig = Leds::currentTrigger(name);
        m.insert(QStringLiteral("trigger"), trig);
        m.insert(QStringLiteral("triggerCount"),
                 readSmall(dir + QStringLiteral("trigger")).split(QLatin1Char(' '), QString::SkipEmptyParts).size());
        const bool writable = ::access(QFile::encodeName(dir + QStringLiteral("brightness")).constData(), W_OK) == 0;
        m.insert(QStringLiteral("writable"), writable);
        const QString pattern = Leds::patternAttr(name);
        m.insert(QStringLiteral("pattern"), pattern);
        m.insert(QStringLiteral("patternWritable"),
                 !pattern.isEmpty()
                     && ::access(QFile::encodeName(dir + pattern).constData(), W_OK) == 0);
        const bool free = trig.isEmpty() || trig == QLatin1String("none");
        m.insert(QStringLiteral("testable"), kind == QLatin1String("indicator") && free && max > 0);
        QVariantList attrs;
        for (const QFileInfo &fi : QDir(dir).entryInfoList(QDir::Files | QDir::System, QDir::Name)) {
            if (fi.fileName() == QLatin1String("uevent"))
                continue;
            QVariantMap a;
            a.insert(QStringLiteral("name"), fi.fileName());
            a.insert(QStringLiteral("perm"), permOf(fi.absoluteFilePath()));
            attrs << a;
        }
        m.insert(QStringLiteral("attrs"), attrs);
        if (kind == QLatin1String("indicator"))
            ++m_indicators;
        if (kind == QLatin1String("vibrator") && m_vib.isEmpty()
            && QFileInfo::exists(dir + QStringLiteral("activate"))
            && QFileInfo::exists(dir + QStringLiteral("duration"))) {
            m_vib = name;
            // The transient trigger holds the LED at "state" while active;
            // state 0 means a pulse that does nothing.
            m_vibTransient = trig == QLatin1String("transient");
            auto w = [&dir](const char *f) {
                return ::access(QFile::encodeName(dir + QLatin1String(f)).constData(), W_OK) == 0;
            };
            m_vibWritable = w("activate") && w("duration") && (!m_vibTransient || w("state"));
        }
        rows << m;
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return kindRank(a.value(QStringLiteral("kind")).toString())
               < kindRank(b.value(QStringLiteral("kind")).toString());
    });
    for (const QVariantMap &m : rows)
        m_leds << m;
    emit ledsChanged();
}

QStringList LedMon::testChannels(bool needPattern) const
{
    const bool root = RootClient::instance()->active();
    QStringList out;
    for (const QVariant &v : m_leds) {
        const QVariantMap m = v.toMap();
        if (!m.value(QStringLiteral("testable")).toBool())
            continue;
        if (needPattern && m.value(QStringLiteral("pattern")).toString().isEmpty())
            continue;
        const bool own = needPattern ? m.value(QStringLiteral("patternWritable")).toBool()
                                     : m.value(QStringLiteral("writable")).toBool();
        if (own || root)
            out << m.value(QStringLiteral("name")).toString();
    }
    return out;
}

bool LedMon::write(const QString &name, const QString &attr, int value)
{
    const QString path = kClass + name + QLatin1Char('/') + attr;
    bool ok = false;
    if (::access(QFile::encodeName(path).constData(), W_OK) == 0)
        ok = writeInt(path, value);
    else if (RootClient::instance()->active())
        ok = RootClient::instance()->ledWrite(name, attr, value);
    if (ok && attr == QLatin1String("brightness"))
        m_written.insert(name, value);
    return ok;
}

void LedMon::startTest()
{
    if (m_running)
        return;
    refresh();
    const QStringList ch = testChannels(false);
    QVector<Step> steps;
    Step dark { {}, QString(), 500 };
    for (const QString &c : ch)
        dark.writes << Write { c, QStringLiteral("brightness"), 0 };
    steps << dark;
    for (const QString &c : ch) {
        Step s { {}, c, 1500 };
        for (const QString &o : ch)
            s.writes << Write { o, QStringLiteral("brightness"), o == c ? m_max.value(o) : 0 };
        steps << s;
    }
    if (ch.size() > 1) {
        Step all { {}, QStringLiteral("all"), 1500 };
        for (const QString &c : ch)
            all.writes << Write { c, QStringLiteral("brightness"), m_max.value(c) };
        steps << all;
    }
    run(steps, ch);
}

void LedMon::startPatternTest()
{
    if (m_running)
        return;
    refresh();
    const QStringList ch = testChannels(true);
    QVector<Step> steps;
    Step dark { {}, QString(), 500 };
    Step on { {}, QStringLiteral("pattern"), 6000 };
    for (const QString &c : ch) {
        dark.writes << Write { c, QStringLiteral("brightness"), 0 };
        on.writes << Write { c, Leds::patternAttr(c), 1 };
    }
    steps << dark << on;
    m_patterned = ch;
    run(steps, ch);
}

void LedMon::run(const QVector<Step> &steps, const QStringList &channels)
{
    m_result.clear();
    m_resultText.clear();
    if (channels.isEmpty()) {
        m_patterned.clear();
        m_result = QStringLiteral("fail");
        m_resultText = tr("No indicator LED this app may switch.");
        emit resultChanged();
        return;
    }
    m_channels = channels;
    m_orig.clear();
    m_written.clear();
    m_failed = false;
    for (const QString &c : channels)
        m_orig.insert(c, readInt(kClass + c + QStringLiteral("/brightness"), 0));
    m_steps = steps;
    m_at = 0;
    m_running = true;
    emit runningChanged();
    emit resultChanged();
    next();
}

void LedMon::next()
{
    if (!m_running)
        return;
    if (m_at >= m_steps.size()) {
        restore();
        return;
    }
    const Step &s = m_steps.at(m_at++);
    for (const Write &w : s.writes) {
        if (!write(w.name, w.attr, w.value)) {
            m_failed = true;
            m_resultText = tr("Writing %1 of %2 was refused.").arg(w.attr, w.name);
            restore();
            return;
        }
    }
    m_step = s.label;
    emit stepChanged();
    m_timer.start(s.ms);
}

void LedMon::cancel()
{
    if (m_running)
        restore();
}

void LedMon::restore()
{
    m_timer.stop();
    // The chip's pattern off first. What brightness reads afterwards is the
    // driver's answer to our own write (0 on the Awinic chip), so it counts
    // as ours in the check below.
    for (const QString &c : m_patterned)
        if (write(c, Leds::patternAttr(c), 0))
            m_written.insert(c, readInt(kClass + c + QStringLiteral("/brightness"), -1));
    m_patterned.clear();
    // Put back what was there, unless something else (mce) has written the
    // channel since -- then its value is the newer one and stays.
    QStringList taken;
    for (const QString &c : m_channels) {
        if (!m_written.contains(c))
            continue;
        const int now = readInt(kClass + c + QStringLiteral("/brightness"), -1);
        if (now != m_written.value(c)) {
            taken << c;
            continue;
        }
        write(c, QStringLiteral("brightness"), m_orig.value(c));
    }
    m_running = false;
    m_step.clear();
    if (m_failed) {
        m_result = QStringLiteral("fail");
    } else if (!taken.isEmpty()) {
        m_result = QStringLiteral("note");
        m_resultText = tr("Done. %1 was changed by another program during the test and keeps that value.")
                           .arg(taken.join(QStringLiteral(", ")));
    } else {
        m_result = QStringLiteral("ok");
        m_resultText = tr("Done. The previous values are back.");
    }
    emit stepChanged();
    emit runningChanged();
    emit resultChanged();
    refresh();
}

void LedMon::startVibration()
{
    if (m_vibrating)
        return;
    refresh();
    m_vibeResult.clear();
    m_vibeText.clear();
    if (m_vib.isEmpty() || !m_vibWritable) {
        m_vibeResult = QStringLiteral("fail");
        m_vibeText = tr("The vibration motor cannot be switched by this app.");
        emit vibratingChanged();
        return;
    }
    const QString dir = kClass + m_vib + QLatin1Char('/');
    m_origDuration = readInt(dir + QStringLiteral("duration"), 0);
    m_origState = readInt(dir + QStringLiteral("state"), 0);
    m_pulses = 0;
    m_vibrating = true;
    emit vibratingChanged();
    pulse();
    if (m_vibrating)
        m_vibTimer.start();
}

void LedMon::pulse()
{
    if (m_pulses >= 3) {
        endVibration(true);
        return;
    }
    ++m_pulses;
    const QString dir = kClass + m_vib + QLatin1Char('/');
    const bool ok = (!m_vibTransient || writeInt(dir + QStringLiteral("state"), 1))
                    && writeInt(dir + QStringLiteral("duration"), 150)
                    && writeInt(dir + QStringLiteral("activate"), 1);
    if (!ok)
        endVibration(false);
}

void LedMon::endVibration(bool ok)
{
    m_vibTimer.stop();
    const QString dir = kClass + m_vib + QLatin1Char('/');
    writeInt(dir + QStringLiteral("duration"), m_origDuration);
    if (m_vibTransient)
        writeInt(dir + QStringLiteral("state"), m_origState);
    m_vibrating = false;
    m_vibeResult = ok ? QStringLiteral("ok") : QStringLiteral("fail");
    m_vibeText = ok ? tr("Three pulses of 150 ms sent. Whether they were felt is for the hand to judge.")
                    : tr("Writing to %1 was refused.").arg(m_vib);
    emit vibratingChanged();
}
