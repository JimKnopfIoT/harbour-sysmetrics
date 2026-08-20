// Client for the optional root helper (--root-helper) on a local socket.
// Privileged reads are one blocking round trip on the GUI thread.
#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QStringList>
#include <QTimer>

class RootClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    static RootClient *instance();

    bool active() const { return m_sock.state() == QLocalSocket::ConnectedState; }
    Q_INVOKABLE void probe();
    Q_INVOKABLE QStringList chargerLog();

    QByteArray readFile(const QString &path);
    QString symlinkTarget(const QString &path);
    QStringList fdDump(int pid);        // "fd|target|flags|pos" per line
    QStringList watcherScan(int pid);   // "pid|comm|paths" per line
    QStringList sockMap();              // "inode|pid|comm" per socket fd, system-wide
    bool sendSignal(int pid, int sig);
    bool setNice(int pid, int nice);

signals:
    void activeChanged();

private:
    explicit RootClient(QObject *parent = nullptr);
    QByteArray request(const QByteArray &line);

    QLocalSocket m_sock;
    QTimer m_retry;   // auto-connect while the helper is not yet running
};
