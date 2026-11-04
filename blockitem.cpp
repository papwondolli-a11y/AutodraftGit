#include "blockitem.h"
#include "gripitem.h"
#include "hatchitem.h"
#include "mymath.h"
#include "textitem.h"
#include <QDebug>
#include <QObject>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

namespace {
// [헬퍼] QPainterPath의 모든 점(직선/베지어 제어점 포함)에 임의의 점 변환 함수를
// 적용한다. move/rotate/scale/mirror 미리보기 경로를 다른 CadItem들과 동일하게
// "현재 씬 좌표 형상에 MyMath 점 변환을 그대로 적용"하는 방식으로 구하기 위함.
template <typename Fn>
QPainterPath mapPathPoints(const QPainterPath &path, Fn f) {
  QPainterPath result;
  const int n = path.elementCount();
  int i = 0;
  while (i < n) {
    const QPainterPath::Element &el = path.elementAt(i);
    if (el.isMoveTo()) {
      result.moveTo(f(QPointF(el.x, el.y)));
      ++i;
    } else if (el.isLineTo()) {
      result.lineTo(f(QPointF(el.x, el.y)));
      ++i;
    } else { // CurveToElement + 2x CurveToDataElement (cubic bezier)
      QPointF c1 = f(QPointF(el.x, el.y));
      QPointF c2 = c1, ep = c1;
      if (i + 2 < n) {
        const QPainterPath::Element &e2 = path.elementAt(i + 1);
        const QPainterPath::Element &e3 = path.elementAt(i + 2);
        c2 = f(QPointF(e2.x, e2.y));
        ep = f(QPointF(e3.x, e3.y));
      }
      result.cubicTo(c1, c2, ep);
      i += 3;
    }
  }
  return result;
}
} // namespace

BlockItem::BlockItem(const QList<CadItem *> &items,
                     const MyVector &insertionPoint, const QString &name,
                     double scaleX, double scaleY, double rotation,
                     QGraphicsItem *parent)
    : CadItem(parent), m_items(items), m_insertionPoint(insertionPoint),
      m_blockName(name), m_scaleX(scaleX), m_scaleY(scaleY),
      m_rotation(rotation) {

  if (m_blockName.trimmed().isEmpty()) {
    m_blockName = "Block";
  }

  // Set all items as children of this block
  for (int i = 0; i < m_items.size(); ++i) {
    CadItem *item = m_items.at(i);
    if (item) {
      item->setParentItem(this);
      // 블록의 자식들은 개별적으로 선택되지 않도록 설정
      item->setFlag(QGraphicsItem::ItemIsSelectable, false);
      // 자식을 부모(BlockItem) 위에 배치
      item->setFlag(QGraphicsItem::ItemStacksBehindParent, false);
    }
  }

  // [수정] 자식 데이터를 직접 건드리지 않고 QGraphicsItem의 변환 행렬을 이용함
  // DXF에서 블록 내 엔티티는 삽입점(0,0) 기준이므로 이 방식으로 완벽히 호환됨
  setPos(m_insertionPoint.toPointF());
  setRotation(m_rotation); // DXF는 CCW+, Qt는 CW+ (씬이 뒤집혀 있으므로 그대로 적용)
  
  QTransform xform;
  xform.scale(m_scaleX, m_scaleY);
  setTransform(xform);

  setFlags(QGraphicsItem::ItemIsSelectable |
           QGraphicsItem::ItemSendsGeometryChanges |
           // [성능] 블록 shape()는 모든 자식 shape의 합집합이므로 자식을 항상
           // 포함한다. 이 힌트로 씬의 items()/컬링이 블록이 영역 밖일 때 자식
           // 수천 개로의 재귀를 건너뛸 수 있다.
           QGraphicsItem::ItemContainsChildrenInShape);
  setAcceptHoverEvents(true);

  // 삽입점 그립은 로컬 좌표 (0,0)에 위치하게 됨
  m_insertionGrip = new GripItem(0, this);
  m_insertionGrip->setPos(0, 0); 
  m_insertionGrip->setVisible(false);

  initBaseEntity();
}

