#ifndef RADIALDIMENSIONITEM_H
#define RADIALDIMENSIONITEM_H

#include "basedimensionitem.h"

class RadialDimensionItem final : public BaseDimensionItem {
public:
    explicit RadialDimensionItem(const MyDimension &data, QGraphicsItem *parent = nullptr);
    ~RadialDimensionItem() override;

    CadItem *clone() const override;
    QList<SnapPoint> getSnapPoints() const override;
    QList<CadItem *> explode() const override;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QPainterPath shape() const override;
    QPainterPath geometryPath() const override;
 //   QRectF boundingRect() const override;

    QPainterPath getGhostShape(int index, const MyVector &pos) const override;
    double distanceTo(const MyVector &pos) const override;

    QPainterPath getMovedPath(const MyVector &delta) const override;
    QPainterPath getMirroredPath(const MyVector &p1, const MyVector &p2) const override;
    QPainterPath getRotatedPath(const MyVector &base, double angle) const override;
    QPainterPath getScaledPath(const MyVector &base, double scaleFactor) const override;
    QPainterPath getStretchedPath(const QList<int> &indices, const MyVector &delta) const override;
};

#endif // RADIALDIMENSIONITEM_H