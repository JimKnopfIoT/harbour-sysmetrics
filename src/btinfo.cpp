#include "btinfo.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QVariantMap>

BtInfo::BtInfo(QObject *parent)
    : QObject(parent)
{
}

void BtInfo::refresh()
{
    m_available = false;
    m_powered = false;
    m_adapter = QVariantMap();
    QVariantList devices;

    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 2000);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        m_available = true;
        const QDBusArgument arg = reply.arguments().first().value<QDBusArgument>();
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            QDBusObjectPath path;
            arg >> path;
            QMap<QString, QVariantMap> ifaces;
            const QDBusArgument ifArg = qvariant_cast<QDBusArgument>(arg.asVariant());
            // manual unpack: a{sa{sv}}
            ifArg.beginMap();
            while (!ifArg.atEnd()) {
                ifArg.beginMapEntry();
                QString iface;
                QVariantMap props;
                ifArg >> iface >> props;
                ifaces.insert(iface, props);
                ifArg.endMapEntry();
            }
            ifArg.endMap();
            arg.endMapEntry();

            if (ifaces.contains(QStringLiteral("org.bluez.Adapter1"))) {
                const QVariantMap a = ifaces.value(QStringLiteral("org.bluez.Adapter1"));
                m_powered = a.value(QStringLiteral("Powered")).toBool() || m_powered;
                QVariantMap ad;
                ad.insert(QStringLiteral("name"), a.value(QStringLiteral("Name")).toString());
                ad.insert(QStringLiteral("alias"), a.value(QStringLiteral("Alias")).toString());
                ad.insert(QStringLiteral("address"), a.value(QStringLiteral("Address")).toString());
                ad.insert(QStringLiteral("addressType"), a.value(QStringLiteral("AddressType")).toString());
                ad.insert(QStringLiteral("modalias"), a.value(QStringLiteral("Modalias")).toString());
                ad.insert(QStringLiteral("class"), a.value(QStringLiteral("Class")).toUInt());
                ad.insert(QStringLiteral("powered"), a.value(QStringLiteral("Powered")).toBool());
                ad.insert(QStringLiteral("discoverable"), a.value(QStringLiteral("Discoverable")).toBool());
                ad.insert(QStringLiteral("pairable"), a.value(QStringLiteral("Pairable")).toBool());
                ad.insert(QStringLiteral("discovering"), a.value(QStringLiteral("Discovering")).toBool());
                m_adapter = ad;
            }
            if (ifaces.contains(QStringLiteral("org.bluez.Device1"))) {
                const QVariantMap p = ifaces.value(QStringLiteral("org.bluez.Device1"));
                QVariantMap d;
                d.insert(QStringLiteral("name"), p.value(QStringLiteral("Name")).toString());
                d.insert(QStringLiteral("address"), p.value(QStringLiteral("Address")).toString());
                d.insert(QStringLiteral("connected"), p.value(QStringLiteral("Connected")).toBool());
                d.insert(QStringLiteral("paired"), p.value(QStringLiteral("Paired")).toBool());
                d.insert(QStringLiteral("icon"), p.value(QStringLiteral("Icon")).toString());
                d.insert(QStringLiteral("rssi"), p.value(QStringLiteral("RSSI")).toInt());
                devices.append(d);
            }
        }
        arg.endMap();
    }

    m_devices = devices;
    emit updated();
}
