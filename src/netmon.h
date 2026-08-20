// System-wide inet connection list with process attribution and direction.
#pragma once

#include <QObject>
#include <QVariantList>

class NetMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList connections READ connections NOTIFY updated)
    Q_PROPERTY(int total READ total NOTIFY updated)
    Q_PROPERTY(int listening READ listening NOTIFY updated)
    Q_PROPERTY(int established READ established NOTIFY updated)
    Q_PROPERTY(int threatLevel READ threatLevel NOTIFY updated)
    Q_PROPERTY(QString threatSummary READ threatSummary NOTIFY updated)
    Q_PROPERTY(QVariantList threatFindings READ threatFindings NOTIFY updated)

public:
    explicit NetMonitor(QObject *parent = nullptr);

    QVariantList connections() const { return m_connections; }
    int total() const { return m_total; }
    int listening() const { return m_listening; }
    int established() const { return m_established; }
    int threatLevel() const { return m_threatLevel; }
    QString threatSummary() const { return m_threatSummary; }
    QVariantList threatFindings() const { return m_threatFindings; }

    Q_INVOKABLE void refresh();

signals:
    void updated();

private:
    QVariantList m_connections;
    int m_total = 0;
    int m_listening = 0;
    int m_established = 0;
    int m_threatLevel = 0;
    QString m_threatSummary;
    QVariantList m_threatFindings;
};
