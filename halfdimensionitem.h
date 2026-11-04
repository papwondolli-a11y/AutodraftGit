#ifndef HALFDIMENSIONITEM_H
#define HALFDIMENSIONITEM_H

#include "basedimensionitem.h"
#include <QGraphicsItem>
#include <QPainterPath>
#include <QRectF>

class HalfDimensionItem : public BaseDimensionItem {
public:
    explicit HalfDimensionItem(const MyDimension &data, QGraphicsItem *parent = nullptr);
    ~HalfDimensionItem() override;

    CadItem *clone() const override;
    QList<SnapPoint> getSnapPoints() const override;

    // 그리기 및 영역 판정
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QPainterPath geometryPath() const override;
    QPainterPath shape() const override;
 //   QRectF boundingRect() const override;

    // 그립 및 편집 관련
    double distanceTo(const MyVector &pos) const override;
    QPainterPath getGhostShape(int index, const MyVector &pos) const override;
    QPainterPath getMovedPath(const MyVector &delta) const override;
    QPainterPath getMirroredPath(const MyVector &p1, const MyVector &p2) const override;
    QPainterPath getRotatedPath(const MyVector &base, double angle) const override;
    QPainterPath getScaledPath(const MyVector &base, double factor) const override;
    QPainterPath getStretchedPath(const QList<int> &indices, const MyVector &delta) const override;

    // 분해 (Explode)
    QList<CadItem *> explode() const override;

private:
    std::pair<MyVector, MyVector> d_p1p2_on_dimline() const;
};

#endif // HALFDIMENSIONITEM_H