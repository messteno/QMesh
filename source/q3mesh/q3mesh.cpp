#include <QTextStream>
#include <QDebug>
#include <QPolygonF>
#include <QFile>
#include <QPixmap>
#include <QTime>
#include <qmath.h>

#include "q3contour.h"
#include "q3mesh.h"

const int Q3Mesh::maxStreamIterationsCount = 2000;
const qreal Q3Mesh::maxStreamError = 1e-8;

Q3Mesh::Q3Mesh(QWidget *parent) :
    Q3PlotDrawable(),
    QWidget(parent),
    square_(0),
    edgeSquare_(0),
    angles_(0),
    drawPolicy_(DrawTriangles),
    obtuseTrianglesCount_(0)
{

}

Q3Mesh::~Q3Mesh()
{

}

QList<Q3MeshNode *>& Q3Mesh::nodes()
{
    return nodes_;
}

QList<Q3MeshEdge *>& Q3Mesh::edges()
{
    return edges_;
}

QList<Q3MeshTriangle *>& Q3Mesh::triangles()
{
    return triangles_;
}

Q3MeshNode* Q3Mesh::addNode(QPointF &point)
{
    Q3MeshNode *node = new Q3MeshNode(point, nodes_.count());
    nodes_.append(node);
    return node;
}

Q3MeshNode* Q3Mesh::addNode(qreal x, qreal y)
{
    Q3MeshNode *node = new Q3MeshNode(QPointF(x, y), nodes_.count());
    nodes_.append(node);
    return node;
}

Q3MeshEdge* Q3Mesh::addEdge(Q3MeshNode *a, Q3MeshNode *b, Q3Boundary *boundary)
{
    Q3MeshEdge *edge = a->edgeAdjacentTo(b);
    if (edge)
        return edge;

    // id доолжен совпадать с индексом в списке
    edge = new Q3MeshEdge(a, b, boundary, edges_.count());
    edges_.append(edge);
    a->addEdge(edge);
    b->addEdge(edge);

    return edge;
}

Q3MeshTriangle* Q3Mesh::addTriangle(Q3MeshEdge *a, Q3MeshEdge *b, Q3MeshEdge *c)
{
    Q3MeshTriangle *triangle = new Q3MeshTriangle(a, b, c, triangles_.count());
    triangles_.append(triangle);

    return triangle;
}

void Q3Mesh::draw(Q3Painter &painter) const
{
    if (drawPolicy_ & DrawEdges || drawPolicy_ & DrawBorders)
        drawEdges(painter);

    if (drawPolicy_ & DrawTriangles)
        drawTriangles(painter);

    qreal scaleX = painter.sx();
    qreal scaleY = painter.sy();

    if (drawPolicy_ & DrawVelocity)
    {
        painter.setPen(QColor(122, 163, 39));
        for (int trIndex = 0; trIndex < triangles_.count(); ++trIndex)
        {
            Q3MeshTriangle *triangle = triangles_.at(trIndex);
            QPointF begin = triangle->center();
            begin = QPointF(begin.x() * scaleX, begin.y() * scaleY);
            QPointF end = (QVector2D(triangle->center())
                           + 0.1 * triangle->predictorVelocity()).toPointF();
            end = QPointF(end.x() * scaleX, end.y() * scaleY);
            painter.drawLine(begin, end);
//            painter.drawText(triangle->center().x() * scaleX,
//                             triangle->center().y() * scaleY,
//                             QString::number(triangle->stream(), 'd', 6));
        }

        painter.setPen(QColor(22, 63, 188));
        for (int eInd = 0; eInd < edges_.count(); ++eInd)
        {
            Q3MeshEdge *edge = edges_.at(eInd);
            if (!edge->boundary())
                continue;
            QPointF begin = edge->center();
            begin = QPointF(begin.x() * scaleX, begin.y() * scaleY);
            QPointF end = (QVector2D(edge->center())
                           + 0.1 * edge->velocity()).toPointF();
            end = QPointF(end.x() * scaleX, end.y() * scaleY);
            painter.drawLine(begin, end);
//            painter.drawText(triangle->center().x() * scaleX,
//                             triangle->center().y() * scaleY,
//                             QString::number(triangle->stream(), 'd', 6));
        }

//        for (int nodeIndex = 0; nodeIndex < nodeValues_.count(); ++nodeIndex)
//        {
//            Q3MeshNode *node = nodes_.at(nodeIndex);
//            painter.drawText(node->x() * scaleX,
//                             node->y() * scaleY,
//                             QString::number(nodeValues_.at(nodeIndex), 'd', 6));
//        }
    }
}

