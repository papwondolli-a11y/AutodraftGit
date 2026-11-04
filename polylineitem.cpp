#include "polylineitem.h"
#include "gripitem.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>

PolylineItem::PolylineItem(const std::vector<MyVector> &vertices, bool isClosed,
                           QGraphicsItem *parent)
    : CadItem(parent) {
  m_data.vertices = vertices;
  m_data.isClosed = isClosed;
  m_pen = QPen(Qt::white, 0);
  updatePath();
  updateGrips();
}

PolylineItem::PolylineItem(const MyPolyline &poly, QGraphicsItem *parent)
    : CadItem(parent), m_data(poly) {
  m_pen = QPen(Qt::white, 0);
  updatePath();
  updateGrips();
}

QRectF PolylineItem::boundingRect() const {
  qreal margin = m_pen.widthF() / 2.0 + 2.0;
  return m_path.boundingRect().adjusted(-margin, -margin, margin, margin);
}

void PolylineItem::paint(QPainter *painter,
                         const QStyleOptionGraphicsItem *option,
                         QWidget *widget) {
  Q_UNUSED(widget);

  QPen currentPen = m_pen;
  currentPen.setCosmetic(true);

  // 씬에서 선가중치 표시 여부 확인
  MyGraphicsScene *myScene = qobject_cast<MyGraphicsScene *>(scene());
  bool showWeight = myScene ? myScene->showLineWeight() : false;

  if (!showWeight) {
    // 가중치 끄기: 무조건 1픽셀 코스메틱 펜
    currentPen.setCosmetic(true);
    currentPen.setWidthF(1.0);
  } else {
    // 가중치 켜기: 실제 두께 반영하되 화면에서 1픽셀 미만으로 흐려지는 것 방지
    currentPen.setCosmetic(false);
    qreal scale = qAbs(painter->transform().m11());
    if (scale > 0.0 && currentPen.widthF() * scale < 1.0) {
      currentPen.setWidthF(1.0 / scale);
    }
  }

  // 1. [복구] 하이라이트(Glow + Dash)를 원본 아래에 배경으로 그리기
  // [핵심] 프린터 출력 시에는 선택 하이라이트를 그리지 않음
  if ((option->state & QStyle::State_Selected) &&
      painter->device()->devType() == QInternal::Widget) {
    painter->save();

    // 1. 글로우 효과 (80 alpha)
    QPen glowPen(QColor(0, 255, 255, 80));
    glowPen.setCosmetic(true);
    glowPen.setWidthF(currentPen.widthF() + 4.0);
    painter->setPen(glowPen);
    painter->drawPath(m_path);

    // 2. 점선 오버레이
    QPen dashPen(Qt::white);
    dashPen.setCosmetic(true);
    dashPen.setWidthF(1.0);
    dashPen.setStyle(Qt::DashLine);
    painter->setPen(dashPen);
    painter->drawPath(m_path);

    painter->restore();
  }

  // 2. 원본 객체 그리기 (하이라이트 위에 그림)
  if (m_brush.style() != Qt::NoBrush) {
    painter->fillPath(m_path, m_brush);
  }
  painter->setPen(currentPen);
  painter->drawPath(m_path);
}

QPainterPath PolylineItem::geometryPath() const { return m_path; }

QPainterPath PolylineItem::shape() const {
  QPainterPath path = geometryPath();
  QPainterPathStroker stroker;
  stroker.setWidth(10.0); // 선택 감도
  return stroker.createStroke(path);
}

QList<SnapPoint> PolylineItem::getSnapPoints() const {
  QList<SnapPoint> pts;
  for (const auto &v : m_data.vertices) {
    pts.append({v, SnapType::Endpoint});
  }

  // 중간점들 추가
  size_t count =
      m_data.isClosed ? m_data.vertices.size() : m_data.vertices.size() - 1;
  for (size_t i = 0; i < count; ++i) {
    MyVector v1 = m_data.vertices[i];
    MyVector v2 = m_data.vertices[(i + 1) % m_data.vertices.size()];
    pts.append({(v1 + v2) * 0.5, SnapType::Midpoint});
  }

  return pts;
}

QList<QPointF> PolylineItem::getGripPoints() const {
  QList<QPointF> pts;
  for (const auto &v : m_data.vertices) {
    pts.append(v.toPointF());
  }
  return pts;
}

