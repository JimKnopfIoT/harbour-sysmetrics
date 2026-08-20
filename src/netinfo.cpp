#include "netinfo.h"

#include <QFile>
#include <QRegExp>
#include <QStringList>

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

QString tcpState(int st)
{
    static const char *names[] = { "", "ESTABLISHED", "SYN_SENT", "SYN_RECV",
        "FIN_WAIT1", "FIN_WAIT2", "TIME_WAIT", "CLOSE", "CLOSE_WAIT",
        "LAST_ACK", "LISTEN", "CLOSING" };
    return (st >= 1 && st <= 11) ? QLatin1String(names[st]) : QString::number(st);
}

QString v4Addr(const QByteArray &hex)
{
    // "0100007F:1F90" -> 127.0.0.1:8080 (kernel prints native-endian __be32)
    bool ok = false;
    const quint32 raw = hex.left(8).toUInt(&ok, 16);
    const quint16 port = hex.mid(9).toUInt(&ok, 16);
    return QStringLiteral("%1.%2.%3.%4:%5")
        .arg(raw & 0xff).arg((raw >> 8) & 0xff).arg((raw >> 16) & 0xff).arg(raw >> 24)
        .arg(port);
}

QString v6Addr(const QByteArray &hex)
{
    // four 32-bit groups, bytes swapped inside each group
    QByteArray bytes(16, 0);
    for (int g = 0; g < 4; ++g) {
        bool ok = false;
        const quint32 v = hex.mid(g * 8, 8).toUInt(&ok, 16);
        bytes[g * 4 + 0] = v & 0xff;
        bytes[g * 4 + 1] = (v >> 8) & 0xff;
        bytes[g * 4 + 2] = (v >> 16) & 0xff;
        bytes[g * 4 + 3] = (v >> 24) & 0xff;
    }
    const quint16 port = hex.mid(33).toUInt(nullptr, 16);
    QStringList groups;
    for (int i = 0; i < 16; i += 2)
        groups << QString::number(((quint8)bytes[i] << 8) | (quint8)bytes[i + 1], 16);
    QString a = groups.join(QLatin1Char(':'));
    a.replace(QRegExp(QStringLiteral("(^|:)(0:)+")), QStringLiteral("::"));
    return QStringLiteral("[%1]:%2").arg(a).arg(port);
}

quint16 portOf(const QByteArray &addr)
{
    const int c = addr.indexOf(':');
    return c < 0 ? 0 : addr.mid(c + 1).toUShort(nullptr, 16);
}

// address part (before ':') all zero -> bound to any / no peer
bool addrAny(const QByteArray &addr)
{
    const int c = addr.indexOf(':');
    const QByteArray a = c < 0 ? addr : addr.left(c);
    for (char ch : a)
        if (ch != '0')
            return false;
    return true;
}

// 0 unspecified, 1 loopback, 2 private/link-local, 3 public
int classifyV4(quint32 raw)  // raw as in v4Addr: octet1 = raw&0xff
{
    const int o1 = raw & 0xff, o2 = (raw >> 8) & 0xff;
    if (raw == 0) return 0;
    if (o1 == 127) return 1;
    if (o1 == 10) return 2;
    if (o1 == 172 && o2 >= 16 && o2 <= 31) return 2;
    if (o1 == 192 && o2 == 168) return 2;
    if (o1 == 169 && o2 == 254) return 2;
    if (o1 == 100 && o2 >= 64 && o2 <= 127) return 2;  // CGNAT 100.64/10
    return 3;
}

int classifyRemote(const QByteArray &hex, bool v6)
{
    if (addrAny(hex))
        return 0;
    if (!v6)
        return classifyV4(hex.left(8).toUInt(nullptr, 16));
    // v6: bytes as in v6Addr
    quint8 b[16];
    for (int g = 0; g < 4; ++g) {
        const quint32 v = hex.mid(g * 8, 8).toUInt(nullptr, 16);
        b[g * 4 + 0] = v & 0xff; b[g * 4 + 1] = (v >> 8) & 0xff;
        b[g * 4 + 2] = (v >> 16) & 0xff; b[g * 4 + 3] = (v >> 24) & 0xff;
    }
    // IPv4-mapped ::ffff:a.b.c.d -> classify the embedded v4
    bool mapped = true;
    for (int i = 0; i < 10; ++i) if (b[i]) { mapped = false; break; }
    if (mapped && b[10] == 0xff && b[11] == 0xff)
        return classifyV4(b[12] | (b[13] << 8) | (b[14] << 16) | ((quint32)b[15] << 24));
    // ::1 loopback
    bool loop = (b[15] == 1);
    for (int i = 0; i < 15; ++i) if (b[i]) { loop = false; break; }
    if (loop) return 1;
    if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) return 2;  // fe80::/10 link-local
    if ((b[0] & 0xfe) == 0xfc) return 2;                  // fc00::/7 ULA
    return 3;
}

