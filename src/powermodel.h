// Splits the CPU's share of the measured drain: energy model for the ratios,
// gauge for the scale -- the model counts core power only, the rest is in that.
#pragma once

#include <QObject>
#include <QVector>

#include "sampler.h"

class PowerModel : public QObject
{
    Q_OBJECT
    // False means the page shows shares, never a made-up figure.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // True once the gauge itself has yielded the scale; false while the figures
    // rest on the energy model alone, which is a floor.
    Q_PROPERTY(bool scaleFromGauge READ calibrated NOTIFY changed)
    Q_PROPERTY(QString basisText READ basisText NOTIFY changed)
    Q_PROPERTY(double cpuMilliAmp READ cpuMilliAmp NOTIFY changed)
    Q_PROPERTY(double totalMilliAmp READ totalMilliAmp NOTIFY changed)
    Q_PROPERTY(double spread READ spread NOTIFY changed)
    Q_PROPERTY(int samples READ samples NOTIFY changed)
    Q_PROPERTY(int calibratedAgoSec READ calibratedAgoSec NOTIFY changed)

public:
    explicit PowerModel(QObject *parent = nullptr);

    // Where the weights came from, for the page to state.
    enum Basis { NoBasis, EnergyModel, CpuCapacity, Frequency };

    QString basisText() const;
    int calibratedAgoSec() const;

    // One system sample. Returns the model load (weight units) of that instant.
    double observe(const SysSnap &snap);

    // Weight of one core running flat out at the given frequency.
    double coreWeight(int cpu, int freqKhz) const;

    // Model load -> milliwatts. The energy model states real power per state,
    // so this needs no calibration; it is a floor, see the .cpp.
    double toMilliWatt(double modelLoad) const;
    // ... and to milliamps, through the cell voltage of the moment.
    double toMilliAmp(double modelLoad, double volts) const;

    bool calibrated() const { return m_scale > 0; }
    bool available() const { return m_scale > 0 || m_unitMilliWatt > 0; }
    Basis basis() const { return m_basis; }
    int samples() const { return m_ratios.size(); }
    double scale() const { return m_scale; }
    // Quartile spread relative to the median: the error bar on every figure.
    double spread() const { return m_spread; }
    qint64 lastCalibratedMs() const { return m_calibratedMs; }
    double cpuMilliAmp() const { return m_cpuMilliAmp; }
    double totalMilliAmp() const { return m_totalMilliAmp; }

    void load();
    void save() const;

public slots:
    void onSystem(const SysSnap &snap);

signals:
    void changed();

private:
    void readWeights();
    void recompute();

    struct Domain {
        QVector<int> cpus;
        QVector<QPair<int, double>> states;   // freq kHz -> power, ascending
        double flat = 0;                      // fallback weight, no table
    };
    QVector<Domain> m_domains;
    Basis m_basis = NoBasis;
    // cs:<freq> counts milliwatts (kernels to 5.x), ps:<freq> microwatts (6.x).
    double m_unitMilliWatt = 0;   // 0 = the basis carries no real power at all

    // Smoothed before pairing: one gauge reading is noisier than the step.
    static const int kSmooth = 8;
    static const int kMinPairs = 40;
    static const qint64 kPairMinMs = 10000;
    static const qint64 kPairMaxMs = 20000;
    // Above this spread the scale is not quoted as milliamps at all.
    static constexpr double kMaxSpread = 0.35;

    struct Obs { qint64 ms; double load; double mA; };
    QVector<Obs> m_hist;
    QVector<double> m_ratios;
    double m_scale = -1;
    double m_spread = -1;
    qint64 m_calibratedMs = 0;
    double m_cpuMilliAmp = -1;
    double m_totalMilliAmp = -1;
};
