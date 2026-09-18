// Binder traffic: who talks to whom over the Android IPC, how often, and
// what fails. The kernel keeps the whole tally itself -- this class only
// reads it and turns absolute counters into rates.
//
// Why this exists: a spinning "binder:1234_5" thread is a thread of process
// 1234's binder pool, and the process list can say no more than its name.
// What the kernel does know, and hands out for free, is which binder domain
// that process is attached to, how many calls it has made, whether its
// thread pool is exhausted, and which of its calls came back as an error.
// That is the difference between "something is spinning" and "this process
// is calling that service and getting a dead reply 1.7 times a second".
//
// Where the numbers come from, in the order they are tried:
//   /dev/binderfs/binder_logs/   -- binderfs, mode 0444, no privilege needed
//   /sys/kernel/debug/binder/    -- older ports; debugfs is 0700 on most of
//                                   them, so this path may need the helper
// Both carry the same files. The formats differ with kernel age (a 3.18 port
// has no context line and no page counters), so every parse here is by
// keyword, never by column.
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QProcess;
class QTimer;

class BinderMon : public QObject
{
    Q_OBJECT
    // Whether the kernel's binder log directory could be read at all.
    Q_PROPERTY(bool available READ available NOTIFY updated)
    // The directory the figures come from, shown verbatim so the reader can
    // go and look for themselves.
    Q_PROPERTY(QString source READ source NOTIFY updated)
    // True when that directory was only reachable through the root helper.
    Q_PROPERTY(bool privileged READ privileged NOTIFY updated)
    // Set when the directory exists but neither we nor the helper may read it.
    Q_PROPERTY(QString error READ error NOTIFY updated)

    // Kernel-wide counters and the rates derived from them.
    Q_PROPERTY(QVariantMap totals READ totals NOTIFY updated)
    // One entry per binder domain (context) that has processes attached.
    Q_PROPERTY(QVariantList domains READ domains NOTIFY updated)
    // Processes ranked by calls per second, the ranking the process list
    // cannot give.
    Q_PROPERTY(QVariantList talkers READ talkers NOTIFY updated)
    // Failed transactions from the kernel's ring, grouped by caller and error.
    Q_PROPERTY(QVariantList failures READ failures NOTIFY updated)
    // Transactions still in flight on the second sighting -- a call that has
    // not come back between two refreshes.
    Q_PROPERTY(QVariantList stuck READ stuck NOTIFY updated)
    // Assessments. Each one names the threshold it applied.
    Q_PROPERTY(QVariantList findings READ findings NOTIFY updated)
    // Seconds between the two samples the rates rest on; 0 before the second.
    Q_PROPERTY(double interval READ interval NOTIFY updated)

