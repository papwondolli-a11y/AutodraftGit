#ifndef HOLEITEM_H
#define HOLEITEM_H

#include "caditem.h"
#include "myhole.h"

class GripItem;

class HoleItem final : public CadItem {
public:
    explicit HoleItem(const MyHole &data, QGraphicsItem *parent = nullptr);
    ~HoleItem();

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;
    QPainterPath shape() const override;
    QPainterPath geometryPath() const override;

    QList<SnapPoint> getSnapPoints() const override;
    QList<QPointF> getGripPoints() const override;
    void applyGripMove(int index, const MyVector &pos) override;
    QPainterPath getGhostShape(int index, const MyVector &pos) const override;
    double distanceTo(const MyVector &pos) const override;

    void setGripsVisible(bool visible) override;

    SnapPoint getNearestSnap(const MyVector &pos) const override;
    const MyEntity *getEntityData() const override { return &m_data; }

    void move(const MyVector &delta) override;
    void mirror(const MyVector &p1, const MyVector &p2) override;
    QPainterPath getMovedPath(const MyVector &delta) const override;
    QPainterPath getMirroredPath(const MyVector &p1,
                                 const MyVector &p2) const override;

    void rotate(const MyVector &base, double angle) override;
    QPainterPath getRotatedPath(const MyVector &base,
                                double angle) const override;

    void scale(const MyVector &base, double scaleFactor) override;
    QPainterPath getScaledPath(const MyVector &base,
                               double scaleFactor) const override;

    void stretch(const QList<int> &indices, const MyVector &delta) override;
    QPainterPath getStretchedPath(const QList<int> &indices,
                                  const MyVector &delta) const override;

    CadItem *clone() const override;

    QMap<QString, QVariant> getProperties() const override;
    bool setProperty(const QString &key, const QVariant &value) override;

    const MyHole &holeData() const { return m_data; }
    void setHoleData(const MyHole &data);

protected:
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant &value) override;

private:
    void updateGrips();
    double maxRadius() const;
    QPainterPath buildPath(const MyVector &center) const;
    // layerOverride 레이어의 색상 + 라인타입(대시 패턴)을 펜에 반영한다.
    QPen resolvePen(const QPen &basePen, const QString &layerOverride) const;
    // m_data.renderMode가 Full이면 완전한 원을, Right/Left면 그 방향의 반원 호만 그린다.
    void drawCircleOrHalf(QPainter *painter, const QPointF &c, double r) const;

    MyHole m_data;
    QList<GripItem *> m_grips; // 0=Center, 1=반지름 핸들

    mutable QPainterPath m_cachedShape;
    mutable bool m_isShapeValid = false;
};

#endif // HOLEITEM_H
