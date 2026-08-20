// /dev node -> sysfs device chain: subsystem, driver, vendor, product, serial.
#pragma once

#include <QString>
#include <QVariantMap>

namespace DeviceInfo {
QVariantMap describe(const QString &devPath);
}
