// Process list model (GUI thread) + sort/filter proxy for QML.
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSortFilterProxyModel>

#include "sampler.h"


class ProcModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        PidRole = Qt::UserRole + 1,
        PpidRole,
        NameRole,
        CmdlineRole,
        UserRole,
        UidRole,
        CpuRole,
        MemRole,
        MemPctRole,
        StateRole,
        ThreadsRole,
        NiceRole,
        KernelRole,
        AppRole,
    };

    explicit ProcModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Sorting and filtering happen here, not in a proxy, and that is not a
    // matter of taste. A proxy re-sort reaches the view as layoutChanged, and a
    // QML ListView answers that by losing its scroll position -- it snaps back
    // to the top. That is why the list used to stop re-sorting as soon as it was
    // scrolled away from the top, and that in turn is why it showed fresh
    // figures in an order that no longer matched them.
    //
    // Sorted here, the view never sees a re-ordering at all: row 3 is always
    // rank 4, and only the figures in it change. What the view gets is
    // dataChanged at stable indices, plus rows appended or dropped at the end
    // when the process count moves. Neither disturbs the scroll position, so the
    // list stays correct wherever the user happens to be looking.
    void setSearch(const QString &s);
    void setSortKey(const QString &k);
    void setDescending(bool d);
    void setShowKernel(bool v);
    void setAppsOnly(bool v);

    // While held, an incoming sample is parked instead of shown: with a finger
    // on the list nothing may move, and holding the figures as well as the order
    // keeps what is on screen one consistent sample.
    void setHeld(bool v);

public slots:
    void onProcesses(const QVector<ProcSample> &procs, qulonglong totalDeltaJiffies);
    void onSystem(const SysSnap &snap);

signals:
    void updated();

private:
    QString userName(uint uid) const;
    void rebuild();
    bool accepts(const ProcSample &p) const;
    bool orderBefore(const ProcSample &a, const ProcSample &b) const;

    QVector<ProcSample> m_all;     // last sample, as read
    QVector<ProcSample> m_rows;    // filtered and sorted, what the view shows
    mutable QHash<uint, QString> m_users;
    qulonglong m_memTotal = 0;
    bool m_held = false;
    bool m_haveSample = false;

    QString m_search;
    QString m_sortKey = QStringLiteral("cpu");
    bool m_desc = true;
    bool m_showKernel = false;
    bool m_appsOnly = false;
};

class ProcProxy : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY filterChanged)
    Q_PROPERTY(bool descending READ descending WRITE setDescending NOTIFY filterChanged)
    Q_PROPERTY(bool showKernel READ showKernel WRITE setShowKernel NOTIFY filterChanged)
    Q_PROPERTY(bool appsOnly READ appsOnly WRITE setAppsOnly NOTIFY filterChanged)
    Q_PROPERTY(bool frozen READ frozen WRITE setFrozen NOTIFY frozenChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit ProcProxy(QObject *parent = nullptr);

    QString search() const { return m_search; }
    void setSearch(const QString &s);
    QString sortBy() const { return m_sortBy; }
    void setSortBy(const QString &s);
    bool descending() const { return m_desc; }
    void setDescending(bool d);
    bool showKernel() const { return m_showKernel; }
    void setShowKernel(bool v);
    bool appsOnly() const { return m_appsOnly; }
    void setAppsOnly(bool v);
    bool frozen() const { return m_frozen; }
    void setFrozen(bool v);
    int count() const { return rowCount(); }

    // top-N processes by current CPU% — a battery-drain proxy
    Q_INVOKABLE QVariantList topByCpu(int n) const;
    // top-N by attributed drain, with the share of CPU work each caused

signals:
    void filterChanged();
    void countChanged();
    void frozenChanged();

private:
    // The proxy is a pass-through: it neither sorts nor filters, it only carries
    // the QML-facing properties through to the model. See the note there.
    ProcModel *model() const;

    QString m_search;
    QString m_sortBy = QStringLiteral("cpu");
    bool m_desc = true;
    bool m_showKernel = false;
    bool m_appsOnly = false;
    bool m_frozen = false;
};