BlockItem::~BlockItem() {
  // All items in m_items and m_insertionGrip are children of this item,
  // so QGraphicsItem will delete them automatically.
  m_items.clear();
}

void BlockItem::initBaseEntity() {
  // Just use a generic entity layer ID for the block since it lacks a single
  // shape layerId is already 0 from the constructor
}

CadItem *BlockItem::clone() const {
  QList<CadItem *> clonedItems;
  for (CadItem *item : m_items) {
    if (item)
      clonedItems.append(item->clone());
  }

  BlockItem *newItem = new BlockItem(clonedItems, m_insertionPoint, m_blockName,
                                     m_scaleX, m_scaleY, m_rotation);
  newItem->setLayerName(m_layerName);
  newItem->setPen(m_pen);
  return newItem;
}

void BlockItem::invalidateGeometryCache() {
  m_geometryCacheDirty = true;
  prepareGeometryChange();
}

void BlockItem::rebuildGeometryCacheIfNeeded() const {
  if (!m_geometryCacheDirty)
    return;

  QPainterPath geomPath;
  QPainterPath shapePath;
  QRectF rect;

  // [수정] 자식 shape()의 선택 스트로크 폭(4.0)은 자식-로컬 단위라 블록 스케일이
  // 곱해져 커진다(도면틀 스케일 100 → 400). 스케일 블록은 폭을 스케일로 나눠
  // 씬 기준 폭이 스케일 1 블록과 같아지도록 직접 스트로크한다.
  const QTransform bt = transform();
  const double blockScale =
      std::sqrt(std::abs(bt.m11() * bt.m22() - bt.m12() * bt.m21()));
  const bool rescaleStroke =
      blockScale > 1e-12 && std::abs(blockScale - 1.0) > 1e-6;
  QPainterPathStroker stroker;
  if (rescaleStroke)
    stroker.setWidth(4.0 / blockScale);

  for (CadItem *item : m_items) {
    if (!item || !item->isVisible())
      continue;

    //  자식의 로컬 변환(텍스트 반전 등)을 반영한 위치로 옮겨 누적
    QTransform t;
    t.translate(item->pos().x(), item->pos().y());
    t *= item->transform();

    const QPainterPath childGeom = item->geometryPath();
    geomPath.addPath(t.map(childGeom));
    // 텍스트·해치는 면적형 shape라 스케일과 함께 늘어나는 것이 맞다
    if (rescaleStroke && !dynamic_cast<TextItem *>(item) &&
        !dynamic_cast<HatchItem *>(item))
      shapePath.addPath(t.map(stroker.createStroke(childGeom)));
    else
      shapePath.addPath(t.map(item->shape()));

    //  자식들의 순수 boundingRect가 아닌 transform() 반영 영역을 합침
    QRectF childRect = item->transform().mapRect(item->boundingRect());
    childRect.translate(item->pos());
    rect = rect.isNull() ? childRect : rect.united(childRect);
  }

  m_cachedGeometryPath = geomPath;
  m_cachedShape = shapePath;
  // [개선] shape() 두께(4.0)에 맞춰 전체 마진을 3.0으로 최적화
  m_cachedBoundingRect = rect.adjusted(-3, -3, 3, 3);
  m_geometryCacheDirty = false;
}

QRectF BlockItem::boundingRect() const {
  if (m_items.isEmpty())
    return QRectF();
  rebuildGeometryCacheIfNeeded();
  return m_cachedBoundingRect;
}

void BlockItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                      QWidget *widget) {
  Q_UNUSED(widget);

  // BlockItem content is drawn by its children.
  // [복구] 하이라이트(Glow + Dash)를 원본 아래에 배경으로 그리기
  // [핵심] 프린터 출력 시에는 선택 하이라이트를 그리지 않음
  if ((option->state & QStyle::State_Selected) &&
      painter->device()->devType() == QInternal::Widget) {
    // [성능] geometryPath()를 두 번 호출하지 않고 캐시본을 한 번만 받아 재사용
    const QPainterPath highlightPath = geometryPath();

    painter->save();

    // 1. 글로우(Glow) 효과: 약간 더 두껍고 반투명한 하늘색 (LineItem과 통일 -
    // 80 alpha)
    QPen glowPen(QColor(0, 255, 255, 80));
    glowPen.setCosmetic(true);
    glowPen.setWidthF(5.0);
    painter->setPen(glowPen);
    painter->setBrush(Qt::NoBrush);
    // [수정] shape() 대신 geometryPath()를 사용하여 단일선 스타일 하이라이트
    // 구현
    painter->drawPath(highlightPath);

    // 2. 점선 오버레이: 가시성을 위한 흰색 점선 (LineItem과 통일)
    QPen dashPen(Qt::white);
    dashPen.setCosmetic(true);
    dashPen.setWidthF(1.0);
    dashPen.setStyle(Qt::DashLine);
    painter->setPen(dashPen);
    painter->drawPath(highlightPath);

    painter->restore();
  }
}

QPainterPath BlockItem::geometryPath() const {
  rebuildGeometryCacheIfNeeded();
  return m_cachedGeometryPath;
}

QPainterPath BlockItem::shape() const {
  rebuildGeometryCacheIfNeeded();
  return m_cachedShape;
}

QList<SnapPoint> BlockItem::getSnapPoints() const {
  QList<SnapPoint> snapPts;
  // Use the block insertion point as a node
  snapPts.append(SnapPoint(m_insertionPoint, SnapType::Endpoint));

  for (CadItem *item : m_items) {
    if (item) {
      QList<SnapPoint> childPts = item->getSnapPoints();
      // Adjust child pts (child-local) to scene space directly, so the
      // result matches the scene-coordinate contract every other CadItem
      // follows (mapFromItem(item, ...) would stop at BlockItem's own local
      // frame instead of the scene, which is wrong once the block itself is
      // placed/rotated/scaled away from identity).
      for (SnapPoint &pt : childPts) {
        pt.pos = MyVector(item->mapToScene(QPointF(pt.pos.x, pt.pos.y)));
        snapPts.append(pt);
      }
    }
  }
  return snapPts;
}

QList<QPointF> BlockItem::getGripPoints() const {
  return {m_insertionPoint.toPointF()};
}

void BlockItem::applyGripMove(int index, const MyVector &pos) {
  if (index == 0) {
    // Grip moving the block is essentially moving the entire block
    MyVector delta = pos - m_insertionPoint;
    move(delta);

    // Update grip position conceptually
    // In AutoCAD, moving the grip moves the block.
  }
}

QPainterPath BlockItem::getGhostShape(int index, const MyVector &pos) const {
  if (index == 0) {
    MyVector delta = pos - m_insertionPoint;
    return getMovedPath(delta);
  }
  return QPainterPath();
}

