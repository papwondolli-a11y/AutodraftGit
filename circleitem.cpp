#include "circleitem.h"
#include "detailregiondata.h"
#include "gripitem.h"
#include "hairlinepen.h"
#include "myentity.h"
#include "mygraphicsscene.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

CircleItem::CircleItem(const MyVector &center, double radius,
                       QGraphicsItem *parent)
    : CircleItem(MyCircle(center, radius), parent) {}

CircleItem::CircleItem(const MyCircle &circle, QGraphicsItem *parent)
    : CadItem(parent), m_data(circle) {
  m_pen = QPen(Qt::black, 0);
  // 그립 5개 생성
  for (int i = 0; i < 5; ++i) {
    m_grips.append(new GripItem(i, this));
  }
  updateGrips();
  setGripsVisible(false);
}

QRectF CircleItem::boundingRect() const {
  QPointF c = m_data.center.toPointF();
  double r = m_data.radius;
  // shape() 두께(4)와 일치시키기 위해 마진을 3.0으로 최적화.
  // 너무 큰 마진은 인클루시브(Window) 선택을 어렵게 만듭니다.
  double margin = 3.0;

  return QRectF(c.x() - r, c.y() - r, r * 2, r * 2)
      .adjusted(-margin, -margin, margin, margin);
}