void Q3Mesh::drawEdges(Q3Painter &painter) const
{
    qreal scaleX = painter.sx();
    qreal scaleY = painter.sy();

    QListIterator<Q3MeshEdge *> eit(edges_);

    while(eit.hasNext())
    {
        const Q3MeshEdge *edge = eit.next();
        if (!(drawPolicy_ & DrawEdges) && !edge->boundary())
            continue;

        QColor color;
        if (edge->label() != 0)
            color.setHsv((73 * edge->label()) % 360, 230, 180);
        else
            color = QColor(Qt::white);

        painter.setPen(color);

        painter.drawLine(edge->a()->x() * scaleX, edge->a()->y() * scaleY,
                         edge->b()->x() * scaleX, edge->b()->y() * scaleY);
    }
}

void Q3Mesh::drawTriangles(Q3Painter &painter) const
{
    qreal scaleX = painter.sx();
    qreal scaleY = painter.sy();

    // Возможно переделать на count циклы
    QListIterator<Q3MeshTriangle *> tit(triangles_);
    while(tit.hasNext())
    {
        const Q3MeshTriangle *triangle = tit.next();

//        if (triangle->isBad())
//            painter.setBrush(QColor(163, 39, 39));
//        else
//            painter.setBrush(QColor(122, 163, 39));

        QPolygonF polygon = triangle->toPolygonF(scaleX, scaleY);
        painter.drawPolygon(polygon);
    }
}

void Q3Mesh::calcStream()
{
    calcVorticity();

    for (int trInd = 0; trInd < triangles_.count(); ++trInd)
    {
        Q3MeshTriangle *triangle = triangles_.at(trInd);
        triangle->setTempStream(triangle->stream());
    }

    int iterationsCount = 0;
    qreal streamDelta = 0;
    while (iterationsCount < maxStreamIterationsCount)
    {
        streamDelta = 0;
        for (int trInd = 0; trInd < triangles_.count(); ++trInd)
        {
            Q3MeshTriangle *triangle = triangles_.at(trInd);
            qreal A = 0;
            qreal B = 0;

            for (int eInd = 0; eInd < triangle->edges().size(); ++eInd)
            {
                Q3MeshEdge *edge = triangle->edges().at(eInd);
                Q3MeshTriangle *adjTriangle = triangle->adjacentTriangles().at(eInd);
                QVector2D normal = triangle->normalVectors().at(eInd);

                if (adjTriangle)
                {
                    qreal dL = triangle->distanceToTriangles().at(eInd);

                    QVector2D tAt(adjTriangle->center() - triangle->center());
                    tAt.normalize();

                    qreal cosin = qAbs(QVector2D::dotProduct(tAt, normal));
                    A += edge->length() / dL / cosin;
                    B += edge->length() / dL * adjTriangle->stream() / cosin;
                }
                else
                {
                    qreal deltaB = edge->processBoundaryStream();
                    B += deltaB;
                }
            }
            qreal diff = (triangle->omega() + B) / A - triangle->tempStream();
            triangle->setTempStream(triangle->tempStream() + diff);
            qreal trStreamDelta = qAbs(diff);
            if (trStreamDelta > streamDelta)
                streamDelta = trStreamDelta;
        }

        for (int trInd = 0; trInd < triangles_.count(); ++trInd)
        {
            Q3MeshTriangle *triangle = triangles_.at(trInd);
            triangle->setStream(triangle->tempStream());
        }

        if (streamDelta < maxStreamError)
            break;
        iterationsCount++;
    }
}

