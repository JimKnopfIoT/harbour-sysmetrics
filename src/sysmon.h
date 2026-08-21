// GUI-thread system state: latest snapshot, ring-buffer histories, actions.
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVector>

#include "sampler.h"

class SysMon : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal cpuPercent READ cpuPercent NOTIFY updated)
    Q_PROPERTY(int coreCount READ coreCount NOTIFY updated)
    Q_PROPERTY(int coresOnline READ coresOnline NOTIFY updated)
    Q_PROPERTY(QVariantList corePercents READ corePercents NOTIFY updated)
    Q_PROPERTY(QVariantList coreFreqsMhz READ coreFreqsMhz NOTIFY updated)
    Q_PROPERTY(double memTotal READ memTotal NOTIFY updated)
    Q_PROPERTY(double memUsed READ memUsed NOTIFY updated)
    Q_PROPERTY(double memAvailable READ memAvailable NOTIFY updated)
    Q_PROPERTY(double swapTotal READ swapTotal NOTIFY updated)
    Q_PROPERTY(double swapUsed READ swapUsed NOTIFY updated)
    Q_PROPERTY(double cached READ cached NOTIFY updated)
    Q_PROPERTY(double buffers READ buffers NOTIFY updated)
    Q_PROPERTY(qreal load1 READ load1 NOTIFY updated)
    Q_PROPERTY(qreal load5 READ load5 NOTIFY updated)
    Q_PROPERTY(qreal load15 READ load15 NOTIFY updated)
    Q_PROPERTY(int uptimeSec READ uptimeSec NOTIFY updated)
    Q_PROPERTY(int processCount READ processCount NOTIFY updated)
    Q_PROPERTY(int threadCount READ threadCount NOTIFY updated)
    Q_PROPERTY(int runnable READ runnable NOTIFY updated)
    Q_PROPERTY(double netRxRate READ netRxRate NOTIFY updated)
    Q_PROPERTY(double netTxRate READ netTxRate NOTIFY updated)
    Q_PROPERTY(double netRxTotal READ netRxTotal NOTIFY updated)
    Q_PROPERTY(double netTxTotal READ netTxTotal NOTIFY updated)
    Q_PROPERTY(double diskReadRate READ diskReadRate NOTIFY updated)
    Q_PROPERTY(double diskWriteRate READ diskWriteRate NOTIFY updated)
    Q_PROPERTY(QVariantList thermalZones READ thermalZones NOTIFY updated)
    Q_PROPERTY(QVariantList interfaces READ interfaces NOTIFY updated)
    Q_PROPERTY(int battCapacity READ battCapacity NOTIFY updated)
    Q_PROPERTY(double battCurrentA READ battCurrentA NOTIFY updated)
    Q_PROPERTY(double battVoltageV READ battVoltageV NOTIFY updated)
    Q_PROPERTY(double battTempC READ battTempC NOTIFY updated)
    Q_PROPERTY(double battPowerW READ battPowerW NOTIFY updated)
    Q_PROPERTY(int battHealthPct READ battHealthPct NOTIFY updated)
    Q_PROPERTY(bool battHealthFromGauge READ battHealthFromGauge NOTIFY updated)
    Q_PROPERTY(int battCycles READ battCycles NOTIFY updated)
    Q_PROPERTY(double battChargeFull READ battChargeFull NOTIFY updated)
    Q_PROPERTY(double battChargeDesign READ battChargeDesign NOTIFY updated)
    Q_PROPERTY(QString battTech READ battTech NOTIFY updated)
    Q_PROPERTY(QString battModel READ battModel NOTIFY updated)
    Q_PROPERTY(QString battHealthReport READ battHealthReport NOTIFY updated)
    Q_PROPERTY(QString battQuality READ battQuality NOTIFY updated)
    Q_PROPERTY(QString battStatus READ battStatus NOTIFY updated)
    Q_PROPERTY(QString kernel READ kernel NOTIFY updated)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY updated)
    Q_PROPERTY(QVariantList memHistory READ memHistory NOTIFY updated)
    Q_PROPERTY(QVariantList rxHistory READ rxHistory NOTIFY updated)
    Q_PROPERTY(QVariantList txHistory READ txHistory NOTIFY updated)
    Q_PROPERTY(QVariantList battHistory READ battHistory NOTIFY updated)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    // Bound to ApplicationWindow.applicationActive: false means the app is
    // covered, so nobody is looking at the process list.
    Q_PROPERTY(bool foreground READ foreground WRITE setForeground NOTIFY foregroundChanged)
    Q_PROPERTY(int intervalMs READ intervalMs WRITE setIntervalMs NOTIFY intervalChanged)

