#ifndef LINEITEM_H
#define LINEITEM_H

#include "caditem.h"
#include "myline.h"
#include "mymath.h"
#include "myvector.h"

#include <QPen>

class GripItem;

// [View] 그래픽스 아이템
class LineItem final : public CadItem {
public:
    CadItem *clone() const override;
    // 생성자
    LineItem(const MyVector &start, const MyVector &end,
             QGraphicsItem *parent = nullptr);
    LineItem(const MyLine &line, QGraphicsItem *parent = nullptr);

    // QGraphicsItem 필수 구현
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;
    QPainterPath shape() const override;
    QPainterPath geometryPath() const override;

    // CadItem 필수 구현 (인터랙션)
    QList<SnapPoint> getSnapPoints() const override;
    bool getOrthoBasePoint(int index, MyVector &outBasePoint) const override;
    QList<QPointF> getGripPoints() const override;
    void applyGripMove(int index, const MyVector &pos) override;
    QPainterPath getGhostShape(int index, const MyVector &pos) const override;
    double distanceTo(const MyVector &pos) const override;

    // 데이터 접근 (Setter/Getter)
    void setLine(const MyLine &line);
    MyLine getLine() const { return m_data; }

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

    void updateEndpoint(const QPointF &newPos, bool isFirstEnd) override;

    // [신규] 속성창(Property Dock) 지원
    QMap<QString, QVariant> getProperties() const override;
    void setGripsVisible(bool visible) override;
    bool setProperty(const QString &key, const QVariant &value) override;

protected:
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant &value) override;

private:
    void updateGrips();

    // [핵심] 순수 데이터 모델
    MyLine m_data;

    // 그립 (0:Start, 1:End, 2:Mid)
    GripItem *m_startGrip = nullptr;
    GripItem *m_endGrip = nullptr;
    GripItem *m_midGrip = nullptr;

    mutable QPainterPath m_cachedShape;
    mutable bool m_isShapeValid = false;
};

#endif // LINEITEM_H