void Q3Mesh::calcVorticity()
{
    for (int trInd = 0; trInd < triangles_.count(); ++trInd)
    {
        Q3MeshTriangle *triangle = triangles_.at(trInd);
        qreal dvXByY = 0;
        qreal dvYByX = 0;

        for (int eInd = 0; eInd < triangle->edges().count(); ++eInd)
        {
            Q3MeshEdge *edge = triangle->edges().at(eInd);
            Q3MeshTriangle *adjTriangle = triangle->adjacentTriangles().at(eInd);
            if (adjTriangle)
            {
                qreal dL = triangle->distanceToTriangles().at(eInd);
                qreal dl = triangle->distancesToEdges().at(eInd);
                QVector2D vv = (triangle->correctorVelocity() * (dL - dl)
                                + adjTriangle->correctorVelocity() * dl) / dL;
                dvXByY += edge->length() * vv.x()
                          * triangle->normalVectors().at(eInd).y();
                dvYByX += edge->length() * vv.y()
                          * triangle->normalVectors().at(eInd).x();
            }
            else
            {
                edge->processBoundaryOmega(dvXByY, dvYByX);
            }
        }
        triangle->setOmega(dvXByY - dvYByX);
    }
}

void Q3Mesh::clear()
{
    qDeleteAll(nodes_.begin(), nodes_.end());
    nodes_.clear();
    qDeleteAll(edges_.begin(), edges_.end());
    edges_.clear();
    qDeleteAll(triangles_.begin(), triangles_.end());
    triangles_.clear();

    boundaries_.clear();

    square_ = 0;
    angles_ = 0;
    edgeSquare_ = 0;
    obtuseTrianglesCount_ = 0;
}

QString Q3Mesh::info()
{
    QString out;
    QTextStream stream(&out);
    stream << tr("Количество узлов:  ")
           << QString::number(nodes_.count()) << "\n";
    stream << tr("Количество ребер:  ")
           << QString::number(edges_.count()) << "\n";
    stream << tr("Количество тр-в:   ")
           << QString::number(triangles_.count()) << "\n";
    stream << tr("Площадь:           ")
           << QString::number(square_) << "\n";
    stream << tr("Площадь (ребра):   ")
           << QString::number(edgeSquare_) << "\n";
    stream << tr("Кол-во границ:     ")
           << QString::number(boundaries_.count()) << "\n";
    stream << tr("Проверка углов:    ")
           << QString::number(angles_) << "\n";
    stream << tr("Тупоугольных тр-в: ")
           << QString::number((100. * obtuseTrianglesCount_) / triangles_.count())
           << "\n";
    return out;
}

qreal Q3Mesh::square()
{
    return square_;
}

Q3Mesh::EdgeBoundaries &Q3Mesh::boundaries()
{
    return boundaries_;
}

void Q3Mesh::setDrawPolicy(const uint &drawPolicy)
{
    drawPolicy_ = drawPolicy;
}

void Q3Mesh::setNodeValues(const QVector<qreal> &nodeValues)
{
    nodeValues_ = nodeValues;
}

QRectF Q3Mesh::boundingRect() const
{
    return boundingRect_;
}

void Q3Mesh::update()
{
    square_ = 0;
    obtuseTrianglesCount_ = 0;
    foreach (Q3MeshTriangle *triangle, triangles_)
    {
        square_ += triangle->square();
        if (triangle->isBad())
            obtuseTrianglesCount_++;
    }

    angles_ = 0;
    edgeSquare_ = 0;
    foreach (Q3MeshEdge *edge, edges_)
    {
        foreach (qreal contangent, edge->adjacentCotangents())
        {
            qreal actg = qAtan(contangent);
            actg = M_PI_2 - actg;
            actg = actg * 180. / M_PI;
            angles_ += actg;
        }

        edgeSquare_ += edge->adjacentSquare();
    }
    angles_ /= 2. * triangles_.count();

    boundingRect_ = QRectF(*nodes_[0], *nodes_[0]);
    foreach (Q3MeshNode *node, nodes_)
    {
        if (node->x() < boundingRect_.left())
            boundingRect_.setLeft(node->x());
        else if (node->x() > boundingRect_.right())
            boundingRect_.setRight(node->x());

        if (node->y() > boundingRect_.bottom())
            boundingRect_.setBottom(node->y());
        else if (node->y() < boundingRect_.top())
            boundingRect_.setTop(node->y());
    }
    Q_ASSERT(qAbs(angles_ - 180) < 1e-4);
}