double BlockItem::distanceTo(const MyVector &pos) const {
  double minDist = 999999.0;
  for (CadItem *item : m_items) {
    if (item) {
      // [수정] pos는 scene 좌표계(다른 모든 CadItem::distanceTo와 동일한 계약).
      // 이전에는 mapToItem(item, pos)를 써서 pos를 "BlockItem 자신의 로컬
      // 좌표"로 잘못 취급했다 — BlockItem은 삽입점에 setPos()/setRotation()/
      // setTransform()을 적용하므로, 블록이 원점·회전 없음이 아닌 한(즉 실제로
      // 배치된 거의 모든 블록에서) 결과가 완전히 어긋나 distanceTo()가 항상
      // 큰 값을 반환 → 클릭해도 선택되지 않고 박스 선택으로 빠지는 원인이었다.
      // item->mapFromScene()는 부모 체인을 그대로 따라가므로 곧바로 올바른
      // 자식-로컬 좌표를 준다.
      double d;
      if (dynamic_cast<BlockItem *>(item)) {
        // 중첩 블록은 자신의 distanceTo()가 scene 좌표를 받아 scene 거리를 준다
        d = item->distanceTo(pos);
      } else {
        QPointF localPosF = item->mapFromScene(pos.toPointF());
        MyVector localPos(localPosF);
        // [수정] 자식 distanceTo()는 자식-로컬 단위 거리다. 블록 스케일(예: 도면틀
        // 삽입 시 도면 스케일 100)이 곱해지지 않으면 선택 허용오차가 스케일배로
        // 커져 도면에서 멀리 떨어진 곳을 클릭해도 블록이 선택되었다.
        // sceneTransform()의 면적 배율로 scene 거리로 환산한다.
        const QTransform st = item->sceneTransform();
        const double s = std::sqrt(std::abs(st.m11() * st.m22() - st.m12() * st.m21()));
        d = item->distanceTo(localPos) * (s > 1e-12 ? s : 1.0);
      }

      if (d < minDist) {
        minDist = d;
      }
    }
  }
  return minDist;
}

SnapPoint BlockItem::getNearestSnap(const MyVector &pos) const {
  SnapPoint nearest;
  nearest.type = SnapType::None;
  double minDist = 999999.0;

  for (CadItem *item : m_items) {
    if (item) {
      // [수정] distanceTo()와 동일한 이유로 scene 좌표를 item->mapFromScene()로
      // 자식-로컬 좌표로 바꿔야 한다 (mapToItem(item, pos)는 pos를 잘못 취급함).
      MyVector localPos(item->mapFromScene(pos.toPointF()));
      SnapPoint p = item->getNearestSnap(localPos);
      if (p.type != SnapType::None) {
        // 자식-로컬 결과를 scene 좌표로 되돌림 (mapFromItem이 아니라 mapToScene)
        MyVector worldPt(item->mapToScene(QPointF(p.pos.x, p.pos.y)));
        double d = pos.distanceTo(worldPt);
        if (d < minDist) {
          minDist = d;
          nearest = p;
          nearest.pos = worldPt;
        }
      }
    }
  }
  return nearest;
}

void BlockItem::move(const MyVector &delta) {
  prepareGeometryChange();
  m_insertionPoint = m_insertionPoint + delta;
  setPos(m_insertionPoint.toPointF());
  updateGrips();
}

void BlockItem::mirror(const MyVector &p1, const MyVector &p2) {
  // [수정] 예전에는 자식(m_items)의 로컬(블록 기준) 좌표를 씬 좌표인 p1,p2로
  // 직접 mirror()했다. 자식 좌표는 블록의 배치(회전/스케일/삽입점) 이전 기준의
  // 로컬 좌표라서, 씬 좌표 기준선으로 그대로 반사하면 좌표계가 어긋나 블록이
  // 엉뚱한 위치/모양으로 튀는 버그였다. rotate()/scale()이 자식 데이터를 건드리지
  // 않고 컨테이너(this)의 배치만 바꾸는 것과 동일하게, mirror도 자식은 그대로 두고
  // 블록 자신의 회전각/스케일만 바꿔서 표현한다.
  //
  // 회전각 rot, 스케일 (sx,sy)로 배치된 강체를 각도 θ(=mirror 선의 각도)로
  // 반사하면: new_rot = 2θ - rot, new_sy = -sy (좌우 반전은 한 축의 스케일
  // 부호를 뒤집는 것으로 표현된다).
  MyVector d = p2 - p1;
  double lineAngleDeg = qRadiansToDegrees(std::atan2(d.y, d.x));

  prepareGeometryChange();
  m_insertionPoint = MyMath::reflectPoint(m_insertionPoint, p1, p2);
  m_rotation = 2.0 * lineAngleDeg - m_rotation;
  m_scaleY = -m_scaleY;

  setPos(m_insertionPoint.toPointF());
  setRotation(m_rotation);
  QTransform xform;
  xform.scale(m_scaleX, m_scaleY);
  setTransform(xform);

  updateGrips();
  invalidateGeometryCache();
}

