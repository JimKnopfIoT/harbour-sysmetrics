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

#include <QFile>
#include <QSettings>

#include <cstring>
#include <sys/stat.h>

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

    // The launcher hands the app a umask of 0, so QSettings created its file
    // world-writable -- and that file carries the gauge calibration the
    // milliamp figures rest on. Nothing this app writes concerns anyone but
    // its own user.
    ::umask(0077);

    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

    // A file from an earlier version keeps the mode it was created with, so
    // tighten it once rather than leaving old installs open.
    {
        QSettings s;
        const QString conf = s.fileName();
        if (QFile::exists(conf))
            QFile::setPermissions(conf, QFile::ReadOwner | QFile::WriteOwner);
    }

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

    // First: rows are attributed against the frequencies of their own sample,
    // and the sampler emits the system snapshot before the process list.
    QObject::connect(sampler, &Sampler::systemSampled, &sysmon, &SysMon::onSystem);
    QObject::connect(sampler, &Sampler::systemSampled, &model, &ProcModel::onSystem);
    QObject::connect(sampler, &Sampler::processesSampled, &model, &ProcModel::onProcesses);
    QObject::connect(sampler, &Sampler::processesSampled, &recorder, &Recorder::onProcesses);
    QObject::connect(&sysmon, &SysMon::pauseRequested, sampler, &Sampler::setPaused);
    QObject::connect(&sysmon, &SysMon::intervalRequested, sampler, &Sampler::setIntervalMs);

    // Walking /proc for the process list costs far more than everything else in a
    // tick, and while the app is covered nothing shows that list -- except a
    // running recording, which needs it regardless of what is on screen.
    auto updateProcSampling = [&sysmon, &recorder, sampler]() {
        QMetaObject::invokeMethod(sampler, "setProcessesEnabled", Qt::QueuedConnection,
                                  Q_ARG(bool, sysmon.foreground() || recorder.running()));
    };
    QObject::connect(&sysmon, &SysMon::foregroundChanged, &sysmon, updateProcSampling);
    QObject::connect(&recorder, &Recorder::stateChanged, &recorder, updateProcSampling);

    // A pass over the thermal framework is 134 ms on the Jolla Phone (2026),
    // because each zone read goes out to the part it measures. Only the
    // overview shows them live, so it says when they are worth having.
    auto updateThermalSampling = [&sysmon, sampler]() {
        QMetaObject::invokeMethod(sampler, "setThermalEnabled", Qt::QueuedConnection,
                                  Q_ARG(bool, sysmon.thermalWanted() && sysmon.foreground()));
    };
    QObject::connect(&sysmon, &SysMon::thermalWantedChanged, &sysmon, updateThermalSampling);
    QObject::connect(&sysmon, &SysMon::foregroundChanged, &sysmon, updateThermalSampling);

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
