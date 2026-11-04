#ifndef LEADERITEM_H
#define LEADERITEM_H

#include "caditem.h"
#include "myleader.h"
#include "mydimension.h" // ArrowheadStyle

class LeaderItem : public CadItem {
public:
    explicit LeaderItem(const MyLeader &data, QGraphicsItem *parent = nullptr);
    virtual ~LeaderItem();

    CadItem *clone() const override;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    QPainterPath shape() const override; // [추가] 텍스트 클릭/선택 영역 처리
    QPainterPath geometryPath() const override;

    QList<SnapPoint> getSnapPoints() const override;
    QList<QPointF> getGripPoints() const override;
    void applyGripMove(int index, const MyVector &pos) override;
    void setGripsVisible(bool visible) override;

    QMap<QString, QVariant> getProperties() const override;
    bool setProperty(const QString &key, const QVariant &value) override;

    // CadItem mandatory implementations
    double distanceTo(const MyVector &pos) const override;
    SnapPoint getNearestSnap(const MyVector &pos) const override;
    const MyEntity *getEntityData() const override { return &m_data; }

    void move(const MyVector &delta) override;
    QPainterPath getMovedPath(const MyVector &delta) const override;

    void mirror(const MyVector &p1, const MyVector &p2) override;
    QPainterPath getMirroredPath(const MyVector &p1, const MyVector &p2) const override;

    void rotate(const MyVector &base, double angle) override;
    QPainterPath getRotatedPath(const MyVector &base, double angle) const override;

    void scale(const MyVector &base, double scaleFactor) override;
    QPainterPath getScaledPath(const MyVector &base, double scaleFactor) const override;

    void stretch(const QList<int> &indices, const MyVector &delta) override;
    QPainterPath getStretchedPath(const QList<int> &indices, const MyVector &delta) const override;

    QPainterPath getGhostShape(int index, const MyVector &pos) const override;

    QList<CadItem *> explode() const;

    const MyLeader& leaderData() const { return m_data; }

    // 인라인 텍스트 편집 중에는 텍스트만 숨기고 지시선은 계속 그린다
    void setTextHidden(bool hidden);

private:
    void updateGrips();
    QRectF textRect() const;

    // 화살표 그리기 공통 함수
    void drawArrow(QPainter *painter, const MyVector &pos, const MyVector &direction, double arrowSize, const QColor &color,
                   ArrowheadStyle headStyle = ArrowheadStyle::Filled) const;

    MyLeader m_data;
    mutable QRectF m_cachedBoundingRect;
    mutable bool m_isBoundingRectValid = false;
    bool m_textHidden = false;
};

#endif // LEADERITEM_H