/*
  harbour-sysmetrics — readtest.cpp
  Copyright (C) 2026  harbour-sysmetrics contributors — GPLv3 or later.
*/
#include "readtest.h"

#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QVector>

#include <algorithm>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

// One mebibyte per read. Small enough that a slow card still answers within a
// blink, large enough that the per-call overhead does not show up in the
// figure.
const int kBlock = 1024 * 1024;

// The raw block device would be the cleaner thing to read, and it is out of
// reach: /dev/mmcblk* belongs to root and the disk group, and the user this
// app runs as is in neither. So the measurement reads a file that already
// exists on the medium.
QString largestFile(const QString &mountPoint, qint64 *sizeOut, int budgetMs)
{
    QElapsedTimer clock;
    clock.start();
    QString best;
    qint64 bestSize = 0;

    QDirIterator it(mountPoint, QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        // A scan over a full card must not become the slow part of the test.
        if (clock.elapsed() > budgetMs)
            break;
        const qint64 sz = it.fileInfo().size();
        if (sz > bestSize && it.fileInfo().isReadable()) {
            bestSize = sz;
            best = it.filePath();
        }
    }
    if (sizeOut)
        *sizeOut = bestSize;
    return best;
}

} // namespace

ReadTest::ReadTest(QObject *parent)
    : QObject(parent)
{
}

QVariantMap ReadTest::findSample(const QString &mountPoint) const
{
    QVariantMap m;
    qint64 size = 0;
    const QString path = largestFile(mountPoint, &size, 1500);
    m.insert(QStringLiteral("path"), path);
    m.insert(QStringLiteral("size"), (double)size);
    m.insert(QStringLiteral("ok"), !path.isEmpty() && size >= 4 * 1024 * 1024);
    if (path.isEmpty())
        m.insert(QStringLiteral("error"), tr("Nothing readable found here."));
    else if (size < 4 * 1024 * 1024)
        m.insert(QStringLiteral("error"),
                 tr("The largest file here is under 4 MiB. That is too little to "
                    "measure against — a figure from it would say more about the "
                    "first access than about the medium."));
    return m;
}

