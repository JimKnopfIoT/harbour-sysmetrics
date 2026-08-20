// Session recorder: accumulates per-process CPU-time deltas, ranks consumers.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QVariantList>

#include "sampler.h"

class Recorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(int elapsedSec READ elapsedSec NOTIFY resultsChanged)
    Q_PROPERTY(double totalCpuSec READ totalCpuSec NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)

public:
    explicit Recorder(QObject *parent = nullptr);

    bool running() const { return m_running; }
    int elapsedSec() const;
    double totalCpuSec() const { return m_totalCpuSec; }
    QVariantList results() const { return m_results; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void reset();

public slots:
    void onProcesses(const QVector<ProcSample> &procs, qulonglong totalDeltaJiffies);

signals:
    void stateChanged();
    void resultsChanged();

private:
    void rebuild();

    struct Acc {
        QString name;
        qulonglong start = 0;     // starttime identity
        qulonglong base = 0;      // jiffies at first sight
        qulonglong last = 0;
        qulonglong finished = 0;  // accumulated from exited incarnations
        bool app = false;
    };

    bool m_running = false;
    qint64 m_startedMs = 0;
    qint64 m_stoppedMs = 0;
    long m_clkTck = 100;
    double m_totalCpuSec = 0;
    QHash<int, Acc> m_acc;
    QVariantList m_results;
};
