// CVE lookup for the self-built Ultimate variant. See cvelookup.h.
#include "cvelookup.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>

CveLookup::CveLookup(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_current(0)
    , m_busy(false)
{
    connect(m_nam, &QNetworkAccessManager::finished, this, &CveLookup::onFinished);
    fetchKev();
}

void CveLookup::fetchKev()
{
    QNetworkRequest req(QUrl("https://www.cisa.gov/sites/default/files/"
                             "feeds/known_exploited_vulnerabilities.json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, "harbour-sysmetrics");
    m_nam->get(req);
}

void CveLookup::search(const QString &terms)
{
    // supersede any running search so the previous query's result can't land
    // in the new router's view
    if (m_current) {
        m_current->abort();
        m_current = 0;
    }
    m_results.clear();
    const QString t = terms.trimmed();
    if (t.isEmpty()) {
        m_busy = false;
        m_status = tr("No model to search for");
        emit changed();
        return;
    }
    // ENISA EUVD search API (JSON, no key, reachable where NVD is Cloudflare-blocked)
    QUrl url("https://euvdservices.enisa.europa.eu/api/search");
    QUrlQuery q;
    q.addQueryItem("text", t);
    q.addQueryItem("size", "40");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "harbour-sysmetrics");
    req.setRawHeader("Accept", "application/json");
    m_busy = true;
    m_status = tr("Searching EUVD for \"%1\"…").arg(t);
    emit changed();
    m_current = m_nam->get(req);
}

void CveLookup::reset()
{
    if (m_current) {
        m_current->abort();
        m_current = 0;
    }
    m_results.clear();
    m_status.clear();
    m_busy = false;
    emit changed();
}

void CveLookup::onFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    const bool isKev = reply->url().toString().contains("known_exploited");

    if (isKev) {
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonArray vulns = QJsonDocument::fromJson(reply->readAll())
                    .object().value("vulnerabilities").toArray();
            for (const QJsonValue &v : vulns)
                m_kev.insert(v.toObject().value("cveID").toString());
            // re-flag results that were already shown before KEV finished
            bool changedAny = false;
            for (int i = 0; i < m_results.size(); ++i) {
                QVariantMap m = m_results[i].toMap();
                const bool k = m_kev.contains(m.value("id").toString());
                if (m.value("kev").toBool() != k) {
                    m.insert("kev", k);
                    m_results[i] = m;
                    changedAny = true;
                }
            }
            if (changedAny)
                emit changed();
        }
        return;
    }

    // CVE search reply: discard anything that isn't the latest request, so a
    // slow or aborted previous search can never overwrite the current view.
    if (reply != m_current)
        return;
    m_current = 0;
    m_busy = false;

    if (reply->error() != QNetworkReply::NoError) {
        m_status = tr("Network error: %1").arg(reply->errorString());
        emit changed();
        return;
    }

    const QByteArray data = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        m_status = tr("Unexpected response from NVD");
        emit changed();
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray items = root.value("items").toArray();

    QRegularExpression cveRe(QStringLiteral("CVE-\\d{4}-\\d{4,7}"));
    QVariantList list;
    for (const QJsonValue &v : items) {
        const QJsonObject it = v.toObject();
        const QString euvdId = it.value("id").toString();
        const QString desc = it.value("description").toString();

        // aliases holds the CVE id (string or array); fall back to the description
        QString aliases;
        const QJsonValue av = it.value("aliases");
        if (av.isString()) aliases = av.toString();
        else if (av.isArray())
            for (const QJsonValue &a : av.toArray()) aliases += a.toString() + QLatin1Char(' ');
        QString cveId = cveRe.match(aliases).captured(0);
        if (cveId.isEmpty()) cveId = cveRe.match(desc).captured(0);

        const double score = it.value("baseScore").toDouble(-1.0);
        const QString severity = score >= 9.0 ? QStringLiteral("CRITICAL")
                               : score >= 7.0 ? QStringLiteral("HIGH")
                               : score >= 4.0 ? QStringLiteral("MEDIUM")
                               : score > 0.0  ? QStringLiteral("LOW")
                                              : QStringLiteral("?");

        const QString id = !cveId.isEmpty() ? cveId : euvdId;
        QVariantMap m;
        m.insert("id", id);
        m.insert("severity", severity);
        m.insert("score", score);
        m.insert("summary", desc);
        m.insert("url", !cveId.isEmpty()
                 ? (QStringLiteral("https://nvd.nist.gov/vuln/detail/") + cveId)
                 : (QStringLiteral("https://euvd.enisa.europa.eu/vulnerability/") + euvdId));
        m.insert("kev", !cveId.isEmpty() && m_kev.contains(cveId));
        m.insert("exploitdb", QStringLiteral("https://www.exploit-db.com/search?cve=") + cveId);
        const double epss = it.value("epss").toDouble(-1.0);
        m.insert("epss", epss);
        QString published = it.value("datePublished").toString();
        if (published.isEmpty())
            published = it.value("published").toString();
        m.insert("published", published);
        list.append(m);
    }

    m_results = list;
    const int total = root.value("total").toInt(list.size());
    m_status = list.isEmpty()
            ? tr("No CVEs found in EUVD")
            : tr("%1 CVE(s) — showing %2").arg(total).arg(list.size());
    emit changed();
}

// Local fix evidence: rpm changelogs on this distro name the CVEs a patch
// addressed, and the package build time bounds what CAN be inside. Both are
// read locally via rpm; results are cached per package.
QVariantMap CveLookup::packageFixInfo(const QString &pkg)
{
    const QString key = pkg.trimmed().toLower();
    if (key.isEmpty())
        return QVariantMap();
    if (m_fixCache.contains(key))
        return m_fixCache.value(key);

    QVariantMap info;
    info.insert(QStringLiteral("installed"), false);

    QProcess q;
    q.start(QStringLiteral("rpm"),
            QStringList() << QStringLiteral("-q")
                          << QStringLiteral("--queryformat") << QStringLiteral("%{NAME}|%{VERSION}-%{RELEASE}|%{BUILDTIME}")
                          << key);
    if (q.waitForFinished(5000) && q.exitCode() == 0) {
        const QList<QByteArray> f = q.readAllStandardOutput().split('|');
        if (f.size() >= 3) {
            info.insert(QStringLiteral("installed"), true);
            info.insert(QStringLiteral("package"), QString::fromUtf8(f[0]));
            info.insert(QStringLiteral("version"), QString::fromUtf8(f[1]));
            info.insert(QStringLiteral("buildTime"), f[2].trimmed().toLongLong());

            QProcess cl;
            cl.start(QStringLiteral("rpm"),
                     QStringList() << QStringLiteral("-q") << QStringLiteral("--changelog") << key);
            QStringList cves;
            if (cl.waitForFinished(10000)) {
                QRegularExpression re(QStringLiteral("CVE-\\d{4}-\\d{4,7}"));
                QRegularExpressionMatchIterator it =
                    re.globalMatch(QString::fromUtf8(cl.readAllStandardOutput()));
                while (it.hasNext()) {
                    const QString id = it.next().captured(0);
                    if (!cves.contains(id))
                        cves << id;
                }
            }
            info.insert(QStringLiteral("cves"), cves);
        }
    }
    m_fixCache.insert(key, info);
    return info;
}
