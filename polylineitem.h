#ifndef POLYLINEITEM_H
#define POLYLINEITEM_H

#include "caditem.h"
#include "mypolyline.h"
#include <QBrush>
#include <QPen>


class GripItem;

class PolylineItem : public CadItem {
public:
  CadItem *clone() const override;

  // [신규] 속성창(Property Dock) 지원
  QMap<QString, QVariant> getProperties() const override;
  bool setProperty(const QString &key, const QVariant &value) override;
  PolylineItem(const std::vector<MyVector> &vertices, bool isClosed,
               QGraphicsItem *parent = nullptr);
  PolylineItem(const MyPolyline &poly, QGraphicsItem *parent = nullptr);

  // QGraphicsItem 필수 구현
  QRectF boundingRect() const override;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget) override;
  QPainterPath shape() const override;
  QPainterPath geometryPath() const override;

  // CadItem 필수 구현
  QList<SnapPoint> getSnapPoints() const override;
  QList<QPointF> getGripPoints() const override;
  void applyGripMove(int index, const MyVector &pos) override;
  QPainterPath getGhostShape(int index, const MyVector &pos) const override;
  double distanceTo(const MyVector &pos) const override;
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
  void setGripsVisible(bool visible) override;

  void setPolyline(const MyPolyline &poly);
  MyPolyline getPolyline() const { return m_data; }

  void setBrush(const QBrush &brush) {
    m_brush = brush;
    update();
  }
  QBrush brush() const { return m_brush; }

protected:
  QVariant itemChange(GraphicsItemChange change,
                      const QVariant &value) override;

private:
  void updatePath();
  void updateGrips();

  MyPolyline m_data;
  QPainterPath m_path;
  QBrush m_brush = Qt::NoBrush;
  QList<GripItem *> m_grips;
};

#endif // POLYLINEITEM_H
