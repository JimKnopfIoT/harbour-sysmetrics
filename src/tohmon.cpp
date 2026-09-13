#include "tohmon.h"

#include "rootclient.h"

#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QProcess>
#include <QStringList>
#include <QTimerEvent>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

// The class directory the pogo-pin driver publishes. Named after the driver
// rather than after the function, so it is searched for and not assumed: a
// device without this controller simply has no such directory.
const char *CLASS_DIR   = "/sys/class/yft_pogo_pin";
const char *OF_DIR      = "/sys/firmware/devicetree/base/yft_pogo_pin";
const char *EINT_NAME   = "pogo_pin_eint";

// The reader the platform ships (package csd-toh). It carries
// cap_dac_override, which is why it can open the bus while the app cannot —
// the app is not privileged and the i2c character devices are root-only.
const char *CSD_READER  = "/usr/libexec/csd/toh-memory";

QByteArray slurp(const QString &path, int max = 65536)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.read(max);
}

QString textOf(const QString &path)
{
    return QString::fromLatin1(slurp(path, 256)).trimmed();
}

int intOf(const QString &path, int fallback = -1)
{
    bool ok = false;
    const int v = textOf(path).toInt(&ok);
    return ok ? v : fallback;
}

// Device-tree cells are big-endian 32-bit words in a file of their own.
QVector<quint32> ofCells(const QString &name)
{
    QVector<quint32> out;
    const QByteArray b = slurp(QString::fromLatin1(OF_DIR) + QLatin1Char('/') + name, 256);
    for (int i = 0; i + 4 <= b.size(); i += 4)
        out << ((quint32(quint8(b[i])) << 24) | (quint32(quint8(b[i + 1])) << 16)
                | (quint32(quint8(b[i + 2])) << 8) | quint32(quint8(b[i + 3])));
    return out;
}

// How often the interrupt line has fired since boot, summed over the cores.
// The interrupt is named in /proc/interrupts, so it is found by name and not
// by a number that changes with the kernel configuration.
qint64 eintCount(int *irq)
{
    *irq = -1;
    const QList<QByteArray> lines = slurp(QStringLiteral("/proc/interrupts"), 1 << 20).split('\n');
    for (const QByteArray &line : lines) {
        if (!line.contains(EINT_NAME))
            continue;
        const QList<QByteArray> f = QString::fromLatin1(line).simplified().toLatin1().split(' ');
        if (f.isEmpty())
            continue;
        *irq = QString::fromLatin1(f.first()).remove(QLatin1Char(':')).toInt();
        qint64 sum = 0;
        for (int i = 1; i < f.size(); ++i) {
            bool ok = false;
            const qint64 v = QString::fromLatin1(f.at(i)).toLongLong(&ok);
            if (!ok)
                break;          // the counters end where the chip name begins
            sum += v;
        }
        return sum;
    }
    return -1;
}

// CRC-32 as the maker specifies it: the ordinary reflected IEEE polynomial,
// the one zip and Ethernet use. Computed bit by bit — fifty bytes do not
// justify a lookup table.
quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0xffffffffu;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= quint8(data.at(i));
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// ---------------------------------------------------------------- CBOR ----
// The payload is a CBOR map. Qt 5.6 has no CBOR of its own, so this is a
// decoder for exactly as much of the format as the published layout uses —
// and for the rest it stops rather than inventing a value. Unknown keys are
// carried through on purpose: the maker says the table may grow, and a key
// this build does not know is still worth showing.
QVariant cborRead(const QByteArray &d, int &p, int depth = 0);

bool cborHead(const QByteArray &d, int &p, int &major, quint64 &arg, bool &indefinite)
{
    if (p >= d.size())
        return false;
    const quint8 ib = quint8(d.at(p++));
    major = ib >> 5;
    const int minor = ib & 0x1f;
    indefinite = false;
    int extra = 0;
    if (minor < 24) {
        arg = minor;
        return true;
    }
    switch (minor) {
    case 24: extra = 1; break;
    case 25: extra = 2; break;
    case 26: extra = 4; break;
    case 27: extra = 8; break;
    case 31: indefinite = true; arg = 0; return true;
    default: return false;       // 28..30 are not assigned
    }
    if (p + extra > d.size())
        return false;
    arg = 0;
    for (int i = 0; i < extra; ++i)
        arg = (arg << 8) | quint8(d.at(p++));
    return true;
}