void parseInetInto(QVector<SockInfo> &list, const QString &file, const QString &proto, bool v6)
{
    const QList<QByteArray> lines = readAll(file).split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> f = lines.at(i).simplified().split(' ');
        if (f.size() < 10)
            continue;
        SockInfo s;
        s.proto = proto;
        s.local = v6 ? v6Addr(f[1]) : v4Addr(f[1]);
        s.remote = v6 ? v6Addr(f[2]) : v4Addr(f[2]);
        s.localPort = portOf(f[1]);
        s.remotePort = portOf(f[2]);
        const int st = f[3].toInt(nullptr, 16);
        const bool tcp = proto.startsWith(QLatin1String("tcp"));
        s.state = tcp ? tcpState(st) : (st == 7 ? QStringLiteral("UNCONN") : tcpState(st));
        s.listening = tcp ? (st == 10) : (st == 7);
        const QList<QByteArray> q = f[4].split(':');
        if (q.size() == 2) {
            s.txq = q[0].toULongLong(nullptr, 16);
            s.rxq = q[1].toULongLong(nullptr, 16);
        }
        s.uid = f[7].toUInt();
        s.inode = f[9].toULongLong();
        s.localAny = addrAny(f[1]);
        s.remoteClass = classifyRemote(f[2], v6);
        list.append(s);
    }
}

void parseInet(QHash<quint64, SockInfo> &out, const QString &file, const QString &proto, bool v6)
{
    QVector<SockInfo> list;
    parseInetInto(list, file, proto, v6);
    for (const SockInfo &s : list)
        out.insert(s.inode, s);
}

void parseUnix(QHash<quint64, SockInfo> &out)
{
    const QList<QByteArray> lines = readAll(QStringLiteral("/proc/net/unix")).split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> f = lines.at(i).simplified().split(' ');
        if (f.size() < 7)
            continue;
        SockInfo s;
        s.proto = QStringLiteral("unix");
        s.remote = f.size() > 7 ? QString::fromLocal8Bit(f[7]) : QString();
        s.state = f[5] == "01" ? QStringLiteral("LISTEN")
                : f[5] == "03" ? QStringLiteral("CONNECTED") : QString::fromLatin1(f[5]);
        out.insert(f[6].toULongLong(), s);
    }
}

void parseSimple(QHash<quint64, SockInfo> &out, const QString &file,
                 const QString &proto, int inodeCol)
{
    const QList<QByteArray> lines = readAll(file).split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> f = lines.at(i).simplified().split(' ');
        if (f.size() <= inodeCol)
            continue;
        SockInfo s;
        s.proto = proto;
        out.insert(f[inodeCol].toULongLong(), s);
    }
}

} // namespace

QHash<quint64, SockInfo> NetInfo::snapshot()
{
    QHash<quint64, SockInfo> out;
    parseInet(out, QStringLiteral("/proc/net/tcp"), QStringLiteral("tcp"), false);
    parseInet(out, QStringLiteral("/proc/net/tcp6"), QStringLiteral("tcp6"), true);
    parseInet(out, QStringLiteral("/proc/net/udp"), QStringLiteral("udp"), false);
    parseInet(out, QStringLiteral("/proc/net/udp6"), QStringLiteral("udp6"), true);
    parseInet(out, QStringLiteral("/proc/net/raw"), QStringLiteral("raw"), false);
    parseInet(out, QStringLiteral("/proc/net/raw6"), QStringLiteral("raw6"), true);
    parseUnix(out);
    parseSimple(out, QStringLiteral("/proc/net/netlink"), QStringLiteral("netlink"), 9);
    parseSimple(out, QStringLiteral("/proc/net/packet"), QStringLiteral("packet"), 8);
    return out;
}

QVector<SockInfo> NetInfo::inetConnections()
{
    QVector<SockInfo> out;
    parseInetInto(out, QStringLiteral("/proc/net/tcp"), QStringLiteral("tcp"), false);
    parseInetInto(out, QStringLiteral("/proc/net/tcp6"), QStringLiteral("tcp6"), true);
    parseInetInto(out, QStringLiteral("/proc/net/udp"), QStringLiteral("udp"), false);
    parseInetInto(out, QStringLiteral("/proc/net/udp6"), QStringLiteral("udp6"), true);
    return out;
}
