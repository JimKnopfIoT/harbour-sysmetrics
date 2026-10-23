#include "fmtest.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QVector>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char *kNode = "/dev/fm";

// MediaTek FM ioctls: _IOWR(0xF5, nr, pointer). The size field is the size of
// a pointer, so the numbers differ between 32 and 64 bit -- one reason the test
// runs only where it was measured.
inline unsigned long fmIoc(unsigned nr)
{
    return _IOWR(0xF5, nr, void *);
}
const unsigned kPowerUp = 0x00, kPowerDown = 0x01, kMute = 0x06, kAnaSwitch = 0x1e,
               kSoftMute = 0x3f;

// Channels in 10 kHz units, the driver's own.
const int kFirstCh = 8750, kLastCh = 10800, kStepCh = 10;

struct TuneParm {                // struct fm_tune_parm
    quint8 err, band, space, hilo, deemphasis;
    quint8 pad;
    quint16 freq;
};
struct SoftMute {                // struct fm_softmute_tune_t
    qint32 rssi;                 // raw; dBm = rssi * 3 / 8, checked against GETRSSI to 1 dB
    quint16 freq;
    quint16 pad;
    qint32 valid;                // the chip's own "this is a station"
};
static_assert(sizeof(TuneParm) == 8, "fm_tune_parm");
static_assert(sizeof(SoftMute) == 12, "fm_softmute_tune_t");

// Drivers whose ioctl layout the test was measured against.
const char *kDrivers[] = { "fmradio_drv_connac2x" };

} // namespace

FmTest::FmTest(QObject *parent) : QObject(parent)
{
    connect(this, &FmTest::sweepProgress, this, [this](qreal p) {
        m_progress = p;
        emit progressChanged();
    }, Qt::QueuedConnection);
    connect(this, &FmTest::sweepDone, this, &FmTest::finish, Qt::QueuedConnection);
    refresh();
}

FmTest::~FmTest()
{
    m_stop = true;
    join();
}

void FmTest::join()
{
    if (m_thread.joinable())
        m_thread.join();
}

void FmTest::refresh()
{
    const QFileInfo fi{QString::fromLatin1(kNode)};
    m_present = fi.exists();
    m_nodeInfo.clear();
    m_accessible = false;
    m_driver.clear();
    m_usedBy.clear();
    if (m_present) {
        struct stat st;
        if (::stat(kNode, &st) == 0)
            m_nodeInfo = QStringLiteral("%1:%2\u00a0%3").arg(fi.owner(), fi.group())
                             .arg(st.st_mode & 07777, 4, 8, QLatin1Char('0'));
        m_accessible = ::access(kNode, R_OK | W_OK) == 0;
    }
    for (const char *d : kDrivers) {
        if (QFileInfo::exists(QStringLiteral("/sys/module/") + QLatin1String(d))) {
            m_driver = QLatin1String(d);
            break;
        }
    }
    m_supported = m_present && !m_driver.isEmpty() && sizeof(void *) == 8;

    // Who else has the node open. Only our own user's processes are readable,
    // which covers the one case that matters: a radio app of this user.
    if (m_present && !m_running) {
        const QString self = QString::number(::getpid());
        const QDir proc(QStringLiteral("/proc"));
        for (const QString &pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (pid == self || !pid.at(0).isDigit())
                continue;
            const QDir fds(QStringLiteral("/proc/%1/fd").arg(pid));
            for (const QString &fd : fds.entryList(QDir::Files | QDir::System)) {
                if (QFile::symLinkTarget(fds.filePath(fd)) != QLatin1String(kNode))
                    continue;
                QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
                QString name;
                if (comm.open(QIODevice::ReadOnly))
                    name = QString::fromLocal8Bit(comm.readAll()).trimmed();
                m_usedBy = QStringLiteral("%1 (%2)").arg(name, pid);
                break;
            }
            if (!m_usedBy.isEmpty())
                break;
        }
    }
    emit infoChanged();
}

void FmTest::start()
{
    if (m_running)
        return;
    refresh();
    if (!m_supported)
        return;
    m_spectrum.clear();
    m_peaks.clear();
    if (!m_usedBy.isEmpty()) {
        // Powering the chip down afterwards would cut that app off.
        m_result = QStringLiteral("busy");
        m_resultText = tr("%1 is using the FM chip. Close it and run the test again — "
                          "the test switches the chip off when it ends.").arg(m_usedBy);
        emit resultChanged();
        return;
    }
    join();
    m_stop = false;
    m_result.clear();
    m_resultText.clear();
    m_progress = 0;
    m_running = true;
    emit resultChanged();
    emit progressChanged();
    emit runningChanged();
    m_thread = std::thread([this] { sweep(); });
}

void FmTest::cancel()
{
    m_stop = true;
}

