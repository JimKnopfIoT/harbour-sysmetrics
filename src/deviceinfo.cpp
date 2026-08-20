#include "deviceinfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace {

QString readTrimmed(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromLocal8Bit(f.readAll().trimmed());
}

QString firstOf(const QString &dir, const QStringList &names)
{
    for (const QString &n : names) {
        const QString v = readTrimmed(dir + QLatin1Char('/') + n);
        if (!v.isEmpty())
            return v;
    }
    return QString();
}

} // namespace

QVariantMap DeviceInfo::describe(const QString &devPath)
{
    QVariantMap m;
    m.insert(QStringLiteral("path"), devPath);

    struct stat st;
    if (::stat(QFile::encodeName(devPath).constData(), &st) != 0)
        return m;
    const bool block = S_ISBLK(st.st_mode);
    if (!block && !S_ISCHR(st.st_mode))
        return m;

    const QString link = QStringLiteral("/sys/dev/%1/%2:%3")
        .arg(block ? QStringLiteral("block") : QStringLiteral("char"))
        .arg(major(st.st_rdev)).arg(minor(st.st_rdev));
    QString sys = QFileInfo(link).canonicalFilePath();
    if (sys.isEmpty())
        return m;
    m.insert(QStringLiteral("sysfs"), sys);

    // walk up the device chain, take the first non-empty value of each key
    QString vendor, product, serial, driver, subsystem, model;
    QString dir = sys;
    for (int depth = 0; depth < 8 && dir.startsWith(QLatin1String("/sys/")); ++depth) {
        if (subsystem.isEmpty())
            subsystem = QFileInfo(dir + QStringLiteral("/subsystem")).symLinkTarget().section(QLatin1Char('/'), -1);
        if (driver.isEmpty())
            driver = QFileInfo(dir + QStringLiteral("/driver")).symLinkTarget().section(QLatin1Char('/'), -1);
        if (vendor.isEmpty())
            vendor = firstOf(dir, { QStringLiteral("manufacturer"), QStringLiteral("vendor"),
                                    QStringLiteral("idVendor"), QStringLiteral("device/vendor") });
        if (product.isEmpty())
            product = firstOf(dir, { QStringLiteral("product"), QStringLiteral("model"),
                                     QStringLiteral("name"), QStringLiteral("idProduct"),
                                     QStringLiteral("device/model"), QStringLiteral("device/name") });
        if (serial.isEmpty())
            serial = firstOf(dir, { QStringLiteral("serial"), QStringLiteral("device/serial") });
        if (model.isEmpty())
            model = readTrimmed(dir + QStringLiteral("/uevent"))
                        .split(QLatin1Char('\n')).filter(QStringLiteral("MODALIAS=")).value(0)
                        .mid(9);
        const QString parent = QFileInfo(dir + QStringLiteral("/..")).canonicalFilePath();
        if (parent == dir)
            break;
        dir = parent;
    }

    if (block) {
        const QString sizeStr = readTrimmed(sys + QStringLiteral("/size"));
        if (!sizeStr.isEmpty())
            m.insert(QStringLiteral("size"), sizeStr.toDouble() * 512);
        if (readTrimmed(sys + QStringLiteral("/removable")) == QLatin1String("1"))
            m.insert(QStringLiteral("removable"), true);
    }

    if (!vendor.isEmpty()) m.insert(QStringLiteral("vendor"), vendor);
    if (!product.isEmpty()) m.insert(QStringLiteral("product"), product);
    if (!serial.isEmpty()) m.insert(QStringLiteral("serial"), serial);
    if (!driver.isEmpty()) m.insert(QStringLiteral("driver"), driver);
    if (!subsystem.isEmpty()) m.insert(QStringLiteral("subsystem"), subsystem);
    if (!model.isEmpty()) m.insert(QStringLiteral("modalias"), model);
    return m;
}