QVariantMap ReadTest::measure(const QString &filePath, int mib, int runs)
{
    QVariantMap r;
    r.insert(QStringLiteral("path"), filePath);
    r.insert(QStringLiteral("blockKiB"), kBlock / 1024);
    mib = qBound(1, mib, 512);

    m_busy = true;
    emit changed();

    // O_DIRECT hands the read straight to the medium. Without it the second
    // run of the same test measures the page cache and reports a number that
    // has nothing to do with the card.
    bool direct = true;
    int fd = ::open(filePath.toLocal8Bit().constData(), O_RDONLY | O_DIRECT);
    if (fd < 0 && errno == EINVAL) {
        // Some filesystems refuse O_DIRECT. Then the figure is still worth
        // having, but it must not be presented as if it came from the medium.
        direct = false;
        fd = ::open(filePath.toLocal8Bit().constData(), O_RDONLY);
    }
    if (fd < 0) {
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"),
                 tr("Could not open the file: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        m_busy = false;
        m_result = r;
        emit changed();
        return r;
    }
    ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);

    // O_DIRECT wants an aligned buffer; the page size is always enough.
    void *buf = nullptr;
    const size_t align = (size_t)::sysconf(_SC_PAGESIZE);
    if (::posix_memalign(&buf, align, kBlock) != 0 || !buf) {
        ::close(fd);
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), tr("Out of memory for the read buffer."));
        m_busy = false;
        m_result = r;
        emit changed();
        return r;
    }

    // Several passes over the same bytes. One pass cannot tell a slow medium
    // from a moment when something else was reading; the spread between passes
    // is the only thing on hand that says whether the figure means anything.
    const qint64 want = (qint64)mib * kBlock;
    runs = qBound(1, runs, 9);
    QVector<double> rates;
    qint64 got = 0;
    double seconds = 0;
    for (int pass = 0; pass < runs; ++pass) {
        ::lseek(fd, 0, SEEK_SET);
        ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        qint64 passGot = 0;
        QElapsedTimer clock;
        clock.start();
        while (passGot < want) {
            const ssize_t n = ::read(fd, buf, kBlock);
            if (n <= 0)
                break;                 // end of file, or the medium gave up
            passGot += n;
        }
        const double passSec = clock.nsecsElapsed() / 1e9;
        if (passGot > 0 && passSec > 0)
            rates.append((passGot / 1e6) / passSec);
        got = passGot;
        seconds = passSec;
    }
    ::free(buf);
    ::close(fd);

    r.insert(QStringLiteral("bytes"), (double)got);
    r.insert(QStringLiteral("seconds"), seconds);
    r.insert(QStringLiteral("direct"), direct);
    r.insert(QStringLiteral("runs"), rates.size());
    r.insert(QStringLiteral("ok"), !rates.isEmpty());
    if (!rates.isEmpty()) {
        QVector<double> sorted = rates;
        std::sort(sorted.begin(), sorted.end());
        const double median = sorted.at(sorted.size() / 2);
        const double lo = sorted.first(), hi = sorted.last();
        r.insert(QStringLiteral("mbPerSecond"), median);
        r.insert(QStringLiteral("slowest"), lo);
        r.insert(QStringLiteral("fastest"), hi);
        r.insert(QStringLiteral("spreadPercent"), median > 0 ? (hi - lo) / median * 100.0 : 0.0);
    }
    if (!direct)
        r.insert(QStringLiteral("note"),
                 tr("This filesystem refused a direct read, so the figure passed "
                    "through the page cache and is a best case rather than the "
                    "speed of the medium."));
    else if (got < want)
        r.insert(QStringLiteral("note"),
                 tr("The file ended before the requested amount was read; the "
                    "figure covers what was there."));

    m_busy = false;
    m_result = r;
    emit changed();
    return r;
}

QVariantMap ReadTest::rate(double mbPerSecond, double spreadPercent,
                           double referenceMbPerSecond) const
{
    QVariantMap m;

    // First: is the figure worth reading at all? Three passes over the same
    // bytes on an idle device land within a few percent of each other. Ten
    // percent is the line drawn here, and it is drawn by this app, not by any
    // standard - a wider spread means something else was using the medium, and
    // then the number describes the moment rather than the medium.
    const bool reliable = spreadPercent <= 10.0;
    m.insert(QStringLiteral("reliable"), reliable);
    m.insert(QStringLiteral("basis"),
             reliable
                 ? tr("Three passes agreed to within %1 %. Below 10 % — a threshold this app sets, not a standard — the passes are close enough that the figure describes the medium and not the moment.")
                       .arg(spreadPercent, 0, 'f', 1)
                 : tr("The three passes differed by %1 %. Above 10 % — a threshold this app sets, not a standard — something else was using the medium while measuring, and the figure says more about that than about the medium. Measure again on an idle device.")
                       .arg(spreadPercent, 0, 'f', 1));

    // Second: the only comparison available without inventing a number. The
    // same measurement on this device's built-in storage. No published figure
    // fits one stream at queue depth one, so the catalogue speed of the
    // interface would flatter every medium alike.
    if (referenceMbPerSecond > 0 && mbPerSecond > 0) {
        const double ratio = mbPerSecond / referenceMbPerSecond * 100.0;
        m.insert(QStringLiteral("ratioPercent"), ratio);
        m.insert(QStringLiteral("verdict"),
                 tr("%1 % of what the built-in storage of this phone reaches under the same measurement (%2 MB/s).")
                     .arg(ratio, 0, 'f', 0).arg(referenceMbPerSecond, 0, 'f', 0));
    } else {
        m.insert(QStringLiteral("verdict"),
                 tr("There is nothing to compare against yet. Measure the built-in storage as well and the two figures can be held against each other — both taken the same way, on the same phone, which is the only comparison here that is not borrowed from a catalogue."));
    }
    return m;
}