QVariant cborRead(const QByteArray &d, int &p, int depth)
{
    int major = 0;
    quint64 arg = 0;
    bool indef = false;
    if (depth > 8 || !cborHead(d, p, major, arg, indef))
        return QVariant();
    switch (major) {
    case 0:                                    // unsigned
        return QVariant(qulonglong(arg));
    case 1:                                    // negative
        return QVariant(qlonglong(-1) - qlonglong(arg));
    case 2:                                    // byte string
    case 3: {                                  // text string
        if (indef || p + int(arg) > d.size())
            return QVariant();
        const QByteArray s = d.mid(p, int(arg));
        p += int(arg);
        return major == 3 ? QVariant(QString::fromUtf8(s)) : QVariant(s);
    }
    case 4: {                                  // array
        QVariantList l;
        for (quint64 i = 0; i < arg && !indef; ++i) {
            const QVariant v = cborRead(d, p, depth + 1);
            if (!v.isValid())
                return QVariant();
            l << v;
        }
        return indef ? QVariant() : QVariant(l);
    }
    case 5: {                                  // map
        QVariantMap m;
        for (quint64 i = 0; i < arg && !indef; ++i) {
            const QVariant k = cborRead(d, p, depth + 1);
            const QVariant v = cborRead(d, p, depth + 1);
            if (!k.isValid() || !v.isValid())
                return QVariant();
            m.insert(k.toString(), v);
        }
        return indef ? QVariant() : QVariant(m);
    }
    case 7:                                    // simple values
        if (arg == 20) return QVariant(false);
        if (arg == 21) return QVariant(true);
        if (arg == 22) return QVariant();      // null
        return QVariant();
    default:
        return QVariant();                     // tags are not used by the layout
    }
}

// ----------------------------------------------------------------- I2C ----
// Kept here rather than pulled from <linux/i2c-dev.h>: that header is not in
// every target's sysroot, and the two numbers are kernel ABI.
const unsigned long IOCTL_I2C_SLAVE = 0x0703;
const int TOH_ADDRESS = 0x50;

// Buses that carry no registered client. The contacts are wired to one of
// them, and reading a memory at 0x50 on a bus no driver has claimed cannot
// disturb anything. A bus with clients is never touched.
QStringList freeBuses()
{
    QStringList adapters, out;
    const QStringList entries = QDir(QStringLiteral("/sys/bus/i2c/devices")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &e : entries)
        if (e.startsWith(QLatin1String("i2c-")))
            adapters << e.mid(4);
    for (const QString &n : adapters) {
        bool busy = false;
        for (const QString &e : entries)
            if (e.startsWith(n + QLatin1Char('-'))) { busy = true; break; }
        if (!busy)
            out << n;
    }
    // On the Jolla Phone (2026) the platform's own reader holds /dev/i2c-0.
    // Trying it first means the usual case needs one open, not six.
    if (out.removeAll(QStringLiteral("0")) > 0)
        out.prepend(QStringLiteral("0"));
    return out;
}

} // namespace

