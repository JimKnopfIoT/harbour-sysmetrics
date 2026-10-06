// The diagnosis path for a home screen that keeps drawing. See redrawcheck.h.
//
// What was established on the devices (J2 5.2.0.18, Xperia 10 III 5.1.0.11,
// Gemini 4.6) and what the thresholds below rest on:
//  - lipstick's QSGRenderThread sleeps between frames. Its voluntary context
//    switches per second follow the frames it wants to draw: below 1/s on an
//    idle home screen, about one per display frame in the stuck state, three
//    to four per frame while it really animates.
//  - In the stuck state the main thread works harder than the render thread
//    (13 % against 4 % of a core on the J2): something in QML ticks every
//    frame while nothing visible changes.
//  - A display interrupt counts frames only on some chips (J2 dsi0: 0/s idle);
//    on others it has a base rate (Xperia msm_drm 31/s, Gemini dsi0 284/s).
//    A source is therefore only believed once it has been seen near zero
//    while lipstick was quiet.
//  - Touch wakes lipstick's evdev thread, so touches show without access to
//    the input devices.
#include "redrawcheck.h"

#include <QDateTime>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QtMath>

#include <unistd.h>

namespace {

// Thresholds. Each is stated on the page next to the value it judges.
const double kQuietWake = 2.0;      // render wakeups/s below which lipstick is idle
const double kHotWake = 30.0;       // render wakeups/s that count as continuous drawing
const double kCoolWake = 5.0;       // below this the watch clears a suspicion
const int kWatchSec = 5;            // watch window
const int kHotWindows = 6;          // 6 x 5 s = 30 s of drawing without touch
const int kMeasureSec = 30;
const int kTestSec = 20;
const int kSettleSec = 15;          // a preview shows ~5-7 s, then hides
const qint64 kOnsetLookbackMs = 180000;

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// utime+stime of a /proc stat line, robust against spaces in the name
qulonglong statJiffies(const QByteArray &st, qulonglong *start = nullptr)
{
    const int close = st.lastIndexOf(')');
    if (close < 0)
        return 0;
    const QList<QByteArray> f = st.mid(close + 2).split(' ');
    if (f.size() < 20)
        return 0;
    if (start)
        *start = f[19].toULongLong();
    return f[11].toULongLong() + f[12].toULongLong();
}

qulonglong voluntary(const QByteArray &status)
{
    const int i = status.indexOf("voluntary_ctxt_switches:");
    if (i < 0 || (i > 0 && status.at(i - 1) != '\n'))
        return 0;
    const int e = status.indexOf('\n', i);
    return status.mid(i + 24, e - i - 24).trimmed().toULongLong();
}

QString cmdline(int pid)
{
    QByteArray c = readAll(QStringLiteral("/proc/%1/cmdline").arg(pid));
    c.replace('\0', ' ');
    return QString::fromLocal8Bit(c).trimmed();
}

} // namespace

RedrawCheck::RedrawCheck(QObject *parent) : QObject(parent)
{
    m_clkTck = sysconf(_SC_CLK_TCK);
    if (m_clkTck <= 0)
        m_clkTck = 100;
    m_watchTimer.setInterval(kWatchSec * 1000);
    connect(&m_watchTimer, &QTimer::timeout, this, &RedrawCheck::watchTick);
    m_runTimer.setInterval(1000);
    connect(&m_runTimer, &QTimer::timeout, this, &RedrawCheck::runTick);

    m_shotDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                + QStringLiteral("/Screenshots");
    m_fsw = new QFileSystemWatcher(this);
    connect(m_fsw, &QFileSystemWatcher::directoryChanged, this, &RedrawCheck::onScreenshotDir);
    if (QFileInfo(m_shotDir).isDir()) {
        m_fsw->addPath(m_shotDir);
        m_shots = QDir(m_shotDir).entryList(QDir::Files);
    }

    // Which display interrupt counts frames is a fact about the device, not
    // about this session: once seen near zero while lipstick was idle, it
    // stays believed across launches.
    QSettings st;
    st.beginGroup(QStringLiteral("redraw/irqQuietMin"));
    for (const QString &k : st.childKeys())
        m_irqQuietMin.insert(k, st.value(k).toDouble());
    st.endGroup();

    findLipstick();
    m_watch.insert(QStringLiteral("state"), QStringLiteral("unknown"));
    setWatching(true);
}

RedrawCheck::~RedrawCheck()
{
    if (m_monitor) {
        m_monitor->kill();
        m_monitor->waitForFinished(500);
    }
}

