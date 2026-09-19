#include "procmodel.h"

#include <pwd.h>

#include <algorithm>

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

void ProcModel::onProcesses(const QVector<ProcSample> &in, qulonglong)
{
    // No power attribution here. Weighting a process by the core it last ran
    // on ranked the list well, but the milliamps put on it were a model, not a
    // measurement, and read like one. Nothing on these devices meters a
    // process; the list ranks by CPU time, which is what is actually counted.
    m_all = in;
    m_haveSample = true;
    if (m_held)
        return;        // a finger is on the list: park it, show it on release
    rebuild();
}

void ProcModel::setHeld(bool v)
{
    if (m_held == v)
        return;
    m_held = v;
    if (!m_held && m_haveSample)
        rebuild();
}

void ProcModel::setSearch(const QString &s)
{
    if (m_search == s)
        return;
    m_search = s;
    rebuild();
}

void ProcModel::setSortKey(const QString &k)
{
    if (m_sortKey == k)
        return;
    m_sortKey = k;
    rebuild();
}

void ProcModel::setDescending(bool d)
{
    if (m_desc == d)
        return;
    m_desc = d;
    rebuild();
}

void ProcModel::setShowKernel(bool v)
{
    if (m_showKernel == v)
        return;
    m_showKernel = v;
    rebuild();
}

void ProcModel::setAppsOnly(bool v)
{
    if (m_appsOnly == v)
        return;
    m_appsOnly = v;
    rebuild();
}

bool ProcModel::accepts(const ProcSample &p) const
{
    if (!m_showKernel && p.kernelThread)
        return false;
    if (m_appsOnly && p.uid < 100000)
        return false;
    if (m_search.isEmpty())
        return true;
    return p.name.contains(m_search, Qt::CaseInsensitive)
        || p.cmdline.contains(m_search, Qt::CaseInsensitive)
        || QString::number(p.pid) == m_search;
}

bool ProcModel::orderBefore(const ProcSample &x, const ProcSample &y) const
{
    // descending = swapped operands; keeps strict weak ordering intact
    const ProcSample &a = m_desc ? y : x;
    const ProcSample &b = m_desc ? x : y;

    if (m_sortKey == QLatin1String("name")) {
        const int c = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        if (c != 0)
            return c < 0;
    } else {
        double va = 0, vb = 0;
        if (m_sortKey == QLatin1String("mem")) {
            va = (double)a.rssBytes;   vb = (double)b.rssBytes;
        } else if (m_sortKey == QLatin1String("pid")) {
            va = a.pid;                vb = b.pid;
        } else if (m_sortKey == QLatin1String("threads")) {
            va = a.threads;            vb = b.threads;
        } else {
            va = a.cpuPct;             vb = b.cpuPct;
        }
        if (va != vb)
            return va < vb;
    }
    return a.pid < b.pid;
}

void ProcModel::rebuild()
{
    QVector<ProcSample> next;
    next.reserve(m_all.size());
    for (const ProcSample &p : m_all)
        if (accepts(p))
            next.append(p);

    std::sort(next.begin(), next.end(),
              [this](const ProcSample &a, const ProcSample &b) { return orderBefore(a, b); });

    // Length first, contents second. Rows are appended or dropped only at the
    // end, where neither disturbs what the user is looking at; everything else
    // is a value change at an index that keeps its meaning.
    if (next.size() > m_rows.size()) {
        beginInsertRows(QModelIndex(), m_rows.size(), next.size() - 1);
        m_rows.resize(next.size());
        endInsertRows();
    } else if (next.size() < m_rows.size()) {
        beginRemoveRows(QModelIndex(), next.size(), m_rows.size() - 1);
        m_rows.resize(next.size());
        endRemoveRows();
    }

    m_rows = next;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1));

    emit updated();
}

ProcProxy::ProcProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // No sort(), no filter: both live in the model now, where re-ordering does
    // not cost the view its scroll position. This proxy exists only to carry the
    // QML-facing properties and to keep the QML side unchanged.
    connect(this, &QAbstractItemModel::rowsInserted, this, &ProcProxy::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ProcProxy::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ProcProxy::countChanged);
}

ProcModel *ProcProxy::model() const
{
    return qobject_cast<ProcModel *>(sourceModel());
}

void ProcProxy::setSearch(const QString &s)
{
    if (m_search == s)
        return;
    m_search = s;
    if (ProcModel *m = model())
        m->setSearch(s);
    emit filterChanged();
    emit countChanged();
}

void ProcProxy::setSortBy(const QString &s)
{
    if (m_sortBy == s)
        return;
    m_sortBy = s;
    if (ProcModel *m = model())
        m->setSortKey(s);
    emit filterChanged();
}

void ProcProxy::setDescending(bool d)
{
    if (m_desc == d)
        return;
    m_desc = d;
    if (ProcModel *m = model())
        m->setDescending(d);
    emit filterChanged();
}

void ProcProxy::setShowKernel(bool v)
{
    if (m_showKernel == v)
        return;
    m_showKernel = v;
    if (ProcModel *m = model())
        m->setShowKernel(v);
    emit filterChanged();
    emit countChanged();
}

void ProcProxy::setAppsOnly(bool v)
{
    if (m_appsOnly == v)
        return;
    m_appsOnly = v;
    if (ProcModel *m = model())
        m->setAppsOnly(v);
    emit filterChanged();
    emit countChanged();
}

void ProcProxy::setFrozen(bool v)
{
    if (m_frozen == v)
        return;
    m_frozen = v;
    // Only while a finger is on the list. Nothing may move under it, so the
    // model parks incoming samples until it is released -- figures and order
    // together, so what stands still is one consistent sample.
    if (ProcModel *m = model())
        m->setHeld(v);
    emit frozenChanged();
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