namespace TohI2c {

// One open of one bus, one target address, one transfer. Everything the
// probe does is a read: a write would be a write to whatever answers, and
// nothing on this page is worth that.
static QByteArray transfer(const QByteArray &dev, int addr, int offset, int count, bool sendOffset)
{
    const int fd = ::open(dev.constData(), O_RDWR);
    if (fd < 0)
        return QByteArray();
    QByteArray got;
    if (::ioctl(fd, IOCTL_I2C_SLAVE, addr) >= 0) {
        bool ready = true;
        if (sendOffset) {
            const char off = char(offset);
            ready = (::write(fd, &off, 1) == 1);
        }
        if (ready) {
            got.resize(count);
            const ssize_t n = ::read(fd, got.data(), size_t(count));
            got.resize(n > 0 ? int(n) : 0);
        }
    }
    ::close(fd);
    return got;
}

Reading read(int count)
{
    Reading r;
    const QStringList buses = freeBuses();
    for (const QString &n : buses) {
        const QByteArray dev = QByteArray("/dev/i2c-") + n.toLatin1();
        const QByteArray got = transfer(dev, TOH_ADDRESS, 0, count, true);
        if (!got.startsWith("JTOH"))
            continue;
        r.data = got;
        r.bus = QStringLiteral("i2c-") + n;

        // How wide the chip wants its address. Sending a single byte 0x10
        // and getting the bytes that live at 0x10 can only happen on a chip
        // that takes an 8-bit address; a 16-bit one would have read that
        // byte as the upper half and answered from somewhere else.
        if (got.size() > 0x20) {
            const QByteArray at10 = transfer(dev, TOH_ADDRESS, 0x10, 16, true);
            r.eightBit = (at10.size() == 16 && at10 == got.mid(0x10, 16));
        }

        // How many 256-byte blocks the chip occupies. An 8-bit addressed
        // chip spreads over consecutive target addresses, so counting which
        // of them answer is how its size is measured rather than assumed.
        for (int a = TOH_ADDRESS; a < TOH_ADDRESS + 8; ++a) {
            if (transfer(dev, a, 0, 1, false).size() == 1)
                ++r.blocks;
            else
                break;
        }
        return r;
    }
    return r;
}

} // namespace TohI2c

namespace {

// The platform reader prints four big-endian words per line:
//   0000: 4a544f48 b88d9f29 00010001 00000027 |JTOH...|
QByteArray parseCsdDump(const QByteArray &text)
{
    QByteArray out;
    for (const QByteArray &line : text.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        QByteArray body = line.mid(colon + 1);
        const int bar = body.indexOf('|');
        if (bar >= 0)
            body.truncate(bar);
        const QByteArray hex = body.simplified().replace(" ", "");
        if (hex.isEmpty())
            continue;
        out += QByteArray::fromHex(hex);
    }
    return out;
}

// The covers the maker publishes, and the fingerprint each of their memory
// chips must carry.
//
// The repository sailfishos/toh-content holds the *sources* of that content —
// four yaml files naming vendor id, product id, vendor name and product name —
// not the images themselves. So the image is rebuilt from those four values
// the way the published layout prescribes, and the SHA-256 of the result is
// what stands here. That this is the right reconstruction is not an
// assumption: the image rebuilt for The Orange is, byte for byte, what the
// chip of an Orange cover answers with.
//
// The comparison covers the defined block — the header and the payload — and
// not the erased remainder of the chip, which a cover may use for itself.
struct Official {
    int vendorId;
    int productId;
    const char *name;
    const char *sha256;
};

const Official OFFICIAL[] = {
    { 1, 1, "The Orange",   "691ad8088cc545aaacc418c5b2f5cd819a631770efb59522ed600479d21a70d2" },
    { 1, 2, "Kaamos Black", "01a4bf3b70e1d2daa23f3b5e9507a299898dc421d5ac61882730c4b02578a3d3" },
    { 1, 3, "Snow White",   "dc08b77ec63f15bed581f6dc4496f0f1d08b5d3e7898ec887a73a97ddc2dd0bd" },
    { 1, 4, "Inari Blue",   "09e7a3b0c2408243d66f2abf78b5f981a1b24beab02508e570cce8c43bebfdcb" },
};

QString vendorName(int id)
{
    if (id == 0x0000) return TohMon::tr("reserved for development");
    if (id == 0x0001) return QStringLiteral("Jolla Mobile Ltd");
    return QString();
}

} // namespace

TohMon::TohMon(QObject *parent)
    : QObject(parent)
{
    m_supported = QFile::exists(QString::fromLatin1(CLASS_DIR)
                                + QLatin1String("/yft_pogo_pin_int_state"));
    refresh();
}

