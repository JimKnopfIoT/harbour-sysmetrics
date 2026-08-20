// Worker-thread sampler: reads /proc and /sys, emits value-copied snapshots.
#pragma once

#include <QHash>
#include <QMetaType>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

struct ProcSample {
    int pid = 0;
    int ppid = 0;
    uint uid = 0;
    char state = '?';
    int nice = 0;
    int threads = 0;
    float cpuPct = 0.f;          // percent of one core
    qulonglong rssBytes = 0;
    qulonglong jiffies = 0;      // utime+stime
    qulonglong startJiffies = 0; // starttime, pid-reuse guard
    QString name;
    QString cmdline;
    bool kernelThread = false;
};
Q_DECLARE_METATYPE(QVector<ProcSample>)

struct SysSnap {
    float cpuPct = 0.f;          // all cores aggregated, 0..100
    QVector<float> corePct;
    QVector<int> coreFreqKhz;
    qulonglong memTotal = 0, memAvailable = 0, swapTotal = 0, swapFree = 0;
    qulonglong cached = 0, buffers = 0;
    double load1 = 0, load5 = 0, load15 = 0;
    qlonglong uptimeSec = 0;
    int processCount = 0, threadCount = 0, runnable = 0;
    double netRxRate = 0, netTxRate = 0;       // B/s
    qulonglong netRxTotal = 0, netTxTotal = 0;
    double diskReadRate = 0, diskWriteRate = 0;
    QVector<QPair<QString, float>> thermal;    // zone type, degC
    int battCapacity = -1;
    double battCurrentA = 0, battVoltageV = 0, battTempC = 0, battPowerW = 0;
    int battHealthPct = -1;
    bool battHealthFromGauge = false;
    int battCycles = -1;
    double battChargeFull = 0, battChargeDesign = 0;  // µAh
    QString battStatus, battTech;
    QString battHealthReport;   // driver's own health string, if any
    QString battModel;
    QString kernel;
    QVector<QPair<QString, QString>> ifaces;   // name, "rx|tx" totals encoded by sysmon
};
Q_DECLARE_METATYPE(SysSnap)

class QTimer;

class Sampler : public QObject
{
    Q_OBJECT
public:
    explicit Sampler(QObject *parent = nullptr);

public slots:
    void start();                 // must run in the worker thread
    void setIntervalMs(int ms);
    void setPaused(bool paused);
    void sampleNow();

signals:
    void systemSampled(const SysSnap &snap);
    void processesSampled(const QVector<ProcSample> &procs, qulonglong totalDeltaJiffies);

private:
    void sample();
    void sampleSystem(SysSnap &s, qulonglong &totalDelta);
    void sampleProcesses(QVector<ProcSample> &out, qint64 dtMs);

    QTimer *m_timer = nullptr;
    int m_intervalMs = 3000;
    bool m_paused = false;

    long m_clkTck = 100;
    // previous /proc/stat per-core (busy,total), index 0 = aggregate
    QVector<QPair<qulonglong, qulonglong>> m_prevCpu;
    qulonglong m_prevRx = 0, m_prevTx = 0;
    qulonglong m_prevDiskRd = 0, m_prevDiskWr = 0;
    qint64 m_prevMs = 0;
    struct PrevProc { qulonglong start; qulonglong jiffies; QString name; QString cmdline; };
    QHash<int, PrevProc> m_prevProc;
};
