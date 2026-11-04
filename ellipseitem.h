#ifndef ELLIPSEITEM_H
#define ELLIPSEITEM_H

#include "caditem.h"
#include "myellipse.h"
#include <QPen>

// 타원 / 타원호 도형 (DXF ELLIPSE 대응)
// 그립 인덱스: 0=중심, 1=장축 +끝, 2=단축 +끝, 3=장축 -끝, 4=단축 -끝,
//             (타원호인 경우) 5=시작점, 6=끝점
class EllipseItem : public CadItem {
public:
  explicit EllipseItem(const MyEllipse &ellipse, QGraphicsItem *parent = nullptr);

  CadItem *clone() const override;
  QMap<QString, QVariant> getProperties() const override;
  bool setProperty(const QString &key, const QVariant &value) override;

  QRectF boundingRect() const override;
  void setPen(const QPen &pen) override;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget) override;
  QPainterPath shape() const override;
  QPainterPath geometryPath() const override;

  QList<SnapPoint> getSnapPoints() const override;
  QList<QPointF> getGripPoints() const override;
  void applyGripMove(int index, const MyVector &pos) override;
  QPainterPath getGhostShape(int index, const MyVector &pos) const override;
  double distanceTo(const MyVector &pos) const override;
  SnapPoint getNearestSnap(const MyVector &pos) const override;
  const MyEntity *getEntityData() const override { return &m_data; }

  void setEllipse(const MyEllipse &ellipse);
  MyEllipse getEllipse() const { return m_data; }

  void move(const MyVector &delta) override;
  void mirror(const MyVector &p1, const MyVector &p2) override;
  void rotate(const MyVector &base, double angle) override;
  void scale(const MyVector &base, double scaleFactor) override;
  void stretch(const QList<int> &indices, const MyVector &delta) override;

  QPainterPath getMovedPath(const MyVector &delta) const override;
  QPainterPath getMirroredPath(const MyVector &p1,
                               const MyVector &p2) const override;
  QPainterPath getRotatedPath(const MyVector &base, double angle) const override;
  QPainterPath getScaledPath(const MyVector &base,
                             double scaleFactor) const override;
  QPainterPath getStretchedPath(const QList<int> &indices,
                                const MyVector &delta) const override;

  void setGripsVisible(bool visible) override;

  // 타원 데이터 → 정밀 Bezier 경로 (정적 헬퍼: 고스트/미리보기에서도 사용)
  static QPainterPath createQtPath(const MyEllipse &e);

private:
  void updatePath();

  MyEllipse m_data;
  QPainterPath m_path;
};

#endif // ELLIPSEITEM_H
