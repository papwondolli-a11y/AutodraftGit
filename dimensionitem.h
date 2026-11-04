#ifndef DIMENSIONITEM_H
#define DIMENSIONITEM_H

#include "caditem.h"
#include "mydimension.h"

class GripItem;

class DimensionItem final : public CadItem {
public:
  DimensionItem(const MyDimension &data, QGraphicsItem *parent = nullptr);
  ~DimensionItem();

  CadItem *clone() const override;

  // QGraphicsItem 필수 구현
  QRectF boundingRect() const override;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget) override;
  QPainterPath shape() const override;
  QPainterPath geometryPath() const override;
  QVariant itemChange(GraphicsItemChange change,
                      const QVariant &value) override;

  // CadItem 필수 구현
  QList<SnapPoint> getSnapPoints() const override;
  QList<QPointF> getGripPoints() const override;
  void applyGripMove(int index, const MyVector &pos) override;
  QPainterPath getGhostShape(int index, const MyVector &pos) const override;
  double distanceTo(const MyVector &pos) const override;
  SnapPoint getNearestSnap(const MyVector &pos) const override;
  const MyEntity *getEntityData() const override { return &m_data; }
  const MyDimension &data() const { return m_data; }

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

  QMap<QString, QVariant> getProperties() const override;
  void setGripsVisible(bool visible) override;
  bool setProperty(const QString &key, const QVariant &value) override;
  QList<CadItem *> explode() const;

private:
  void updateGrips();

  MyDimension m_data;

  GripItem *m_startGrip = nullptr;
  GripItem *m_endGrip = nullptr;
  GripItem *m_dimLineGrip = nullptr;
  GripItem *m_end2Grip = nullptr;

  mutable QRectF m_cachedBoundingRect;
  mutable bool m_isBoundingRectValid = false;
};

#endif // DIMENSIONITEM_H