void CircleItem::paint(QPainter *painter,
                       const QStyleOptionGraphicsItem *option,
                       QWidget *widget) {
  Q_UNUSED(widget);

  QPen currentPen = m_pen;

  // 1. 선가중치 표시 여부 확인
  MyGraphicsScene *myScene = qobject_cast<MyGraphicsScene *>(scene());
  bool showWeight = myScene ? myScene->showLineWeight() : false;

  if (!showWeight) {
    // [모드 A: 끄기] 화면 1.1px 가는 선 (hairlinepen.h 참고)
    HairlinePen::apply(currentPen, painter->transform());
  } else {
    // [모드 B: 켜기] 실제 두께 반영 + 확대 시 뭉개짐/흐려짐 방지
    currentPen.setCosmetic(false);
    qreal scale = qAbs(painter->transform().m11()); // 현재 뷰의 스케일 (미러된 블록 등 음수 스케일 방지)
    if (scale > 0.0 && currentPen.widthF() * scale < 1.0) {
      currentPen.setWidthF(1.0 / scale);
    }
  }

  // 1. [복구] 하이라이트(Glow + Dash)를 원본 아래에 배경으로 그리기
  // [핵심] 프린터 출력 시에는 선택 하이라이트를 그리지 않음
  if ((option->state & QStyle::State_Selected) &&
      painter->device()->devType() == QInternal::Widget) {
    painter->save();

    // 1. 글로우(Glow) 효과: 반투명 하늘색 (80 alpha)
    QPen glowPen(QColor(0, 255, 255, 80));
    glowPen.setCosmetic(true);
    glowPen.setWidthF(HairlinePen::glowWidth(currentPen, painter->transform()));
    painter->setPen(glowPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(m_data.center.toPointF(), m_data.radius,
                         m_data.radius);

    // 2. 점선 오버레이: 흰색 점선
    QPen dashPen(Qt::white);
    dashPen.setCosmetic(true);
    dashPen.setWidthF(1.0);
    dashPen.setStyle(Qt::DashLine);
    painter->setPen(dashPen);
    painter->drawEllipse(m_data.center.toPointF(), m_data.radius,
                         m_data.radius);

    painter->restore();
  }

  // 2. 원본 객체 그리기 (하이라이트 위에 그림)
  painter->setPen(currentPen);
  painter->setBrush(Qt::NoBrush);
  painter->drawEllipse(m_data.center.toPointF(), m_data.radius, m_data.radius);
}

QPainterPath CircleItem::geometryPath() const {
  QPainterPath path;
  path.addEllipse(m_data.center.toPointF(), m_data.radius, m_data.radius);
  return path;
}

QPainterPath CircleItem::shape() const {
  if (m_isShapeValid)
    return m_cachedShape;

  QPainterPath path = geometryPath();
  QPainterPathStroker stroker;
  stroker.setWidth(4.0); // 10.0 -> 4.0 최적화 (Window Select 가독성 개선)
  m_cachedShape = stroker.createStroke(path);
  m_isShapeValid = true;
  return m_cachedShape;
}

QList<SnapPoint> CircleItem::getSnapPoints() const {
  MyVector c = m_data.center;
  double r = m_data.radius;

  return {
      // 1. 중심점 (Center)
      {c, SnapType::Center},

      // 2. 4분점 (Quadrant) - 기존 Endpoint에서 변경됨
      {c + MyVector(r, 0, 0), SnapType::Quadrant},  // 0도 (Right)
      {c + MyVector(0, -r, 0), SnapType::Quadrant}, // 90도 (Top, Qt좌표계 유의)
      {c + MyVector(-r, 0, 0), SnapType::Quadrant}, // 180도 (Left)
      {c + MyVector(0, r, 0), SnapType::Quadrant}   // 270도 (Bottom)
  };
}

// 2. getNearestSnap 추가
SnapPoint CircleItem::getNearestSnap(const MyVector &pos) const {
  MyVector pt = m_data.getNearestPoint(pos);
  return SnapPoint(pt, SnapType::Nearest);
}

QList<QPointF> CircleItem::getGripPoints() const {
  return {}; // updateGrips에서 처리
}

void CircleItem::applyGripMove(int index, const MyVector &pos) {
  prepareGeometryChange();

  if (index == 0) {
    // 중심 이동
    m_data.center = pos;
  } else {
    // 반지름 변경 (4분점 중 하나를 잡고 드래그)
    // 중심 ~ 새 위치 사이의 거리 = 새 반지름
    m_data.radius = m_data.center.distanceTo(pos);
  }
  m_isShapeValid = false; // 캐시 무효화
  updateGrips();
  update();
}

QPainterPath CircleItem::getGhostShape(int index, const MyVector &pos) const {
  QPainterPath path;
  MyVector tempC = m_data.center;
  double tempR = m_data.radius;

  if (index == 0) {
    tempC = pos;
  } else {
    tempR = m_data.center.distanceTo(pos);
  }

  path.addEllipse(tempC.toPointF(), tempR, tempR);
  return path;
}

double CircleItem::distanceTo(const MyVector &pos) const {
  // 원주와의 거리 = |중심거리 - 반지름|
  double distToCenter = m_data.center.distanceTo(pos);
  return std::abs(distToCenter - m_data.radius);
}

void CircleItem::updateGrips() {
  if (m_grips.size() < 5)
    return;

  QList<SnapPoint> sp = getSnapPoints();
  // sp 순서: Center, Right, Top, Left, Bottom

  // Center Grip
  m_grips[0]->setPos(sp[0].pos.toPointF());

  // Quadrant Grips
  for (int i = 1; i <= 4; ++i) {
    if (i < sp.size()) {
      m_grips[i]->setPos(sp[i].pos.toPointF());
    }
  }
}

void CircleItem::setGripsVisible(bool visible) {
  for (auto grip : std::as_const(m_grips))
    grip->setVisible(visible);
}

void CircleItem::setCircle(const MyCircle &circle) {
  prepareGeometryChange();
  m_data = circle;
  m_isShapeValid = false; // 캐시 무효화
  updateGrips();
  update();
}

QVariant CircleItem::itemChange(GraphicsItemChange change,
                                const QVariant &value) {
  if (change == QGraphicsItem::ItemSelectedChange) {
    if (!value.toBool()) {
      setGripsVisible(false);
    }
  }
  return CadItem::itemChange(change, value);
}

#include "mymath.h"

void CircleItem::move(const MyVector &delta) {
  prepareGeometryChange();
  m_data.center = m_data.center + delta;
  m_isShapeValid = false; // 캐시 무효화
  updateGrips();
  update();
}

void CircleItem::mirror(const MyVector &p1, const MyVector &p2) {
  prepareGeometryChange();
  m_data.center = MyMath::reflectPoint(m_data.center, p1, p2);
  m_isShapeValid = false; // 캐시 무효화
  updateGrips();
  update();
}

QPainterPath CircleItem::getMovedPath(const MyVector &delta) const {
  QPainterPath path;
  MyVector c = m_data.center + delta;
  path.addEllipse(c.toPointF(), m_data.radius, m_data.radius);
  return path;
}

void CircleItem::rotate(const MyVector &base, double angle) {
  prepareGeometryChange();
  m_data.center = MyMath::rotatePoint(m_data.center, base, angle);
  m_isShapeValid = false; // 캐시 무효화
  updateGrips();
  update();
}

QPainterPath CircleItem::getRotatedPath(const MyVector &base,
                                        double angle) const {
  MyVector rCenter = MyMath::rotatePoint(m_data.center, base, angle);
  QPainterPath path;
  path.addEllipse(rCenter.toPointF(), m_data.radius, m_data.radius);
  return path;
}

CadItem *CircleItem::clone() const {
  auto *item = new CircleItem(m_data);
  item->setLayerName(layerName());
  item->setPen(pen());
  return item;
}

QMap<QString, QVariant> CircleItem::getProperties() const {
  QMap<QString, QVariant> props;
  props["Layer"] = layerName();
  // 좌표/반지름은 파일 저장(filemanager)이 참조하므로 항상 포함한다.
  props["CenterX"] = m_data.center.x;
  props["CenterY"] = m_data.center.y;
  props["Radius"] = m_data.radius;

  // [중요] Type 은 파일 저장(entities.type 컬럼)·역직렬화·DXF 분기의 키이므로
  //        상세도 경계원이라도 항상 "Circle" 로 둔다.
  //        속성창 표기(유형: 상세영역)는 IsDetailRegion 플래그로만 처리한다.
  props["Type"] = "Circle";

  const QVariant sv = data(DETAIL_REGION_SCALE);
  if (sv.isValid() && sv.toDouble() > 0.0) {
    props["IsDetailRegion"] = true;
    props["Scale"] = sv.toDouble();
    props["Label"] = data(DETAIL_REGION_LABEL).toString();
  }
  return props;
}

bool CircleItem::setProperty(const QString &key, const QVariant &value) {
  if (key == "Layer") {
    setLayerName(value.toString());
    return true;
  }

  if (key == "Scale") {
    const QVariant sv = data(DETAIL_REGION_SCALE);
    if (!sv.isValid())
      return false;
    const double oldScale = sv.toDouble();
    const double newScale = value.toDouble();
    if (oldScale <= 0.0 || newScale <= 0.0 || qFuzzyCompare(oldScale, newScale))
      return false;
    applyDetailRegionRescale(oldScale, newScale);
    setData(DETAIL_REGION_SCALE, newScale);
    return true;
  }
  if (key == "Label" || key == "Type" || key == "IsDetailRegion")
    return false; // 읽기 전용

  prepareGeometryChange();
  if (key == "CenterX")
    m_data.center.x = value.toDouble();
  else if (key == "CenterY")
    m_data.center.y = value.toDouble();
  else if (key == "Radius")
    m_data.radius = value.toDouble();
  else
    return false;

  updateGrips();
  update();
  return true;
}

static QString detailTrimZeros(double v) {
  QString s = QString::number(v, 'f', 3);
  if (s.contains(QLatin1Char('.'))) {
    while (s.endsWith(QLatin1Char('0')))
      s.chop(1);
    if (s.endsWith(QLatin1Char('.')))
      s.chop(1);
  }
  return s;
}

// "N : 1" / "1 : N" 형태로 약분 없이 표기
static QString detailScaleRatioText(double factor) {
  if (factor <= 0.0)
    return QStringLiteral("1 : 1");
  if (factor >= 1.0)
    return QString::fromLatin1("%1 : 1").arg(detailTrimZeros(factor));
  return QString::fromLatin1("1 : %1").arg(detailTrimZeros(1.0 / factor));
}

void CircleItem::applyDetailRegionRescale(double oldScale, double newScale) {
  const double ratio = newScale / oldScale;
  if (ratio <= 0.0 || qFuzzyCompare(ratio, 1.0))
    return;

  const MyVector base = m_data.center; // 상세도 배치 중심 (이 경계원의 중심)
  const QString myLabel = data(DETAIL_REGION_LABEL).toString();

  // 1) 경계원 자신
  prepareGeometryChange();
  m_data.radius *= ratio;
  updateGrips();
  update();

  MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
  if (!sc)
    return;

  // 2) "이 상세도의 내용"만 같은 비율로 스케일한다.
  //    판정은 위치(주변 객체 오염 위험)가 아니라 태그로만 한다:
  //      · role == DETAIL_ROLE_CONTENT  (buildDetailView 가 만든 조각)
  //      · label == 이 경계원의 label   (다른 상세도 내용 제외)
  //    블록 자식(부모가 CadItem)은 건드리지 않는다 — 최상위 BlockItem 을
  //    한 번만 scale() 하면 트리 전체가 함께 변환되기 때문.
  const QList<QGraphicsItem *> all = sc->items();
  for (int i = 0; i < all.size(); ++i) {
    CadItem *ci = dynamic_cast<CadItem *>(all.at(i));
    if (!ci || ci == this)
      continue;
    if (dynamic_cast<CadItem *>(ci->parentItem()))
      continue; // 블록 자식 등 → 최상위만 처리
    if (ci->data(DETAIL_REGION_ROLE).toInt() != DETAIL_ROLE_CONTENT)
      continue;
    if (!myLabel.isEmpty() &&
        ci->data(DETAIL_REGION_LABEL).toString() != myLabel)
      continue;

    ci->scale(base, ratio);

    // 치수: measureScale 을 나눠 표시 숫자(원본 실측값)를 유지
    const MyEntity *e = ci->getEntityData();
    if (e && e->type() == EntityType::Dimension) {
      double ms = ci->getProperties().value(QStringLiteral("MeasureScale"))
                      .toDouble();
      if (ms <= 0.0)
        ms = 1.0;
      ci->setProperty(QStringLiteral("MeasureScale"), ms / ratio);
    }

    // "축척 N:1" 캡션 텍스트 갱신 (전용 표식 우선, 없으면 접두어로 판별)
    bool isScaleCaption = (ci->data(DETAIL_REGION_SCALE_CAPTION).toInt() == 1);
    if (!isScaleCaption) {
      const QString content =
          ci->getProperties().value(QStringLiteral("Content")).toString();
      isScaleCaption = content.startsWith(tr("축척"));
    }
    if (isScaleCaption)
      ci->setProperty(QStringLiteral("Content"),
                      tr("축척  %1").arg(
                          detailScaleRatioText(newScale)));
  }
}

QPainterPath CircleItem::getMirroredPath(const MyVector &p1,
                                         const MyVector &p2) const {
  MyVector mirroredCenter = MyMath::reflectPoint(m_data.center, p1, p2);
  QPainterPath path;
  path.addEllipse(mirroredCenter.toPointF(), m_data.radius, m_data.radius);
  return path;
}

void CircleItem::scale(const MyVector &base, double scaleFactor) {
  prepareGeometryChange();
  m_data.center = MyMath::scalePoint(m_data.center, base, scaleFactor);
  m_data.radius = m_data.radius * std::abs(scaleFactor);
  m_isShapeValid = false; // 캐시 무효화
  updateGrips();
  update();
}

QPainterPath CircleItem::getScaledPath(const MyVector &base,
                                       double scaleFactor) const {
  MyVector sCenter = MyMath::scalePoint(m_data.center, base, scaleFactor);
  double sRadius = m_data.radius * std::abs(scaleFactor);
  QPainterPath path;
  path.addEllipse(sCenter.toPointF(), sRadius, sRadius);
  return path;
}

void CircleItem::stretch(const QList<int> &indices, const MyVector &delta) {
  // 원은 정점 개념이 모호하므로, 중심(0)이 포함된 경우에만 전체 이동(Move) 처리
  if (indices.contains(0)) {
    move(delta);
    m_isShapeValid = false; // 캐시 무효화
  }
}

QPainterPath CircleItem::getStretchedPath(const QList<int> &indices,
                                          const MyVector &delta) const {
  if (indices.contains(0)) {
    return getMovedPath(delta);
  }
  // 변형되지 않는 경우 빈 패스 반환 (고스트 가독성 개선)
  return QPainterPath();
}
