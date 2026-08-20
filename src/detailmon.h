// Per-process deep inspection, instantiated from QML with a pid.
#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class DetailMon : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int pid READ pid WRITE setPid NOTIFY pidChanged)
    Q_PROPERTY(bool alive READ alive NOTIFY updated)
    Q_PROPERTY(bool sameUser READ sameUser NOTIFY updated)
    Q_PROPERTY(QVariantMap info READ info NOTIFY updated)
    Q_PROPERTY(QVariantMap cpu READ cpu NOTIFY updated)
    Q_PROPERTY(QVariantMap mem READ mem NOTIFY updated)
    Q_PROPERTY(QVariantMap io READ io NOTIFY updated)
    Q_PROPERTY(QVariantMap energy READ energy NOTIFY updated)
    Q_PROPERTY(QVariantMap watch READ watch NOTIFY updated)
    Q_PROPERTY(QVariantList files READ files NOTIFY updated)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY updated)
    Q_PROPERTY(QVariantList sockets READ sockets NOTIFY updated)
    Q_PROPERTY(QVariantList threads READ threads NOTIFY updated)
    Q_PROPERTY(QVariantList notes READ notes NOTIFY updated)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY updated)

public:
    explicit DetailMon(QObject *parent = nullptr);

    int pid() const { return m_pid; }
    void setPid(int pid);
    bool alive() const { return m_alive; }
    bool sameUser() const { return m_sameUser; }
    QVariantMap info() const { return m_info; }
    QVariantMap cpu() const { return m_cpu; }
    QVariantMap mem() const { return m_mem; }
    QVariantMap io() const { return m_io; }
    QVariantMap energy() const { return m_energy; }
    QVariantMap watch() const { return m_watch; }
    QVariantList files() const { return m_files; }
    QVariantList devices() const { return m_devices; }
    QVariantList sockets() const { return m_sockets; }
    QVariantList threads() const { return m_threads; }
    QVariantList notes() const { return m_notes; }
    QVariantList cpuHistory() const { return m_cpuHist; }

signals:
    void pidChanged();
    void updated();

private:
    void sample();
    void sampleStatic();
    void sampleCpu(qint64 dtMs, double &pct, double &sharePct);
    void sampleFds();
    void sampleWatchers();
    void assess();
    static QString procName(int pid);

    int m_pid = 0;
    bool m_alive = false;
    bool m_sameUser = false;
    QTimer m_timer;
    qint64 m_prevMs = 0;
    int m_tick = 0;
    long m_clkTck = 100;
    int m_nCores = 0;

    // deltas
    qulonglong m_prevJiffies = 0;
    qulonglong m_prevSysBusy = 0, m_prevSysTotal = 0;
    QVector<qulonglong> m_prevCoreBusy;
    QHash<int, qulonglong> m_prevTid;
    qulonglong m_prevVctx = 0, m_prevNvctx = 0;
    qulonglong m_prevMinflt = 0, m_prevMajflt = 0;
    qulonglong m_prevRd = 0, m_prevWr = 0;
    QHash<quint64, QPair<qulonglong, qulonglong>> m_prevQueues; // sock inode -> tx,rx
    double m_emaCpu = -1, m_emaWake = -1, m_emaWrite = -1;

    QVariantMap m_info, m_cpu, m_mem, m_io, m_energy, m_watch;
    QVariantList m_files, m_devices, m_sockets, m_threads, m_notes, m_cpuHist;
};
