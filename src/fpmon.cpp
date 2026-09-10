#include "fpmon.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace {

const char *kService = "org.sailfishos.fingerprint1";
const char *kPath    = "/org/sailfishos/fingerprint1";
const char *kIface   = "org.sailfishos.fingerprint1";

// The daemon answers every call with one of these. The order is the one the
// daemon's own symbol table carries; the numeric reply is always shown next to
// the name so the reader can check it rather than take it on trust.
QString replyWord(int code)
{
    switch (code) {
    case 0:  return QStringLiteral("FPREPLY_STARTED");
    case 1:  return QStringLiteral("FPREPLY_FAILED");
    case 2:  return QStringLiteral("FPREPLY_ALREADY_IDLE");
    case 3:  return QStringLiteral("FPREPLY_ALREADY_BUSY");
    case 4:  return QStringLiteral("FPREPLY_DENIED");
    case 5:  return QStringLiteral("FPREPLY_KEY_ALREADY_EXISTS");
    case 6:  return QStringLiteral("FPREPLY_KEY_DOES_NOT_EXIST");
    case 7:  return QStringLiteral("FPREPLY_NO_KEYS_AVAILABLE");
    case 8:  return QStringLiteral("FPREPLY_KEY_IS_INVALID");
    default: return QStringLiteral("FPREPLY_UNKNOWN");
    }
}

// Vendors whose part names appear in a device tree compatible string. This is
// a match against the vendor's own name for the part, not a guess from a
// driver or a node name.
const char *kVendors[] = {
    "goodix", "gf_spi", "fpc", "fingerprint", "silead", "egis", "egistec",
    "chipone", "elan", "focaltech", "synaptics", "betterlife", "microarray"
};

QString compatOf(const QString &devPath)
{
    QFile f(devPath + QStringLiteral("/of_node/compatible"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QByteArray b = f.read(512);
    while (b.endsWith('\0'))
        b.chop(1);
    b.replace('\0', ", ");
    return QString::fromLatin1(b).trimmed();
}

} // namespace

FpMon::FpMon(QObject *parent) : QObject(parent)
{
    m_iface = new QDBusInterface(QLatin1String(kService), QLatin1String(kPath),
                                 QLatin1String(kIface), QDBusConnection::systemBus(), this);
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
                QStringLiteral("StateChanged"), this, SLOT(onStateChanged(QString)));
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
                QStringLiteral("Identified"), this, SLOT(onIdentified(QString)));
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
                QStringLiteral("Verified"), this, SLOT(onVerified()));
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
                QStringLiteral("Failed"), this, SLOT(onFailed()));
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
                QStringLiteral("Aborted"), this, SLOT(onAborted()));
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
                QStringLiteral("ErrorInfo"), this, SLOT(onErrorInfo(QString)));
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
                QStringLiteral("AcquisitionInfo"), this, SLOT(onAcquisitionInfo(QString)));

    // A reader nobody touches would otherwise stay armed for the rest of the
    // session; the test ends itself.
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(30000);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (!m_testing)
            return;
        setResult(QStringLiteral("error"), tr("No finger within 30 seconds — test ended"));
        endTest();
    });

    readSensor();
}

// The sensor itself, from the buses a fingerprint reader is wired to. The
// compatible string is the vendor's own name for the part; the driver symlink
// says whether the kernel bound anything to it.
void FpMon::readSensor()
{
    m_sensor.clear();
    static const char *buses[] = { "/sys/bus/spi/devices", "/sys/bus/platform/devices",
                                   "/sys/bus/i2c/devices" };
    for (const char *b : buses) {
        const QDir d = QDir(QLatin1String(b));
        for (const QString &e : d.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot
                                            | QDir::System, QDir::Name)) {
            const QString path = d.filePath(e);
            const QString comp = compatOf(path);
            if (comp.isEmpty())
                continue;
            bool hit = false;
            for (const char *v : kVendors) {
                if (comp.contains(QLatin1String(v), Qt::CaseInsensitive)) {
                    hit = true;
                    break;
                }
            }
            if (!hit)
                continue;
            m_sensor.insert(QStringLiteral("compatible"), comp);
            m_sensor.insert(QStringLiteral("device"), e);
            m_sensor.insert(QStringLiteral("bus"), QString::fromLatin1(b).section(QLatin1Char('/'), -2, -2));
            const QString drv = QFileInfo(path + QStringLiteral("/driver")).symLinkTarget()
                                    .section(QLatin1Char('/'), -1);
            if (!drv.isEmpty())
                m_sensor.insert(QStringLiteral("driver"), drv);
            break;
        }
        if (!m_sensor.isEmpty())
            break;
    }

    // The character device the vendor's HAL talks to, if the driver made one.
    static const char *devNodes[] = { "/dev/goodix_fp", "/dev/gf_spi", "/dev/fpc1020",
                                      "/dev/fingerprint", "/dev/esfp0", "/dev/silead_fp",
                                      "/dev/madev0" };
    for (const char *n : devNodes) {
        if (QFileInfo::exists(QLatin1String(n))) {
            m_sensor.insert(QStringLiteral("node"), QLatin1String(n));
            break;
        }
    }
    emit sensorChanged();
}

