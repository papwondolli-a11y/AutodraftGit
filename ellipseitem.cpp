#include "ellipseitem.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <algorithm>
#include <cmath>

namespace {
const double kTwoPi = 6.283185307179586;
const double kPi = 3.141592653589793;

double wrap2Pi(double t) {
  t = std::fmod(t, kTwoPi);
  if (t < 0.0)
    t += kTwoPi;
  return t;
}

// 그립 위치 목록 (인덱스 규약은 ellipseitem.h 참조)
QList<MyVector> gripPositions(const MyEllipse &e) {
  QList<MyVector> g;
  MyVector mn = e.minorAxis();
  g << e.center;
  g << MyVector(e.center.x + e.majorAxis.x, e.center.y + e.majorAxis.y);
  g << MyVector(e.center.x + mn.x, e.center.y + mn.y);
  g << MyVector(e.center.x - e.majorAxis.x, e.center.y - e.majorAxis.y);
  g << MyVector(e.center.x - mn.x, e.center.y - mn.y);
  if (!e.isFull()) {
    g << e.startPoint();
    g << e.endPoint();
  }
  return g;
}

// 그립 이동을 타원 데이터에 적용
void applyGrip(MyEllipse &e, int index, const MyVector &pos) {
  switch (index) {
  case 0:
    e.center = pos;
    break;
  case 1:
  case 3: {
    MyVector v = pos - e.center;
    if (index == 3)
      v = e.center - pos;
    if (v.magnitude() > 1.0e-9)
      e.majorAxis = v;
    break;
  }
  case 2:
  case 4: {
    double a = e.majorRadius();
    if (a < 1.0e-12)
      break;
    MyVector mnDir = e.minorAxis().normalized();
    MyVector d = pos - e.center;
    double len = std::fabs(d.x * mnDir.x + d.y * mnDir.y);
    e.ratio = std::min(1.0, std::max(1.0e-4, len / a));
    break;
  }
  case 5: {
    double span = wrap2Pi(e.endParam - e.paramAt(pos));
    e.startParam = e.paramAt(pos);
    e.endParam = e.startParam + (span < 1.0e-9 ? kTwoPi : span);
    e.normalizeParams();
    break;
  }
  case 6: {
    double span = wrap2Pi(e.paramAt(pos) - e.startParam);
    e.endParam = e.startParam + (span < 1.0e-9 ? kTwoPi : span);
    e.normalizeParams();
    break;
  }
  default:
    break;
  }
}

// 매개변수 미분 P'(t) = -majorAxis*sin(t) + minorAxis*cos(t)
MyVector derivAt(const MyEllipse &e, double t) {
  MyVector v = e.minorAxis();
  return MyVector(-e.majorAxis.x * std::sin(t) + v.x * std::cos(t),
                  -e.majorAxis.y * std::sin(t) + v.y * std::cos(t));
}

MyEllipse movedCopy(const MyEllipse &src, const MyVector &delta) {
  MyEllipse e = src;
  e.center = e.center + delta;
  return e;
}

MyEllipse mirroredCopy(const MyEllipse &src, const MyVector &p1,
                       const MyVector &p2) {
  MyEllipse e = src;
  MyVector newCenter = MyMath::reflectPoint(src.center, p1, p2);
  MyVector newMajorEnd = MyMath::reflectPoint(src.center + src.majorAxis, p1, p2);
  e.center = newCenter;
  e.majorAxis = newMajorEnd - newCenter;
  // 반사는 방향(손잡이)을 뒤집으므로 P'(t) = P_new(-t)
  if (!src.isFull()) {
    double s = -src.endParam, en = -src.startParam;
    e.startParam = s;
    e.endParam = en;
    e.normalizeParams();
  }
  return e;
}

MyEllipse rotatedCopy(const MyEllipse &src, const MyVector &base, double angle) {
  MyEllipse e = src;
  MyVector newCenter = MyMath::rotatePoint(src.center, base, angle);
  MyVector newMajorEnd =
      MyMath::rotatePoint(src.center + src.majorAxis, base, angle);
  e.center = newCenter;
  e.majorAxis = newMajorEnd - newCenter;
  return e;
}

MyEllipse scaledCopy(const MyEllipse &src, const MyVector &base, double f) {
  MyEllipse e = src;
  e.center = MyMath::scalePoint(src.center, base, f);
  e.majorAxis = src.majorAxis * f;
  return e;
}

MyEllipse stretchedCopy(const MyEllipse &src, const QList<int> &indices,
                        const MyVector &delta) {
  MyEllipse e = src;
  if (indices.contains(0))
    return movedCopy(src, delta);
  QList<MyVector> grips = gripPositions(src);
  for (int idx : indices) {
    if (idx > 0 && idx < grips.size())
      applyGrip(e, idx, grips[idx] + delta);
  }
  return e;
}
} // namespace

