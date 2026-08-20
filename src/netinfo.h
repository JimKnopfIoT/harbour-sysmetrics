// Socket-inode tables from /proc/net/* for fd-to-connection mapping.
#pragma once

#include <QHash>
#include <QString>
#include <QVector>

struct SockInfo {
    QString proto;   // tcp, tcp6, udp, udp6, raw, unix, netlink, packet
    QString local;
    QString remote;
    QString state;
    qulonglong txq = 0;
    qulonglong rxq = 0;
    quint64 inode = 0;
    uint uid = 0;
    quint16 localPort = 0;
    quint16 remotePort = 0;
    bool listening = false;
    bool localAny = false;         // bound to 0.0.0.0 / ::
    int remoteClass = 0;           // 0 none/unspecified, 1 loopback, 2 private, 3 public
};

namespace NetInfo {
// inode -> socket, for mapping a process's fds to connections
QHash<quint64, SockInfo> snapshot();
// full inet connection list (tcp/tcp6/udp/udp6) for the system-wide view
QVector<SockInfo> inetConnections();
}
