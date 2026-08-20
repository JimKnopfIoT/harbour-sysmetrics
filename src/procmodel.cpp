#include "procmodel.h"

#include <pwd.h>

ProcModel::ProcModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ProcModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> ProcModel::roleNames() const
{
    QHash<int, QByteArray> r;
    r[PidRole] = "pid";
    r[PpidRole] = "ppid";
    r[NameRole] = "name";
    r[CmdlineRole] = "cmdline";
    r[UserRole] = "user";
    r[UidRole] = "uid";
    r[CpuRole] = "cpu";
    r[MemRole] = "mem";
    r[MemPctRole] = "memPct";
    r[StateRole] = "state";
    r[ThreadsRole] = "threads";
    r[NiceRole] = "nice";
    r[KernelRole] = "isKernel";
    r[AppRole] = "isApp";
    return r;
}

QVariant ProcModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();
    const ProcSample &p = m_rows.at(index.row());
    switch (role) {
    case PidRole: return p.pid;
    case PpidRole: return p.ppid;
    case NameRole: return p.name;
    case CmdlineRole: return p.cmdline;
    case UserRole: return userName(p.uid);
    case UidRole: return p.uid;
    case CpuRole: return (double)p.cpuPct;
    case MemRole: return (double)p.rssBytes;
    case MemPctRole: return m_memTotal ? 100.0 * p.rssBytes / m_memTotal : 0.0;
    case StateRole: return QString(QChar::fromLatin1(p.state));
    case ThreadsRole: return p.threads;
    case NiceRole: return p.nice;
    case KernelRole: return p.kernelThread;
    case AppRole: return p.uid >= 100000;
    }
    return QVariant();
}

QString ProcModel::userName(uint uid) const
{
    const auto it = m_users.constFind(uid);
    if (it != m_users.constEnd())
        return it.value();
    QString name = QString::number(uid);
    if (const struct passwd *pw = getpwuid(uid))
        name = QString::fromLocal8Bit(pw->pw_name);
    m_users.insert(uid, name);
    return name;
}

void ProcModel::onSystem(const SysSnap &snap)
{
    m_memTotal = snap.memTotal;
}

void ProcModel::onProcesses(const QVector<ProcSample> &procs, qulonglong)
{
    QHash<int, int> incoming;
    incoming.reserve(procs.size());
    for (int i = 0; i < procs.size(); ++i)
        incoming.insert(procs.at(i).pid, i);

    // removals, back to front
    for (int row = m_rows.size() - 1; row >= 0; --row) {
        if (!incoming.contains(m_rows.at(row).pid)) {
            beginRemoveRows(QModelIndex(), row, row);
            m_rows.remove(row);
            endRemoveRows();
        }
    }

    // in-place updates
    QVector<bool> used(procs.size(), false);
    for (int row = 0; row < m_rows.size(); ++row) {
        const int idx = incoming.value(m_rows.at(row).pid, -1);
        if (idx >= 0) {
            m_rows[row] = procs.at(idx);
            used[idx] = true;
        }
    }
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1));

    // additions appended; proxy sorts
    QVector<ProcSample> add;
    for (int i = 0; i < procs.size(); ++i)
        if (!used.at(i))
            add.append(procs.at(i));
    if (!add.isEmpty()) {
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size() + add.size() - 1);
        m_rows += add;
        endInsertRows();
    }

    m_rowByPid = incoming;
    emit updated();
}

ProcProxy::ProcProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::rowsInserted, this, &ProcProxy::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ProcProxy::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ProcProxy::countChanged);
    sort(0);
}

void ProcProxy::setSearch(const QString &s)
{
    if (m_search == s)
        return;
    m_search = s;
    invalidateFilter();
    emit filterChanged();
    emit countChanged();
}

void ProcProxy::setSortBy(const QString &s)
{
    if (m_sortBy == s)
        return;
    m_sortBy = s;
    invalidate();
    sort(0);
    emit filterChanged();
}

void ProcProxy::setDescending(bool d)
{
    if (m_desc == d)
        return;
    m_desc = d;
    invalidate();
    sort(0);
    emit filterChanged();
}

