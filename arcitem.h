#ifndef ARCITEM_H
#define ARCITEM_H

#include "caditem.h"
#include "myarc.h"
#include "myvector.h"

class GripItem;

// [View]
class ArcItem final : public CadItem {
public:
  CadItem *clone() const override;

  // [신규] 속성창(Property Dock) 지원
  QMap<QString, QVariant> getProperties() const override;
  bool setProperty(const QString &key, const QVariant &value) override;
  ArcItem(const MyVector &center, const MyVector &start, const MyVector &end,
          QGraphicsItem *parent = nullptr);
  ArcItem(const MyArc &arc, QGraphicsItem *parent = nullptr);

  // QGraphicsItem 필수
  QRectF boundingRect() const override;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget) override;
  QPainterPath shape() const override;
  QPainterPath geometryPath() const override;

  // CadItem 필수 (가상 함수 오버라이딩 - 매개변수 타입 MyVector 확인!)
  QList<SnapPoint> getSnapPoints() const override;

  void applyGripMove(int index, const MyVector &pos) override;
  QPainterPath getGhostShape(int index, const MyVector &pos) const override;
  double distanceTo(const MyVector &pos) const override;

  // GripItem용 (이건 QPointF 반환이 맞음)
  QList<QPointF> getGripPoints() const override;

  // 데이터 접근
  void setArc(const MyArc &arc);
  MyArc getArc() const { return m_data; }
  void setGripsVisible(bool visible) override;

  // Static Helpers
  static MyArc createFrom3Points(const MyVector &center, const MyVector &start,
                                 const MyVector &end);
  static QPainterPath createQtPath(const MyArc &arc);

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

protected:
  QVariant itemChange(GraphicsItemChange change,
                      const QVariant &value) override;

private:
  void updateGrips();
  void updateCache();

  MyArc m_data;
  GripItem *m_centerGrip = nullptr;
  GripItem *m_startGrip = nullptr;
  GripItem *m_endGrip = nullptr;

  mutable QRectF m_cachedRect;
  mutable QPainterPath m_cachedPath;
  mutable QPainterPath m_cachedShape;  // [신규] shape 캐시
  mutable bool m_isCacheValid = false; // geometryPath 캐시 플래그
  mutable bool m_isShapeValid = false; // shape 캐시 플래그
};

#endif // ARCITEM_H
