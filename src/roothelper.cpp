// Root helper: started via `devel-su harbour-sysmetrics --root-helper`. Serves
// privileged /proc-, /sys-, /dev-reads over a local socket restricted to
// root and gid 100000 (defaultuser). Whitelisted paths only.
#include "roothelper.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTimer>

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <sys/klog.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char *SOCK_PATH = "/tmp/sysmetrics-root.sock";

bool pathAllowed(const QString &p)
{
    return p.startsWith(QLatin1String("/proc/")) || p.startsWith(QLatin1String("/sys/"))
        || p.startsWith(QLatin1String("/dev/"));
}

// Numeric entries of a /proc/<pid>/fd directory, listed via readdir so that
// socket/pipe/anon fds (whose symlink targets are not real paths and thus fail
// QDir's stat-based type filtering) are not silently dropped.
QStringList fdEntries(const QString &dirPath)
{
    QStringList out;
    DIR *d = opendir(QFile::encodeName(dirPath).constData());
    if (!d)
        return out;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9')
            continue;
        out << QLatin1String(e->d_name);
    }
    closedir(d);
    return out;
}

// Raw readlink: QFile::symLinkTarget canonicalises and prepends the directory
// to proc magic links (socket:[n], anon_inode:[..], pipe:[n]); readlink(2)
// returns the literal target we need to classify.
QString readLinkRaw(const QString &path)
{
    char buf[4096];
    const ssize_t n = ::readlink(QFile::encodeName(path).constData(), buf, sizeof(buf) - 1);
    return n < 0 ? QString() : QString::fromLocal8Bit(buf, n);
}

QByteArray cmdRead(const QString &path)
{
    if (!pathAllowed(path))
        return QByteArray();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.read(4 * 1024 * 1024);
}

QByteArray cmdSymlink(const QString &path)
{
    if (!pathAllowed(path))
        return QByteArray();
    return QFile::symLinkTarget(path).toLocal8Bit();
}

QByteArray cmdFdDump(int pid)
{
    QByteArray out;
    const QString fdDir = QStringLiteral("/proc/%1/fd").arg(pid);
    for (const QString &fd : fdEntries(fdDir)) {
        const QString target = readLinkRaw(fdDir + QLatin1Char('/') + fd);
        if (target.isEmpty())
            continue;
        QFile fi(QStringLiteral("/proc/%1/fdinfo/%2").arg(pid).arg(fd));
        QByteArray flags, pos;
        if (fi.open(QIODevice::ReadOnly)) {
            for (const QByteArray &line : fi.readAll().split('\n')) {
                if (line.startsWith("flags:"))
                    flags = line.mid(6).trimmed();
                else if (line.startsWith("pos:"))
                    pos = line.mid(4).trimmed();
            }
        }
        out += fd.toLatin1() + '|' + target.toLocal8Bit() + '|' + flags + '|' + pos + '\n';
    }
    return out;
}

QByteArray cmdWatchers(int pid)
{
    QByteArray out;
    const QString needle = QStringLiteral("/proc/%1/").arg(pid);
    DIR *proc = opendir("/proc");
    if (!proc)
        return out;
    struct dirent *de;
    while ((de = readdir(proc))) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        const int opid = atoi(de->d_name);
        if (opid == pid)
            continue;
        const QString fdDir = QStringLiteral("/proc/%1/fd").arg(opid);
        QStringList hits;
        for (const QString &fd : fdEntries(fdDir)) {
            const QString t = readLinkRaw(fdDir + QLatin1Char('/') + fd);
            if (t.startsWith(needle))
                hits << t.mid(needle.size());
        }
        if (!hits.isEmpty()) {
            QFile comm(QStringLiteral("/proc/%1/comm").arg(opid));
            comm.open(QIODevice::ReadOnly);
            out += QByteArray::number(opid) + '|' + comm.readAll().trimmed() + '|'
                 + hits.join(QStringLiteral(", ")).toLocal8Bit() + '\n';
        }
    }
    closedir(proc);
    return out;
}

