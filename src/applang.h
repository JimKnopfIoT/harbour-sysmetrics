// Persisted UI language override ("system" | "de" | "en"). Applied at startup
// by steering the locale before the app is created; Qt 5.6 has no runtime
// retranslate, so a change takes effect on the next launch.
#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

class AppLang : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
    explicit AppLang(QObject *parent = nullptr) : QObject(parent) { m_lang = saved(); }

    QString language() const { return m_lang; }
    void setLanguage(const QString &v)
    {
        if (v == m_lang)
            return;
        m_lang = v;
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("harbour-sysmetrics"), QStringLiteral("harbour-sysmetrics"));
        s.setValue(QStringLiteral("language"), v);
        s.sync();
        emit languageChanged();
    }

    static QString saved()
    {
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("harbour-sysmetrics"), QStringLiteral("harbour-sysmetrics"));
        return s.value(QStringLiteral("language"), QStringLiteral("system")).toString();
    }

signals:
    void languageChanged();

private:
    QString m_lang;
};
