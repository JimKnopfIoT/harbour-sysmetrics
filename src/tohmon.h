/*
  harbour-sysmetrics — tohmon.h
  Copyright (C) 2026  harbour-sysmetrics contributors — GPLv3 or later.

  The Other Half: the seven spring contacts on the back, and the chip in the
  cover that sits on them.

  Two very different things are read here, and the page keeps them apart.

  The contacts are a platform device. On the Jolla Phone (2026) the device
  tree carries a node `yft_pogo_pin` and its driver publishes three values
  under /sys/class/yft_pogo_pin: the state of the interrupt line, the voltage
  at the identify contact, and whether the 5 V output is switched on. Those
  are measurements — the connector's published figures (5–9 V in, 3,3 V bus,
  2,90 mm pitch) are not, they describe what a contact is for.

  The chip is an I2C memory at target address 0x50 whose layout the maker
  publishes: the four bytes "JTOH", a CRC-32 over everything that follows up
  to the end of the payload, vendor and product id, and a CBOR map naming the
  cover. Reading it is a bus transfer, so it happens on request and never
  while the page is merely open.

  Nothing here writes. The 5 V output is world-writable on the device and
  switching it would be acting on the hardware, not reporting it; the same
  goes for the memory, which a wrong write would brick the cover with.
*/
#ifndef TOHMON_H
#define TOHMON_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>

// What one read of the memory chip produced, and what the bus revealed while
// it was open. The block count and the address width are measured, not read
// off a label: the chip carries no type number a bus can ask for.
namespace TohI2c {
struct Reading {
    QByteArray data;     // bytes from offset 0, empty when nothing answered
    QString bus;         // the adapter that answered, e.g. "i2c-0"
    int blocks = 0;      // consecutive target addresses that answered, 0x50 up
    bool eightBit = false;  // the chip takes an 8-bit address
};
Reading read(int count);
}

class TohMon : public QObject
{
    Q_OBJECT
    // Whether this device has the pogo-pin controller at all. Everything else
    // on the page is written for a connector that exists; on a phone without
    // one the page says so instead of showing empty rows.
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(QVariantMap pins READ pins NOTIFY pinsChanged)

public:
    explicit TohMon(QObject *parent = nullptr);

    bool supported() const { return m_supported; }

    // The live state of the contacts. Keys: supported, attached, intState,
    // intText, idMillivolt, powerOut, interrupts, plus the controller's own
    // description (compatible, intGpio, powerGpio, adcChannel, irq).
    // `attached` follows the maker's rule that a cover pulls the interrupt
    // line low; it is a reading of intState, not a separate measurement.
    QVariantMap pins() const;
    Q_INVOKABLE QVariantMap readPins();

    // Read the memory chip and take it apart. Keys: ok, error, source, bus,
    // address, addressing, blocks, chipBytes, raw (hex), magic, magicOk,
    // crcStated, crcComputed, crcOk, vendorId, vendorName, productId,
    // reserved, payloadSize, payload (QVariantMap of the CBOR keys),
    // payloadUnknown (keys the published table does not name), and — when
    // the vendor and product id name one of the maker's own covers —
    // blockSha256, officialName, officialSha256, officialMatch.
    Q_INVOKABLE QVariantMap readMemory();

    // Start/stop the refresh of `pins`. A page that is not on screen has no
    // business reading sysfs four times a second.
    Q_INVOKABLE void setWatching(bool on);

signals:
    void pinsChanged();

private:
    void refresh();

    bool m_supported = false;
    QVariantMap m_pins;
    int m_timer = 0;

protected:
    void timerEvent(QTimerEvent *e) override;
};

#endif // TOHMON_H