void RedrawCheck::setWatching(bool on)
{
    if (on == m_watching && (on == m_watchTimer.isActive()))
        return;
    m_watching = on;
    if (on) {
        m_watchTimer.start();
        startNotifyWatch();
    } else {
        m_watchTimer.stop();
        if (m_monitor) {
            m_monitor->kill();
            m_monitor->deleteLater();
            m_monitor = nullptr;
        }
        m_suspect = false;
        m_hotWindows = 0;
        m_watch.insert(QStringLiteral("state"), QStringLiteral("off"));
        emit watchChanged();
    }
    emit watchingChanged();
}

void RedrawCheck::setForeground(bool on)
{
    if (on == m_foreground)
        return;
    m_foreground = on;
    emit foregroundChanged();
}

// ---------------------------------------------------------------- reading

void RedrawCheck::findLipstick()
{
    if (m_lipstick > 0) {
        qulonglong start = 0;
        statJiffies(readAll(QStringLiteral("/proc/%1/stat").arg(m_lipstick)), &start);
        if (start == m_lipStart && start != 0)
            return;
    }
    m_lipstick = 0;
    m_lipStart = 0;
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs);
    for (const QString &p : pids) {
        const int pid = p.toInt();
        if (pid <= 0)
            continue;
        if (readAll(QStringLiteral("/proc/%1/cmdline").arg(pid)).startsWith("/usr/bin/lipstick")) {
            m_lipstick = pid;
            statJiffies(readAll(QStringLiteral("/proc/%1/stat").arg(pid)), &m_lipStart);
            break;
        }
    }
}

QVector<RedrawCheck::Thr> RedrawCheck::readThreads(int pid) const
{
    QVector<Thr> out;
    if (pid <= 0)
        return out;
    const QDir task(QStringLiteral("/proc/%1/task").arg(pid));
    for (const QString &t : task.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        Thr th;
        th.tid = t.toInt();
        const QByteArray st = readAll(task.filePath(t) + QStringLiteral("/stat"));
        const int open = st.indexOf('('), close = st.lastIndexOf(')');
        if (open < 0 || close < 0)
            continue;
        th.name = QString::fromLocal8Bit(st.mid(open + 1, close - open - 1));
        th.jiffies = statJiffies(st);
        th.vctx = voluntary(readAll(task.filePath(t) + QStringLiteral("/status")));
        out.append(th);
    }
    return out;
}

// Interrupts of the display path, by the name /proc/interrupts gives them.
QHash<QString, qulonglong> RedrawCheck::readDisplayIrqs() const
{
    QHash<QString, qulonglong> out;
    static const QRegularExpression name(QStringLiteral("(dsi|msm_drm|mdss)"));
    QFile f(QStringLiteral("/proc/interrupts"));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &l : lines) {
        const QList<QByteArray> parts = l.simplified().split(' ');
        if (parts.size() < 3)
            continue;
        const QString last = QString::fromLatin1(parts.last());
        if (!name.match(last).hasMatch())
            continue;
        qulonglong sum = 0;
        for (int i = 1; i < parts.size(); ++i) {
            bool ok = false;
            const qulonglong v = parts[i].toULongLong(&ok);
            if (!ok)
                break;
            sum += v;
        }
        out.insert(last, sum);
    }
    return out;
}

// Processes of this user that are Wayland clients: only they can hand
// lipstick a new frame. Whether a process is one is read once from its maps.
QHash<int, RedrawCheck::ProcTotals> RedrawCheck::readClients()
{
    QHash<int, ProcTotals> out;
    const uint uid = getuid();
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs);
    for (const QString &p : pids) {
        const int pid = p.toInt();
        if (pid <= 0 || pid == m_lipstick || pid == getpid())
            continue;
        if (QFileInfo(QStringLiteral("/proc/%1").arg(pid)).ownerId() != uid)
            continue;
        auto c = m_wlCache.constFind(pid);
        bool wl;
        if (c == m_wlCache.constEnd()) {
            wl = readAll(QStringLiteral("/proc/%1/maps").arg(pid)).contains("libwayland-client");
            m_wlCache.insert(pid, wl);
        } else {
            wl = c.value();
        }
        if (!wl)
            continue;
        ProcTotals t;
        const QString cmd = cmdline(pid);
        t.name = cmd.section(QLatin1Char(' '), 0, 0).section(QLatin1Char('/'), -1);
        for (const Thr &th : readThreads(pid)) {
            t.jiffies += th.jiffies;
            t.vctx.insert(th.tid, th.vctx);
        }
        out.insert(pid, t);
    }
    return out;
}

qulonglong RedrawCheck::composerJiffies() const
{
    qulonglong sum = 0;
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs);
    for (const QString &p : pids) {
        const int pid = p.toInt();
        if (pid <= 0)
            continue;
        const QByteArray c = readAll(QStringLiteral("/proc/%1/cmdline").arg(pid));
        if (c.startsWith("/vendor/bin/hw/") && c.contains("composer"))
            sum += statJiffies(readAll(QStringLiteral("/proc/%1/stat").arg(pid)));
    }
    return sum;
}

