#include <QDebug>
#include <QTime>

#include <qmath.h>

#include "q3calc.h"
#include "conjugategradient.h"
#include "bicgstablinearsolver.h"
#include "preconditioner.h"

const int Q3Calc::maxPredictorIterationsCount = 1000;
const qreal Q3Calc::maxPredictorError = 1e-6;

// Возможно переделать геттеры в треугольниках на ссылки

Q3Calc::Q3Calc(Q3Mesh &mesh, qreal tau, qreal Re, QObject *parent) :
    QThread(parent),
    mesh_(mesh),
    tau_(tau),
    Re_(Re),
    residual_(0),
    time_(0),
    started_(false),
    abort_(false),
    badTriangleFix_(false),
    monotoneTerm_(true),
    calcTime_(0)
{
}

Q3Calc::~Q3Calc()
{
    abort_ = true;
    wait();
}

void Q3Calc::run()
{
    calcTimer_.start();

    started_ = true;
    abort_ = false;
    prepare();
    while(!abort_)
    {
//        QTime timer;
//        timer.start();
        predictor();
//        qDebug() << "Predictor time:" << timer.elapsed();
//        timer.start();
        corrector();
//        qDebug() << "Corrector time: " << timer.elapsed();
        calcFaithfulResidualNS();
        calcFaithfulResidualDiv();
        time_ += tau_;
        emit updateInfo();

//        QFile pr("/home/mesteno/pr.txt");
//        if (pr.open(QFile::WriteOnly | QFile::Truncate))
//        {
//            QTextStream out(&pr);
//            for (int trIndex = 0; trIndex < mesh_.triangles().count(); ++trIndex)
//            {
//                Q3MeshTriangle *triangle = mesh_.triangles().at(trIndex);
//                out << trIndex << " "
//                    << QString::number(triangle->predictorVelocity().x(), 'd', 6) << " "
//                    << QString::number(triangle->predictorVelocity().y(), 'd', 6) << " "
//                    << "\n";
//            }
//            pr.close();
//        }

//        QFile co("/home/mesteno/co.txt");
//        if (co.open(QFile::WriteOnly | QFile::Truncate))
//        {
//            QTextStream out(&co);
//            for (int trIndex = 0; trIndex < mesh_.triangles().count(); ++trIndex)
//            {
//                Q3MeshTriangle *triangle = mesh_.triangles().at(trIndex);
//                out << trIndex << " "
//                    << QString::number(triangle->correctorVelocity().x(), 'd', 6) << " "
//                    << QString::number(triangle->correctorVelocity().y(), 'd', 6) << " "
//                    << "\n";
//            }
//            co.close();
//        }

//        break;
    }
    calcTime_ += calcTimer_.elapsed();
}

void Q3Calc::prepare()
{
    // TODO:
    int edgesCount = mesh_.edges().count();

    AN_.clear();
    JA_.clear();
    IA_.clear();
    BN_.clear();
    XN_.clear();
    TN_.clear();

    AN_.fill(0, 3 * edgesCount);
    JA_.fill(0, 3 * edgesCount);
    IA_.fill(0, edgesCount + 1);
    BN_.fill(0, edgesCount);
    XN_.fill(0, edgesCount);
    TN_.fill(0, 4 * edgesCount);

    int anIndex = 0;
    int boundaryCount = 0;
    for (int edgeIndex = 0; edgeIndex < edgesCount; ++edgeIndex)
    {
        Q3MeshEdge *edge = mesh_.edges().at(edgeIndex);

        edge->processBoundaryVelocity();

        // Возможно вынести в соответствующий boundarytype-класс
        if (edge->boundary())
        {
            boundaryCount++;
            if (edge->boundary()->type()->toEnum() == Q3BoundaryType::OutBoundary)
            {
                AN_[anIndex] = 1.;
                JA_[anIndex] = edgeIndex;
                IA_[edgeIndex] = anIndex;
                anIndex++;
                continue;
            }
        }

        for (int cotIndex = 0; cotIndex < edge->adjacentCotangents().count();
             ++cotIndex)
        {
            AN_[anIndex] += edge->adjacentCotangents().at(cotIndex);
        }

        AN_[anIndex] *= 2. * tau_;
        JA_[anIndex] = edgeIndex;
        IA_[edgeIndex] = anIndex;
        anIndex++;

//        Q_ASSERT(edge->adjacentEdges().count() == (edge->boundary() ? 2 : 4));

        for (int adjEdgeIndex = 0; adjEdgeIndex < edge->adjacentEdges().count();
             ++adjEdgeIndex)
        {
            Q3MeshEdge *adjEdge = edge->adjacentEdges().at(adjEdgeIndex);
            if (adjEdge->id() > edge->id())
            {
                AN_[anIndex] = - 2. * tau_
                               * edge->adjacentCotangents().at(adjEdgeIndex);
                JA_[anIndex] = adjEdge->id();
                anIndex++;
            }
        }
    }
    IA_[edgesCount] = anIndex;
    MN_ = AN_;

    incompleteCholesky(MN_.data(), JA_.data(), IA_.data(), edgesCount);
}