void PolylineItem::applyGripMove(int index, const MyVector &pos) {
  if (index >= 0 && index < (int)m_data.vertices.size()) {
    prepareGeometryChange();
    m_data.vertices[index] = pos;
    updatePath();
    updateGrips();
  }
}

QPainterPath PolylineItem::getGhostShape(int index, const MyVector &pos) const {
  if (index < 0 || index >= (int)m_data.vertices.size())
    return QPainterPath();

  MyPolyline temp = m_data;
  temp.vertices[index] = pos;

  QPainterPath path;
  if (temp.vertices.empty())
    return path;

  path.moveTo(temp.vertices[0].toPointF());
  for (size_t i = 1; i < temp.vertices.size(); ++i) {
    path.lineTo(temp.vertices[i].toPointF());
  }
  if (temp.isClosed)
    path.closeSubpath();

  return path;
}

double PolylineItem::distanceTo(const MyVector &pos) const {
  if (m_data.vertices.size() < 2)
    return 1e10;

  double minDist = -1.0;
  size_t count =
      m_data.isClosed ? m_data.vertices.size() : m_data.vertices.size() - 1;

  for (size_t i = 0; i < count; ++i) {
    MyVector v1 = m_data.vertices[i];
    MyVector v2 = m_data.vertices[(i + 1) % m_data.vertices.size()];

    MyVector ab = v2 - v1;
    MyVector ap = pos - v1;
    double lenSq = ab.x * ab.x + ab.y * ab.y;

    double d;
    if (lenSq < 1e-9) {
      d = v1.distanceTo(pos);
    } else {
      double t = (ap.x * ab.x + ap.y * ab.y) / lenSq;
      if (t < 0.0)
        t = 0.0;
      else if (t > 1.0)
        t = 1.0;
      MyVector closestPoint = v1 + (ab * t);
      d = pos.distanceTo(closestPoint);
    }

    if (minDist < 0 || d < minDist)
      minDist = d;
  }
  return minDist;
}

SnapPoint PolylineItem::getNearestSnap(const MyVector &pos) const {
  MyVector pt = m_data.getNearestPoint(pos);
  return {pt, SnapType::Nearest};
}

void PolylineItem::move(const MyVector &delta) {
  prepareGeometryChange();
  for (auto &v : m_data.vertices) {
    v += delta;
  }
  updatePath();
  updateGrips();
}

void PolylineItem::mirror(const MyVector &p1, const MyVector &p2) {
  prepareGeometryChange();
  for (auto &v : m_data.vertices) {
    v = MyMath::reflectPoint(v, p1, p2);
  }
  updatePath();
  updateGrips();
  update();
}

QPainterPath PolylineItem::getMovedPath(const MyVector &delta) const {
  QPainterPath path;
  if (m_data.vertices.empty())
    return path;

  path.moveTo((m_data.vertices[0] + delta).toPointF());
  for (size_t i = 1; i < m_data.vertices.size(); ++i) {
    path.lineTo((m_data.vertices[i] + delta).toPointF());
  }
  if (m_data.isClosed)
    path.closeSubpath();

  return path;
}

CadItem *PolylineItem::clone() const {
  auto *item = new PolylineItem(m_data);
  item->setLayerName(layerName());
  item->setPen(m_pen);
  item->setBrush(m_brush);
  return item;
}

QMap<QString, QVariant> PolylineItem::getProperties() const {
  QMap<QString, QVariant> props;
  props["Type"] = "Polyline";
  props["Layer"] = layerName();
  props["VerticesCount"] = (int)m_data.vertices.size();
  props["IsClosed"] = m_data.isClosed;
  return props;
}

bool PolylineItem::setProperty(const QString &key, const QVariant &value) {
  if (key == "Layer") {
    setLayerName(value.toString());
    return true;
  }

  if (key == "IsClosed") {
    prepareGeometryChange();
    m_data.isClosed = value.toBool();
    updatePath();
    update();
    return true;
  }

  // 폴리라인의 개별 정점 수정은 인덱스 처리가 필요하므로 일단 여기서는
  // 생략하거나 Vertices_0_X 같은 네이밍 규칙 필요
  return false;
}

