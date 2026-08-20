#include "graphitem.h"

#include <QPainter>
#include <QPainterPath>

GraphItem::GraphItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

#define SETTER(fn, member, type) \
    void GraphItem::fn(type v) { if (member == v) return; member = v; emit changed(); update(); }
SETTER(setMaxValue, m_max, qreal)
SETTER(setMinSpan, m_minSpan, qreal)
SETTER(setLineWidth, m_lineWidth, qreal)
SETTER(setCapacity, m_capacity, int)
SETTER(setLineColor, m_line, const QColor &)
SETTER(setFillColor, m_fill, const QColor &)
SETTER(setGridColor, m_grid, const QColor &)
#undef SETTER

void GraphItem::setValues(const QVariantList &v)
{
    m_values = v;
    emit changed();
    update();
}

void GraphItem::paint(QPainter *p)
{
    const qreal w = width(), h = height();
    if (w < 4 || h < 4)
        return;

    if (m_grid.alpha() > 0) {
        p->setPen(QPen(m_grid, 1));
        for (int i = 1; i < 4; ++i)
            p->drawLine(QPointF(0, h * i / 4.0), QPointF(w, h * i / 4.0));
    }

    const int n = m_values.size();
    if (n < 2)
        return;

    qreal maxV = m_max;
    if (maxV <= 0) {
        for (const QVariant &v : m_values)
            maxV = qMax(maxV, v.toReal());
        maxV = qMax(maxV * qreal(1.1), m_minSpan);
    }

    const int cap = qMax(m_capacity, n);
    const qreal dx = w / (cap - 1);
    const qreal x0 = w - dx * (n - 1); // newest right-aligned

    QPainterPath line;
    for (int i = 0; i < n; ++i) {
        const qreal x = x0 + dx * i;
        const qreal y = h - qBound<qreal>(0, m_values.at(i).toReal() / maxV, 1) * (h - m_lineWidth) - m_lineWidth / 2;
        if (i == 0)
            line.moveTo(x, y);
        else
            line.lineTo(x, y);
    }

    QPainterPath fill = line;
    fill.lineTo(w, h);
    fill.lineTo(x0, h);
    fill.closeSubpath();
    p->fillPath(fill, m_fill);

    p->setPen(QPen(m_line, m_lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawPath(line);
}
