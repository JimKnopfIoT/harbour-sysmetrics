// SysMetrics — system diagnostics for Sailfish OS. Sampling runs in a worker
// thread; the GUI thread receives value-copied snapshots.
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QScopedPointer>
#include <QThread>

#include <sailfishapp.h>

#include "btinfo.h"
#include "detailmon.h"
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

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--root-helper") == 0)
            return rootHelperMain(argc, argv);

    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

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

    QObject::connect(sampler, &Sampler::systemSampled, &sysmon, &SysMon::onSystem);
    QObject::connect(sampler, &Sampler::systemSampled, &model, &ProcModel::onSystem);
    QObject::connect(sampler, &Sampler::processesSampled, &model, &ProcModel::onProcesses);
    QObject::connect(sampler, &Sampler::processesSampled, &recorder, &Recorder::onProcesses);
    QObject::connect(&sysmon, &SysMon::pauseRequested, sampler, &Sampler::setPaused);
    QObject::connect(&sysmon, &SysMon::intervalRequested, sampler, &Sampler::setIntervalMs);

    workerThread.start();

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("sysmon"), &sysmon);
    view->rootContext()->setContextProperty(QStringLiteral("procs"), &proxy);
    view->rootContext()->setContextProperty(QStringLiteral("recorder"), &recorder);
    view->rootContext()->setContextProperty(QStringLiteral("bt"), &bt);
    view->rootContext()->setContextProperty(QStringLiteral("netmon"), &netmon);
    view->rootContext()->setContextProperty(QStringLiteral("rootmon"), RootClient::instance());
    view->rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                            QStringLiteral(SYSMETRICS_VERSION_STRING));
    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/harbour-sysmetrics.qml")));
    view->showFullScreen();

    const int rc = app->exec();
    workerThread.quit();
    workerThread.wait(2000);
    return rc;
}
