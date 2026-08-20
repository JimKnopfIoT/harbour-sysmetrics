#include "recorder.h"

#include <QSet>
#include <QVariantMap>
#include <algorithm>
#include <unistd.h>

Recorder::Recorder(QObject *parent)
    : QObject(parent)
{
    m_clkTck = sysconf(_SC_CLK_TCK);
    if (m_clkTck <= 0)
        m_clkTck = 100;
}

int Recorder::elapsedSec() const
{
    if (m_startedMs == 0)
        return 0;
    const qint64 end = m_running ? QDateTime::currentMSecsSinceEpoch() : m_stoppedMs;
    return (int)((end - m_startedMs) / 1000);
}

void Recorder::start()
{
    m_acc.clear();
    m_results.clear();
    m_totalCpuSec = 0;
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
    m_running = true;
    emit stateChanged();
    emit resultsChanged();
}

void Recorder::stop()
{
    m_running = false;
    m_stoppedMs = QDateTime::currentMSecsSinceEpoch();
    emit stateChanged();
}

void Recorder::reset()
{
    m_running = false;
    m_startedMs = 0;
    m_acc.clear();
    m_results.clear();
    m_totalCpuSec = 0;
    emit stateChanged();
    emit resultsChanged();
}

void Recorder::onProcesses(const QVector<ProcSample> &procs, qulonglong)
{
    if (!m_running)
        return;

    QSet<int> seen;
    for (const ProcSample &p : procs) {
        seen.insert(p.pid);
        Acc &a = m_acc[p.pid];
        if (a.base == 0 && a.last == 0) {
            a.name = p.name;
            a.start = p.startJiffies;
            a.base = p.jiffies;
            a.last = p.jiffies;
            a.app = p.uid >= 100000;
        } else if (a.start != p.startJiffies) {
            // pid reused: bank the old incarnation
            a.finished += a.last - a.base;
            a.name = p.name;
            a.start = p.startJiffies;
            a.base = p.jiffies;
            a.last = p.jiffies;
            a.app = p.uid >= 100000;
        } else {
            a.last = qMax(a.last, p.jiffies);
        }
    }
    // exited processes keep their accumulated time in m_acc

    rebuild();
}

void Recorder::rebuild()
{
    struct Row { QString name; int pid; double sec; bool app; };
    QVector<Row> rows;
    double total = 0;
    for (auto it = m_acc.constBegin(); it != m_acc.constEnd(); ++it) {
        const double sec = (it->finished + it->last - it->base) / (double)m_clkTck;
        if (sec <= 0.005)
            continue;
        rows.append({ it->name, it.key(), sec, it->app });
        total += sec;
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        return a.sec != b.sec ? a.sec > b.sec : a.pid < b.pid;
    });

    QVariantList out;
    for (const Row &r : rows) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), r.name);
        m.insert(QStringLiteral("pid"), r.pid);
        m.insert(QStringLiteral("cpuSec"), r.sec);
        m.insert(QStringLiteral("share"), total > 0 ? 100.0 * r.sec / total : 0.0);
        m.insert(QStringLiteral("isApp"), r.app);
        out.append(m);
        if (out.size() >= 100)
            break;
    }
    m_totalCpuSec = total;
    m_results = out;
    emit resultsChanged();
}