QByteArray cmdSockMap()
{
    QByteArray out;
    DIR *proc = opendir("/proc");
    if (!proc)
        return out;
    struct dirent *de;
    while ((de = readdir(proc))) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        const QString pidStr = QLatin1String(de->d_name);
        const QString fdDir = QStringLiteral("/proc/") + pidStr + QStringLiteral("/fd");
        QByteArray comm;
        for (const QString &fd : fdEntries(fdDir)) {
            const QString t = readLinkRaw(fdDir + QLatin1Char('/') + fd);
            if (!t.startsWith(QLatin1String("socket:[")))
                continue;
            const QByteArray inode = t.mid(8, t.size() - 9).toLatin1();
            if (comm.isEmpty()) {
                QFile c(QStringLiteral("/proc/") + pidStr + QStringLiteral("/comm"));
                if (c.open(QIODevice::ReadOnly))
                    comm = c.readAll().trimmed();
            }
            out += inode + '|' + pidStr.toLatin1() + '|' + comm + '\n';
        }
    }
    closedir(proc);
    return out;
}

QByteArray cmdChargerLog()
{
    // read the kernel ring buffer (non-destructive) and keep charger/PD lines
    int len = klogctl(10 /*SIZE_BUFFER*/, nullptr, 0);
    if (len <= 0)
        len = 1 << 20;
    QByteArray buf(len + 1, 0);
    const int n = klogctl(3 /*READ_ALL*/, buf.data(), len);
    if (n < 0)
        return QByteArray();
    buf.truncate(n);

    QStringList keep;
    for (const QByteArray &raw : buf.split('\n')) {
        QByteArray l = raw;
        // strip the "<pri>" syslog prefix
        if (l.startsWith('<')) {
            const int gt = l.indexOf('>');
            if (gt > 0)
                l = l.mid(gt + 1);
        }
        const QByteArray low = l.toLower();
        if (low.contains("charger") || low.contains("pd_") || low.contains("usbpd")
            || low.contains("typec") || low.contains("apsd") || low.contains("hvdcp")
            || low.contains("smblib") || low.contains("real_charger") || low.contains("pmic")
            || low.contains("icl_settled") || low.contains(" pd ") || low.contains("power_supply"))
            keep << QString::fromUtf8(l);
    }
    while (keep.size() > 60)
        keep.removeFirst();
    return keep.join(QLatin1Char('\n')).toUtf8();
}

// Log excerpt for bug reports: journal + kernel ring buffer, filtered to
// lines containing the term. Read-only; journalctl runs with fixed argv and
// no shell, the term is only used as an in-process filter needle.
QByteArray cmdLogGrep(const QByteArray &term)
{
    QByteArray out;
    const QByteArray low = term.toLower().trimmed();
    if (low.isEmpty())
        return out;

    QProcess jp;
    jp.start(QStringLiteral("journalctl"),
             QStringList() << QStringLiteral("--no-pager") << QStringLiteral("-n")
                           << QStringLiteral("5000") << QStringLiteral("-o")
                           << QStringLiteral("short-iso"));
    if (jp.waitForFinished(10000)) {
        QList<QByteArray> keep;
        for (const QByteArray &l : jp.readAllStandardOutput().split('\n'))
            if (!l.isEmpty() && l.toLower().contains(low))
                keep << l;
        while (keep.size() > 200)
            keep.removeFirst();
        out += "== journal (last " + QByteArray::number(keep.size()) + " matching lines) ==\n";
        for (const QByteArray &l : keep)
            out += l + '\n';
    } else {
        out += "== journal: journalctl not available ==\n";
    }

    int len = klogctl(10 /*SIZE_BUFFER*/, nullptr, 0);
    if (len <= 0)
        len = 1 << 20;
    QByteArray buf(len + 1, 0);
    const int n = klogctl(3 /*READ_ALL*/, buf.data(), len);
    if (n > 0) {
        buf.truncate(n);
        QList<QByteArray> keep;
        for (const QByteArray &raw : buf.split('\n')) {
            QByteArray l = raw;
            if (l.startsWith('<')) {
                const int gt = l.indexOf('>');
                if (gt > 0)
                    l = l.mid(gt + 1);
            }
            if (!l.isEmpty() && l.toLower().contains(low))
                keep << l;
        }
        while (keep.size() > 100)
            keep.removeFirst();
        out += "\n== kernel log (last " + QByteArray::number(keep.size()) + " matching lines) ==\n";
        for (const QByteArray &l : keep)
            out += l + '\n';
    }
    return out;
}

