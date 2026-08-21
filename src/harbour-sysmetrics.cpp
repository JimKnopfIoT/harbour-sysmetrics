// SysMetrics — system diagnostics for Sailfish OS. Sampling runs in a worker
// thread; the GUI thread receives value-copied snapshots.
#include <QLocale>
#include <QTranslator>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QScopedPointer>
#include <QThread>

#include <sailfishapp.h>

#include "applang.h"
#include "btinfo.h"
#include "detailmon.h"
#include "diagnostics.h"
#ifdef SYSMETRICS_ULTIMATE
#include "cvelookup.h"
#endif
#include "graphitem.h"
#include "sysmetrics_version.h"
#include "netmon.h"
#include "procmodel.h"
#include "recorder.h"
#include "rootclient.h"
#include "roothelper.h"
#include "sampler.h"
#include "sysmon.h"

#include <cstring>

// English mode: an identity translator that answers every lookup with the
// source string. Installed last, it is consulted first and thus overrides any
// locale translator sailfishapp installed — regardless of where that one hangs.
class IdentityTranslator : public QTranslator
{
public:
    using QTranslator::QTranslator;
    QString translate(const char *, const char *sourceText,
                      const char * = nullptr, int = -1) const override
    {
        return QString::fromUtf8(sourceText);
    }
    bool isEmpty() const override { return false; }
};

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--root-helper") == 0)
            return rootHelperMain(argc, argv);

    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

    static AppLang applang;

    qRegisterMetaType<SysSnap>();
    qRegisterMetaType<QVector<ProcSample>>();

    qmlRegisterType<DetailMon>("harbour.sysmetrics", 1, 0, "ProcessDetail");
    qmlRegisterType<GraphItem>("harbour.sysmetrics", 1, 0, "HistoryGraph");

    Sampler *sampler = new Sampler();
    QThread workerThread;
    sampler->moveToThread(&workerThread);
    QObject::connect(&workerThread, &QThread::started, sampler, &Sampler::start);
    QObject::connect(&workerThread, &QThread::finished, sampler, &QObject::deleteLater);

    SysMon sysmon;
    ProcModel model;
    ProcProxy proxy;
    proxy.setSourceModel(&model);
    Recorder recorder;
    BtInfo bt;
    NetMonitor netmon;
    Diagnostics diagnostics;

    QObject::connect(sampler, &Sampler::systemSampled, &sysmon, &SysMon::onSystem);
    QObject::connect(sampler, &Sampler::systemSampled, &model, &ProcModel::onSystem);
    QObject::connect(sampler, &Sampler::processesSampled, &model, &ProcModel::onProcesses);
    QObject::connect(sampler, &Sampler::processesSampled, &recorder, &Recorder::onProcesses);
    QObject::connect(&sysmon, &SysMon::pauseRequested, sampler, &Sampler::setPaused);
    QObject::connect(&sysmon, &SysMon::intervalRequested, sampler, &Sampler::setIntervalMs);

    workerThread.start();

    QScopedPointer<QQuickView> view(SailfishApp::createView());

    // UI language override. Installed last, our translator is consulted first
    // and overrides the locale translator sailfishapp installed in createView().
    // "en" uses an identity translator (source = English); "de" loads the German
    // .qm; "system" leaves sailfishapp's default. Qt 5.6 cannot retranslate a
    // running engine, so a change applies on the next launch.
    {
        QString eff = AppLang::saved();
        if (eff == QLatin1String("system"))
            eff = QLocale::system().name().startsWith(QLatin1String("de"))
                      ? QStringLiteral("de") : QStringLiteral("en");
        if (eff == QLatin1String("en")) {
            app->installTranslator(new IdentityTranslator(app.data()));
        } else if (eff == QLatin1String("de")) {
            QTranslator *tr = new QTranslator(app.data());
            if (tr->load(QStringLiteral("harbour-sysmetrics-de"),
                         SailfishApp::pathTo(QStringLiteral("translations")).toLocalFile()))
                app->installTranslator(tr);
        }
    }

    view->rootContext()->setContextProperty(QStringLiteral("sysmon"), &sysmon);
    view->rootContext()->setContextProperty(QStringLiteral("procs"), &proxy);
    view->rootContext()->setContextProperty(QStringLiteral("recorder"), &recorder);
    view->rootContext()->setContextProperty(QStringLiteral("bt"), &bt);
    view->rootContext()->setContextProperty(QStringLiteral("netmon"), &netmon);
    view->rootContext()->setContextProperty(QStringLiteral("diagnostics"), &diagnostics);
#ifdef SYSMETRICS_ULTIMATE
    // Ultimate only: the "cve" context property is the QML-side feature gate.
    CveLookup cvelookup;
    view->rootContext()->setContextProperty(QStringLiteral("cve"), &cvelookup);
#endif
    view->rootContext()->setContextProperty(QStringLiteral("rootmon"), RootClient::instance());
    view->rootContext()->setContextProperty(QStringLiteral("applang"), &applang);
    view->rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                            QStringLiteral(SYSMETRICS_VERSION_STRING));
    view->rootContext()->setContextProperty(QStringLiteral("appBuildDate"),
                                            QStringLiteral(SYSMETRICS_BUILD_DATE));
    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/harbour-sysmetrics.qml")));
    view->showFullScreen();

    const int rc = app->exec();
    workerThread.quit();
    workerThread.wait(2000);
    return rc;
}