void Q3Calc::predictor()
{
    int iterationsCount = 0;
    qreal maxVelocityDelta;
    while(!abort_ && iterationsCount++ < maxPredictorIterationsCount)
    {
        maxVelocityDelta = 0;
        for (int trIndex = 0; trIndex < mesh_.triangles().size(); ++trIndex)
        {
            Q3MeshTriangle *triangle = mesh_.triangles().at(trIndex);
            QVector2D C = triangle->square() / tau_
                          * triangle->correctorVelocity();
            triangle->setTempVelocity(QVector2D(0, 0));
            qreal A = triangle->square() / tau_;

//            if (!triangle->adjacentTriangles().at(0)
//                || !triangle->adjacentTriangles().at(1)
//                || !triangle->adjacentTriangles().at(2))
//                continue;

            for (int edgeIndex = 0; edgeIndex < triangle->edges().size();
                 ++edgeIndex)
            {
                Q3MeshEdge *edge = triangle->edges().at(edgeIndex);
                Q3MeshTriangle *adjacentTriangle =
                        triangle->adjacentTriangles().at(edgeIndex);
                QVector2D normal = triangle->normalVectors().at(edgeIndex);

                // Вроде бы всегда так
                C -= edge->length() * edge->preassure() * normal;

                if (adjacentTriangle)
                {
                    qreal dL = triangle->distanceToTriangles().at(edgeIndex);
                    qreal dl = triangle->distancesToEdges().at(edgeIndex);

                    qreal vni = (dl * QVector2D::dotProduct(
                                     adjacentTriangle->correctorVelocity(),
                                     normal)
                                 + (dL - dl) * QVector2D::dotProduct(
                                     triangle->correctorVelocity(), normal)) / dL;

                    qreal tnu = 0;
                    if (monotoneTerm_)
                    {
                        tnu = 0.5 * dL * qAbs(vni) * Re_;
                        QVector2D tAt = QVector2D(adjacentTriangle->center()
                                                  - triangle->center());
                        tAt.normalize();
                        qreal cosin = QVector2D::dotProduct(tAt, normal);
                        tnu /= cosin;
                    }

                    qreal B = edge->length() * ((1. + tnu) / Re_ / dL - 0.5 *  vni);

                    triangle->setTempVelocity(
                                triangle->tempVelocity()
                                + B * adjacentTriangle->predictorVelocity());
                    A += B;
                }
                else
                {
                    qreal deltaA = edge->processBoundaryPredictor(Re_, monotoneTerm_);
                    A += deltaA;
                }
            }

            triangle->setTempVelocity((triangle->tempVelocity() + C) / A);

            qreal velocityDelta = (triangle->tempVelocity()
                                   - triangle->predictorVelocity()).length();
            if (velocityDelta > maxVelocityDelta)
                maxVelocityDelta = velocityDelta;
        }

        for (int trIndex = 0; trIndex < mesh_.triangles().size(); ++trIndex)
        {
            Q3MeshTriangle *triangle = mesh_.triangles().at(trIndex);
//            if (!triangle->adjacentTriangles().at(0)
//                || !triangle->adjacentTriangles().at(1)
//                || !triangle->adjacentTriangles().at(2))
//                continue;
            triangle->setPredictorVelocity(triangle->tempVelocity());
        }

        if (maxVelocityDelta < maxPredictorError)
            break;
    }

    qreal maxVelocity = 0;
    for (int trIndex = 0; trIndex < mesh_.triangles().size(); ++trIndex)
    {
        Q3MeshTriangle *triangle = mesh_.triangles().at(trIndex);
        qreal absV = qAbs(triangle->predictorVelocity().length() > maxVelocity);
        if (absV)
            maxVelocity = absV;
    }



    qDebug() << maxVelocityDelta;
}