public:
    explicit SysMon(QObject *parent = nullptr);

    qreal cpuPercent() const { return m_s.cpuPct; }
    // coreCount is every CPU the SoC has; on hotplug SoCs some of them are parked
    // at any moment and report SysSnap::CoreOffline instead of a load.
    int coreCount() const { return m_s.corePct.size(); }
    int coresOnline() const
    {
        int n = 0;
        for (float f : m_s.corePct)
            if (f >= 0.f)
                ++n;
        return n;
    }
    QVariantList corePercents() const;
    QVariantList coreFreqsMhz() const;
    double memTotal() const { return m_s.memTotal; }
    double memUsed() const { return (double)m_s.memTotal - m_s.memAvailable; }
    double memAvailable() const { return m_s.memAvailable; }
    double swapTotal() const { return m_s.swapTotal; }
    double swapUsed() const { return (double)m_s.swapTotal - m_s.swapFree; }
    double cached() const { return m_s.cached; }
    double buffers() const { return m_s.buffers; }
    qreal load1() const { return m_s.load1; }
    qreal load5() const { return m_s.load5; }
    qreal load15() const { return m_s.load15; }
    int uptimeSec() const { return (int)m_s.uptimeSec; }
    int processCount() const { return m_s.processCount; }
    int threadCount() const { return m_s.threadCount; }
    int runnable() const { return m_s.runnable; }
    double netRxRate() const { return m_s.netRxRate; }
    double netTxRate() const { return m_s.netTxRate; }
    double netRxTotal() const { return m_s.netRxTotal; }
    double netTxTotal() const { return m_s.netTxTotal; }
    double diskReadRate() const { return m_s.diskReadRate; }
    double diskWriteRate() const { return m_s.diskWriteRate; }
    QVariantList thermalZones() const;
    QVariantList interfaces() const;
    int battCapacity() const { return m_s.battCapacity; }
    double battCurrentA() const { return m_s.battCurrentA; }
    double battVoltageV() const { return m_s.battVoltageV; }
    double battTempC() const { return m_s.battTempC; }
    double battPowerW() const { return m_s.battPowerW; }
    int battHealthPct() const { return m_s.battHealthPct; }
    bool battHealthFromGauge() const { return m_s.battHealthFromGauge; }
    int battCycles() const { return m_s.battCycles; }
    double battChargeFull() const { return m_s.battChargeFull; }
    double battChargeDesign() const { return m_s.battChargeDesign; }
    QString battTech() const { return m_s.battTech; }
    QString battModel() const { return m_s.battModel; }
    QString battHealthReport() const { return m_s.battHealthReport; }
    QString battQuality() const;
    QString battStatus() const { return m_s.battStatus; }
    QString kernel() const { return m_s.kernel; }
    QVariantList cpuHistory() const { return toList(m_cpuHist); }
    QVariantList memHistory() const { return toList(m_memHist); }
    QVariantList rxHistory() const { return toList(m_rxHist); }
    QVariantList txHistory() const { return toList(m_txHist); }
    QVariantList battHistory() const { return toList(m_battHist); }
    bool paused() const { return m_paused; }
    void setPaused(bool p);
    bool foreground() const { return m_foreground; }
    void setForeground(bool f);
    int intervalMs() const { return m_intervalMs; }
    void setIntervalMs(int ms);

    Q_INVOKABLE bool sendSignal(int pid, int sig);
    Q_INVOKABLE bool setNice(int pid, int nice);
    Q_INVOKABLE QVariantList storageMounts() const;
    Q_INVOKABLE QVariantList storageHardware() const;
    Q_INVOKABLE QVariantList networkHardware() const;
    Q_INVOKABLE QVariantMap wifiDetail() const;
    Q_INVOKABLE QVariantMap batteryHardware() const;
    Q_INVOKABLE QVariantMap chargerDetail() const;
    Q_INVOKABLE QVariantMap memoryDetail() const;
    Q_INVOKABLE QVariantMap cpuDetail() const;
    Q_INVOKABLE QVariantMap graphicsDetail() const;
    Q_INVOKABLE QVariantMap audioDetail() const;
    Q_INVOKABLE QVariantMap audioStreams() const;
    Q_INVOKABLE QVariantMap cameraDetail() const;
    Q_INVOKABLE QVariantMap modemDetail() const;
    Q_INVOKABLE QVariantMap usbDetail() const;
    Q_INVOKABLE QVariantMap wirelessDetail() const;
    Q_INVOKABLE QVariantList halServices() const;
    Q_INVOKABLE QString bugReportInfo(const QString &term) const;
    Q_INVOKABLE QString fmtBytes(double b) const;
    Q_INVOKABLE QString fmtRate(double bps) const;
    Q_INVOKABLE QString fmtDuration(int sec) const;

public slots:
    void onSystem(const SysSnap &snap);

signals:
    void updated();
    void pausedChanged();
    void foregroundChanged();
    void intervalChanged();
    void pauseRequested(bool paused);
    void intervalRequested(int ms);

private:
    static QVariantList toList(const QVector<double> &v);
    static void push(QVector<double> &v, double value);

    SysSnap m_s;
    QVector<double> m_cpuHist, m_memHist, m_rxHist, m_txHist, m_battHist;
    bool m_paused = false;
    bool m_foreground = true;
    int m_intervalMs = 3000;
};
