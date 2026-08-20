// Time-series graph: filled area + line, autoscaling, theme-agnostic colors.
#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QVariantList>

class GraphItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariantList values READ values WRITE setValues NOTIFY changed)
    Q_PROPERTY(qreal maxValue READ maxValue WRITE setMaxValue NOTIFY changed)
    Q_PROPERTY(qreal minSpan READ minSpan WRITE setMinSpan NOTIFY changed)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY changed)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY changed)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY changed)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY changed)
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY changed)

public:
    explicit GraphItem(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    QVariantList values() const { return m_values; }
    void setValues(const QVariantList &v);
    qreal maxValue() const { return m_max; }
    void setMaxValue(qreal m);
    qreal minSpan() const { return m_minSpan; }
    void setMinSpan(qreal m);
    QColor lineColor() const { return m_line; }
    void setLineColor(const QColor &c);
    QColor fillColor() const { return m_fill; }
    void setFillColor(const QColor &c);
    QColor gridColor() const { return m_grid; }
    void setGridColor(const QColor &c);
    qreal lineWidth() const { return m_lineWidth; }
    void setLineWidth(qreal w);
    int capacity() const { return m_capacity; }
    void setCapacity(int c);

signals:
    void changed();

private:
    QVariantList m_values;
    qreal m_max = 0;       // 0 = autoscale
    qreal m_minSpan = 1;
    QColor m_line = QColor(25, 210, 255);
    QColor m_fill = QColor(25, 210, 255, 45);
    QColor m_grid = QColor(255, 255, 255, 18);
    qreal m_lineWidth = 2.5;
    int m_capacity = 180;
};