void Q3Calc::corrector()
{

    for (int trIndex = 0; trIndex < mesh_.triangles().size(); ++trIndex)
    {
        Q3MeshTriangle *triangle = mesh_.triangles().at(trIndex);
        triangle->setPreviousCorrectorVelocity(triangle->correctorVelocity());
    }

    qreal flow = 0;
    for (int edgeIndex = 0; edgeIndex < mesh_.edges().size(); ++edgeIndex)
    {
        Q3MeshEdge *edge = mesh_.edges().at(edgeIndex);
        flow += edge->processBoundaryFlow();
    }
    qDebug() << "Flow: " << flow;

    for (int edgeIndex = 0; edgeIndex < mesh_.edges().size(); ++edgeIndex)
    {
        Q3MeshEdge *edge = mesh_.edges().at(edgeIndex);
        Q3MeshTriangle *tr0 = edge->adjacentTriangles().at(0);
        int trEdgeIndex = tr0->edges().indexOf(edge);
        QVector2D normal = tr0->normalVectors().at(trEdgeIndex);

        if (edge->adjacentTriangles().size() == 2)
        {
            Q3MeshTriangle *tr1 = edge->adjacentTriangles().at(1);

            BN_[edgeIndex] = QVector2D::dotProduct(tr1->predictorVelocity(),
                                                   normal)
                             - QVector2D::dotProduct(tr0->predictorVelocity(),
                                                     normal);
            if (badTriangleFix_)
            {
                QVector2D tAt = QVector2D(tr0->center() - tr1->center());
                qreal cosin = qAbs(QVector2D::dotProduct(tAt, normal) / tAt.length());
                BN_[edgeIndex] /= cosin;
            }
        }
        else
        {
            BN_[edgeIndex] = edge->processBoundaryCorrector();
        }

        BN_[edgeIndex] *= - edge->length();
        BN_[edgeIndex] += edge->adjacentSquare() * flow / mesh_.square();
    }

    XN_.fill(0, mesh_.edges().count());
    ConjugateGradient::calculate(AN_.data(), JA_.data(), IA_.data(), XN_.data(),
                                 BN_.data(), MN_.data(), TN_.data(),
                                 mesh_.edges().count());

    for (int edgeIndex = 0; edgeIndex < mesh_.edges().count(); ++edgeIndex)
    {
        Q3MeshEdge *edge = mesh_.edges().at(edgeIndex);
        edge->setPreassure(edge->preassure() + XN_[edgeIndex]);
    }

    qreal residual = 0;
    for (int trIndex = 0; trIndex < mesh_.triangles().count(); ++trIndex)
    {
        Q3MeshTriangle *triangle = mesh_.triangles().at(trIndex);

        QVector2D sum(0, 0);
        for (int edgeInd = 0; edgeInd < triangle->edges().count(); ++edgeInd)
        {
            Q3MeshEdge *edge = triangle->edges().at(edgeInd);
            sum += edge->length() * XN_[edge->id()]
                    * triangle->normalVectors().at(edgeInd);
        }
        QVector2D prevVelocity = triangle->correctorVelocity();
        triangle->setCorrectorVelocity(triangle->predictorVelocity()
                                       - tau_ * sum / triangle->square());
        qreal deltaV = (prevVelocity - triangle->correctorVelocity()).length();
        if (deltaV > residual)
            residual = deltaV;
    }
    residual_ = residual / tau_;
//    qDebug() << residual_;
}