void ProcProxy::setShowKernel(bool v)
{
    if (m_showKernel == v)
        return;
    m_showKernel = v;
    invalidateFilter();
    emit filterChanged();
    emit countChanged();
}

void ProcProxy::setAppsOnly(bool v)
{
    if (m_appsOnly == v)
        return;
    m_appsOnly = v;
    invalidateFilter();
    emit filterChanged();
    emit countChanged();
}

QVariantList ProcProxy::topByCpu(int n) const
{
    QAbstractItemModel *m = sourceModel();
    if (!m)
        return QVariantList();
    struct Row { QString name; int pid; double cpu; double mem; bool app; };
    QVector<Row> rows;
    const int rc = m->rowCount();
    rows.reserve(rc);
    for (int i = 0; i < rc; ++i) {
        const QModelIndex idx = m->index(i, 0);
        rows.append({ idx.data(ProcModel::NameRole).toString(),
                      idx.data(ProcModel::PidRole).toInt(),
                      idx.data(ProcModel::CpuRole).toDouble(),
                      idx.data(ProcModel::MemRole).toDouble(),
                      idx.data(ProcModel::AppRole).toBool() });
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        return a.cpu != b.cpu ? a.cpu > b.cpu : a.pid < b.pid;
    });
    QVariantList out;
    for (int i = 0; i < rows.size() && i < n; ++i) {
        QVariantMap r;
        r.insert(QStringLiteral("name"), rows[i].name);
        r.insert(QStringLiteral("pid"), rows[i].pid);
        r.insert(QStringLiteral("cpu"), rows[i].cpu);
        r.insert(QStringLiteral("mem"), rows[i].mem);
        r.insert(QStringLiteral("isApp"), rows[i].app);
        out.append(r);
    }
    return out;
}

void ProcProxy::setFrozen(bool v)
{
    if (m_frozen == v)
        return;
    m_frozen = v;
    // While frozen: keep the current row order (values still update in place via
    // dataChanged) so the user can tap a row without it jumping. On thaw: catch up.
    setDynamicSortFilter(!v);
    if (!v) {
        invalidate();
        sort(0);
    }
    emit frozenChanged();
}

bool ProcProxy::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QModelIndex i = sourceModel()->index(row, 0, parent);
    if (!m_showKernel && i.data(ProcModel::KernelRole).toBool())
        return false;
    if (m_appsOnly && !i.data(ProcModel::AppRole).toBool())
        return false;
    if (!m_search.isEmpty()) {
        return i.data(ProcModel::NameRole).toString().contains(m_search, Qt::CaseInsensitive)
            || i.data(ProcModel::CmdlineRole).toString().contains(m_search, Qt::CaseInsensitive)
            || i.data(ProcModel::PidRole).toString() == m_search;
    }
    return true;
}

bool ProcProxy::lessThan(const QModelIndex &ia, const QModelIndex &ib) const
{
    // descending = swapped operands; keeps strict weak ordering intact
    const QModelIndex &a = m_desc ? ib : ia;
    const QModelIndex &b = m_desc ? ia : ib;

    int role = ProcModel::CpuRole;
    bool numeric = true;
    if (m_sortBy == QLatin1String("mem"))
        role = ProcModel::MemRole;
    else if (m_sortBy == QLatin1String("pid"))
        role = ProcModel::PidRole;
    else if (m_sortBy == QLatin1String("name")) {
        role = ProcModel::NameRole;
        numeric = false;
    } else if (m_sortBy == QLatin1String("threads"))
        role = ProcModel::ThreadsRole;

    if (numeric) {
        const double va = a.data(role).toDouble();
        const double vb = b.data(role).toDouble();
        if (va != vb)
            return va < vb;
    } else {
        const int c = QString::compare(a.data(role).toString(), b.data(role).toString(), Qt::CaseInsensitive);
        if (c != 0)
            return c < 0;
    }
    return a.data(ProcModel::PidRole).toInt() < b.data(ProcModel::PidRole).toInt();
}