bool RedrawCheck::displayOn() const
{
    QDBusMessage m = QDBusMessage::createMethodCall(
        QStringLiteral("com.nokia.mce"), QStringLiteral("/com/nokia/mce/request"),
        QStringLiteral("com.nokia.mce.request"), QStringLiteral("get_display_status"));
    const QDBusMessage rep = QDBusConnection::systemBus().call(m, QDBus::Block, 500);
    if (rep.type() != QDBusMessage::ReplyMessage || rep.arguments().isEmpty())
        return true; // no mce: do not block the check on it
    return rep.arguments().first().toString() != QLatin1String("off");
}

void RedrawCheck::noteQuietIrq(const QString &name, double rate)
{
    if (m_irqQuietMin.contains(name) && rate >= m_irqQuietMin.value(name))
        return;
    m_irqQuietMin.insert(name, rate);
    QSettings st;
    // Interrupt names hold no '/', the only character QSettings would read
    // as a group separator.
    st.setValue(QStringLiteral("redraw/irqQuietMin/") + name, rate);
}

QString RedrawCheck::roleOf(int pid, int tid, const QString &name)
{
    if (tid == pid)
        return QStringLiteral("main");
    if (name == QLatin1String("QSGRenderThread"))
        return QStringLiteral("render");
    if (name.startsWith(QLatin1String("QEvdevTouch")))
        return QStringLiteral("touch");
    if (name.startsWith(QLatin1String("mali")) || name.startsWith(QLatin1String("kgsl")))
        return QStringLiteral("gpu");
    if (name.startsWith(QLatin1String("binder:")) || name.startsWith(QLatin1String("HwBinder:")))
        return QStringLiteral("binder");
    if (name == QLatin1String("QDBusConnection") || name == QLatin1String("gdbus"))
        return QStringLiteral("dbus");
    return QStringLiteral("other");
}

QString RedrawCheck::threadRole(int pid, int tid, const QString &name) const
{
    if (pid <= 0 || pid != m_lipstick)
        return QString();
    const QString r = roleOf(pid, tid, name);
    if (r == QLatin1String("main"))   return tr("main thread: QML, bindings, animations");
    if (r == QLatin1String("render")) return tr("render thread: hands frames to the GPU");
    if (r == QLatin1String("touch"))  return tr("touch input");
    if (r == QLatin1String("gpu"))    return tr("GPU driver");
    if (r == QLatin1String("binder")) return tr("binder pool: calls to the composer");
    if (r == QLatin1String("dbus"))   return tr("D-Bus");
    return QString();
}

// ---------------------------------------------------------------- watch

void RedrawCheck::watchTick()
{
    findLipstick();
    const qint64 now = nowMs();
    if (m_lipstick <= 0 || busy() || m_foreground || !displayOn()) {
        // Nothing to judge right now. A suspicion stays until a quiet window
        // with the display on clears it: the stuck state survives a screen-off.
        m_wPrevMs = 0;
        m_hotWindows = 0;
        m_watch.insert(QStringLiteral("state"), m_suspect ? QStringLiteral("suspect")
                                                           : QStringLiteral("paused"));
        emit watchChanged();
        return;
    }
    qulonglong render = 0, touch = 0;
    for (const Thr &t : readThreads(m_lipstick)) {
        const QString r = roleOf(m_lipstick, t.tid, t.name);
        if (r == QLatin1String("render")) render = t.vctx;
        else if (r == QLatin1String("touch")) touch += t.vctx;
    }
    const QHash<QString, qulonglong> irq = readDisplayIrqs();
    if (m_wPrevMs == 0 || render < m_wPrevRender) {
        m_wPrevMs = now;
        m_wPrevRender = render;
        m_wPrevTouch = touch;
        m_wPrevIrq = irq;
        return;
    }
    const double dt = (now - m_wPrevMs) / 1000.0;
    const double wake = dt > 0 ? (render - m_wPrevRender) / dt : 0;
    const bool touched = touch != m_wPrevTouch;
    if (wake < kQuietWake && !touched && dt > 0) {
        for (auto it = irq.constBegin(); it != irq.constEnd(); ++it) {
            noteQuietIrq(it.key(), (it.value() - m_wPrevIrq.value(it.key(), it.value())) / dt);
        }
    }
    if (touched)
        addEvent(Event{now, QStringLiteral("touch"), QString(), -1});

    const bool wasSuspect = m_suspect;
    if (wake >= kHotWake && !touched) {
        if (m_hotWindows == 0)
            m_hotSince = m_wPrevMs;
        if (++m_hotWindows >= kHotWindows && !m_suspect) {
            m_suspect = true;
            m_watch.insert(QStringLiteral("since"), m_hotSince);
        }
    } else if (wake < kCoolWake) {
        m_hotWindows = 0;
        m_suspect = false;
    } else {
        m_hotWindows = 0;
    }
    m_watch.insert(QStringLiteral("state"), m_suspect ? QStringLiteral("suspect")
                                     : (wake < kQuietWake ? QStringLiteral("quiet")
                                                          : QStringLiteral("drawing")));
    m_watch.insert(QStringLiteral("renderWake"), wake);
    m_watch.insert(QStringLiteral("checked"), now);
    m_watch.insert(QStringLiteral("hotWake"), kHotWake);
    m_watch.insert(QStringLiteral("hotSeconds"), kHotWindows * kWatchSec);
    if (m_suspect && !wasSuspect) {
        // What happened just before the onset: the leads to follow first.
        QVariantList lead;
        for (const Event &e : m_events) {
            if (e.ms > m_hotSince || e.ms < m_hotSince - kOnsetLookbackMs || e.kind == QLatin1String("touch"))
                continue;
            QVariantMap m;
            m.insert(QStringLiteral("ms"), e.ms);
            m.insert(QStringLiteral("kind"), e.kind);
            m.insert(QStringLiteral("app"), e.app);
            m.insert(QStringLiteral("actions"), e.actions);
            lead.append(m);
        }
        m_watch.insert(QStringLiteral("leads"), lead);
    }
    if (!m_suspect)
        m_watch.remove(QStringLiteral("leads"));
    m_wPrevMs = now;
    m_wPrevRender = render;
    m_wPrevTouch = touch;
    m_wPrevIrq = irq;
    emit watchChanged();
}

