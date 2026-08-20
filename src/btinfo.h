// BlueZ system-bus queries: connected/paired devices with identity data.
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class BtInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY updated)
    Q_PROPERTY(bool powered READ powered NOTIFY updated)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY updated)
    Q_PROPERTY(QVariantMap adapter READ adapter NOTIFY updated)

public:
    explicit BtInfo(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool powered() const { return m_powered; }
    QVariantList devices() const { return m_devices; }
    QVariantMap adapter() const { return m_adapter; }

    Q_INVOKABLE void refresh();

signals:
    void updated();

private:
    bool m_available = false;
    bool m_powered = false;
    QVariantList m_devices;
    QVariantMap m_adapter;
};
