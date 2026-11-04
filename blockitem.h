#ifndef BLOCKITEM_H
#define BLOCKITEM_H

#include "caditem.h"
#include "myentity.h"
#include <QCoreApplication>
#include <QList>
#include <QPen>

class GripItem;

// Dummy Entity to satisfy CadItem's getEntityData requirement
class BlockEntity : public MyEntity {
public:
  BlockEntity() { layerId = 0; }
  EntityType type() const override { return EntityType::Line; } // Dummy
  std::vector<MyVector> intersect(const MyEntity *other) const override {
    Q_UNUSED(other);
    return {};
  }
  MyVector getNearestPoint(const MyVector &pos) const override {
    return pos; // Dummy
  }
};

// [View] 하나의 덩어리로 동작하는 블록 아이템
class BlockItem final : public CadItem {
public:
  BlockItem(const QList<CadItem *> &items, const MyVector &insertionPoint,
            const QString &name = "", double scaleX = 1.0, double scaleY = 1.0,
            double rotation = 0.0, QGraphicsItem *parent = nullptr);
  ~BlockItem() override;

  CadItem *clone() const override;

  // QGraphicsItem 필수 구현
  QRectF boundingRect() const override;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget) override;
  QPainterPath shape() const override;
  QPainterPath geometryPath() const override;

  // CadItem 필수 구현 (인터랙션)
  QList<SnapPoint> getSnapPoints() const override;
  QList<QPointF> getGripPoints() const override;
  void applyGripMove(int index, const MyVector &pos) override;
  QPainterPath getGhostShape(int index, const MyVector &pos) const override;
  double distanceTo(const MyVector &pos) const override;

  // 데이터/속성 접근 (Setter/Getter)
  SnapPoint getNearestSnap(const MyVector &pos) const override;
  const MyEntity *getEntityData() const override { return &m_baseEntity; }
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

  // 속성창(Property Dock) 지원
  QMap<QString, QVariant> getProperties() const override;
  void setGripsVisible(bool visible) override;
  bool setProperty(const QString &key, const QVariant &value) override;

  void setLayerName(const QString &name) override;
  void setPen(const QPen &pen) override;

  // Block 전용 Getter
  QList<CadItem *> getItems() const { return m_items; }
  MyVector insertionPoint() const { return m_insertionPoint; }
  QString blockName() const { return m_blockName; }
  double blockScaleX() const { return m_scaleX; }
  double blockScaleY() const { return m_scaleY; }
  double blockRotation() const { return m_rotation; }

protected:
  QVariant itemChange(GraphicsItemChange change,
                      const QVariant &value) override;

private:
  void updateGrips();
  void initBaseEntity();

  // [성능] 자식 엔티티를 순회해 만드는 geometryPath()/shape()/boundingRect()는
  // 매 paint()·매 선택질의마다 재구성되어 도면을 여러 개 배치·선택하면 심각한
  // 저하를 유발했다. 자식 기하가 바뀔 때(mirror 등)만 무효화하고 캐시한다.
  void rebuildGeometryCacheIfNeeded() const;
  void invalidateGeometryCache();

  mutable QPainterPath m_cachedGeometryPath;
  mutable QPainterPath m_cachedShape;
  mutable QRectF m_cachedBoundingRect;
  mutable bool m_geometryCacheDirty = true;

  QList<CadItem *> m_items;
  MyVector m_insertionPoint;
  QString m_blockName;
  double m_scaleX;
  double m_scaleY;
  double m_rotation;

  // Dummy Entity for generic operations that expect an entity
  BlockEntity m_baseEntity;

  // 그립 (블록은 삽입점에 1개만 생성)
  GripItem *m_insertionGrip = nullptr;
};

#endif // BLOCKITEM_H
