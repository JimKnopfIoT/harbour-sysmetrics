// Known-issue diagnostics: read-only checks that recognise documented device
// problems and point to their fixes. Informs, never repairs.
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Diagnostics : public QObject
{
    Q_OBJECT
public:
    explicit Diagnostics(QObject *parent = nullptr);

    // One-shot run. cpuPct/load1 come from the live SysMon snapshot so the
    // load-vs-CPU check does not need its own sampling delay.
    Q_INVOKABLE QVariantList run(double cpuPct, double load1) const;

private:
    void checkCpuVulnerabilities(QVariantList &out) const;
    void checkCpuGovernor(QVariantList &out) const;
    void checkFreqResidency(QVariantList &out) const;
    void checkTouchBoost(QVariantList &out) const;
    void checkGpuFloor(QVariantList &out) const;
    void checkLoadVsCpu(QVariantList &out, double cpuPct, double load1) const;
    void checkCameraProvider(QVariantList &out) const;
    void checkMicGain(QVariantList &out) const;
    void checkMtkHotplug(QVariantList &out) const;
    void checkBtAdapters(QVariantList &out) const;
    void checkWlanRadio(QVariantList &out) const;
};