void Q3Calc::incompleteCholesky(qreal *AN, int *JA, int *IA, int n)
{
    for (int k = 0; k < n - 1; ++k)
    {
        int d = IA[k];

        if (AN[d] < 0)
            AN[d] *= -1;

        double z = AN[d] = sqrt(AN[d]);

        for (int i = d + 1; i < IA[k + 1]; ++i)
            AN[i] /= z;

        for (int i = d + 1; i < IA[k + 1]; ++i)
        {
            z = AN[i];
            int h = JA[i];
            int g = i;
            for (int j = IA[h] ; j < IA[h + 1]; ++j)
            {
                for ( ; g < IA[k + 1] && JA[g] <= JA[j]; g++)
                {
                    if (JA[g] == JA[j])
                        AN[j] -= z * AN[g];
                }
            }
        }
    }

    int d = IA[n - 1];
    if (AN[d] < 0)
        AN[d] *= -1;

    AN[d] = sqrt(AN[d]);
}

void Q3Calc::calcFaithfulResidualNS()
{
    qreal maxDelta = 0.0;

    for (int trIndex = 0; trIndex < mesh_.triangles().size(); ++trIndex)
    {
        Q3MeshTriangle *triangle = mesh_.triangles().at(trIndex);

        // Производная по времени
        QVector2D deltaV = (triangle->correctorVelocity() -
                triangle->previousCorrectorVelocity()) / tau_ * triangle->square();

        for (int edgeIndex = 0; edgeIndex < triangle->edges().count(); ++edgeIndex)
        {
            Q3MeshEdge *edge = triangle->edges().at(edgeIndex);
            Q3MeshTriangle *adjacentTriangle =
                        triangle->adjacentTriangles().at(edgeIndex);
            QVector2D normal = triangle->normalVectors().at(edgeIndex);

            // Давление
            deltaV += edge->length() * edge->preassure() * normal;

            if (adjacentTriangle)
            {
                // Конвективный поток
                qreal dL = triangle->distanceToTriangles().at(edgeIndex);
                qreal dl = triangle->distancesToEdges().at(edgeIndex);

                qreal vni = (dl * QVector2D::dotProduct(
                        adjacentTriangle->correctorVelocity(),
                        normal)
                    + (dL - dl) * QVector2D::dotProduct(
                        triangle->correctorVelocity(), normal)) / dL;

                deltaV += vni * edge->length() * (
                            triangle->correctorVelocity() +
                            adjacentTriangle->correctorVelocity())  * 0.5;

                // Лапласиан
                deltaV -= edge->length() / Re_ * (
                            adjacentTriangle->correctorVelocity() -
                            triangle->correctorVelocity()) / dL;
            }
            else
            {
                // dL равна dl
                qreal dl = triangle->distancesToEdges().at(edgeIndex);
                qreal vni;

                switch ( edge->boundary()->type()->toEnum() )
                {
                case Q3BoundaryType::NoSlipBoundary:
                    // Конвективный поток вклада не дает
                    // Лапласиан
                    // Скорость на границе равна 0

                    deltaV += edge->length() / Re_ *
                            triangle->correctorVelocity() / dl;
                    break;

                case Q3BoundaryType::FixedVelocity:
                case Q3BoundaryType::InBoundary:
                    // Конвективный поток
                    vni = QVector2D::dotProduct(edge->velocity(), normal);

                    deltaV += vni * edge->length() *
                                edge->velocity();

                    // Лапласиан
                    deltaV -= edge->length() / Re_ *
                            (edge->velocity() -
                             triangle->correctorVelocity()) / dl;

                    break;

                case Q3BoundaryType::OutBoundary:
                    // Конвективный поток
                    vni = QVector2D::dotProduct(
                                triangle->correctorVelocity(), normal);

                    deltaV += vni * edge->length() *
                                triangle->correctorVelocity();

                    // Лапласиан вклада не дает
                    // Производная по нормали должна равняться 0
                    break;

                default:
                    break;
                }
            }
        }

        deltaV /= triangle->square();

        if (maxDelta < deltaV.length())
            maxDelta = deltaV.length();
    }

    faithfulResidualNS_ = maxDelta;
    return;
}