    // The registered services, once identified. Each entry carries how it was
    // established -- nothing in here is inferred from a name.
    Q_PROPERTY(QVariantList services READ services NOTIFY servicesChanged)
    Q_PROPERTY(bool canIdentify READ canIdentify NOTIFY servicesChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
    Q_PROPERTY(int scanDone READ scanDone NOTIFY scanChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanChanged)
    Q_PROPERTY(QString scanCurrent READ scanCurrent NOTIFY scanChanged)

public:
    explicit BinderMon(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString source() const { return m_source; }
    bool privileged() const { return m_privileged; }
    QString error() const { return m_error; }
    QVariantMap totals() const { return m_totals; }
    QVariantList domains() const { return m_domains; }
    QVariantList talkers() const { return m_talkers; }
    QVariantList failures() const { return m_failures; }
    QVariantList stuck() const { return m_stuck; }
    QVariantList findings() const { return m_findings; }
    double interval() const { return m_interval; }
    QVariantList services() const { return m_services; }
    bool canIdentify() const { return m_canIdentify; }
    bool scanning() const { return m_scanning; }
    int scanDone() const { return m_scanDone; }
    int scanTotal() const { return m_scanTotal; }
    QString scanCurrent() const { return m_scanCurrent; }

    // One pass: stats, both transaction rings, and the per-process files of
    // the busiest few. Cheap enough for a 2 s timer -- measured 45 KB and
    // 20 ms on the Jolla phone of 2026 with 78 binder processes.
    Q_INVOKABLE void refresh();

    // The binder facts about one process, for the process detail page.
    // Empty when the process holds no binder connection.
    Q_INVOKABLE QVariantMap processDetail(int pid) const;

    // "binder:3610_2" -> everything that is known for certain about that
    // thread: it is thread 2 of the binder pool of process 3610, that process
    // is <name>, it is attached to domain <ctx>, and it serves these
    // registered services. The kernel builds the thread name from the pid of
    // the pool owner, so the first step is exact rather than a guess; every
    // further field says where it comes from.
    Q_INVOKABLE QVariantMap describeThread(const QString &threadName) const;
    // Just the owner's display name, for a one-line caption.
    Q_INVOKABLE QString threadOwner(const QString &threadName) const;
    // The services a process was proven to serve, empty until identified.
    Q_INVOKABLE QVariantList servicesOf(int pid) const;
    // The same, short enough for a list row: the first service name and how
    // many more there are. Empty until the services have been identified, so
    // a row that has no proof shows nothing rather than a guess.
    Q_INVOKABLE QString serviceLabel(int pid) const;

    // Ask every registered service who serves it. This sends a ping
    // transaction -- the liveness check every binder service must answer and
    // which performs no action inside it -- and then reads back the kernel's
    // own transaction log to see which process answered. That log entry is
    // the proof; nothing is concluded from the service's name. A service that
    // does not answer within the timeout is reported as such.
    Q_INVOKABLE void identifyServices();
    Q_INVOKABLE void cancelScan();

    // The dead-node section of the state file: nodes whose owner is gone but
    // which somebody still holds a reference to. Read on request only -- the
    // state file dumps every node and reference of every process (239 KB on
    // the Jolla phone) and takes the binder locks while doing it.
    Q_INVOKABLE QVariantMap deadNodes();

    // The two transaction rings as the kernel prints them, line for line.
    // Both hold 32 entries, so this is the tail of the traffic and never the
    // whole of it -- shown because a reader who wants to check a figure
    // should be able to see the lines it was counted from.
    Q_INVOKABLE QVariantMap rawLog();

signals:
    void updated();
    void servicesChanged();
    void scanChanged();

private slots:
    void scanStep();
    void onPingFinished();
    void onPingTimeout();

private:
    // A process section of the stats file, as the kernel prints it.
    struct ProcStat {
        int pid = 0;
        QString context;          // empty on kernels before binderfs contexts
        int threads = 0;
        int readyThreads = -1;
        int requestedStarted = 0; // "requested threads: a+b/max"
        int requestedNow = 0;
        int maxThreads = 0;
        qlonglong freeAsync = -1;
        int nodes = 0;
        int refs = 0;
        int buffers = 0;
        int pending = 0;
        quint64 calls = 0;        // BC_TRANSACTION + BC_TRANSACTION_SG
        quint64 replies = 0;      // BC_REPLY + BC_REPLY_SG
        quint64 incoming = 0;     // BR_TRANSACTION
    };

    // A service of the registry, with the evidence for its owner.
    struct Service {
        QString name;
        QString device;      // the binder device it is registered on
        QString context;     // basename of that device = the binder context
        int node = -1;       // its binder node, from the ping's log entry
        int pid = -1;        // the process that answered
        QString proof;       // "ping" | "state" | "no answer" | "unproven"
    };

    // One line of transaction_log / failed_transaction_log.
    struct LogEntry {
        quint64 id = 0;
        QString kind;             // call | reply | async
        int fromPid = 0, fromTid = 0, toPid = 0, toTid = 0;
        QString context;
        int node = -1, handle = -1;
        int retCode = 0;          // BR_* the caller received
        int retParam = 0;         // negative errno behind it
        bool hasRet = false;
    };

    QByteArray readLog(const QString &name);
    bool locateSource();
    void parseStats(const QByteArray &blob, QHash<int, ProcStat> *procs);
    QList<LogEntry> parseLog(const QByteArray &blob) const;
    void buildTalkers(const QHash<int, ProcStat> &now, double dt);
    void buildDomains(const QHash<int, ProcStat> &now, double dt);
    void buildFailures(const QList<LogEntry> &entries, double dt);
    void buildStuck(const QHash<int, ProcStat> &now);
    void assess(const QHash<int, ProcStat> &now);
    void loadGbinderConfig();
    QString procName(int pid) const;
    QString procCmdline(int pid) const;

    // node id -> owning pid, read out of the state file. This is what keeps a
    // once-identified service honest: a node that moved or vanished loses its
    // claim instead of carrying a stale pid.
    QHash<int, int> nodeOwners() const;
    void reverifyServices();
    void publishServices();
    QStringList listServices(const QString &device) const;
    void loadServiceCache();
    void saveServiceCache() const;

    QString m_root;        // directory the log files live in
    QString m_source;
    QString m_error;
    bool m_available = false;
    bool m_privileged = false;

    QVariantMap m_totals;
    QVariantList m_domains;
    QVariantList m_talkers;
    QVariantList m_failures;
    QVariantList m_stuck;
    QVariantList m_findings;
    double m_interval = 0;

    QHash<int, ProcStat> m_prev;       // previous sample, for the rates
    QHash<int, QVariantMap> m_byPid;   // this sample per process, for the detail page
    QHash<QString, quint64> m_prevGlobal;
    quint64 m_lastFailedId = 0;
    quint64 m_lastLogId = 0;
    QElapsedTimer m_since;
    bool m_havePrev = false;

    // pid:transaction-id -> when it was first seen, so "still in flight on
    // the second sighting" can be told from ordinary traffic.
    QHash<QString, QVariantMap> m_inFlight;

    QVariantList m_services;
    QList<Service> m_registry;
    QStringList m_scanQueue;         // "device\tname" per entry
    QString m_scanDevice;
    QString m_scanName;
    QProcess *m_ping = nullptr;
    QTimer *m_pingGuard = nullptr;
    quint64 m_scanLogMark = 0;
    bool m_scanning = false;
    bool m_canIdentify = false;
    int m_scanDone = 0;
    int m_scanTotal = 0;
    QString m_scanCurrent;
    QString m_bootId;                // the cache is only valid within one boot

    // context name -> what declared it, from /etc/gbinder.conf and
    // /etc/gbinder.d/*.conf. Nothing is assumed about a name: a domain is
    // called AppSupport's only if AppSupport's own config file declares it.
    QHash<QString, QString> m_ctxProtocol;
    QHash<QString, QString> m_ctxDeclaredBy;
};