QPainterPath BlockItem::getMovedPath(const MyVector &delta) const {
  // [수정] 자식 로컬 변환만 합성하고 블록 자신의 배치(pos/rotation/transform)를
  // 반영하지 않아, 고스트 미리보기가 원점 기준으로 그려지는 버그였다.
  // mapToScene(geometryPath())로 "현재 씬 좌표 형상"을 얻은 뒤 다른 CadItem들과
  // 동일하게 delta만큼 평행이동한다.
  return mapToScene(geometryPath()).translated(delta.x, delta.y);
}

QPainterPath BlockItem::getMirroredPath(const MyVector &p1,
                                        const MyVector &p2) const {
  // [수정] 자식 로컬 좌표를 씬 좌표 기준선(p1,p2)으로 바로 mirror()하던 것을,
  // 현재 씬 좌표 형상에 다른 CadItem들과 동일한 MyMath::reflectPoint를
  // 점 단위로 적용하는 방식으로 교체 (mirror()의 실제 적용 로직과 결과가
  // 일치하도록).
  QPainterPath current = mapToScene(geometryPath());
  return mapPathPoints(current, [&p1, &p2](const QPointF &pt) {
    return MyMath::reflectPoint(MyVector(pt), p1, p2).toPointF();
  });
}

void BlockItem::rotate(const MyVector &base, double angle) {
  prepareGeometryChange();
  m_insertionPoint = MyMath::rotatePoint(m_insertionPoint, base, angle);
  m_rotation += angle;
  
  setPos(m_insertionPoint.toPointF());
  setRotation(m_rotation);
  updateGrips();
}

QPainterPath BlockItem::getRotatedPath(const MyVector &base,
                                       double angle) const {
  // [수정] 자식 로컬 좌표에 씬 좌표 기준점(base)을 그대로 적용하던 버그를
  // getMirroredPath()와 동일한 방식(현재 씬 좌표 형상 + 점 단위 변환)으로 수정.
  QPainterPath current = mapToScene(geometryPath());
  return mapPathPoints(current, [&base, angle](const QPointF &pt) {
    return MyMath::rotatePoint(MyVector(pt), base, angle).toPointF();
  });
}

void BlockItem::scale(const MyVector &base, double scaleFactor) {
  prepareGeometryChange();
  m_insertionPoint = MyMath::scalePoint(m_insertionPoint, base, scaleFactor);
  m_scaleX *= scaleFactor;
  m_scaleY *= scaleFactor;
  
  setPos(m_insertionPoint.toPointF());
  QTransform xform;
  xform.scale(m_scaleX, m_scaleY);
  setTransform(xform);
  updateGrips();
  invalidateGeometryCache(); // shape 스트로크 폭이 블록 스케일에 의존
}

QPainterPath BlockItem::getScaledPath(const MyVector &base,
                                      double scaleFactor) const {
  // [수정] getRotatedPath()와 동일한 이유로 현재 씬 좌표 형상 기준으로 계산.
  QPainterPath current = mapToScene(geometryPath());
  return mapPathPoints(current, [&base, scaleFactor](const QPointF &pt) {
    return MyMath::scalePoint(MyVector(pt), base, scaleFactor).toPointF();
  });
}

void BlockItem::stretch(const QList<int> &indices, const MyVector &delta) {
  // Block handles stretching based on its grip point (if crossing window
  // catches grip)
  if (indices.contains(0)) {
    move(delta);
  }
}

QPainterPath BlockItem::getStretchedPath(const QList<int> &indices,
                                         const MyVector &delta) const {
  if (indices.contains(0)) {
    return getMovedPath(delta);
  }
  return shape();
}

void BlockItem::updateEndpoint(const QPointF &newPos, bool isFirstEnd) {
  Q_UNUSED(newPos);
  Q_UNUSED(isFirstEnd);
}