QPainterPath EllipseItem::createQtPath(const MyEllipse &e) {
  QPainterPath path;
  if (e.majorRadius() < 1.0e-9)
    return path;

  double sp = e.endParam - e.startParam;
  int n = std::max(1, (int)std::ceil(sp / (kPi / 4.0) - 1.0e-9));
  double dt = sp / n;
  double k = 4.0 / 3.0 * std::tan(dt / 4.0);

  path.moveTo(e.pointAt(e.startParam).toPointF());
  for (int i = 0; i < n; ++i) {
    double t0 = e.startParam + dt * i;
    double t1 = t0 + dt;
    MyVector p0 = e.pointAt(t0), p3 = e.pointAt(t1);
    MyVector d0 = derivAt(e, t0), d1 = derivAt(e, t1);
    path.cubicTo(QPointF(p0.x + k * d0.x, p0.y + k * d0.y),
                 QPointF(p3.x - k * d1.x, p3.y - k * d1.y), p3.toPointF());
  }
  if (e.isFull())
    path.closeSubpath();
  return path;
}

EllipseItem::EllipseItem(const MyEllipse &ellipse, QGraphicsItem *parent)
    : CadItem(parent), m_data(ellipse) {
  m_pen = QPen(Qt::white, 0);
  m_data.normalizeParams();
  updatePath();
}

CadItem *EllipseItem::clone() const {
  EllipseItem *cloned = new EllipseItem(m_data);
  cloned->setPen(pen());
  cloned->setLayerName(layerName());
  return cloned;
}

QMap<QString, QVariant> EllipseItem::getProperties() const {
  QMap<QString, QVariant> props;
  props["Type"] = "Ellipse";
  props["Layer"] = layerName();
  props["CenterX"] = m_data.center.x;
  props["CenterY"] = m_data.center.y;
  props["MajorRadius"] = m_data.majorRadius();
  props["MinorRadius"] = m_data.minorRadius();
  props["Rotation"] = m_data.rotationRad() * 180.0 / kPi;
  return props;
}

bool EllipseItem::setProperty(const QString &key, const QVariant &value) {
  if (key == "Layer") {
    setLayerName(value.toString());
    return true;
  }
  if (key == "CenterX") {
    m_data.center.x = value.toDouble();
    updatePath();
    return true;
  }
  if (key == "CenterY") {
    m_data.center.y = value.toDouble();
    updatePath();
    return true;
  }
  if (key == "MajorRadius") {
    double a = value.toDouble();
    double cur = m_data.majorRadius();
    if (a <= 0.0 || cur < 1.0e-12)
      return false;
    m_data.majorAxis = m_data.majorAxis * (a / cur);
    updatePath();
    return true;
  }
  if (key == "MinorRadius") {
    double b = value.toDouble();
    double a = m_data.majorRadius();
    if (b <= 0.0 || a < 1.0e-12)
      return false;
    m_data.ratio = std::min(1.0, std::max(1.0e-4, b / a));
    updatePath();
    return true;
  }
  if (key == "Rotation") {
    double rad = value.toDouble() * kPi / 180.0;
    double a = m_data.majorRadius();
    m_data.majorAxis = MyVector(a * std::cos(rad), a * std::sin(rad));
    updatePath();
    return true;
  }
  return false;
}

QRectF EllipseItem::boundingRect() const {
  qreal margin = m_pen.widthF() / 2.0 + 2.0;
  return m_path.boundingRect().adjusted(-margin, -margin, margin, margin);
}

void EllipseItem::setPen(const QPen &pen) {
  prepareGeometryChange(); // 펜 폭 변화가 boundingRect 여백을 바꾸므로 통지
  CadItem::setPen(pen);
}

void EllipseItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                        QWidget *widget) {
  Q_UNUSED(widget);

  QPen currentPen = m_pen;
  currentPen.setCosmetic(true);

  MyGraphicsScene *myScene = qobject_cast<MyGraphicsScene *>(scene());
  bool showWeight = myScene ? myScene->showLineWeight() : false;

  if (!showWeight) {
    currentPen.setCosmetic(true);
    currentPen.setWidthF(1.0);
  } else {
    currentPen.setCosmetic(false);
    qreal scale = qAbs(painter->transform().m11());
    if (scale > 0.0 && currentPen.widthF() * scale < 1.0) {
      currentPen.setWidthF(1.0 / scale);
    }
  }

  if ((option->state & QStyle::State_Selected) &&
      painter->device()->devType() == QInternal::Widget) {
    painter->save();
    QPen glowPen(QColor(0, 255, 255, 80));
    glowPen.setCosmetic(true);
    glowPen.setWidthF(currentPen.widthF() + 4.0);
    painter->setPen(glowPen);
    painter->drawPath(m_path);

    QPen dashPen(Qt::white);
    dashPen.setCosmetic(true);
    dashPen.setWidthF(1.0);
    dashPen.setStyle(Qt::DashLine);
    painter->setPen(dashPen);
    painter->drawPath(m_path);
    painter->restore();
  }

  painter->setPen(currentPen);
  painter->setBrush(Qt::NoBrush);
  painter->drawPath(m_path);
}

