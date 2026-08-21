#include "rootclient.h"

#include <QDBusConnection>
#include <QDBusMessage>

RootClient *RootClient::instance()
{
    static RootClient *inst = new RootClient();
    return inst;
}

RootClient::RootClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_sock, &QLocalSocket::stateChanged, this, &RootClient::activeChanged);
    // poll for the helper socket so a helper started later is picked up
    // automatically, without the user pressing Reconnect.
    m_retry.setInterval(3000);
    connect(&m_retry, &QTimer::timeout, this, [this]() {
        if (m_sock.state() == QLocalSocket::UnconnectedState)
            probe();
    });
    m_retry.start();
    probe();
}

void RootClient::setHelper(bool on)
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.systemd1"),
        QStringLiteral("/org/freedesktop/systemd1"),
        QStringLiteral("org.freedesktop.systemd1.Manager"),
        on ? QStringLiteral("StartUnit") : QStringLiteral("StopUnit"));
    call << QStringLiteral("harbour-sysmetrics-helper.service")
         << QStringLiteral("replace");
    const QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning("sysmetrics: %s helper failed: %s",
                 on ? "start" : "stop", qPrintable(reply.errorMessage()));
    else if (on)
        QTimer::singleShot(1000, this, &RootClient::probe);
}

void RootClient::probe()
{
    if (m_sock.state() == QLocalSocket::ConnectedState)
        return;
    m_sock.abort();
    m_sock.connectToServer(QStringLiteral("/tmp/sysmetrics-root.sock"));
    m_sock.waitForConnected(200);
}

QByteArray RootClient::request(const QByteArray &line, int timeoutMs)
{
    if (m_sock.state() != QLocalSocket::ConnectedState)
        return QByteArray();
    m_sock.write(line + '\n');
    if (!m_sock.waitForBytesWritten(300))
        return QByteArray();
    // response: "OK <len>\n<payload>" or "ERR\n"
    QByteArray header;
    while (!header.contains('\n')) {
        if (!m_sock.waitForReadyRead(timeoutMs)) {
            m_sock.abort();
            return QByteArray();
        }
        header += m_sock.readAll();
    }
    const int nl = header.indexOf('\n');
    QByteArray payload = header.mid(nl + 1);
    header.truncate(nl);
    if (!header.startsWith("OK "))
        return QByteArray();
    const int len = header.mid(3).toInt();
    while (payload.size() < len) {
        if (!m_sock.waitForReadyRead(500)) {
            m_sock.abort();
            return QByteArray();
        }
        payload += m_sock.readAll();
    }
    payload.truncate(len);
    return payload;
}

QByteArray RootClient::readFile(const QString &path)
{
    return request("R " + path.toLocal8Bit());
}

QString RootClient::symlinkTarget(const QString &path)
{
    return QString::fromLocal8Bit(request("S " + path.toLocal8Bit()));
}

QStringList RootClient::fdDump(int pid)
{
    const QByteArray r = request("F " + QByteArray::number(pid));
    if (r.isEmpty())
        return QStringList();
    return QString::fromLocal8Bit(r).split(QLatin1Char('\n'), QString::SkipEmptyParts);
}

QStringList RootClient::watcherScan(int pid)
{
    const QByteArray r = request("W " + QByteArray::number(pid));
    if (r.isEmpty())
        return QStringList();
    return QString::fromLocal8Bit(r).split(QLatin1Char('\n'), QString::SkipEmptyParts);
}

QStringList RootClient::sockMap()
{
    const QByteArray r = request("M");
    if (r.isEmpty())
        return QStringList();
    return QString::fromLocal8Bit(r).split(QLatin1Char('\n'), QString::SkipEmptyParts);
}

QStringList RootClient::chargerLog()
{
    const QByteArray r = request("D");
    if (r.isEmpty())
        return QStringList();
    return QString::fromUtf8(r).split(QLatin1Char('\n'), QString::SkipEmptyParts);
}

QString RootClient::logGrep(const QString &term)
{
    return QString::fromUtf8(request("J " + term.toUtf8(), 15000));
}

bool RootClient::sendSignal(int pid, int sig)
{
    return request("K " + QByteArray::number(pid) + ' ' + QByteArray::number(sig)) == "1";
}

bool RootClient::setNice(int pid, int nice)
{
    return request("N " + QByteArray::number(pid) + ' ' + QByteArray::number(nice)) == "1";
}
