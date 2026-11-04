#ifndef LINEARDIMENSIONITEM_H
#define LINEARDIMENSIONITEM_H

#include "basedimensionitem.h"

class LinearDimensionItem final : public BaseDimensionItem {
public:
    explicit LinearDimensionItem(const MyDimension &data, QGraphicsItem *parent = nullptr);
    ~LinearDimensionItem() override;

    CadItem *clone() const override;
    QList<SnapPoint> getSnapPoints() const override;
    QList<CadItem *> explode() const override;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QPainterPath shape() const override;
    QPainterPath geometryPath() const override;
    // boundingRect()는 부모 클래스에서 처리하므로 삭제되었습니다.

    QPainterPath getGhostShape(int index, const MyVector &pos) const override;
    double distanceTo(const MyVector &pos) const override;

    QPainterPath getMovedPath(const MyVector &delta) const override;
    QPainterPath getMirroredPath(const MyVector &p1, const MyVector &p2) const override;
    QPainterPath getRotatedPath(const MyVector &base, double angle) const override;
    QPainterPath getScaledPath(const MyVector &base, double scaleFactor) const override;
    QPainterPath getStretchedPath(const QList<int> &indices, const MyVector &delta) const override;
};

#endif // LINEARDIMENSIONITEM_H