void PolylineItem::updateEndpoint(const QPointF &newPos, bool isFirstEnd) {
  if (m_data.vertices.empty())
    return;
  prepareGeometryChange();
  if (isFirstEnd) {
    m_data.vertices.front() = MyVector(newPos);
  } else {
    m_data.vertices.back() = MyVector(newPos);
  }
  updatePath();
  updateGrips();
  update();
}

void PolylineItem::setPolyline(const MyPolyline &poly) {
  prepareGeometryChange();
  m_data = poly;
  updatePath();
  updateGrips();
}

QVariant PolylineItem::itemChange(GraphicsItemChange change,
                                  const QVariant &value) {
  if (change == QGraphicsItem::ItemSelectedChange) {
    bool selected = value.toBool();
    if (!selected) {
      setGripsVisible(false);
    }
  }
  return CadItem::itemChange(change, value);
}

void PolylineItem::updatePath() {
  m_path = QPainterPath();
  if (m_data.vertices.empty())
    return;

  m_path.moveTo(m_data.vertices[0].toPointF());

  size_t count = m_data.vertices.size();
  for (size_t i = 0; i < (m_data.isClosed ? count : count - 1); ++i) {
    size_t i1 = i;
    size_t i2 = (i + 1) % count;
    MyVector v1 = m_data.vertices[i1];
    MyVector v2 = m_data.vertices[i2];
    double b = (i1 < m_data.bulges.size()) ? m_data.bulges[i1] : 0.0;

    if (qAbs(b) < 1e-6) {
      m_path.lineTo(v2.toPointF());
    } else {
      // Bulge를 사용하여 Arc 그리기
      double dx = v2.x - v1.x;
      double dy = v2.y - v1.y;
      double d = std::hypot(dx, dy);
      if (d < 1e-9) continue;

      // Included angle
      double theta = 4.0 * std::atan(b);
      // Radius
      double r = qAbs(d * (1.0 + b * b) / (4.0 * b));
      // Sagitta
      double s = (d / 2.0) * qAbs(b);
      // 중심점까지의 거리 (중점 기준 법선 방향으로 이동 거리)
      double distToCenter = r - s; // b > 0 일 때 중심은 chord의 오른쪽에 있음 (CCW 벌지)

      // 중점
      double mx = (v1.x + v2.x) / 2.0;
      double my = (v1.y + v2.y) / 2.0;
      
      // 수직 벡터 (v1->v2의 법선) -> RIGHT 방향
      // Y-down 기하학에서 b > 0 (CCW 벌지)를 짧은 경로로 그리려면 중심을 오른쪽에 두고 CW로 그려야 합니다.
      double nx = -dy / d;
      double ny = dx / d;
      
      // b < 0 이면 CW 벌지 -> 중심은 선의 왼쪽에 위치해야 함
      if (b < 0) {
          nx = -nx;
          ny = -ny;
      }
      
      double cx = mx + nx * distToCenter;
      double cy = my + ny * distToCenter;
      
      double sweep = theta * 180.0 / MyMath::PI;
      // Qt QPainterPath::arcTo: startAngle(0=3시, CCW+), sweepLength(CCW+)
      // [최종 교정] Y-down 시각적 좌표계의 각도를 직접 사용 (마이너스 제거)
      double startAngle = std::atan2(v1.y - cy, v1.x - cx) * 180.0 / MyMath::PI;
      
      // Y-down에서 b>0(CCW 벌지)를 짧은 경로로 그리려면 시각적 CW sweep(-sweep)을 사용
      m_path.arcTo(cx - r, cy - r, 2 * r, 2 * r, -startAngle, -sweep);
    }
  }
}

void PolylineItem::updateGrips() {
  // 정점 개수와 그립 개수 맞추기
  while (m_grips.size() < (int)m_data.vertices.size()) {
    GripItem *grip = new GripItem(m_grips.size(), this);
    grip->setVisible(isSelected());
    m_grips.append(grip);
  }
  while (m_grips.size() > (int)m_data.vertices.size()) {
    GripItem *grip = m_grips.takeLast();
    delete grip;
  }

  // 그립 위치 업데이트
  for (int i = 0; i < m_grips.size(); ++i) {
    m_grips[i]->setPos(m_data.vertices[i].toPointF());
  }
}

