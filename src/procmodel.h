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
        AppRole
    };

    explicit ProcModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

public slots:
    void onProcesses(const QVector<ProcSample> &procs, qulonglong totalDeltaJiffies);
    void onSystem(const SysSnap &snap);

signals:
    void updated();

private:
    QString userName(uint uid) const;

    QVector<ProcSample> m_rows;
    QHash<int, int> m_rowByPid;
    mutable QHash<uint, QString> m_users;
    qulonglong m_memTotal = 0;
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

signals:
    void filterChanged();
    void countChanged();
    void frozenChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &a, const QModelIndex &b) const override;

private:
    QString m_search;
    QString m_sortBy = QStringLiteral("cpu");
    bool m_desc = true;
    bool m_showKernel = false;
    bool m_appsOnly = false;
    bool m_frozen = false;
};
