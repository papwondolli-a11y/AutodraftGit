#ifndef CIRCLEITEM_H
#define CIRCLEITEM_H

#include <QCoreApplication>

#include "caditem.h"
#include "mycircle.h"
#include "myvector.h"

class GripItem;

class CircleItem final : public CadItem {
  Q_DECLARE_TR_FUNCTIONS(CircleItem)

public:
  CircleItem(const MyVector &center, double radius,
             QGraphicsItem *parent = nullptr);
  CircleItem(const MyCircle &circle, QGraphicsItem *parent = nullptr);

  QRectF boundingRect() const override;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget) override;
  QPainterPath shape() const override;
  QPainterPath geometryPath() const override;

  // CadItem 인터페이스
  QList<SnapPoint> getSnapPoints() const override;
  QList<QPointF> getGripPoints() const override;
  void applyGripMove(int index, const MyVector &pos) override;
  QPainterPath getGhostShape(int index, const MyVector &pos) const override;
  double distanceTo(const MyVector &pos) const override;

  void setCircle(const MyCircle &circle);
  MyCircle getCircle() const { return m_data; }

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

protected:
  QVariant itemChange(GraphicsItemChange change,
                      const QVariant &value) override;

private:
  void updateGrips();

  // [상세영역] 이 원이 상세도 경계원(DETAIL_REGION_SCALE 태그)일 때,
  // 속성 dock에서 축척을 바꾸면 상세도 내용 전체를 이 원의 중심 기준으로
  // oldScale→newScale 비율만큼 다시 스케일한다.
  void applyDetailRegionRescale(double oldScale, double newScale);

  MyCircle m_data;

  // 그립: 0(Center), 1(Right), 2(Top), 3(Left), 4(Bottom)
  QList<GripItem *> m_grips;

  mutable QPainterPath m_cachedShape;
  mutable bool m_isShapeValid = false;

public:
  CadItem *clone() const override;

  // [신규] 속성창(Property Dock) 지원
  QMap<QString, QVariant> getProperties() const override;
  bool setProperty(const QString &key, const QVariant &value) override;
};

#endif // CIRCLEITEM_H