void TohMon::setWatching(bool on)
{
    if (on && !m_timer) {
        refresh();
        m_timer = startTimer(1000);
    } else if (!on && m_timer) {
        killTimer(m_timer);
        m_timer = 0;
    }
}

void TohMon::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == m_timer)
        refresh();
    else
        QObject::timerEvent(e);
}

QVariantMap TohMon::pins() const
{
    return m_pins;
}

QVariantMap TohMon::readPins()
{
    refresh();
    return m_pins;
}

void TohMon::refresh()
{
    QVariantMap m;
    m.insert(QStringLiteral("supported"), m_supported);
    if (!m_supported) {
        if (m_pins != m) { m_pins = m; emit pinsChanged(); }
        return;
    }

    const QString base = QString::fromLatin1(CLASS_DIR) + QLatin1Char('/');
    const int intState = intOf(base + QLatin1String("yft_pogo_pin_int_state"));
    const int idMv     = intOf(base + QLatin1String("yft_pogo_pin_adc_value"));
    const int fiveVolt = intOf(base + QLatin1String("yft_pogo_pin_5v_out_state"));

    m.insert(QStringLiteral("intState"), intState);
    m.insert(QStringLiteral("idMillivolt"), idMv);
    m.insert(QStringLiteral("powerOut"), fiveVolt);
    // The maker's rule: a cover ties the interrupt line to ground, and the
    // line going high is how detaching is noticed. So this is a reading of
    // the line, not a contact of its own.
    m.insert(QStringLiteral("attached"), intState == 0);

    int irq = -1;
    m.insert(QStringLiteral("interrupts"), qlonglong(eintCount(&irq)));
    m.insert(QStringLiteral("irq"), irq);

    m.insert(QStringLiteral("compatible"),
             QString::fromLatin1(slurp(QString::fromLatin1(OF_DIR) + QLatin1String("/compatible"), 128))
                 .remove(QLatin1Char('\0')).trimmed());
    const QVector<quint32> eintGpio  = ofCells(QStringLiteral("pogo_pin_eint_gpio"));
    const QVector<quint32> powerGpio = ofCells(QStringLiteral("pogo_pin_5v_out_gpio"));
    const QVector<quint32> adcChan   = ofCells(QStringLiteral("io-channels"));
    if (eintGpio.size() >= 2)  m.insert(QStringLiteral("intGpio"), int(eintGpio.at(1)));
    if (powerGpio.size() >= 2) m.insert(QStringLiteral("powerGpio"), int(powerGpio.at(1)));
    if (adcChan.size() >= 2)   m.insert(QStringLiteral("adcChannel"), int(adcChan.at(1)));

    if (m_pins != m) {
        m_pins = m;
        emit pinsChanged();
    }
}