// ---------------------------------------------------------------- events

bool RedrawCheck::notifyWatchAvailable() const
{
    return QFileInfo(QStringLiteral("/usr/bin/dbus-monitor")).isExecutable();
}

// Who sent a notification, and whether it carried action buttons. The bus
// lets a process of the same user watch its own session; of each Notify call
// only the sender's application name and the action names are kept --
// summary and body are skipped while parsing and never stored.
void RedrawCheck::startNotifyWatch()
{
    if (m_monitor || !notifyWatchAvailable())
        return;
    m_monitor = new QProcess(this);
    m_monitor->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_monitor, &QProcess::readyReadStandardOutput, this, &RedrawCheck::onNotifyOutput);
    m_monitor->start(QStringLiteral("/usr/bin/dbus-monitor"),
                     QStringList() << QStringLiteral("--session")
                     << QStringLiteral("type='method_call',interface='org.freedesktop.Notifications',member='Notify'"));
}

void RedrawCheck::onNotifyOutput()
{
    m_monBuf += m_monitor->readAllStandardOutput();
    int nl;
    while ((nl = m_monBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_monBuf.left(nl);
        m_monBuf.remove(0, nl + 1);
        if (line.startsWith("method call") && line.contains("member=Notify")) {
            m_monState = 1;
            m_monStrings = 0;
            m_monEvent = Event{nowMs(), QStringLiteral("notification"), QString(), 0};
            continue;
        }
        if (m_monState == 1) {
            // top level: app_name, replaces_id, icon, summary, body, actions
            if (line.startsWith("   string \"") && m_monStrings == 0) {
                m_monEvent.app = QString::fromUtf8(line.mid(11, line.size() - 12));
                ++m_monStrings;
            } else if (line.startsWith("   array [")) {
                m_monState = 2;
                m_monStrings = 0;
            }
        } else if (m_monState == 2) {
            if (line.startsWith("      string \"")) {
                // pairs of (name, label); a button is any name but default/app
                if (m_monStrings % 2 == 0) {
                    const QByteArray name = line.mid(14, line.size() - 15);
                    if (!name.isEmpty() && name != "default" && name != "app")
                        ++m_monEvent.actions;
                }
                ++m_monStrings;
            } else if (line.startsWith("   ]")) {
                addEvent(m_monEvent);
                m_monState = 0;
            }
        }
    }
}

void RedrawCheck::onScreenshotDir()
{
    const QStringList now = QDir(m_shotDir).entryList(QDir::Files);
    for (const QString &f : now)
        if (!m_shots.contains(f))
            addEvent(Event{nowMs(), QStringLiteral("screenshot"), f, -1});
    m_shots = now;
}

void RedrawCheck::addEvent(const Event &e)
{
    // touches arrive every window while someone uses the phone; keep one
    if (e.kind == QLatin1String("touch") && !m_events.isEmpty()
        && m_events.last().kind == QLatin1String("touch")) {
        m_events.last().ms = e.ms;
    } else {
        m_events.append(e);
    }
    while (m_events.size() > 40)
        m_events.removeFirst();
    emit eventsChanged();
}

QVariantList RedrawCheck::events() const
{
    QVariantList out;
    for (int i = m_events.size() - 1; i >= 0; --i) {
        const Event &e = m_events.at(i);
        QVariantMap m;
        m.insert(QStringLiteral("ms"), e.ms);
        m.insert(QStringLiteral("kind"), e.kind);
        m.insert(QStringLiteral("app"), e.app);
        m.insert(QStringLiteral("actions"), e.actions);
        out.append(m);
    }
    return out;
}

// ---------------------------------------------------------------- guided run

QString RedrawCheck::coverText() const
{
    if (m_phase == QLatin1String("wait"))
        return tr("Minimise SysMetrics now");
    if (m_settle > 0)
        return tr("Test notification sent\nDo not touch");
    if (!m_phase.isEmpty())
        return tr("Measuring\nDo not touch");
    return QString();
}

void RedrawCheck::startMeasurement()
{
    if (busy())
        return;
    m_mode = QStringLiteral("measure");
    m_measurement.clear();
    m_phase = QStringLiteral("wait");
    m_waitFg = 0;
    m_left = 60;
    m_runTimer.start();
    emit runChanged();
}

void RedrawCheck::startTriggerTest()
{
    if (busy())
        return;
    m_mode = QStringLiteral("trigger");
    m_trigger.clear();
    m_steps.clear();
    m_phase = QStringLiteral("wait");
    m_waitFg = 0;
    m_left = 60;
    m_runTimer.start();
    emit runChanged();
}

void RedrawCheck::cancel()
{
    if (!busy())
        return;
    finishRun(tr("Cancelled."));
}

void RedrawCheck::beginWindow(int seconds)
{
    findLipstick();
    m_t0 = readThreads(m_lipstick);
    m_irq0 = readDisplayIrqs();
    m_clients0 = readClients();
    m_comp0 = composerJiffies();
    m_ms0 = nowMs();
    m_perSecWake.clear();
    m_prevRenderSec = 0;
    m_prevTouchSec = 0;
    for (const Thr &t : m_t0) {
        const QString r = roleOf(m_lipstick, t.tid, t.name);
        if (r == QLatin1String("render")) m_prevRenderSec = t.vctx;
        else if (r == QLatin1String("touch")) m_prevTouchSec += t.vctx;
    }
    m_invalid.clear();
    m_settle = 0;
    m_left = seconds;
}

void RedrawCheck::runTick()
{
    if (m_phase == QLatin1String("wait")) {
        // The page itself draws while it is in front; measure only once the
        // app is out of sight and its cover holds still.
        m_waitFg = m_foreground ? 0 : m_waitFg + 1;
        if (--m_left <= 0) {
            finishRun(tr("SysMetrics stayed in front; nothing was measured."));
            return;
        }
        if (m_waitFg >= 3)
            advance();
        emit tick();
        return;
    }
    if (m_settle > 0) {
        m_left = --m_settle;
        if (m_settle == 0)
            beginWindow(kTestSec);
        emit tick();
        return;
    }
    // inside a measuring window
    qulonglong render = 0, touch = 0;
    for (const Thr &t : readThreads(m_lipstick)) {
        const QString r = roleOf(m_lipstick, t.tid, t.name);
        if (r == QLatin1String("render")) render = t.vctx;
        else if (r == QLatin1String("touch")) touch += t.vctx;
    }
    m_perSecWake.append(render >= m_prevRenderSec ? double(render - m_prevRenderSec) : 0.0);
    m_prevRenderSec = render;
    if (m_invalid.isEmpty()) {
        if (touch != m_prevTouchSec)
            m_invalid = tr("the screen was touched");
        else if (m_foreground)
            m_invalid = tr("SysMetrics came to the front");
        else if (!displayOn())
            m_invalid = tr("the display went off");
    }
    m_prevTouchSec = touch;
    if (--m_left <= 0) {
        advance(finishWindow());
        return;
    }
    emit tick();
}

QVariantMap RedrawCheck::finishWindow()
{
    QVariantMap res;
    const double dt = qMax(1.0, (nowMs() - m_ms0) / 1000.0);
    const int pid0 = m_lipstick;
    findLipstick();
    if (m_lipstick != pid0 && m_invalid.isEmpty())
        m_invalid = tr("lipstick restarted");
    const QVector<Thr> t1 = readThreads(m_lipstick);
    QHash<int, Thr> before;
    for (const Thr &t : m_t0)
        before.insert(t.tid, t);

    const double pct = 100.0 / (m_clkTck * dt);
    QHash<QString, double> rolePct;
    double renderWake = 0;
    QVariantList threads;
    for (const Thr &t : t1) {
        const auto b = before.constFind(t.tid);
        if (b == before.constEnd())
            continue;
        const double cpu = (t.jiffies >= b->jiffies ? t.jiffies - b->jiffies : 0) * pct;
        const double wake = (t.vctx >= b->vctx ? t.vctx - b->vctx : 0) / dt;
        const QString r = roleOf(m_lipstick, t.tid, t.name);
        rolePct[r] += cpu;
        if (r == QLatin1String("render"))
            renderWake = wake;
        if (cpu >= 0.5 || wake >= 5) {
            QVariantMap m;
            m.insert(QStringLiteral("tid"), t.tid);
            m.insert(QStringLiteral("name"), t.name);
            m.insert(QStringLiteral("role"), threadRole(m_lipstick, t.tid, t.name));
            m.insert(QStringLiteral("cpu"), cpu);
            m.insert(QStringLiteral("wake"), wake);
            threads.append(m);
        }
    }
    double total = 0;
    for (double v : rolePct)
        total += v;

    // display interrupts, believed only where seen near zero while quiet
    const QHash<QString, qulonglong> irq1 = readDisplayIrqs();
    QVariantList irqs;
    double frames = -1;
    QString frameSource;
    for (auto it = irq1.constBegin(); it != irq1.constEnd(); ++it) {
        const double r = (it.value() - m_irq0.value(it.key(), it.value())) / dt;
        if (renderWake < kQuietWake && m_invalid.isEmpty())
            noteQuietIrq(it.key(), r);
        const bool verified = m_irqQuietMin.contains(it.key()) && m_irqQuietMin.value(it.key()) <= kQuietWake;
        QVariantMap m;
        m.insert(QStringLiteral("name"), it.key());
        m.insert(QStringLiteral("rate"), r);
        m.insert(QStringLiteral("verified"), verified);
        m.insert(QStringLiteral("quietMin"), m_irqQuietMin.value(it.key(), -1));
        irqs.append(m);
        if (verified && r > frames) {
            frames = r;
            frameSource = it.key();
        }
    }

    // Wayland clients that woke in step with lipstick's frames
    const QHash<int, ProcTotals> c1 = readClients();
    QVariantList clients;
    for (auto it = c1.constBegin(); it != c1.constEnd(); ++it) {
        const auto b = m_clients0.constFind(it.key());
        if (b == m_clients0.constEnd())
            continue;
        double maxWake = 0;
        for (auto v = it->vctx.constBegin(); v != it->vctx.constEnd(); ++v) {
            const qulonglong w0 = b->vctx.value(v.key(), v.value());
            maxWake = qMax(maxWake, (v.value() >= w0 ? v.value() - w0 : 0) / dt);
        }
        const double cpu = (it->jiffies >= b->jiffies ? it->jiffies - b->jiffies : 0) * pct;
        if (renderWake >= kHotWake && maxWake >= 0.5 * renderWake && cpu > 0) {
            QVariantMap m;
            m.insert(QStringLiteral("pid"), it.key());
            m.insert(QStringLiteral("name"), it->name);
            m.insert(QStringLiteral("wake"), maxWake);
            m.insert(QStringLiteral("cpu"), cpu);
            clients.append(m);
        }
    }
    const qulonglong comp1 = composerJiffies();
    const double composer = (comp1 >= m_comp0 ? comp1 - m_comp0 : 0) * pct;

    int hotSecs = 0;
    for (double w : m_perSecWake)
        if (w >= kHotWake) ++hotSecs;
    const double hotShare = m_perSecWake.isEmpty() ? 0 : double(hotSecs) / m_perSecWake.size();

    QString cls;
    if (renderWake < kQuietWake)
        cls = QStringLiteral("quiet");
    else if (!clients.isEmpty())
        cls = QStringLiteral("client");
    else if (rolePct.value(QStringLiteral("main")) >= rolePct.value(QStringLiteral("render")))
        cls = QStringLiteral("lipstick-main");
    else
        cls = QStringLiteral("lipstick-render");
    const bool stuck = cls.startsWith(QLatin1String("lipstick")) && renderWake >= kHotWake
                       && hotShare >= 0.8;

    res.insert(QStringLiteral("valid"), m_invalid.isEmpty());
    res.insert(QStringLiteral("invalid"), m_invalid);
    res.insert(QStringLiteral("seconds"), qRound(dt));
    res.insert(QStringLiteral("time"), m_ms0);
    res.insert(QStringLiteral("lipstickPid"), m_lipstick);
    res.insert(QStringLiteral("cls"), cls);
    res.insert(QStringLiteral("stuck"), stuck);
    res.insert(QStringLiteral("renderWake"), renderWake);
    res.insert(QStringLiteral("hotShare"), hotShare);
    res.insert(QStringLiteral("lipstickPct"), total);
    res.insert(QStringLiteral("mainPct"), rolePct.value(QStringLiteral("main")));
    res.insert(QStringLiteral("renderPct"), rolePct.value(QStringLiteral("render")));
    res.insert(QStringLiteral("gpuPct"), rolePct.value(QStringLiteral("gpu")));
    res.insert(QStringLiteral("binderPct"), rolePct.value(QStringLiteral("binder")));
    res.insert(QStringLiteral("composerPct"), composer);
    res.insert(QStringLiteral("frames"), frames);
    res.insert(QStringLiteral("frameSource"), frameSource);
    res.insert(QStringLiteral("wakePerFrame"), frames > 0 ? renderWake / frames : -1.0);
    res.insert(QStringLiteral("irqs"), irqs);
    res.insert(QStringLiteral("clients"), clients);
    res.insert(QStringLiteral("threads"), threads);
    res.insert(QStringLiteral("quietWake"), kQuietWake);
    res.insert(QStringLiteral("hotWake"), kHotWake);
    return res;
}

void RedrawCheck::advance(const QVariantMap &result)
{
    if (m_mode == QLatin1String("measure")) {
        if (m_phase == QLatin1String("wait")) {
            m_phase = QStringLiteral("measure");
            beginWindow(kMeasureSec);
            emit runChanged();
            return;
        }
        m_measurement = result;
        finishRun(QString());
        return;
    }

    // trigger test: baseline, then a notification without and one with
    // action buttons, each followed by its own measurement
    if (m_phase == QLatin1String("wait")) {
        m_phase = QStringLiteral("baseline");
        beginWindow(kTestSec);
        emit runChanged();
        return;
    }
    QVariantMap step = result;
    step.insert(QStringLiteral("step"), m_phase);
    m_steps.append(step);
    m_trigger.insert(QStringLiteral("steps"), m_steps);
    if (!result.value(QStringLiteral("valid")).toBool()) {
        finishRun(tr("Measurement invalid: %1. Run the test again.")
                  .arg(result.value(QStringLiteral("invalid")).toString()));
        return;
    }
    const bool stuck = result.value(QStringLiteral("stuck")).toBool();
    const bool quiet = result.value(QStringLiteral("cls")).toString() == QLatin1String("quiet");
    if (m_phase == QLatin1String("baseline")) {
        if (!quiet) {
            m_trigger.insert(QStringLiteral("outcome"), QStringLiteral("busy-before"));
            finishRun(QString());
            return;
        }
        m_phase = QStringLiteral("plain");
        m_testIds.append(sendTestNotification(false));
        m_settle = kSettleSec;
        m_left = m_settle;
        emit runChanged();
        return;
    }
    if (m_phase == QLatin1String("plain")) {
        if (stuck) {
            m_trigger.insert(QStringLiteral("outcome"), QStringLiteral("plain"));
            finishRun(QString());
            return;
        }
        m_phase = QStringLiteral("actions");
        m_testIds.append(sendTestNotification(true));
        m_settle = kSettleSec;
        m_left = m_settle;
        emit runChanged();
        return;
    }
    // actions
    m_trigger.insert(QStringLiteral("outcome"), stuck ? QStringLiteral("actions")
                                                      : QStringLiteral("none"));
    finishRun(QString());
}

void RedrawCheck::finishRun(const QString &verdict)
{
    m_runTimer.stop();
    closeTestNotifications();
    if (m_mode == QLatin1String("trigger") && !verdict.isEmpty())
        m_trigger.insert(QStringLiteral("message"), verdict);
    if (m_mode == QLatin1String("measure") && !verdict.isEmpty())
        m_measurement.insert(QStringLiteral("message"), verdict);
    m_phase.clear();
    m_settle = 0;
    m_left = 0;
    // a fresh start for the watch: the run itself was a long look
    m_wPrevMs = 0;
    m_hotWindows = 0;
    emit runChanged();
    emit tick();
}

// A notification like any app sends. The action buttons call the bus's own
// Ping, which does nothing -- they exist only to be shown.
uint RedrawCheck::sendTestNotification(bool withActions)
{
    QDBusInterface n(QStringLiteral("org.freedesktop.Notifications"),
                     QStringLiteral("/org/freedesktop/Notifications"),
                     QStringLiteral("org.freedesktop.Notifications"));
    QStringList actions;
    QVariantMap hints;
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue<uchar>(1));
    if (withActions) {
        const QString ping = QStringLiteral("org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.Peer Ping");
        actions << QStringLiteral("default") << QString()
                << QStringLiteral("test-a") << tr("Test A")
                << QStringLiteral("test-b") << tr("Test B");
        hints.insert(QStringLiteral("x-nemo-remote-action-default"), ping);
        hints.insert(QStringLiteral("x-nemo-remote-action-test-a"), ping);
        hints.insert(QStringLiteral("x-nemo-remote-action-test-b"), ping);
    }
    const QDBusReply<uint> r = n.call(QStringLiteral("Notify"), QStringLiteral("SysMetrics"), 0u,
        QStringLiteral("icon-lock-information"),
        withActions ? tr("SysMetrics test 2 of 2") : tr("SysMetrics test 1 of 2"),
        withActions ? tr("With action buttons") : tr("Without action buttons"),
        actions, hints, 5000);
    return r.isValid() ? r.value() : 0;
}

