// Why does the home screen keep drawing? The diagnosis path for a lipstick
// that redraws without cause, in the order it was worked out on the device:
// measure the idle screen, attribute the frames, find what came before the
// onset, reproduce the trigger with a counter-test, then match the known
// defects. Reads /proc and the bus; never restarts or changes anything.
#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class QFileSystemWatcher;

class RedrawCheck : public QObject
{
    Q_OBJECT
    // Background watch: a cheap look at lipstick's render thread every few
    // seconds while the display is on and this app is not in front.
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged)
    Q_PROPERTY(bool foreground READ foreground WRITE setForeground NOTIFY foregroundChanged)
    // Set while lipstick has been redrawing without touch for longer than the
    // watch threshold; the process list marks this pid.
    Q_PROPERTY(int flaggedPid READ flaggedPid NOTIFY watchChanged)
    Q_PROPERTY(QVariantMap watch READ watch NOTIFY watchChanged)
    Q_PROPERTY(int lipstickPid READ lipstickPid NOTIFY watchChanged)
    // Guided run (measurement or trigger test).
    Q_PROPERTY(bool busy READ busy NOTIFY runChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY runChanged)
    Q_PROPERTY(QString coverText READ coverText NOTIFY runChanged)
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY tick)
    Q_PROPERTY(QVariantMap measurement READ measurement NOTIFY runChanged)
    Q_PROPERTY(QVariantMap trigger READ trigger NOTIFY runChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(bool notifyWatchAvailable READ notifyWatchAvailable CONSTANT)

public:
    explicit RedrawCheck(QObject *parent = nullptr);
    ~RedrawCheck();

    bool watching() const { return m_watching; }
    void setWatching(bool on);
    bool foreground() const { return m_foreground; }
    void setForeground(bool on);
    int flaggedPid() const { return m_suspect ? m_lipstick : 0; }
    QVariantMap watch() const { return m_watch; }
    int lipstickPid() const { return m_lipstick; }
    bool busy() const { return !m_phase.isEmpty(); }
    QString phase() const { return m_phase; }
    QString coverText() const;
    int secondsLeft() const { return m_left; }
    QVariantMap measurement() const { return m_measurement; }
    QVariantMap trigger() const { return m_trigger; }
    QVariantList events() const;
    bool notifyWatchAvailable() const;

    Q_INVOKABLE void startMeasurement();
    Q_INVOKABLE void startTriggerTest();
    Q_INVOKABLE void cancel();
    // Role of a lipstick thread for display ("" for other processes).
    Q_INVOKABLE QString threadRole(int pid, int tid, const QString &name) const;
    Q_INVOKABLE QString report() const;
    // How the reader ends the state themselves; this app never restarts it.
    Q_INVOKABLE QString restartHint() const;

signals:
    void watchingChanged();
    void foregroundChanged();
    void watchChanged();
    void runChanged();
    void eventsChanged();
    void tick();

private:
    struct Thr {
        int tid = 0;
        QString name;
        qulonglong jiffies = 0;
        qulonglong vctx = 0;
    };
    struct ProcTotals {
        QString name;
        qulonglong jiffies = 0;
        QHash<int, qulonglong> vctx;   // per thread
    };
    struct Event {                      // aggregate: built with Event{...}
        qint64 ms;
        QString kind;                   // "notification", "screenshot", "touch"
        QString app;
        int actions;                    // visible action buttons, -1 = n/a
    };

    void findLipstick();
    QVector<Thr> readThreads(int pid) const;
    QHash<QString, qulonglong> readDisplayIrqs() const;
    QHash<int, ProcTotals> readClients();
    bool displayOn() const;
    qulonglong composerJiffies() const;
    void noteQuietIrq(const QString &name, double rate);
    static QString roleOf(int pid, int tid, const QString &name);

    void watchTick();
    void runTick();
    void beginWindow(int seconds);
    QVariantMap finishWindow();
    void advance(const QVariantMap &result = QVariantMap());
    void finishRun(const QString &verdict);
    uint sendTestNotification(bool withActions);
    void closeTestNotifications();
    void addEvent(const Event &e);
    void startNotifyWatch();
    void onNotifyOutput();
    void onScreenshotDir();

    // watch
    bool m_watching = true;
    bool m_foreground = true;
    QTimer m_watchTimer;
    int m_lipstick = 0;
    qulonglong m_lipStart = 0;
    qulonglong m_wPrevRender = 0, m_wPrevTouch = 0;
    qint64 m_wPrevMs = 0;
    int m_hotWindows = 0;
    qint64 m_hotSince = 0;
    bool m_suspect = false;
    QVariantMap m_watch;

    QHash<QString, qulonglong> m_wPrevIrq;

    // display-IRQ credibility: lowest rate seen while lipstick was quiet
    QHash<QString, double> m_irqQuietMin;
    QHash<int, bool> m_wlCache;         // pid -> is a Wayland client
    long m_clkTck = 100;
    qulonglong m_comp0 = 0;

    // run
    QTimer m_runTimer;
    QString m_phase;                    // "", "wait", "baseline", "plain", "actions", "measure"
    QString m_mode;                     // "measure" or "trigger"
    int m_left = 0;
    int m_waitFg = 0;
    int m_settle = 0;
    QVector<Thr> m_t0;
    QHash<QString, qulonglong> m_irq0;
    QHash<int, ProcTotals> m_clients0;
    qint64 m_ms0 = 0;
    QVector<double> m_perSecWake;
    qulonglong m_prevRenderSec = 0, m_prevTouchSec = 0;
    QString m_invalid;
    QVariantMap m_measurement;
    QVariantMap m_trigger;
    QVariantList m_steps;
    QList<uint> m_testIds;

    // events
    QList<Event> m_events;
    QProcess *m_monitor = nullptr;
    QByteArray m_monBuf;
    int m_monState = 0;                 // parser state for dbus-monitor output
    Event m_monEvent;
    int m_monStrings = 0;
    QFileSystemWatcher *m_fsw = nullptr;
    QString m_shotDir;
    QStringList m_shots;
};