QVariantMap TohMon::readMemory()
{
    QVariantMap r;
    r.insert(QStringLiteral("ok"), false);

    // Three ways to the same bytes, cheapest first. Which one worked is part
    // of the result: a figure is worth what its source is.
    QByteArray raw;
    QString bus, source;
    TohI2c::Reading direct = TohI2c::read(256);
    if (!direct.data.isEmpty()) {
        raw = direct.data;
        bus = direct.bus;
        source = QStringLiteral("direct");
    }

    if (raw.isEmpty() && QFile::exists(QString::fromLatin1(CSD_READER))) {
        QProcess p;
        p.start(QString::fromLatin1(CSD_READER), QStringList());
        if (p.waitForFinished(4000)) {
            const QByteArray got = parseCsdDump(p.readAllStandardOutput());
            if (got.startsWith("JTOH")) {
                raw = got;
                source = QStringLiteral("csd");
            }
        }
    }

    if (raw.isEmpty()) {
        TohI2c::Reading viaHelper;
        if (RootClient::instance()->tohMemory(&viaHelper) && viaHelper.data.startsWith("JTOH")) {
            direct = viaHelper;
            raw = viaHelper.data;
            bus = viaHelper.bus;
            source = QStringLiteral("helper");
        }
    }

    if (raw.size() < 16) {
        r.insert(QStringLiteral("error"),
                 m_supported ? tr("no answer from a memory chip")
                             : tr("this device has no accessory connector"));
        return r;
    }

    r.insert(QStringLiteral("source"), source);
    r.insert(QStringLiteral("bus"), bus);
    r.insert(QStringLiteral("address"), TOH_ADDRESS);
    // Only what was actually probed. Read through the platform's own reader
    // there is no bus to ask, so these stay out rather than being guessed.
    if (direct.blocks > 0) {
        r.insert(QStringLiteral("blocks"), direct.blocks);
        r.insert(QStringLiteral("chipBytes"), direct.blocks * 256);
        r.insert(QStringLiteral("addressing"), direct.eightBit ? 8 : 16);
    }
    r.insert(QStringLiteral("raw"), QString::fromLatin1(raw.toHex()));
    r.insert(QStringLiteral("bytes"), raw.size());

    const QByteArray magic = raw.left(4);
    r.insert(QStringLiteral("magic"), QString::fromLatin1(magic));
    r.insert(QStringLiteral("magicOk"), magic == QByteArray("JTOH"));

    auto be16 = [&raw](int off) {
        return int((quint8(raw.at(off)) << 8) | quint8(raw.at(off + 1)));
    };
    const quint32 crcStated = (quint32(quint8(raw.at(4))) << 24) | (quint32(quint8(raw.at(5))) << 16)
                              | (quint32(quint8(raw.at(6))) << 8) | quint32(quint8(raw.at(7)));
    const int vendorId   = be16(8);
    const int productId  = be16(10);
    const int reserved   = be16(12);
    const int payloadLen = be16(14);

    r.insert(QStringLiteral("crcStated"), qulonglong(crcStated));
    r.insert(QStringLiteral("vendorId"), vendorId);
    r.insert(QStringLiteral("vendorName"), vendorName(vendorId));
    r.insert(QStringLiteral("productId"), productId);
    r.insert(QStringLiteral("reserved"), reserved);
    r.insert(QStringLiteral("payloadSize"), payloadLen);

    // The maker puts the checksum over everything from the vendor id to the
    // end of the payload — the magic and the checksum itself stay outside.
    const int end = 16 + payloadLen;
    if (end <= raw.size()) {
        const quint32 crcHere = crc32(raw.mid(8, end - 8));
        r.insert(QStringLiteral("crcComputed"), qulonglong(crcHere));
        r.insert(QStringLiteral("crcOk"), crcHere == crcStated);

        int p = 0;
        const QByteArray payload = raw.mid(16, payloadLen);
        const QVariant decoded = cborRead(payload, p);
        r.insert(QStringLiteral("payloadHex"), QString::fromLatin1(payload.toHex()));
        if (decoded.type() == QVariant::Map) {
            r.insert(QStringLiteral("payload"), decoded.toMap());
            r.insert(QStringLiteral("payloadOk"), p == payload.size());
        } else {
            r.insert(QStringLiteral("payloadOk"), false);
        }
    } else {
        r.insert(QStringLiteral("crcOk"), false);
        r.insert(QStringLiteral("payloadOk"), false);
    }

    // ---- against the published original ---------------------------------
    // A chip that answers and checksums itself still only proves it is
    // consistent with itself. Whether it holds what the maker published is a
    // different question, and the only one a fingerprint can answer.
    if (end <= raw.size()) {
        const QByteArray block = raw.left(end);
        const QString here = QString::fromLatin1(
            QCryptographicHash::hash(block, QCryptographicHash::Sha256).toHex());
        r.insert(QStringLiteral("blockSha256"), here);
        r.insert(QStringLiteral("blockBytes"), end);
        for (const Official &o : OFFICIAL) {
            if (o.vendorId != vendorId || o.productId != productId)
                continue;
            r.insert(QStringLiteral("officialName"), QString::fromLatin1(o.name));
            r.insert(QStringLiteral("officialSha256"), QString::fromLatin1(o.sha256));
            r.insert(QStringLiteral("officialMatch"), here == QLatin1String(o.sha256));
            break;
        }
    }

    r.insert(QStringLiteral("ok"), true);
    return r;
}
