#include "netmon.h"

#include "netinfo.h"
#include "rootclient.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <dirent.h>
#include <pwd.h>
#include <cstdlib>
#include <unistd.h>

namespace {

QString userName(uint uid, QHash<uint, QString> &cache)
{
    const auto it = cache.constFind(uid);
    if (it != cache.constEnd())
        return it.value();
    QString n = QString::number(uid);
    if (const struct passwd *pw = getpwuid(uid))
        n = QString::fromLocal8Bit(pw->pw_name);
    cache.insert(uid, n);
    return n;
}

// inode -> (pid, comm) by scanning readable /proc/*/fd, plus the root helper.
QHash<quint64, QPair<int, QString>> sockOwners()
{
    QHash<quint64, QPair<int, QString>> map;

    if (RootClient::instance()->active()) {
        // one round trip: "inode|pid|comm" per socket fd, system-wide
        for (const QString &row : RootClient::instance()->sockMap()) {
            const QStringList c = row.split(QLatin1Char('|'));
            if (c.size() < 3)
                continue;
            const quint64 inode = c[0].toULongLong();
            if (!map.contains(inode))
                map.insert(inode, qMakePair(c[1].toInt(), c[2]));
        }
        if (!map.isEmpty())
            return map;
    }

    DIR *proc = opendir("/proc");
    if (!proc)
        return map;
    struct dirent *de;
    while ((de = readdir(proc))) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        const int pid = atoi(de->d_name);
        const QString fdDir = QStringLiteral("/proc/") + QLatin1String(de->d_name) + QStringLiteral("/fd");
        DIR *fdd = opendir(QFile::encodeName(fdDir).constData());
        if (!fdd)
            continue;
        QString comm;
        struct dirent *fe;
        while ((fe = readdir(fdd))) {
            if (fe->d_name[0] < '0' || fe->d_name[0] > '9')
                continue;
            char lbuf[256];
            const QString lp = fdDir + QLatin1Char('/') + QLatin1String(fe->d_name);
            const ssize_t ln = ::readlink(QFile::encodeName(lp).constData(), lbuf, sizeof(lbuf) - 1);
            if (ln < 0)
                continue;
            const QString t = QString::fromLocal8Bit(lbuf, ln);
            if (!t.startsWith(QLatin1String("socket:[")))
                continue;
            const quint64 inode = t.mid(8, t.size() - 9).toULongLong();
            if (map.contains(inode))
                continue;
            if (comm.isEmpty()) {
                QFile c(QStringLiteral("/proc/") + QLatin1String(de->d_name) + QStringLiteral("/comm"));
                if (c.open(QIODevice::ReadOnly))
                    comm = QString::fromLocal8Bit(c.readAll().trimmed());
            }
            map.insert(inode, qMakePair(pid, comm));
        }
        closedir(fdd);
    }
    closedir(proc);
    return map;
}

} // namespace

NetMonitor::NetMonitor(QObject *parent)
    : QObject(parent)
{
}

