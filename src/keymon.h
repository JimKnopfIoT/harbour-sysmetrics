// The hardware buttons: what the input core says this phone has, and a live
// test that names each press as it happens.
//
// Reading an evdev node is passive -- the descriptor is opened read-only and
// never grabbed, so the compositor keeps seeing every key while the test runs.
// Volume still changes, the power key still blanks the screen. That is the
// point: the test says the button reached the kernel, not that this app took
// it away from the system.
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

class QSocketNotifier;

class KeyMon : public QObject
{
    Q_OBJECT
    // One entry per input device that can report keys, touchscreens excluded.
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    // The test checklist: one entry per key the hardware declares it can send.
    Q_PROPERTY(QVariantList keys READ keys NOTIFY keysChanged)
    // Switches (lid, headphone jack, cover) with the state they hold now.
    Q_PROPERTY(QVariantList switches READ switches NOTIFY devicesChanged)
    // Key codes that are down right now rather than pressed: a latching
    // switch reports its position this way, and a position is not an event.
    Q_PROPERTY(QVariantList heldKeys READ heldKeys NOTIFY devicesChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    // Empty while all nodes opened; otherwise why they did not.
    Q_PROPERTY(QString openError READ openError NOTIFY listeningChanged)
    Q_PROPERTY(int openedCount READ openedCount NOTIFY listeningChanged)
    Q_PROPERTY(QString lastKey READ lastKey NOTIFY lastChanged)
    Q_PROPERTY(int lastCode READ lastCode NOTIFY lastChanged)
    Q_PROPERTY(bool lastPressed READ lastPressed NOTIFY lastChanged)
    Q_PROPERTY(QString lastDevice READ lastDevice NOTIFY lastChanged)
    Q_PROPERTY(int seenCount READ seenCount NOTIFY keysChanged)

public:
    explicit KeyMon(QObject *parent = nullptr);
    ~KeyMon() override;

    QVariantList devices() const { return m_devices; }
    QVariantList keys() const { return m_keys; }
    QVariantList switches() const { return m_switches; }
    QVariantList heldKeys() const { return m_held; }
    bool listening() const { return !m_fds.isEmpty(); }
    QString openError() const { return m_openError; }
    int openedCount() const { return m_fds.size(); }
    QString lastKey() const { return m_lastKey; }
    int lastCode() const { return m_lastCode; }
    bool lastPressed() const { return m_lastPressed; }
    QString lastDevice() const { return m_lastDevice; }
    int seenCount() const { return m_seenCount; }

    // Enumerate the devices. Cheap: one open + a handful of ioctls each.
    Q_INVOKABLE void refresh();
    // Open the key-capable nodes and report presses until stop().
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    // Clear the checklist without re-enumerating.
    Q_INVOKABLE void resetSeen();
    // The kernel's name for a code, for callers that have only the number.
    Q_INVOKABLE static QString keyName(int code);

signals:
    void devicesChanged();
    void keysChanged();
    void listeningChanged();
    void lastChanged();

private slots:
    void readReady(int fd);

private:
    struct Node { int fd; QString name; QString dev; };

    void closeAll();
    void markSeen(int code, bool pressed, const QString &device);

    QVariantList m_devices;
    QVariantList m_keys;
    QVariantList m_switches;
    QVariantList m_held;
    QVector<Node> m_fds;
    QVector<QSocketNotifier *> m_notifiers;
    QString m_openError;
    QString m_lastKey;
    QString m_lastDevice;
    int m_lastCode = -1;
    bool m_lastPressed = false;
    int m_seenCount = 0;
};