void BlockItem::setLayerName(const QString &name) {
  CadItem::setLayerName(name); // 자신(BlockItem)의 m_layerName 업데이트
  // [수정] 자식 아이템들의 레이어를 무조건 덮어쓰지 않고,
  // 레이어가 "0" 또는 "Default"인 아이템만 블록의 레이어를 따르도록 합니다.
  // (AutoCAD of standard block layer behavior)
  for (int i = 0; i < m_items.size(); ++i) {
    CadItem *item = m_items.at(i);
    if (item && (item->layerName() == "0" || item->layerName() == "Default")) {
      item->setLayerName(name);
    }
  }
}

void BlockItem::setPen(const QPen &pen) {
  CadItem::setPen(pen);
  // Propagate to children that match block's layer OR are on "0"/"Default"
  for (int i = 0; i < m_items.size(); ++i) {
    CadItem *item = m_items.at(i);
    if (item && (item->layerName() == m_layerName || item->layerName() == "0" ||
                 item->layerName() == "Default"))
      item->setPen(pen);
  }
  // 선 두께가 바뀌면 자식 shape()의 스트로크 폭도 달라지므로 캐시 무효화
  invalidateGeometryCache();
}

QVariant BlockItem::itemChange(GraphicsItemChange change,
                               const QVariant &value) {
  if (change == QGraphicsItem::ItemSelectedChange) {
    bool isSel = value.toBool();

    update(); // 자신도 다시 그려서 하이라이트 반영

    if (!isSel) {
      if (m_insertionGrip) {
        m_insertionGrip->setVisible(false);
      }
    }
  }
  return CadItem::itemChange(change, value);
}

void BlockItem::setGripsVisible(bool visible) {
  if (m_insertionGrip)
    m_insertionGrip->setVisible(visible);
}

void BlockItem::updateGrips() {
  if (m_insertionGrip) {
    m_insertionGrip->setPos(0, 0);
  }
}

QMap<QString, QVariant> BlockItem::getProperties() const {
  QMap<QString, QVariant> props;
  props["Type"] = "Block";
  props["Name"] = m_blockName;
  props["Insert X"] = m_insertionPoint.x;
  props["Insert Y"] = m_insertionPoint.y;
  props["Scale X"] = m_scaleX;
  props["Scale Y"] = m_scaleY;
  props["Rotation"] = m_rotation;
  props["Elements"] = (int)m_items.size();
  props["Layer"] = layerName();
  return props;
}

bool BlockItem::setProperty(const QString &key, const QVariant &value) {
  bool ok;
  if (key == "Insert X") {
    double v = value.toDouble(&ok);
    if (ok)
      move(MyVector(v - m_insertionPoint.x, 0));
    return ok;
  } else if (key == "Insert Y") {
    double v = value.toDouble(&ok);
    if (ok)
      move(MyVector(0, v - m_insertionPoint.y));
    return ok;
  } else if (key == "Scale X") {
    double v = value.toDouble(&ok);
    if (ok && std::abs(v) > 1e-9) {
      m_scaleX = v;
      QTransform xform;
      xform.scale(m_scaleX, m_scaleY);
      setTransform(xform);
      invalidateGeometryCache();
    }
    return ok;
  } else if (key == "Scale Y") {
    double v = value.toDouble(&ok);
    if (ok && std::abs(v) > 1e-9) {
      m_scaleY = v;
      QTransform xform;
      xform.scale(m_scaleX, m_scaleY);
      setTransform(xform);
      invalidateGeometryCache();
    }
    return ok;
  } else if (key == "Rotation") {
    double v = value.toDouble(&ok);
    if (ok) {
      m_rotation = v;
      setRotation(m_rotation);
    }
    return ok;
  } else if (key == "Name") {
    m_blockName = value.toString();
    return true;
  } else if (key == "Layer") {
    setLayerName(value.toString());
    return true;
  } else if (key == "Type" || key == "Elements") {
    return false; // Read-only
  }
  return false;
}
