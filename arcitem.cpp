#include "arcitem.h"
#include "hairlinepen.h"
#include "gripitem.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

// 각도 정규화 (0 ~ 360)
static double normalizeAngle(double angle) {
  while (angle < 0)
    angle += 360.0;
  while (angle >= 360.0)
    angle -= 360.0;
  return angle;
}

// ---------------------------------------------------------
// Static Helpers (Model <-> View 변환 로직)
// ---------------------------------------------------------
MyArc ArcItem::createFrom3Points(const MyVector &center, const MyVector &start,
                                 const MyVector &end) {
  double radius = center.distanceTo(start);
  if (radius < 0.001)
    return MyArc();

  // atan2(y, x) -> Radian -> Degree
  double aStart =
      qRadiansToDegrees(std::atan2(start.y - center.y, start.x - center.x));
  double aEnd =
      qRadiansToDegrees(std::atan2(end.y - center.y, end.x - center.x));

  aStart = normalizeAngle(aStart);
  aEnd = normalizeAngle(aEnd);

  double span = aEnd - aStart;
  if (span < 0)
    span += 360.0;

  return MyArc(center, radius, aStart, span);
}

QPainterPath ArcItem::createQtPath(const MyArc &arc) {
  QPainterPath path;
  if (arc.radius < 0.001)
    return path;

  // 데카르트 좌표계(Y-Up) 보정: Qt의 arcTo는 화면 Y-Down 기준으로 CCW를
  // 계산하므로 Y-Up 논리 좌표계와 일치시키기 위해 시작각과 스팬을 반전(-)
  // 시킵니다.
  double drawStart = -arc.startAngle;
  double drawSpan = -arc.spanAngle;

  double r = arc.radius;
  QRectF rect(arc.center.x - r, arc.center.y - r, r * 2, r * 2);

  path.arcMoveTo(rect, drawStart);
  path.arcTo(rect, drawStart, drawSpan);

  return path;
}

// ---------------------------------------------------------
// 생성자
// ---------------------------------------------------------
ArcItem::ArcItem(const MyVector &center, const MyVector &start,
                 const MyVector &end, QGraphicsItem *parent)
    : ArcItem(createFrom3Points(center, start, end), parent) {}

ArcItem::ArcItem(const MyArc &arc, QGraphicsItem *parent)
    : CadItem(parent), m_data(arc) {
  m_pen = QPen(Qt::black, 0);
  m_centerGrip = new GripItem(0, this);
  m_startGrip = new GripItem(1, this);
  m_endGrip = new GripItem(2, this);

  updateCache();
  updateGrips();
  setGripsVisible(false);
}

// ---------------------------------------------------------
// QGraphicsItem 구현
// ---------------------------------------------------------
QRectF ArcItem::boundingRect() const {
  if (!m_isCacheValid)
    const_cast<ArcItem *>(this)->updateCache();
  // [개선] shape() 두께(4) 대응 위해 마진을 3.0으로 최적화
  return m_cachedRect.adjusted(-3, -3, 3, 3);
}

void ArcItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                    QWidget *widget) {
  Q_UNUSED(widget);
  if (!m_isCacheValid)
    const_cast<ArcItem *>(this)->updateCache();

  QPen currentPen = m_pen;

  // 1. 선가중치 표시 여부 확인
  MyGraphicsScene *myScene = qobject_cast<MyGraphicsScene *>(scene());
  bool showWeight = myScene ? myScene->showLineWeight() : false;

  if (!showWeight) {
    // [모드 A: 끄기] 화면 1.1px 가는 선 (hairlinepen.h 참고)
    HairlinePen::apply(currentPen, painter->transform());
  } else {
    // [모드 B: 켜기] 실제 두께 반영 + 최소 1픽셀 보장
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
    glowPen.setWidthF(HairlinePen::glowWidth(currentPen, painter->transform()));
    painter->setPen(glowPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(m_cachedPath);

    // 2. 점선 오버레이
    QPen dashPen(Qt::white);
    dashPen.setCosmetic(true);
    dashPen.setWidthF(1.0);
    dashPen.setStyle(Qt::DashLine);
    painter->setPen(dashPen);
    painter->drawPath(m_cachedPath);

    painter->restore();
  }

  // 2. 원본 객체 그리기 (하이라이트 위에 그림)
  painter->setPen(currentPen);
  painter->setBrush(Qt::NoBrush);
  painter->drawPath(m_cachedPath);
}

QPainterPath ArcItem::geometryPath() const {
  if (!m_isCacheValid)
    const_cast<ArcItem *>(this)->updateCache();
  return m_cachedPath;
}

QPainterPath ArcItem::shape() const {
  if (m_isShapeValid)
    return m_cachedShape;

  QPainterPath path = geometryPath();
  QPainterPathStroker s;
  s.setWidth(4.0); // 10 -> 4.0 최적화
  m_cachedShape = s.createStroke(path);
  m_isShapeValid = true;
  return m_cachedShape;
}

// ---------------------------------------------------------
// CadItem 구현
// ---------------------------------------------------------
QList<SnapPoint> ArcItem::getSnapPoints() const {
  // MyVector 연산으로 좌표 계산
  MyVector c = m_data.center;
  double r = m_data.radius;
  double radStart = qDegreesToRadians(m_data.startAngle);
  double radEnd = qDegreesToRadians(m_data.startAngle + m_data.spanAngle);
  double radMid = qDegreesToRadians(m_data.startAngle + m_data.spanAngle / 2.0);

  MyVector pStart(c.x + r * std::cos(radStart), c.y + r * std::sin(radStart));
  MyVector pEnd(c.x + r * std::cos(radEnd), c.y + r * std::sin(radEnd));
  MyVector pMid(c.x + r * std::cos(radMid), c.y + r * std::sin(radMid));

  return {{pStart, SnapType::Endpoint},
          {pEnd, SnapType::Endpoint},
          {c, SnapType::Center},
          {pMid, SnapType::Midpoint}};
}

QList<QPointF> ArcItem::getGripPoints() const {
  QList<SnapPoint> sp = getSnapPoints();
  QList<QPointF> pts;
  // Center, Start, End order as per grip indices
  if (sp.size() >= 3) {
    pts.append(sp[2].pos.toPointF()); // Center (Index 0)
    pts.append(sp[0].pos.toPointF()); // Start (Index 1)
    pts.append(sp[1].pos.toPointF()); // End (Index 2)
  }
  return pts;
}

void ArcItem::updateGrips() {
  if (!m_centerGrip)
    return;

  // snaptypes.h가 수정되었다면, SnapPoint::pos는 MyVector입니다.
  QList<SnapPoint> sp = getSnapPoints();

  // MyVector에는 .toPointF()가 있으므로 이제 정상 컴파일됩니다.
  if (sp.size() >= 3) {
    m_startGrip->setPos(sp[0].pos.toPointF());
    m_endGrip->setPos(sp[1].pos.toPointF());
    m_centerGrip->setPos(sp[2].pos.toPointF());
  }
}

void ArcItem::applyGripMove(int index, const MyVector &pos) {
  prepareGeometryChange();

  if (index == 0) { // Center
    m_data.center = pos;
  } else if (index == 1) { // Start
    m_data.radius = m_data.center.distanceTo(pos);
    double newStart = qRadiansToDegrees(
        std::atan2(pos.y - m_data.center.y, pos.x - m_data.center.x));
    newStart = normalizeAngle(newStart);

    double currentEnd = normalizeAngle(m_data.startAngle + m_data.spanAngle);
    m_data.startAngle = newStart;

    double newSpan = currentEnd - newStart;
    if (newSpan < 0)
      newSpan += 360.0;
    m_data.spanAngle = newSpan;
  } else if (index == 2) { // End
    double newEnd = qRadiansToDegrees(
        std::atan2(pos.y - m_data.center.y, pos.x - m_data.center.x));
    newEnd = normalizeAngle(newEnd);

    double newSpan = newEnd - m_data.startAngle;
    if (newSpan < 0)
      newSpan += 360.0;
    m_data.spanAngle = newSpan;
  }

  m_isCacheValid = false;
  updateGrips();
  update();
}

QPainterPath ArcItem::getGhostShape(int index, const MyVector &pos) const {
  // 미리보기 (간소화: 데이터 복사 후 임시 수정)
  MyArc temp = m_data;
  if (index == 0)
    temp.center = pos;
  // (Start/End 미리보기 로직은 applyGripMove와 동일하게 구현 필요)

  return createQtPath(temp);
}

double ArcItem::distanceTo(const MyVector &pos) const {
  // [수정] 단순히 반지름 차이가 아닌, 실제 호 선분 위의 가장 가까운 점과의 거리를 계산
  MyVector nearest = MyMath::getNearestPointOnArc(pos, m_data);
  return pos.distanceTo(nearest);
}

void ArcItem::updateCache() {
  m_cachedPath = createQtPath(m_data);
  m_cachedRect = m_cachedPath.boundingRect();
  m_isCacheValid = true;
  m_isShapeValid = false; // geometry가 바뀌면 shape도 무효화
}

void ArcItem::setGripsVisible(bool visible) {
  if (m_centerGrip)
    m_centerGrip->setVisible(visible);
  if (m_startGrip)
    m_startGrip->setVisible(visible);
  if (m_endGrip)
    m_endGrip->setVisible(visible);
}

QVariant ArcItem::itemChange(GraphicsItemChange change, const QVariant &value) {
  if (change == QGraphicsItem::ItemSelectedChange) {
    if (!value.toBool()) {
      setGripsVisible(false);
    }
  }
  return CadItem::itemChange(change, value);
}

SnapPoint ArcItem::getNearestSnap(const MyVector &pos) const {
  MyVector pt = m_data.getNearestPoint(pos);
  return SnapPoint(pt, SnapType::Nearest);
}

void ArcItem::move(const MyVector &delta) {
  prepareGeometryChange();
  m_data.center = m_data.center + delta;
  // 반지름, 각도는 변하지 않음

  // [핵심 해결] 캐시 초기화! (이게 없으면 옛날 그림이 그대로 남음)
  m_isCacheValid = false;
  m_isShapeValid = false; // [신규]

  updateGrips();
  update();
}

void ArcItem::mirror(const MyVector &p1, const MyVector &p2) {
  prepareGeometryChange();

  // 1. 기존 끝점을 구함 (반사 시 시작/끝이 뒤바뀜)
  MyVector oldEnd = m_data.endPoint();
  MyVector reflectedEnd = MyMath::reflectPoint(oldEnd, p1, p2);

  // 2. 중심점 반사
  m_data.center = MyMath::reflectPoint(m_data.center, p1, p2);

  // 3. 새로운 시작 각도 (반사된 이전 끝점의 각도)
  double newStartDeg = qRadiansToDegrees(std::atan2(
      reflectedEnd.y - m_data.center.y, reflectedEnd.x - m_data.center.x));
  m_data.startAngle = MyMath::normalizeAngle(newStartDeg);

  // SpanAngle은 크기가 유지됨 (기하학적 대칭)
  // 방향이 반전되더라도 시작점/끝점을 교체했으므로 CCW Span은 동일함

  m_isCacheValid = false;
  updateGrips();
  update();
}

QPainterPath ArcItem::getMovedPath(const MyVector &delta) const {
  MyArc temp = m_data;
  temp.center = temp.center + delta;
  return createQtPath(temp); // 이미 만들어둔 static 함수 재활용
}
CadItem *ArcItem::clone() const {
  auto *item = new ArcItem(m_data);
  item->setLayerName(layerName());
  item->setPen(pen());
  return item;
}

QMap<QString, QVariant> ArcItem::getProperties() const {
  QMap<QString, QVariant> props;
  props["Type"] = "Arc";
  props["Layer"] = layerName();
  props["CenterX"] = m_data.center.x;
  props["CenterY"] = m_data.center.y;
  props["Radius"] = m_data.radius;
  props["StartAngle"] = m_data.startAngle;
  props["SpanAngle"] = m_data.spanAngle;
  return props;
}

bool ArcItem::setProperty(const QString &key, const QVariant &value) {
  if (key == "Layer") {
    setLayerName(value.toString());
    return true;
  }

  prepareGeometryChange();
  if (key == "CenterX")
    m_data.center.x = value.toDouble();
  else if (key == "CenterY")
    m_data.center.y = value.toDouble();
  else if (key == "Radius")
    m_data.radius = value.toDouble();
  else if (key == "StartAngle")
    m_data.startAngle = value.toDouble();
  else if (key == "SpanAngle")
    m_data.spanAngle = value.toDouble();
  else
    return false;

  m_isCacheValid = false;
  m_isShapeValid = false; // [신규]
  updateGrips();
  update();
  return true;
}

void ArcItem::updateEndpoint(const QPointF &newPos, bool isFirstEnd) {
  prepareGeometryChange();
  double newAngle = qRadiansToDegrees(
      std::atan2(newPos.y() - m_data.center.y, newPos.x() - m_data.center.x));
  newAngle = MyMath::normalizeAngle(newAngle);

  if (isFirstEnd) {
    // Keep end angle constant
    double currentEndAngle =
        MyMath::normalizeAngle(m_data.startAngle + m_data.spanAngle);
    m_data.startAngle = newAngle;
    double newSpan = MyMath::normalizeAngle(currentEndAngle - newAngle);
    // Adjust span direction based on original (approximate)
    if (m_data.spanAngle < 0 && newSpan > 0)
      newSpan -= 360.0;
    else if (m_data.spanAngle > 0 && newSpan < 0)
      newSpan += 360.0;
    m_data.spanAngle = newSpan;
  } else {
    // Keep start angle constant
    double newSpan = MyMath::normalizeAngle(newAngle - m_data.startAngle);
    if (m_data.spanAngle < 0 && newSpan > 0)
      newSpan -= 360.0;
    else if (m_data.spanAngle > 0 && newSpan < 0)
      newSpan += 360.0;
    m_data.spanAngle = newSpan;
  }
  m_isCacheValid = false;
  m_isShapeValid = false; // [신규]
  updateGrips();
  update();
}

QPainterPath ArcItem::getMirroredPath(const MyVector &p1,
                                      const MyVector &p2) const {
  MyArc mirroredArc = m_data;
  mirroredArc.center = MyMath::reflectPoint(m_data.center, p1, p2);

  // 호의 시작점과 끝점도 대칭시킨 후 새로운 호 데이터 생성
  MyVector s = m_data.startPoint();
//  MyVector e = m_data.endPoint();
  MyVector ms = MyMath::reflectPoint(s, p1, p2);
//  MyVector me = MyMath::reflectPoint(e, p1, p2);

  // 대칭되면 호의 방향이 반전되므로 spanAngle 부호 반전
  mirroredArc.startAngle = qRadiansToDegrees(
      std::atan2(ms.y - mirroredArc.center.y, ms.x - mirroredArc.center.x));
  mirroredArc.startAngle = MyMath::normalizeAngle(mirroredArc.startAngle);
  mirroredArc.spanAngle = -m_data.spanAngle;

  return createQtPath(mirroredArc);
}

void ArcItem::rotate(const MyVector &base, double angle) {
  prepareGeometryChange();
  m_data.center = MyMath::rotatePoint(m_data.center, base, angle);
  m_data.startAngle = MyMath::normalizeAngle(m_data.startAngle + angle);
  m_isCacheValid = false;
  m_isShapeValid = false; // [신규]
  updateGrips();
  update();
}

QPainterPath ArcItem::getRotatedPath(const MyVector &base, double angle) const {
  MyArc rArc = m_data;
  rArc.center = MyMath::rotatePoint(m_data.center, base, angle);
  rArc.startAngle = MyMath::normalizeAngle(m_data.startAngle + angle);
  return createQtPath(rArc);
}

void ArcItem::scale(const MyVector &base, double scaleFactor) {
  prepareGeometryChange();
  m_data.center = MyMath::scalePoint(m_data.center, base, scaleFactor);
  m_data.radius = m_data.radius * std::abs(scaleFactor);
  m_isCacheValid = false;
  m_isShapeValid = false; // [신규]
  updateGrips();
  update();
}

QPainterPath ArcItem::getScaledPath(const MyVector &base,
                                    double scaleFactor) const {
  MyArc sArc = m_data;
  sArc.center = MyMath::scalePoint(m_data.center, base, scaleFactor);
  sArc.radius = m_data.radius * std::abs(scaleFactor);
  return createQtPath(sArc);
}

void ArcItem::stretch(const QList<int> &indices, const MyVector &delta) {
  prepareGeometryChange();
  // 1. 중심(0)이 포함된 경우 -> 전체 이동
  if (indices.contains(0)) {
    move(delta);
    return;
  }

  // 2. 시작(1) 또는 끝(2)만 포함된 경우
  if (indices.contains(1)) {
    MyVector newStart = m_data.startPoint() + delta;
    m_data.radius = m_data.center.distanceTo(newStart);
    double newStartAng = qRadiansToDegrees(
        std::atan2(newStart.y - m_data.center.y, newStart.x - m_data.center.x));
    newStartAng = normalizeAngle(newStartAng);

    double currentEnd = normalizeAngle(m_data.startAngle + m_data.spanAngle);
    m_data.startAngle = newStartAng;

    double newSpan = currentEnd - newStartAng;
    if (newSpan < 0)
      newSpan += 360.0;
    m_data.spanAngle = newSpan;
  }

  if (indices.contains(2)) {
    MyVector newEnd = m_data.endPoint() + delta;
    double newEndAng = qRadiansToDegrees(
        std::atan2(newEnd.y - m_data.center.y, newEnd.x - m_data.center.x));
    newEndAng = normalizeAngle(newEndAng);

    double newSpan = newEndAng - m_data.startAngle;
    if (newSpan < 0)
      newSpan += 360.0;
    m_data.spanAngle = newSpan;
  }

  m_isCacheValid = false;
  updateGrips();
  update();
}

QPainterPath ArcItem::getStretchedPath(const QList<int> &indices,
                                       const MyVector &delta) const {
  if (indices.contains(0)) {
    return getMovedPath(delta);
  }

  // 1번(시작) 또는 2번(끝) 정점이 포함되지 않은 경우 빈 패스 반환
  if (!indices.contains(1) && !indices.contains(2)) {
    return QPainterPath();
  }

  MyArc temp = m_data;
  if (indices.contains(1)) {
    MyVector newStart = temp.startPoint() + delta;
    temp.radius = temp.center.distanceTo(newStart);
    double newStartAng = qRadiansToDegrees(
        std::atan2(newStart.y - temp.center.y, newStart.x - temp.center.x));
    newStartAng = normalizeAngle(newStartAng);

    double currentEnd = normalizeAngle(temp.startAngle + temp.spanAngle);
    temp.startAngle = newStartAng;

    double newSpan = currentEnd - newStartAng;
    if (newSpan < 0)
      newSpan += 360.0;
    temp.spanAngle = newSpan;
  }

  if (indices.contains(2)) {
    MyVector newEnd = temp.endPoint() + delta;
    double newEndAng = qRadiansToDegrees(
        std::atan2(newEnd.y - temp.center.y, newEnd.x - temp.center.x));
    newEndAng = normalizeAngle(newEndAng);

    double newSpan = newEndAng - temp.startAngle;
    if (newSpan < 0)
      newSpan += 360.0;
    temp.spanAngle = newSpan;
  }

  return createQtPath(temp);
}