QByteArray cmdSignal(const QByteArray &arg)
{
    const QList<QByteArray> a = arg.split(' ');
    return ::kill(a.value(0).toInt(), a.value(1).toInt()) == 0 ? "1" : "0";
}

QByteArray cmdNice(const QByteArray &arg)
{
    const QList<QByteArray> a = arg.split(' ');
    return ::setpriority(PRIO_PROCESS, a.value(0).toInt(), a.value(1).toInt()) == 0 ? "1" : "0";
}

void serve(QLocalSocket *sock)
{
    QObject::connect(sock, &QLocalSocket::readyRead, sock, [sock]() {
        while (sock->canReadLine()) {
            const QByteArray line = sock->readLine().trimmed();
            const int sp = line.indexOf(' ');
            const QByteArray cmd = sp < 0 ? line : line.left(sp);
            const QByteArray arg = sp < 0 ? QByteArray() : line.mid(sp + 1);
            QByteArray payload;
            bool ok = true;
            if (cmd == "R")
                payload = cmdRead(QString::fromLocal8Bit(arg));
            else if (cmd == "S")
                payload = cmdSymlink(QString::fromLocal8Bit(arg));
            else if (cmd == "F")
                payload = cmdFdDump(arg.toInt());
            else if (cmd == "W")
                payload = cmdWatchers(arg.toInt());
            else if (cmd == "M")
                payload = cmdSockMap();
            else if (cmd == "D")
                payload = cmdChargerLog();
            else if (cmd == "J")
                payload = cmdLogGrep(arg);
            else if (cmd == "K")
                payload = cmdSignal(arg);
            else if (cmd == "N")
                payload = cmdNice(arg);
            else
                ok = false;
            if (ok)
                sock->write("OK " + QByteArray::number(payload.size()) + '\n' + payload);
            else
                sock->write("ERR\n");
        }
    });
    QObject::connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
}

} // namespace

int rootHelperMain(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (getuid() != 0) {
        fprintf(stderr, "sysmetrics helper: must run as root (devel-su)\n");
        return 1;
    }
    QLocalServer::removeServer(QLatin1String(SOCK_PATH));
    QLocalServer server;
    if (!server.listen(QLatin1String(SOCK_PATH))) {
        fprintf(stderr, "sysmetrics helper: cannot listen on %s\n", SOCK_PATH);
        return 1;
    }
    chmod(SOCK_PATH, 0660);
    chown(SOCK_PATH, 0, 100000);  // root + defaultuser primary group
    static int clients = 0;
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&server]() {
        while (QLocalSocket *s = server.nextPendingConnection()) {
            ++clients;
            QObject::connect(s, &QLocalSocket::disconnected, s, []() { --clients; });
            serve(s);
        }
    });
    // Selbst-Exit ohne verbundenen Client (20 s Anlaufgnade): der Helfer läuft
    // nur, solange die App ihn nutzt.
    QElapsedTimer up;
    up.start();
    QTimer idle;
    idle.setInterval(5000);
    QObject::connect(&idle, &QTimer::timeout, &app, [&app, &up]() {
        if (clients == 0 && up.elapsed() > 20000)
            app.quit();
    });
    idle.start();
    printf("sysmetrics helper: listening on %s\n", SOCK_PATH);
    return app.exec();
}