// Sweep thread: power up, one soft-mute tune per channel, power down. The chip
// is muted throughout and powered down on every way out.
void FmTest::sweep()
{
    QElapsedTimer t;
    t.start();
    QVariantList dbm, valid;
    const int fd = ::open(kNode, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        emit sweepDone(QStringLiteral("fail"),
                       tr("%1 cannot be opened: %2").arg(QLatin1String(kNode),
                                                         QString::fromLocal8Bit(strerror(errno))),
                       dbm, valid, 0);
        return;
    }
    TuneParm tp = { 0, 1 /* 87.5–108 MHz */, 1 /* 100 kHz */, 0, 0 /* 50 µs */, 0, 9800 };
    if (::ioctl(fd, fmIoc(kPowerUp), &tp) != 0 || tp.err != 0) {
        const QString why = tp.err ? tr("driver error %1").arg(tp.err)
                                   : QString::fromLocal8Bit(strerror(errno));
        qint32 zero = 0;
        ::ioctl(fd, fmIoc(kPowerDown), &zero);
        ::close(fd);
        emit sweepDone(QStringLiteral("fail"), tr("The chip did not power up: %1").arg(why),
                       dbm, valid, t.elapsed() / 1000.0);
        return;
    }
    qint32 v = 1;
    ::ioctl(fd, fmIoc(kMute), &v);
    v = 1;                                    // short antenna path; 0 is the headset path
    ::ioctl(fd, fmIoc(kAnaSwitch), &v);

    const int total = (kLastCh - kFirstCh) / kStepCh + 1;
    QString error;
    for (int ch = kFirstCh; ch <= kLastCh && !m_stop; ch += kStepCh) {
        SoftMute sm = { 0, quint16(ch), 0, 0 };
        if (::ioctl(fd, fmIoc(kSoftMute), &sm) != 0) {
            error = tr("Measuring %1 MHz failed: %2").arg(ch / 100.0, 0, 'f', 1)
                        .arg(QString::fromLocal8Bit(strerror(errno)));
            break;
        }
        dbm.append(sm.rssi * 3.0 / 8.0);
        valid.append(sm.valid != 0);
        if (dbm.size() % 10 == 0)
            emit sweepProgress(qreal(dbm.size()) / total);
    }
    qint32 zero = 0;
    ::ioctl(fd, fmIoc(kPowerDown), &zero);
    ::close(fd);

    const qreal secs = t.elapsed() / 1000.0;
    if (!error.isEmpty())
        emit sweepDone(QStringLiteral("fail"), error, dbm, valid, secs);
    else if (m_stop)
        emit sweepDone(QString(), tr("Cancelled — the chip is switched off again."), dbm, valid, secs);
    else
        emit sweepDone(QStringLiteral("ok"),
                       tr("The chip powered up, measured all %1 channels and was switched off "
                          "again.").arg(total), dbm, valid, secs);
}

// Peaks: local maxima at least 8 dB over the floor (the 20th percentile), or
// channels the chip itself flags as a station. Strongest first, at most 12.
void FmTest::finish(const QString &result, const QString &text, const QVariantList &dbm,
                    const QVariantList &valid, qreal seconds)
{
    join();
    m_running = false;
    m_result = result;
    m_resultText = text;
    m_seconds = seconds;
    m_spectrum = result == QLatin1String("ok") ? dbm : QVariantList();
    m_peaks.clear();
    m_floor = 0;
    if (!m_spectrum.isEmpty()) {
        QVector<double> d;
        for (const QVariant &x : dbm)
            d.append(x.toDouble());
        QVector<double> sorted = d;
        std::sort(sorted.begin(), sorted.end());
        m_floor = sorted.at(sorted.size() / 5);
        QList<QVariantMap> found;
        for (int i = 0; i < d.size(); ++i) {
            const bool isMax = (i == 0 || d[i] >= d[i - 1]) && (i == d.size() - 1 || d[i] >= d[i + 1]);
            const bool flagged = valid.value(i).toBool();
            if (!flagged && !(isMax && d[i] >= m_floor + 8))
                continue;
            QVariantMap p;
            p.insert(QStringLiteral("mhz"), (kFirstCh + i * kStepCh) / 100.0);
            p.insert(QStringLiteral("dbm"), d[i]);
            p.insert(QStringLiteral("valid"), flagged);
            found.append(p);
        }
        std::sort(found.begin(), found.end(), [](const QVariantMap &a, const QVariantMap &b) {
            return a.value(QStringLiteral("dbm")).toDouble() > b.value(QStringLiteral("dbm")).toDouble();
        });
        for (int i = 0; i < found.size() && i < 12; ++i)
            m_peaks.append(found.at(i));
    }
    emit resultChanged();
    emit runningChanged();
    refresh();
}
