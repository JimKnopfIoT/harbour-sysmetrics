// CVE lookup for the self-built Ultimate variant: free-text search against
// the ENISA EU Vulnerability Database (EUVD) API (free, no key, reachable
// from the device — unlike NVD, which Cloudflare-blocks), cross-flagged
// against the CISA Known-Exploited (KEV) catalog. The query is seeded from
// the device's own identity (kernel, SoC, Android base).
#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QSet>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class CveLookup : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit CveLookup(QObject *parent = nullptr);

    QVariantList results() const { return m_results; }
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }

    Q_INVOKABLE void search(const QString &terms);

    // Clear results and status — every CVE page opens empty, never showing
    // a previous context's query.
    Q_INVOKABLE void reset();

    // Local fix evidence for one installed package: CVE ids named in its rpm
    // changelog (verifiably fixed here) plus its build time (a CVE published
    // AFTER the build cannot be fixed inside). Cached per package.
    Q_INVOKABLE QVariantMap packageFixInfo(const QString &pkg);

signals:
    void changed();

private slots:
    void onFinished(QNetworkReply *reply);

private:
    void fetchKev();

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_current;   // in-flight CVE search (for cancel/supersede)
    QVariantList m_results;
    QString m_status;
    bool m_busy;
    QSet<QString> m_kev;   // CVE IDs in the CISA Known-Exploited catalog
    QHash<QString, QVariantMap> m_fixCache;  // package → fix info
};
