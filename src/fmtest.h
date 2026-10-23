// The built-in FM receiver: whether there is one, who holds it, and a function
// test that sweeps the band once.
//
// The test talks to /dev/fm with the MediaTek FM ioctls: power up, measure every
// 100 kHz channel with the driver's soft-mute tune, power down. No sound is
// routed anywhere and no mixer control is touched -- a chip that powers up and
// measures the whole band is a working chip. Whether stations come in depends
// on the antenna, which on these phones is the USB-C cable; the result says so
// rather than calling a quiet band a fault.
//
// The ioctl layout is the one measured on the Jolla Phone (2026) with the
// connac2x driver. It depends on the driver and on the pointer size, so the
// test is offered only where both match; elsewhere the node is shown and left
// alone.
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <atomic>
#include <thread>

class FmTest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool present READ present NOTIFY infoChanged)
    // "media:media 0660" -- owner, group and mode of /dev/fm
    Q_PROPERTY(QString nodeInfo READ nodeInfo NOTIFY infoChanged)
    Q_PROPERTY(bool accessible READ accessible NOTIFY infoChanged)
    Q_PROPERTY(QString driver READ driver NOTIFY infoChanged)
    // Driver and ABI are ones the test was measured against.
    Q_PROPERTY(bool supported READ supported NOTIFY infoChanged)
    // Another process of ours holding /dev/fm, e.g. "wfscope (19437)".
    Q_PROPERTY(QString usedBy READ usedBy NOTIFY infoChanged)

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    // "ok" | "fail" | "busy" | ""
    Q_PROPERTY(QString result READ result NOTIFY resultChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    // One dBm figure per channel from 87.5 MHz in 100 kHz steps.
    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY resultChanged)
    Q_PROPERTY(qreal floorDbm READ floorDbm NOTIFY resultChanged)
    // [{mhz, dbm, valid}], strongest first
    Q_PROPERTY(QVariantList peaks READ peaks NOTIFY resultChanged)
    Q_PROPERTY(qreal seconds READ seconds NOTIFY resultChanged)

public:
    explicit FmTest(QObject *parent = nullptr);
    ~FmTest() override;

    bool present() const { return m_present; }
    QString nodeInfo() const { return m_nodeInfo; }
    bool accessible() const { return m_accessible; }
    QString driver() const { return m_driver; }
    bool supported() const { return m_supported; }
    QString usedBy() const { return m_usedBy; }
    bool running() const { return m_running; }
    qreal progress() const { return m_progress; }
    QString result() const { return m_result; }
    QString resultText() const { return m_resultText; }
    QVariantList spectrum() const { return m_spectrum; }
    qreal floorDbm() const { return m_floor; }
    QVariantList peaks() const { return m_peaks; }
    qreal seconds() const { return m_seconds; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();

signals:
    void infoChanged();
    void runningChanged();
    void progressChanged();
    void resultChanged();
    // from the sweep thread, queued into the GUI thread
    void sweepProgress(qreal p);
    void sweepDone(const QString &result, const QString &text, const QVariantList &dbm,
                   const QVariantList &valid, qreal seconds);

private:
    void sweep();
    void finish(const QString &result, const QString &text, const QVariantList &dbm,
                const QVariantList &valid, qreal seconds);
    void join();

    std::thread m_thread;
    std::atomic<bool> m_stop { false };

    bool m_present = false;
    bool m_accessible = false;
    bool m_supported = false;
    QString m_nodeInfo;
    QString m_driver;
    QString m_usedBy;
    bool m_running = false;
    qreal m_progress = 0;
    QString m_result;
    QString m_resultText;
    QVariantList m_spectrum;
    QVariantList m_peaks;
    qreal m_floor = 0;
    qreal m_seconds = 0;
};
