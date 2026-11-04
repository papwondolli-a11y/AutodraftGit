#ifndef SPLINEITEM_H
#define SPLINEITEM_H

#include "caditem.h"
#include "myspline.h"
#include <QPen>

class SplineItem : public CadItem {
public:
  SplineItem(const std::vector<MyVector> &controlPoints, int degree = 3, bool isClosed = false, QGraphicsItem *parent = nullptr);
  SplineItem(const MySpline &spline, QGraphicsItem *parent = nullptr);

  CadItem *clone() const override;
  QMap<QString, QVariant> getProperties() const override;
  bool setProperty(const QString &key, const QVariant &value) override;

  QRectF boundingRect() const override;
  void setPen(const QPen &pen) override; // pen 폭이 boundingRect 여백에 반영됨
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
  QPainterPath shape() const override;
  QPainterPath geometryPath() const override;

  QList<SnapPoint> getSnapPoints() const override;
  QList<QPointF> getGripPoints() const override;
  void applyGripMove(int index, const MyVector &pos) override;
  QPainterPath getGhostShape(int index, const MyVector &pos) const override;
  double distanceTo(const MyVector &pos) const override;
  SnapPoint getNearestSnap(const MyVector &pos) const override;
  const MyEntity *getEntityData() const override { return &m_data; }
  
  void move(const MyVector &delta) override;
  void mirror(const MyVector &p1, const MyVector &p2) override;
  void rotate(const MyVector &base, double angle) override;
  void scale(const MyVector &base, double scaleFactor) override;
  void stretch(const QList<int> &indices, const MyVector &delta) override;

  QPainterPath getMovedPath(const MyVector &delta) const override;
  QPainterPath getMirroredPath(const MyVector &p1, const MyVector &p2) const override;
  QPainterPath getRotatedPath(const MyVector &base, double angle) const override;
  QPainterPath getScaledPath(const MyVector &base, double scaleFactor) const override;
  QPainterPath getStretchedPath(const QList<int> &indices, const MyVector &delta) const override;

  void setGripsVisible(bool visible) override;

private:
  void updatePath();
  MySpline m_data;
  QPainterPath m_path;
};

#endif // SPLINEITEM_H