void FpMon::refresh()
{
    readSensor();

    m_present = false;
    m_state.clear();
    if (m_iface && m_iface->isValid()) {
        const QDBusReply<QString> st = m_iface->call(QStringLiteral("GetState"));
        if (st.isValid()) {
            m_present = true;
            m_state = st.value();
        }
    }
    emit stateChanged();

    QStringList fingers;
    if (m_present) {
        const QDBusReply<QStringList> all = m_iface->call(QStringLiteral("GetAll"));
        if (all.isValid())
            fingers = all.value();
    }
    if (fingers != m_fingers) {
        m_fingers = fingers;
        emit fingersChanged();
    }
}

void FpMon::setResult(const QString &kind, const QString &text)
{
    m_result = kind;
    m_resultText = text;
    emit resultChanged();
}

void FpMon::startTest()
{
    if (m_testing || !m_iface)
        return;
    m_hint.clear();
    emit hintChanged();
    setResult(QString(), QString());

    const QDBusReply<int> r = m_iface->call(QStringLiteral("Identify"));
    if (!r.isValid()) {
        m_replyCode = -1;
        m_replyName = replyWord(-1);
        setResult(QStringLiteral("error"),
                  tr("The fingerprint daemon did not answer: %1").arg(r.error().message()));
        return;
    }
    m_replyCode = r.value();
    m_replyName = replyWord(m_replyCode);
    if (m_replyCode == 0) {
        m_testing = true;
        emit testingChanged();
        setResult(QStringLiteral("started"), tr("Place a finger on the reader"));
        m_timeout->start();
    } else if (m_replyCode == 3) {
        setResult(QStringLiteral("busy"),
                  tr("The reader is already identifying for the lock screen. "
                     "That is the daemon answering, so the reader is alive — but the "
                     "result goes to the lock screen, not here. Unlock the phone and "
                     "try again."));
    } else if (m_replyCode == 4) {
        setResult(QStringLiteral("denied"), tr("The daemon refused the request."));
    } else {
        setResult(QStringLiteral("error"), tr("The daemon did not start an identification."));
    }
    refresh();
}

void FpMon::stopTest()
{
    if (!m_testing) {
        // Abort is only accepted from whoever started the operation, so this
        // cannot end the lock screen's own identification.
        return;
    }
    if (m_iface) {
        const QDBusReply<int> r = m_iface->call(QStringLiteral("Abort"));
        if (r.isValid()) {
            m_replyCode = r.value();
            m_replyName = replyWord(m_replyCode);
            emit resultChanged();
        }
    }
    endTest();
}

void FpMon::endTest()
{
    m_timeout->stop();
    if (m_testing) {
        m_testing = false;
        emit testingChanged();
    }
    refresh();
}

void FpMon::onStateChanged(const QString &s)
{
    if (m_state == s)
        return;
    m_state = s;
    m_present = true;
    emit stateChanged();
}

void FpMon::onIdentified(const QString &finger)
{
    if (!m_testing)
        return;
    setResult(QStringLiteral("match"), tr("Recognised — enrolled as \"%1\"").arg(finger));
    endTest();
}

void FpMon::onVerified()
{
    if (!m_testing)
        return;
    setResult(QStringLiteral("match"), tr("Recognised"));
    endTest();
}

void FpMon::onFailed()
{
    if (!m_testing)
        return;
    setResult(QStringLiteral("nomatch"),
              tr("Read, but not recognised — the reader works, the finger is not one "
                 "of the enrolled ones."));
    endTest();
}

void FpMon::onAborted()
{
    if (!m_testing)
        return;
    setResult(QStringLiteral("error"), tr("The identification was cancelled."));
    endTest();
}

void FpMon::onErrorInfo(const QString &e)
{
    if (!m_testing)
        return;
    // The daemon's own word for what went wrong, kept verbatim.
    setResult(QStringLiteral("error"), e);
    endTest();
}

void FpMon::onAcquisitionInfo(const QString &a)
{
    if (!m_testing)
        return;
    m_hint = a;
    emit hintChanged();
}
