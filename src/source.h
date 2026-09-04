#ifndef SOURCE_H
#define SOURCE_H

#include <QByteArray>
#include <QString>
#include <QVector>

// One place a figure might stand: the path, what one raw unit is worth in the
// unit we display, and the window the result has to fall into to be believed.
// A window is not decoration -- vendors put millivolts where the class says
// microvolts, and a scale that is wrong by a thousand still looks like a number.
struct Candidate {
    QString path;
    double  scale;      // raw * scale = the figure, in the target unit
    double  lo, hi;     // plausible range; lo >= hi means "any value"
};

// A figure that different platforms publish in different places, units and
// spellings. Candidates are tried in order, the first plausible one wins and is
// remembered -- so the fallbacks cost one probe and nothing afterwards.
//
// The point of the class is the third state: a Source that nothing answered is
// unknown, not zero. Zero is what turned a missing register into "0 sensors",
// "0 cycles" and a green "100 % health" on phones this app had not been
// measured against.
class Source
{
public:
    Source &add(const QString &path, double scale = 1.0, double lo = 0, double hi = 0);
    Source &add(const QString &dir, const char *leaf, double scale = 1.0,
                double lo = 0, double hi = 0);

    bool    read(double *out) const;                 // false: nothing answered
    double  value(double fallback = 0) const;
    QString text() const;                            // the raw string, trimmed
    QString from() const { return m_from; }          // which path answered
    bool    known() const { return m_pick >= 0; }
    void    forget() { m_pick = -2; m_from.clear(); }
    bool    empty() const { return m_cand.isEmpty(); }

private:
    bool tryOne(const Candidate &c, double *out) const;

    QVector<Candidate> m_cand;
    mutable int     m_pick = -2;    // -2 not probed, -1 probed, nothing answered
    mutable QString m_from;
};

// One open/read/close. The detail pages read hundreds of these; QFile sets up
// an I/O engine and a buffer for every one.
QByteArray readNode(const QString &path);

#endif // SOURCE_H
