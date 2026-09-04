// Worker-thread sampler: reads /proc and /sys, emits value-copied snapshots.
#pragma once

#include <QHash>
#include <QMetaType>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

#include "source.h"

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
    int lastCpu = -1;            // core it last ran on: which cluster it costs on
    QString name;
    QString cmdline;
    bool kernelThread = false;
};
Q_DECLARE_METATYPE(QVector<ProcSample>)

struct SysSnap {
    // Sentinel for a core the kernel has parked: it has no /proc/stat line, so
    // there is no load and no frequency to report -- distinct from a genuine 0.
    static const int CoreOffline = -1;

    float cpuPct = 0.f;          // all cores aggregated, 0..100
    QVector<float> corePct;      // indexed by CPU number; CoreOffline if parked
    QVector<int> coreFreqKhz;    // indexed by CPU number; CoreOffline if parked
    qulonglong memTotal = 0, memAvailable = 0, swapTotal = 0, swapFree = 0;
    qulonglong cached = 0, buffers = 0;
    double load1 = 0, load5 = 0, load15 = 0;
    qlonglong uptimeSec = 0;
    int processCount = 0, threadCount = 0, runnable = 0;
    double netRxRate = 0, netTxRate = 0;       // B/s, active interface only
    QString netIface;                          // interface carrying the default route
    qulonglong netRxTotal = 0, netTxTotal = 0;
    double diskReadRate = 0, diskWriteRate = 0;
    // corrected marks a reading this app recomputed instead of passing the
    // kernel's through unchanged — see fixFlashTherm() in sampler.cpp
    // suspect: the reading does not belong with the others on this board.
    // A zone carrying millivolts reads as a plausible temperature; what
    // gives it away is standing far below every other sensor in the same
    // phone, which cannot happen to a real one.
    struct ThermalZone { QString name; float degC = 0; bool corrected = false;
                         bool suspect = false; };
    QVector<ThermalZone> thermal;
    int battCapacity = -1;
    double battCurrentA = 0, battVoltageV = 0, battTempC = 0, battPowerW = 0;
    // Whether that current is a reading at all. The Gemini PDA answers its
    // legacy BatteryAverageCurrent with a hard 0 while discharging: the driver
    // publishes no current, and a 0 mA printed on the page would be a claim
    // the device cannot support.
    bool battCurrentValid = true;
    int battHealthPct = -1;
    double battHealthExact = -1;   // same figure, undivided by rounding
    int battSohRegister = -1;      // what the gauge's soh register claims
    bool battHealthFromGauge = false;
    bool battHealthCatalogue = false;  // full == design: one profile figure, not a measurement
    int battCycles = -1;
    double battChargeFull = 0, battChargeDesign = 0;  // mAh, 0 = unknown
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
    void setProcessesEnabled(bool on);
    void setThermalEnabled(bool on);
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
    // Walking /proc is by far the most expensive part of a tick and only the
    // process list needs it. While the app is covered nobody can see that list,
    // so it is skipped and the cover keeps its cheap system figures.
    bool m_procsEnabled = true;
    // A pass over the thermal framework costs 134 ms on the Jolla Phone
    // (2026): 56 zones, and reading one is an I2C transaction to the part
    // it sits on -- primary_dvchg alone takes 11 ms, consys 6. Only the
    // overview shows these live, so off that page they are not read at all
    // and the last pass stands until it is.
    bool m_thermalEnabled = true;
    QVector<SysSnap::ThermalZone> m_lastThermal;
    // A zone's type cannot change while the system runs; re-reading it 56
    // times a tick bought nothing.
    QHash<QString, QString> m_zoneType;
    qint64 m_prevProcMs = 0;     // last tick that actually walked /proc
    int m_lastProcCount = 0, m_lastThreadCount = 0;

    long m_clkTck = 100;
    // previous /proc/stat counters (busy,total): per-core keyed by CPU number so
    // hotplug cannot misalign them, aggregate line kept separately
    QHash<int, QPair<qulonglong, qulonglong>> m_prevCore;
    QPair<qulonglong, qulonglong> m_prevAll = qMakePair(0ull, 0ull);
    bool m_haveAll = false;
    int m_cpuCount = 0;          // every CPU present, parked ones included
    qulonglong m_prevRx = 0, m_prevTx = 0;
    QString m_prevIface;
    qulonglong m_prevDiskRd = 0, m_prevDiskWr = 0;
    qint64 m_prevMs = 0;
    // Latched once a non-zero current has ever been seen; until then a run of
    // zeroes while the cell is discharging is what proves the sensor absent.
    bool m_battCurrentSeen = false;
    int m_battZeroWhileDischarging = 0;

    // Where this phone keeps each battery figure. Resolved on the first sample
    // and remembered: the fallbacks cost one probe, not one per tick.
    QString m_batDir;
    Source m_srcCapacity, m_srcCurrent, m_srcVoltage, m_srcTemp;
    Source m_srcFull, m_srcDesign, m_srcSoh, m_srcCycles;
    // Everything here is either needed for the next delta or is immutable for the
    // lifetime of the process and therefore worth not re-reading every tick.
    struct PrevProc {          // aggregate: always brace-initialised in full
        qulonglong start; qulonglong jiffies;
        QString name; QString cmdline;
        uint uid; bool kernelThread;
    };
    QHash<int, PrevProc> m_prevProc;
};