QPainterPath EllipseItem::shape() const {
  QPainterPathStroker stroker;
  stroker.setWidth(10.0);
  return stroker.createStroke(m_path);
}

QPainterPath EllipseItem::geometryPath() const { return m_path; }

QList<SnapPoint> EllipseItem::getSnapPoints() const {
  QList<SnapPoint> snaps;
  snaps.append(SnapPoint(m_data.center, SnapType::Center));
  QList<MyVector> grips = gripPositions(m_data);
  // 장·단축 끝점은 Quadrant, 호의 시작/끝점은 Endpoint
  for (int i = 1; i <= 4 && i < grips.size(); ++i) {
    if (m_data.containsParam(m_data.paramAt(grips[i])))
      snaps.append(SnapPoint(grips[i], SnapType::Quadrant));
  }
  if (!m_data.isFull()) {
    snaps.append(SnapPoint(m_data.startPoint(), SnapType::Endpoint));
    snaps.append(SnapPoint(m_data.endPoint(), SnapType::Endpoint));
  }
  return snaps;
}

QList<QPointF> EllipseItem::getGripPoints() const {
  QList<QPointF> out;
  for (const MyVector &g : gripPositions(m_data))
    out.append(g.toPointF());
  return out;
}

void EllipseItem::applyGripMove(int index, const MyVector &pos) {
  applyGrip(m_data, index, pos);
  updatePath();
}

QPainterPath EllipseItem::getGhostShape(int index, const MyVector &pos) const {
  MyEllipse temp = m_data;
  applyGrip(temp, index, pos);
  return createQtPath(temp);
}

double EllipseItem::distanceTo(const MyVector &pos) const {
  return pos.distanceTo(m_data.getNearestPoint(pos));
}

SnapPoint EllipseItem::getNearestSnap(const MyVector &pos) const {
  return SnapPoint(m_data.getNearestPoint(pos), SnapType::Nearest);
}

void EllipseItem::setEllipse(const MyEllipse &ellipse) {
  m_data = ellipse;
  m_data.normalizeParams();
  updatePath();
}

void EllipseItem::move(const MyVector &delta) {
  m_data = movedCopy(m_data, delta);
  updatePath();
}

void EllipseItem::mirror(const MyVector &p1, const MyVector &p2) {
  m_data = mirroredCopy(m_data, p1, p2);
  updatePath();
}

void EllipseItem::rotate(const MyVector &base, double angle) {
  m_data = rotatedCopy(m_data, base, angle);
  updatePath();
}

void EllipseItem::scale(const MyVector &base, double scaleFactor) {
  m_data = scaledCopy(m_data, base, scaleFactor);
  updatePath();
}

void EllipseItem::stretch(const QList<int> &indices, const MyVector &delta) {
  m_data = stretchedCopy(m_data, indices, delta);
  updatePath();
}

QPainterPath EllipseItem::getMovedPath(const MyVector &delta) const {
  return createQtPath(movedCopy(m_data, delta));
}

QPainterPath EllipseItem::getMirroredPath(const MyVector &p1,
                                          const MyVector &p2) const {
  return createQtPath(mirroredCopy(m_data, p1, p2));
}

QPainterPath EllipseItem::getRotatedPath(const MyVector &base,
                                         double angle) const {
  return createQtPath(rotatedCopy(m_data, base, angle));
}

QPainterPath EllipseItem::getScaledPath(const MyVector &base,
                                        double scaleFactor) const {
  return createQtPath(scaledCopy(m_data, base, scaleFactor));
}

QPainterPath EllipseItem::getStretchedPath(const QList<int> &indices,
                                           const MyVector &delta) const {
  return createQtPath(stretchedCopy(m_data, indices, delta));
}

void EllipseItem::setGripsVisible(bool visible) { Q_UNUSED(visible) }

void EllipseItem::updatePath() {
  // m_path가 바뀌면 boundingRect()도 바뀌므로 반드시 Qt에 알린다.
  prepareGeometryChange();
  m_path = createQtPath(m_data);
}
