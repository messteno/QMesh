#ifndef Q3SELECTDIRECTOR_H
#define Q3SELECTDIRECTOR_H

#include <QWidget>

#include "q3director.h"
#include "q3sceletonitem.h"
#include "q3sceletonitemform.h"

namespace Ui {
    class Q3SelectDirector;
}

class Q3SelectDirector : public Q3Director
{
    Q_OBJECT

private:
    QList<Q3SceletonItem*> selectedItems_;
    bool moving_;
    bool movable_;
    bool editable_;
    Q3SceletonItemForm *editForm_;
    Ui::Q3SelectDirector *ui;

public slots:
    void setupEditForm();

signals:
    void itemMoved();

public:
    Q3SelectDirector(QWidget *parent = NULL);
    virtual ~Q3SelectDirector();

    virtual bool processClick(QMouseEvent *event, const QPointF &scenePos);
    virtual bool processDragged(const QPointF &oldScenePos,
                                const QPointF &newScenePos);
    virtual bool processDropped(const QPointF &scenePos);
    virtual bool processKeyRelease(int key);
    virtual void stop();
    virtual void draw(Q3Painter &painter) const;

    bool movable() const;
    void setMovable(bool movable);

    bool editable() const;
    void setEditable(bool editable);

private slots:
    void on_editElementButton_clicked();
};

#endif // Q3SELECTEDDIRECTOR_H