void PolylineItem::setGripsVisible(bool visible) {
  for (auto *grip : std::as_const(m_grips)) {
    grip->setVisible(visible);
  }
}

QPainterPath PolylineItem::getMirroredPath(const MyVector &p1,
                                           const MyVector &p2) const {
  QPainterPath path;
  if (m_data.vertices.empty())
    return path;

  for (size_t i = 0; i < m_data.vertices.size(); ++i) {
    MyVector mv = MyMath::reflectPoint(m_data.vertices[i], p1, p2);
    if (i == 0)
      path.moveTo(mv.toPointF());
    else
      path.lineTo(mv.toPointF());
  }

  if (m_data.isClosed && m_data.vertices.size() > 2) {
    path.closeSubpath();
  }

  return path;
}

void PolylineItem::rotate(const MyVector &base, double angle) {
  prepareGeometryChange();
  for (auto &v : m_data.vertices) {
    v = MyMath::rotatePoint(v, base, angle);
  }
  updatePath();
  updateGrips();
  update();
}

QPainterPath PolylineItem::getRotatedPath(const MyVector &base,
                                          double angle) const {
  QPainterPath path;
  if (m_data.vertices.empty())
    return path;

  MyVector rStart = MyMath::rotatePoint(m_data.vertices[0], base, angle);
  path.moveTo(rStart.toPointF());

  for (size_t i = 1; i < m_data.vertices.size(); ++i) {
    MyVector rPt = MyMath::rotatePoint(m_data.vertices[i], base, angle);
    path.lineTo(rPt.toPointF());
  }

  if (m_data.isClosed && m_data.vertices.size() > 2)
    path.closeSubpath();

  return path;
}

void PolylineItem::scale(const MyVector &base, double scaleFactor) {
  prepareGeometryChange();
  for (auto &v : m_data.vertices) {
    v = MyMath::scalePoint(v, base, scaleFactor);
  }
  updatePath();
  updateGrips();
  update();
}

QPainterPath PolylineItem::getScaledPath(const MyVector &base,
                                         double scaleFactor) const {
  QPainterPath path;
  if (m_data.vertices.empty())
    return path;

  MyVector sStart = MyMath::scalePoint(m_data.vertices[0], base, scaleFactor);
  path.moveTo(sStart.toPointF());

  for (size_t i = 1; i < m_data.vertices.size(); ++i) {
    MyVector sPt = MyMath::scalePoint(m_data.vertices[i], base, scaleFactor);
    path.lineTo(sPt.toPointF());
  }

  if (m_data.isClosed && m_data.vertices.size() > 2)
    path.closeSubpath();

  return path;
}

void PolylineItem::stretch(const QList<int> &indices, const MyVector &delta) {
  prepareGeometryChange();
  for (int idx : indices) {
    if (idx >= 0 && idx < (int)m_data.vertices.size()) {
      m_data.vertices[idx] = m_data.vertices[idx] + delta;
    }
  }
  updatePath();
  updateGrips();
  update();
}

QPainterPath PolylineItem::getStretchedPath(const QList<int> &indices,
                                            const MyVector &delta) const {
  QPainterPath path;
  if (m_data.vertices.size() < 2)
    return path;

  // 변형된 위치를 미리 계산한 임시 정점 배열
  std::vector<MyVector> movedVertices = m_data.vertices;
  for (int idx : indices) {
    if (idx >= 0 && idx < (int)movedVertices.size()) {
      movedVertices[idx] = movedVertices[idx] + delta;
    }
  }

  // 각 세그먼트를 검사하여 "늘어나는" 또는 "이동하는" 세그먼트만 추가
  auto getSegmentPath = [&](size_t i, size_t j) {
    bool iChanged = indices.contains((int)i);
    bool jChanged = indices.contains((int)j);
    // 둘 중 하나라도 바뀌면 그 세그먼트는 'Stretch'에 참여하는 부분임
    if (iChanged || jChanged) {
      path.moveTo(movedVertices[i].toPointF());
      path.lineTo(movedVertices[j].toPointF());
    }
  };

  for (size_t i = 0; i < m_data.vertices.size() - 1; ++i) {
    getSegmentPath(i, i + 1);
  }

  if (m_data.isClosed && m_data.vertices.size() > 2) {
    getSegmentPath(m_data.vertices.size() - 1, 0);
  }

  return path;
}
