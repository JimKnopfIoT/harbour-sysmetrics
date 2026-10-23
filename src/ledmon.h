// The LEDs in /sys/class/leds: what is there, who may switch it, and a function
// test for the indicator LEDs.
//
// Reading stays with what is safe on any driver: links, the device tree name,
// max_brightness and trigger. brightness is read only on indicator and
// backlight nodes -- flash controllers sit in the same class, and on those the
// app reads nothing it does not have to (see sysfsUnsafe() in sysmon.cpp).
//
// The test switches each indicator channel on alone, then all together, and
// puts the old values back. It touches a channel only while no kernel trigger
// drives it: writing brightness detaches a trigger, and trigger itself is not
// ours to set back. Where brightness belongs to the system, the root helper
// can do the writing; it checks the same rules on its side.
//
// Some phones list the vibration motor here too. Its test writes duration and
// activate three times; under the transient trigger state has to be 1 as well,
// or the pulse runs with the motor off. Both are put back afterwards.
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>

namespace Leds {
// "indicator" | "backlight" | "flash" | "vibrator" | "other"
QString kindOf(const QString &name, const QString &driver);
// The selected trigger, "" where the node has none.
QString currentTrigger(const QString &name);
// The attribute that starts the chip's own pattern: "blink" (Awinic),
// "breath" (Qualcomm tri-led) or "".
QString patternAttr(const QString &name);
QString driverOf(const QString &name);
// Root helper side: one write to an indicator LED, after the same checks.
bool helperWrite(const QString &name, const QString &attr, int value);
}

class LedMon : public QObject
{
    Q_OBJECT
    // [{name, kind, driver, compatible, device, max, brightness, trigger,
    //   triggerCount, writable, pattern, testable, attrs:[{name, perm}]}]
    Q_PROPERTY(QVariantList leds READ leds NOTIFY ledsChanged)
    Q_PROPERTY(int indicatorCount READ indicatorCount NOTIFY ledsChanged)

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // channel name, "all", "pattern" or "" between steps
    Q_PROPERTY(QString step READ step NOTIFY stepChanged)
    // "ok" | "note" | "fail" | ""
    Q_PROPERTY(QString result READ result NOTIFY resultChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)

    // The vibration motor, where the kernel lists it as an LED with the
    // activate/duration pair (transient trigger or a driver of its own).
    Q_PROPERTY(QString vibrator READ vibrator NOTIFY ledsChanged)
    Q_PROPERTY(bool vibratorWritable READ vibratorWritable NOTIFY ledsChanged)
    Q_PROPERTY(bool vibrating READ vibrating NOTIFY vibratingChanged)
    // "ok" | "fail" | ""
    Q_PROPERTY(QString vibeResult READ vibeResult NOTIFY vibratingChanged)
    Q_PROPERTY(QString vibeText READ vibeText NOTIFY vibratingChanged)

public:
    explicit LedMon(QObject *parent = nullptr);
    ~LedMon() override;

    QVariantList leds() const { return m_leds; }
    int indicatorCount() const { return m_indicators; }
    bool running() const { return m_running; }
    QString step() const { return m_step; }
    QString result() const { return m_result; }
    QString resultText() const { return m_resultText; }
    QString vibrator() const { return m_vib; }
    bool vibratorWritable() const { return m_vibWritable; }
    bool vibrating() const { return m_vibrating; }
    QString vibeResult() const { return m_vibeResult; }
    QString vibeText() const { return m_vibeText; }

    Q_INVOKABLE void refresh();
    // One channel after the other, then all together.
    Q_INVOKABLE void startTest();
    // The chip's own pattern on every channel that has one; root helper only.
    Q_INVOKABLE void startPatternTest();
    Q_INVOKABLE void cancel();
    // Three short pulses straight at the motor, past the feedback daemon.
    Q_INVOKABLE void startVibration();

signals:
    void ledsChanged();
    void runningChanged();
    void stepChanged();
    void resultChanged();
    void vibratingChanged();

private:
    struct Write { QString name, attr; int value; };
    struct Step { QVector<Write> writes; QString label; int ms; };

    void run(const QVector<Step> &steps, const QStringList &channels);
    void next();
    void restore();
    bool write(const QString &name, const QString &attr, int value);
    QStringList testChannels(bool needPattern) const;
    void pulse();
    void endVibration(bool ok);

    QVariantList m_leds;
    int m_indicators = 0;
    QHash<QString, int> m_max;

    QTimer m_timer;
    QVector<Step> m_steps;
    int m_at = 0;
    QStringList m_channels;
    QHash<QString, int> m_orig;      // brightness before the test
    QHash<QString, int> m_written;   // last brightness we wrote
    QStringList m_patterned;         // channels whose pattern we switched
    bool m_failed = false;

    QString m_vib;
    bool m_vibWritable = false;
    bool m_vibTransient = false;
    QTimer m_vibTimer;
    int m_pulses = 0;
    int m_origDuration = 0;
    int m_origState = 0;
    bool m_vibrating = false;
    QString m_vibeResult;
    QString m_vibeText;

    bool m_running = false;
    QString m_step;
    QString m_result;
    QString m_resultText;
};