void NetMonitor::refresh()
{
    const QVector<SockInfo> conns = NetInfo::inetConnections();
    const QHash<quint64, QPair<int, QString>> owners = sockOwners();
    QHash<uint, QString> userCache;

    // listener set: (family, port) -> inbound connections are ours to serve
    QSet<QString> listenKeys;
    for (const SockInfo &s : conns) {
        if (!s.listening)
            continue;
        const QString fam = s.proto.startsWith(QLatin1String("tcp")) ? QStringLiteral("tcp")
                                                                      : QStringLiteral("udp");
        listenKeys.insert(fam + QLatin1Char('/') + QString::number(s.localPort));
    }

    QVariantList list;
    int nListen = 0, nEst = 0;
    int nSsh = 0, nInPublic = 0, nOutPublic = 0, nWildcard = 0, nPlaintext = 0;

    for (const SockInfo &s : conns) {
        QVariantMap m;
        m.insert(QStringLiteral("proto"), s.proto);
        m.insert(QStringLiteral("laddr"), s.local);
        m.insert(QStringLiteral("raddr"), s.remote);
        m.insert(QStringLiteral("state"), s.state);
        m.insert(QStringLiteral("localPort"), s.localPort);
        m.insert(QStringLiteral("remotePort"), s.remotePort);
        m.insert(QStringLiteral("user"), userName(s.uid, userCache));

        QString dir;
        bool inbound = false;
        if (s.listening) {
            dir = QStringLiteral("listen");
            ++nListen;
        } else {
            const QString fam = s.proto.startsWith(QLatin1String("tcp")) ? QStringLiteral("tcp")
                                                                          : QStringLiteral("udp");
            inbound = listenKeys.contains(fam + QLatin1Char('/') + QString::number(s.localPort));
            dir = inbound ? QStringLiteral("in") : QStringLiteral("out");
            if (s.state == QLatin1String("ESTABLISHED"))
                ++nEst;
        }
        m.insert(QStringLiteral("direction"), dir);
        m.insert(QStringLiteral("active"), s.txq > 0 || s.rxq > 0);

        const bool ssh = s.localPort == 22 || s.remotePort == 22;
        // plaintext / high-risk service ports
        const bool plaintext = s.localPort == 23 || s.remotePort == 23    // telnet
                            || s.localPort == 21 || s.remotePort == 21    // ftp
                            || s.localPort == 512 || s.localPort == 513 || s.localPort == 514; // r-services
        m.insert(QStringLiteral("ssh"), ssh);

        // per-connection threat: SSH always red (3); public established elevated;
        // wildcard listener notable; plaintext service red.
        int threat = 0;
        if (ssh) { threat = 3; ++nSsh; }
        else if (plaintext) { threat = 3; ++nPlaintext; }
        else if (!s.listening && s.remoteClass == 3) {
            threat = inbound ? 3 : 2;
            if (inbound) ++nInPublic; else ++nOutPublic;
        } else if (s.listening && s.localAny) {
            threat = 1; ++nWildcard;
        }
        m.insert(QStringLiteral("threat"), threat);

        const auto owner = owners.constFind(s.inode);
        if (owner != owners.constEnd()) {
            m.insert(QStringLiteral("pid"), owner->first);
            m.insert(QStringLiteral("name"), owner->second);
        } else {
            m.insert(QStringLiteral("pid"), 0);
            m.insert(QStringLiteral("name"), QString());
        }
        list.append(m);
    }

    // overall assessment
    QVariantList findings;
    int level = 0;
    auto add = [&](int lvl, const QString &text) {
        QVariantMap f;
        f.insert(QStringLiteral("level"), lvl);
        f.insert(QStringLiteral("text"), text);
        findings.append(f);
        if (lvl > level) level = lvl;
    };
    if (nSsh > 0)
        add(3, tr("SSH active (port 22): %n connection(s) — remote shell exposed", "", nSsh));
    if (nPlaintext > 0)
        add(3, tr("%n unencrypted service connection(s) (telnet/ftp/r-services)", "", nPlaintext));
    if (nInPublic > 0)
        add(3, tr("%n inbound connection(s) from public addresses", "", nInPublic));
    if (nOutPublic > 0)
        add(2, tr("%n outbound connection(s) to public addresses", "", nOutPublic));
    if (nWildcard > 0)
        add(1, tr("%n port(s) listening on all interfaces", "", nWildcard));
    if (findings.isEmpty())
        add(0, tr("No exposed or public connections"));

    QString summary;
    switch (level) {
    case 3: summary = tr("Critical exposure"); break;
    case 2: summary = tr("Elevated exposure"); break;
    case 1: summary = tr("Worth watching"); break;
    default: summary = tr("Inconspicuous"); break;
    }

    m_connections = list;
    m_total = list.size();
    m_listening = nListen;
    m_established = nEst;
    m_threatLevel = level;
    m_threatSummary = summary;
    m_threatFindings = findings;
    emit updated();
}
