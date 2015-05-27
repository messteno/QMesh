#ifndef Q3CONTOUR_H
#define Q3CONTOUR_H

#include <QList>

#include "q3plotdrawable.h"
#include "q3mesh.h"

class Q3ContourLine : public QList<QPointF>
{
public:
    Q3ContourLine();
};

class Q3Contour : public QList<Q3ContourLine>, public Q3PlotDrawable
{
public:
    Q3Contour();
    Q3Contour(bool filled);
    void draw(Q3Painter &painter) const;

    void setColor(const QColor &color);

private:
    bool filled_;
    QColor color_;
};

class Q3ContourGenerator
{
public:
    Q3ContourGenerator(Q3Mesh *mesh, const QVector<qreal> &values);
    ~Q3ContourGenerator();

    Q3Contour createContour(qreal level);
    Q3Contour createFilledContour(qreal lowerLevel, qreal upperLevel);

private:
    void clearVisitedFlags(bool includeBoundaries);
    void findBoundaryLinesFilled(Q3Contour &contour, qreal lowerLevel,
                                 qreal upperLevel);
    void findBoundaryLines(Q3Contour &contour, qreal level);
    void findInteriorLines(Q3Contour &contour, qreal level,
                           bool onUpper, bool filled);
    void followInterior(Q3ContourLine &contourLine, Q3MeshTriangle *&triangle,
                        Q3MeshEdge * &edge,
                        bool endOnBoundary,
                        qreal level,
                        bool onUpper);
    bool followBoundary(Q3ContourLine &contourLine, Q3MeshEdge* &edge,
                        qreal lowerLevel, qreal upperLevel, bool onUpper);
    QPointF edgeInterpolation(const Q3MeshEdge *edge, qreal level);
    Q3MeshEdge* getExitEdge(Q3MeshTriangle *triangle, qreal level, bool onUpper);

    Q3Mesh *mesh_;
    const QVector<qreal> &values_;

    QVector<bool> interiorVisited_;
    QVector<QVector<bool> > boundariesVisited_;
    QVector<bool> boundariesUsed_;
};

class Q3ContourPlot : public Q3PlotDrawable
{
public:
    Q3ContourPlot(Q3Mesh *mesh);
    void createContour(int levels);
    void createContour(QList<qreal> &levelsList);
    void createFilledContour(int levels);
    void draw(Q3Painter &painter) const;

    QVector<qreal>& values();
    void setValues(const QVector<qreal> &values);

    bool clear();

private:
    qreal minValue();
    qreal maxValue();
    QVector<qreal> normalize();

    Q3Mesh *mesh_;
    QVector<Q3Contour> contours_;
    QVector<Q3Contour> filledContours_;
    QVector<qreal> values_;
};

QColor getColour(qreal level);

#endif // Q3CONTOUR_H