void Q3Calc::calcFaithfulResidualDiv()
{
    qreal maxDelta = 0.0;

    for (int trIndex = 0; trIndex < mesh_.triangles().size(); ++trIndex)
    {
        Q3MeshTriangle *triangle = mesh_.triangles().at(trIndex);

        qreal delta = 0.0;

        for (int edgeIndex = 0; edgeIndex < triangle->edges().count(); ++edgeIndex)
        {
            Q3MeshEdge *edge = triangle->edges().at(edgeIndex);
            Q3MeshTriangle *adjacentTriangle =
                    triangle->adjacentTriangles().at(edgeIndex);
            QVector2D normal = triangle->normalVectors().at(edgeIndex);

            if (adjacentTriangle)
            {
                qreal dL = triangle->distanceToTriangles().at(edgeIndex);
                qreal dl = triangle->distancesToEdges().at(edgeIndex);

                qreal vni = (dl * QVector2D::dotProduct(
                        adjacentTriangle->correctorVelocity(),
                        normal)
                    + (dL - dl) * QVector2D::dotProduct(
                        triangle->correctorVelocity(), normal)) / dL;

                delta += vni * edge->length();
            }
            else
            {
                qreal vni;

                switch ( edge->boundary()->type()->toEnum() )
                {
                case Q3BoundaryType::NoSlipBoundary:
                    vni = 0.0;
                    break;

                case Q3BoundaryType::FixedVelocity:
                case Q3BoundaryType::InBoundary:

                    vni = QVector2D::dotProduct(edge->velocity(), normal);
                    break;

                case Q3BoundaryType::OutBoundary:
                    vni = QVector2D::dotProduct(
                                triangle->correctorVelocity(), normal);
                    break;

                default:
                    break;
                }

                delta += vni * edge->length();
            }
        }

        delta /= triangle->square();

        if (maxDelta < qAbs(delta))
            maxDelta = qAbs(delta);
   }

   faithfulResidualDiv_ = maxDelta;
   return;
}

void Q3Calc::setMonotoneTerm(bool monotoneTerm)
{
    monotoneTerm_ = monotoneTerm;
}


void Q3Calc::setRe(const qreal &Re)
{
    Re_ = Re;
}

void Q3Calc::setTau(const qreal &tau)
{
    tau_ = tau;
}

void Q3Calc::setBadTriangleFix(bool badTriangleFix)
{
    badTriangleFix_ = badTriangleFix;
}

void Q3Calc::abort()
{
    abort_ = true;
}

void Q3Calc::reset()
{
    time_ = 0;
    abort_ = true;
    wait();
    started_ = false;
    residual_ = 0;
    calcTime_ = 0;
    emit updateInfo();
}

QString Q3Calc::info()
{
    if (started_)
        calcTime_ += calcTimer_.restart();

    QString out;
    QTextStream stream(&out);
    if (started_)
    {
        stream << trUtf8("Невязка предиктор-корректор: ") << QString::number(residual_) << "\n"
               << trUtf8("Настоящая невязка Навье-Стокс: ") << QString::number(faithfulResidualNS_) << "\n"
               << trUtf8("Настоящая невязка Дивергенция: ") << QString::number(faithfulResidualDiv_) << "\n"
               << trUtf8("Время: ") << QString::number(time_) << "\n"
               << trUtf8("Время расчета: ") << QString::number(calcTime_ / 1000.) << "\n";
    }
    else
        stream << trUtf8("Инфоормация о расчете\n");
    return out;
}