void RedrawCheck::closeTestNotifications()
{
    if (m_testIds.isEmpty())
        return;
    QDBusInterface n(QStringLiteral("org.freedesktop.Notifications"),
                     QStringLiteral("/org/freedesktop/Notifications"),
                     QStringLiteral("org.freedesktop.Notifications"));
    for (uint id : m_testIds)
        if (id)
            n.call(QDBus::NoBlock, QStringLiteral("CloseNotification"), id);
    m_testIds.clear();
}

// ---------------------------------------------------------------- report

static QString fmt(double v, int prec = 1) { return QString::number(v, 'f', prec); }

QString RedrawCheck::report() const
{
    QStringList o;
    auto window = [&o](const QVariantMap &m, const QString &head) {
        if (m.isEmpty())
            return;
        o << head;
        if (!m.value(QStringLiteral("valid"), true).toBool())
            o << QStringLiteral("  ") + tr("invalid: %1").arg(m.value(QStringLiteral("invalid")).toString());
        o << QStringLiteral("  ") + tr("render thread wakeups: %1/s").arg(fmt(m.value(QStringLiteral("renderWake")).toDouble()));
        if (m.value(QStringLiteral("frames")).toDouble() >= 0)
            o << QStringLiteral("  ") + tr("frames to the display: %1/s (%2)")
                     .arg(fmt(m.value(QStringLiteral("frames")).toDouble()), m.value(QStringLiteral("frameSource")).toString());
        o << QStringLiteral("  ") + tr("lipstick: %1 % of one core (main %2 %, render %3 %)")
                 .arg(fmt(m.value(QStringLiteral("lipstickPct")).toDouble()),
                      fmt(m.value(QStringLiteral("mainPct")).toDouble()),
                      fmt(m.value(QStringLiteral("renderPct")).toDouble()));
        o << QStringLiteral("  ") + tr("composer: %1 % of one core").arg(fmt(m.value(QStringLiteral("composerPct")).toDouble()));
        o << QStringLiteral("  ") + tr("classification: %1").arg(m.value(QStringLiteral("cls")).toString());
        for (const QVariant &c : m.value(QStringLiteral("clients")).toList()) {
            const QVariantMap cm = c.toMap();
            o << QStringLiteral("  ") + tr("client in step: %1 (PID %2), %3 wakeups/s")
                     .arg(cm.value(QStringLiteral("name")).toString())
                     .arg(cm.value(QStringLiteral("pid")).toInt())
                     .arg(fmt(cm.value(QStringLiteral("wake")).toDouble()));
        }
    };
    o << tr("SysMetrics: home screen redraw check");
    QFile rel(QStringLiteral("/etc/sailfish-release"));
    if (rel.open(QIODevice::ReadOnly))
        for (const QByteArray &l : rel.readAll().split('\n'))
            if (l.startsWith("VERSION="))
                o << QStringLiteral("Sailfish OS ") + QString::fromUtf8(l.mid(8)).remove(QLatin1Char('"'));
    window(m_measurement, tr("Idle measurement:"));
    for (const QVariant &s : m_trigger.value(QStringLiteral("steps")).toList()) {
        const QVariantMap sm = s.toMap();
        const QString step = sm.value(QStringLiteral("step")).toString();
        window(sm, step == QLatin1String("baseline") ? tr("Trigger test, before:")
                 : step == QLatin1String("plain") ? tr("After a notification without action buttons:")
                                                 : tr("After a notification with action buttons:"));
    }
    const QString outcome = m_trigger.value(QStringLiteral("outcome")).toString();
    if (outcome == QLatin1String("actions"))
        o << tr("Result: a notification with action buttons leaves lipstick redrawing; one without does not.");
    else if (outcome == QLatin1String("plain"))
        o << tr("Result: a notification without action buttons already leaves lipstick redrawing.");
    else if (outcome == QLatin1String("none"))
        o << tr("Result: neither test notification left lipstick redrawing.");
    if (m_suspect)
        o << tr("Watch: redrawing without touch since %1")
                 .arg(QDateTime::fromMSecsSinceEpoch(m_watch.value(QStringLiteral("since")).toLongLong())
                      .toString(QStringLiteral("HH:mm:ss")));
    return o.join(QLatin1Char('\n'));
}

QString RedrawCheck::restartHint() const
{
    if (QFileInfo::exists(QStringLiteral("/usr/share/jolla-settings/entries/utilities.json")))
        return tr("Restarting the home screen ends the state: Settings → Utilities → Home Screen → Restart. This closes all running apps.");
    return tr("Restarting the device ends the state.");
}
