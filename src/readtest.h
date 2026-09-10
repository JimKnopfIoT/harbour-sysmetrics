/*
  harbour-sysmetrics — readtest.h
  Copyright (C) 2026  harbour-sysmetrics contributors — GPLv3 or later.

  How fast a storage device actually reads.

  No card and no filesystem reports its own speed: the class printed on a
  microSD lives in a register the kernel does not export, and everything else
  in sysfs describes the bus, not the medium. A figure can therefore only be
  measured — and this class measures it the one way that is honest on a phone:
  by reading a file that is already there, with the page cache bypassed, and
  stating what it read and under which conditions.

  It never writes. Writing would cost the card cells and, on a raw device,
  could destroy a partition table for a number nobody needs.
*/
#ifndef READTEST_H
#define READTEST_H

#include <QObject>
#include <QString>
#include <QVariantMap>

class ReadTest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)

public:
    explicit ReadTest(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QVariantMap result() const { return m_result; }

    // The largest readable file under a mount point, and its size. Without one
    // there is nothing to measure: an empty card holds no data to read, and
    // this class will not create any.
    Q_INVOKABLE QVariantMap findSample(const QString &mountPoint) const;

    // Read up to mib mebibytes of that file and time it, runs times over.
    // Repeating is what makes the figure worth anything: a single run cannot
    // tell a slow medium from a busy moment, and the spread between runs says
    // whether the number can be trusted at all.
    // Result keys: ok, error, path, bytes, seconds, mbPerSecond (the median),
    // fastest, slowest, spreadPercent, runs, blockKiB, direct, note.
    Q_INVOKABLE QVariantMap measure(const QString &filePath, int mib = 64, int runs = 3);

    // Reading of a figure, with the basis named. Keys: verdict, basis,
    // reliable. Never a school grade: the comparison is against this device's
    // own internal storage, measured the same way, because no published figure
    // exists for one stream at queue depth one.
    Q_INVOKABLE QVariantMap rate(double mbPerSecond, double spreadPercent,
                                 double referenceMbPerSecond) const;

signals:
    void changed();

private:
    bool m_busy = false;
    QVariantMap m_result;
};

#endif // READTEST_H
