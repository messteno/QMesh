#ifndef Q3SCELETONITEM_H
#define Q3SCELETONITEM_H

#include <q3painter.h>

class Q3SceletonItem
{
public:
    enum Type
    {
        Base,
        PointConnection,
        Point,
        Circle,
    };

    static const QColor BackgroundColor;
    static const QColor SelectedBackgroundColor;
    static const QColor PenColor;
    static const QColor SelectedPenColor;

    Q3SceletonItem(Type type);
    virtual ~Q3SceletonItem();

    virtual void draw(Q3Painter &painter) = 0;
    virtual qreal distanceTo(const QPointF &pos) const = 0;
    virtual qreal distanceFromBoundaryTo(const QPointF &pos) const = 0;
    virtual QRectF boundingRect() const = 0;
    virtual void move(const QPointF diff) = 0;
    virtual void resize(const QPointF from, const QPointF to);

    bool isSelectable() const;
    void setSelectable(bool selectable);

    bool isSelected() const ;
    void setSelected(bool selected);

    bool isResizable() const;
    void setResizable(bool resizable);

    bool isResizing() const ;
    void setResizing(bool resizing);

    Type type();

    void setBackgroundColor(const QColor &backgroundColor);
    void setSelectedBackgroundColor(const QColor &backgroundColor);

    const QColor &backgroundColor() const;
    const QColor &penColor() const;

protected:
    Type type_;

private:
    bool selected_;
    bool selectable_;
    bool resizing_;
    bool resizable_;
    QColor backgroundColor_;
    QColor selectedBackgroundColor_;
    QColor penColor_;
    QColor selectedPenColor_;
};

#endif // Q3SCELETONITEM_H