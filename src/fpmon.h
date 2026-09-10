// The fingerprint reader: which sensor is fitted, what the fingerprint daemon
// says about it, and a test that asks the daemon to identify a finger.
//
// The test goes through sailfish-fpd on the system bus. Its policy lets any
// process call the interface (only SetUser is denied), so this needs neither
// root nor the device lock -- which an ordinary application cannot reach at
// all on this platform. Nothing here enrolls or removes a finger: the only
// calls made are Identify and the Abort that ends it.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QDBusInterface;
class QTimer;

class FpMon : public QObject
{
    Q_OBJECT
    // Sensor identity read out of sysfs -- present even when no daemon runs.
    Q_PROPERTY(QVariantMap sensor READ sensor NOTIFY sensorChanged)
    Q_PROPERTY(bool daemonPresent READ daemonPresent NOTIFY stateChanged)
    // The daemon's own state word, e.g. FPSTATE_IDLE.
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QStringList fingers READ fingers NOTIFY fingersChanged)
    // True between our Identify and the result that ends it.
    Q_PROPERTY(bool testing READ testing NOTIFY testingChanged)
    // "started" | "match" | "nomatch" | "busy" | "denied" | "error" | ""
    Q_PROPERTY(QString result READ result NOTIFY resultChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    // The daemon's numeric reply to our last call, shown beside our reading of
    // it so the figure and the interpretation never look like the same thing.
    Q_PROPERTY(int replyCode READ replyCode NOTIFY resultChanged)
    Q_PROPERTY(QString replyName READ replyName NOTIFY resultChanged)
    // Live quality hints while a finger is on the reader.
    Q_PROPERTY(QString hint READ hint NOTIFY hintChanged)

public:
    explicit FpMon(QObject *parent = nullptr);

    QVariantMap sensor() const { return m_sensor; }
    bool daemonPresent() const { return m_present; }
    QString state() const { return m_state; }
    QStringList fingers() const { return m_fingers; }
    bool testing() const { return m_testing; }
    QString result() const { return m_result; }
    QString resultText() const { return m_resultText; }
    int replyCode() const { return m_replyCode; }
    QString replyName() const { return m_replyName; }
    QString hint() const { return m_hint; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void startTest();
    Q_INVOKABLE void stopTest();

signals:
    void sensorChanged();
    void stateChanged();
    void fingersChanged();
    void testingChanged();
    void resultChanged();
    void hintChanged();

private slots:
    void onStateChanged(const QString &s);
    void onIdentified(const QString &finger);
    void onVerified();
    void onFailed();
    void onAborted();
    void onErrorInfo(const QString &e);
    void onAcquisitionInfo(const QString &a);

private:
    void readSensor();
    void setResult(const QString &kind, const QString &text);
    void endTest();

    QDBusInterface *m_iface = nullptr;
    QTimer *m_timeout = nullptr;
    QVariantMap m_sensor;
    QStringList m_fingers;
    QString m_state;
    QString m_result;
    QString m_resultText;
    QString m_replyName;
    QString m_hint;
    int m_replyCode = 0;
    bool m_present = false;
    bool m_testing = false;
};
