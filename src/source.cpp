#include "source.h"

#include <fcntl.h>
#include <unistd.h>

QByteArray readNode(const QString &path)
{
    const QByteArray p = path.toLocal8Bit();
    const int fd = ::open(p.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return QByteArray();
    char buf[4096];
    int total = 0;
    ssize_t r;
    while (total < (int)sizeof(buf) - 1
           && (r = ::read(fd, buf + total, sizeof(buf) - 1 - total)) > 0)
        total += (int)r;
    ::close(fd);
    return QByteArray(buf, total).trimmed();
}

Source &Source::add(const QString &path, double scale, double lo, double hi)
{
    m_cand.append(Candidate{ path, scale, lo, hi });
    m_pick = -2;
    return *this;
}

Source &Source::add(const QString &dir, const char *leaf, double scale, double lo, double hi)
{
    return add(dir + QLatin1Char('/') + QLatin1String(leaf), scale, lo, hi);
}

bool Source::tryOne(const Candidate &c, double *out) const
{
    const QByteArray raw = readNode(c.path);
    if (raw.isEmpty())
        return false;
    bool ok = false;
    const double v = raw.toDouble(&ok) * c.scale;
    if (!ok)
        return false;
    if (c.lo < c.hi && (v < c.lo || v > c.hi))
        return false;   // right node, wrong scale -- or a register full of junk
    if (out)
        *out = v;
    return true;
}

bool Source::read(double *out) const
{
    if (m_pick >= 0 && m_pick < m_cand.size() && tryOne(m_cand.at(m_pick), out))
        return true;
    if (m_pick == -1)
        return false;   // probed once, nothing there; nodes do not appear later
    for (int i = 0; i < m_cand.size(); ++i) {
        if (!tryOne(m_cand.at(i), out))
            continue;
        m_pick = i;
        m_from = m_cand.at(i).path;
        return true;
    }
    m_pick = -1;
    m_from.clear();
    return false;
}

double Source::value(double fallback) const
{
    double v = 0;
    return read(&v) ? v : fallback;
}

QString Source::text() const
{
    if (!read(nullptr))
        return QString();
    return QString::fromLatin1(readNode(m_from));
}
