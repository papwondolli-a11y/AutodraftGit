#include "overlaymanager.h"
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

OverlayManager::OverlayManager(QObject *parent) : QObject(parent) {}

void OverlayManager::setGhostLine(const QLineF &sceneLine, bool visible,
                                  const QColor &color,
                                  const QVector<qreal> &dashPattern) {
  m_ghostSceneLine = sceneLine;
  m_ghostLineVisible = visible;
  m_ghostLineColor = color;
  m_ghostLineDashPattern = dashPattern;
}

void OverlayManager::setGhostPath(const QPainterPath &scenePath, bool visible,
                                  const QColor &color, double width,
                                  const QVector<qreal> &dashPattern) {
  m_ghostScenePath = scenePath;
  m_ghostPathVisible = visible;
  m_ghostPathColor = color;
  m_ghostPathWidth = width;
  m_ghostPathDashPattern = dashPattern;
}

void OverlayManager::setGhostPixmap(const QPixmap &pixmap, const QPointF &pos,
                                    const QRectF &blockRect, double scale,
                                    double rotation, bool visible) {
  m_ghostPixmap = pixmap;
  m_ghostPixmapPos = pos;
  m_ghostPixmapRect = blockRect;
  m_ghostPixmapScale = scale;
  m_ghostPixmapRotation = rotation;
  m_ghostPixmapVisible = visible;
}

void OverlayManager::setSnapMarker(const QPointF &scenePos, SnapType type) {
  m_snapScenePos = scenePos;
  m_snapType = type;
  m_snapVisible = (type != SnapType::None);
}

void OverlayManager::setSelectionRect(const QPointF &p1, const QPointF &p2,
                                      SelectionMode mode) {
  QRectF newRect = QRectF(p1, p2).normalized();
  // 이전 프레임 사각형과 현재 사각형을 합쳐야 이전에 그려둔 테두리를 지울 수 있다.
  m_selectionDirty = m_prevSelectionRect.isNull()
                         ? newRect
                         : newRect.united(m_prevSelectionRect);
  m_prevSelectionRect = newRect;

  m_selectionP1 = p1;
  m_selectionP2 = p2;
  m_selectionMode = mode;
}

QRectF OverlayManager::selectionDirtyRect() const { return m_selectionDirty; }

void OverlayManager::setDashRect(const QPointF &p1, const QPointF &p2,
                                 const QColor &color) {
  m_dashP1 = p1;
  m_dashP2 = p2;
  m_dashColor = color;
  m_dashRectVisible = true;
}

void OverlayManager::setDrawingLimits(const QRectF &limits) {
  m_drawingLimits = limits;
}

void OverlayManager::setSelectionColors(const QColor &windowColor,
                                        const QColor &crossingColor) {
  m_selectionWindowColor  = windowColor;
  m_selectionCrossingColor = crossingColor;
}

void OverlayManager::clear() {
  m_ghostLineVisible = false;
  m_ghostPathVisible = false;
  m_ghostPixmapVisible = false;
  m_snapVisible = false;
  m_selectionMode = Mode_None;
  m_prevSelectionRect = QRectF(); // 다음 드래그가 새로 시작되도록 초기화
  m_dashRectVisible = false;
  // m_drawingLimits = QRectF(); // 유지!
}

void OverlayManager::draw(QPainter *painter, bool showOrigin) {
  // [핵심] 프린터나 PDF 출력 시에는 UI 전용 도구(가이드라인, 스냅, 선택박스 등)를 그리지 않음
  bool isWidget = (painter->device()->devType() == QInternal::Widget);

  if (showOrigin && isWidget) {
    painter->save();
    QTransform transform = painter->worldTransform();
    QPointF pixelOrigin = transform.map(QPointF(0, 0));
    QPointF pxDir = transform.map(QPointF(1, 0)) - pixelOrigin;
    QPointF pyDir = transform.map(QPointF(0, 1)) - pixelOrigin;

    double lenX = std::sqrt(pxDir.x() * pxDir.x() + pxDir.y() * pxDir.y());
    double lenY = std::sqrt(pyDir.x() * pyDir.x() + pyDir.y() * pyDir.y());
    if (lenX > 1e-9)
      pxDir /= lenX;
    if (lenY > 1e-9)
      pyDir /= lenY;

    painter->setWorldTransform(QTransform());
    painter->translate(pixelOrigin);
    painter->setRenderHint(QPainter::Antialiasing, true);

    QPen axisPen(Qt::gray, 2);
    axisPen.setCosmetic(true);
    painter->setPen(axisPen);

    double axisLen = 40.0;
    double arrowLen = 50.0;
    painter->drawLine(QPointF(0, 0), pxDir * axisLen);
    painter->drawLine(QPointF(0, 0), pyDir * axisLen);
    painter->setBrush(Qt::gray);
    {
      QPolygonF arrow;
      QPointF tip = pxDir * arrowLen;
      QPointF perp(-pxDir.y(), pxDir.x());
      arrow << tip << (pxDir * axisLen + perp * 4)
            << (pxDir * axisLen - perp * 4);
      painter->drawPolygon(arrow);
    }
    {
      QPolygonF arrow;
      QPointF tip = pyDir * arrowLen;
      QPointF perp(-pyDir.y(), pyDir.x());
      arrow << tip << (pyDir * axisLen + perp * 4)
            << (pyDir * axisLen - perp * 4);
      painter->drawPolygon(arrow);
    }
    painter->setFont(QFont("Arial", 10, QFont::Bold));
    painter->drawText(pxDir * (arrowLen + 15) + QPointF(-5, 5), "X");
    painter->drawText(pyDir * (arrowLen + 15) + QPointF(-5, 5), "Y");
    painter->restore();
  }

  painter->save();

  if (isWidget && m_ghostLineVisible) {
    QPen pen(m_ghostLineColor, 1);
    if (m_ghostLineDashPattern.isEmpty()) {
      pen.setStyle(Qt::DashLine);
    } else {
      pen.setStyle(Qt::CustomDashLine);
      pen.setDashPattern(m_ghostLineDashPattern);
    }
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->drawLine(m_ghostSceneLine);
  }

  if (isWidget && m_ghostPathVisible) {
    QPen pen(m_ghostPathColor, m_ghostPathWidth);
    if (m_ghostPathDashPattern.isEmpty()) {
      pen.setStyle(Qt::DashLine);
    } else {
      pen.setStyle(Qt::CustomDashLine);
      pen.setDashPattern(m_ghostPathDashPattern);
    }
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(m_ghostScenePath);
  }

  if (isWidget && m_ghostPixmapVisible && !m_ghostPixmap.isNull()) {
    painter->save();
    painter->setOpacity(0.5);

    // 1. 마우스 위치(Scene POS)로 이동
    painter->translate(m_ghostPixmapPos);
    // 2. 사용자가 입력한 회전/스케일 적용
    painter->rotate(m_ghostPixmapRotation);
    painter->scale(m_ghostPixmapScale, m_ghostPixmapScale);

    // 3. [핵심] 씬(Y-up)에서 이미지(Y-down)를 그리기 위해 로컬하게 한 번 더
    // 뒤집음
    painter->scale(1, -1);

    // 4. 블록의 바운딩 박스(0,0 기준)에 맞춰 렌더링
    double x = m_ghostPixmapRect.left();
    double y = -m_ghostPixmapRect.bottom(); // maxY를 뒤집음
    double w = m_ghostPixmapRect.width();
    double h = m_ghostPixmapRect.height();

    painter->drawPixmap(QRectF(x, y, w, h), m_ghostPixmap,
                        m_ghostPixmap.rect());

    painter->restore();
  }

  if (isWidget && m_selectionMode != Mode_None) {
    QColor base = (m_selectionMode == Mode_Window) ? m_selectionWindowColor
                                                   : m_selectionCrossingColor;
    QColor fillColor   = base; fillColor.setAlpha(60);
    QColor borderColor = base; borderColor.setAlpha(255);
    QPen pen(borderColor, 2);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(fillColor);
    painter->drawRect(QRectF(m_selectionP1, m_selectionP2).normalized());
  }

  if (isWidget && m_dashRectVisible) {
    QPen pen(m_dashColor, 1, Qt::DashLine);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(QRectF(m_dashP1, m_dashP2).normalized());
  }

  if (isWidget && m_snapVisible) {
    drawSnapMarker(painter, m_snapScenePos, m_snapType);
  }

  if (isWidget && !m_drawingLimits.isNull()) {
    QPen limitPen(QColor(255, 130, 0), 1, Qt::DashLine);
    limitPen.setCosmetic(true);
    painter->setPen(limitPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(m_drawingLimits);
  }

  painter->restore();
}

void OverlayManager::drawSnapMarker(QPainter *painter, const QPointF &center,
                                    SnapType type) {
  QPointF pixelPos = painter->worldTransform().map(center);
  painter->save();
  painter->setWorldTransform(QTransform());
  painter->setRenderHint(QPainter::Antialiasing, true);
  QPen pen(Qt::red, 1.5);
  painter->setPen(pen);
  painter->setBrush(Qt::NoBrush);
  double s = 7.0;
  switch (type) {
  case SnapType::Endpoint:
    painter->drawRect(QRectF(pixelPos.x() - s, pixelPos.y() - s, s * 2, s * 2));
    break;
  case SnapType::Midpoint: {
    QPolygonF tri;
    tri << QPointF(pixelPos.x(), pixelPos.y() - s)
        << QPointF(pixelPos.x() - s, pixelPos.y() + s)
        << QPointF(pixelPos.x() + s, pixelPos.y() + s);
    painter->drawPolygon(tri);
  } break;
  case SnapType::Center:
    painter->drawEllipse(pixelPos, s, s);
    break;
  case SnapType::Intersection:
    painter->drawLine(pixelPos.x() - s, pixelPos.y() - s, pixelPos.x() + s,
                      pixelPos.y() + s);
    painter->drawLine(pixelPos.x() - s, pixelPos.y() + s, pixelPos.x() + s,
                      pixelPos.y() - s);
    break;
  case SnapType::Quadrant: {
    QPolygonF dia;
    dia << QPointF(pixelPos.x(), pixelPos.y() - s)
        << QPointF(pixelPos.x() + s, pixelPos.y())
        << QPointF(pixelPos.x(), pixelPos.y() + s)
        << QPointF(pixelPos.x() - s, pixelPos.y());
    painter->drawPolygon(dia);
  } break;
  case SnapType::Nearest: {
    QPolygonF hg;
    hg << QPointF(pixelPos.x() - s, pixelPos.y() - s)
       << QPointF(pixelPos.x() + s, pixelPos.y() - s)
       << QPointF(pixelPos.x() - s, pixelPos.y() + s)
       << QPointF(pixelPos.x() + s, pixelPos.y() + s)
       << QPointF(pixelPos.x() - s, pixelPos.y() - s);
    painter->drawPolyline(hg);
  } break;
  case SnapType::Perpendicular: {
    // 직교 마커: ㄴ자 형태의 직각 표시
    painter->drawLine(pixelPos.x() - s, pixelPos.y() - s, pixelPos.x() - s,
                      pixelPos.y() + s);
    painter->drawLine(pixelPos.x() - s, pixelPos.y() + s, pixelPos.x() + s,
                      pixelPos.y() + s);
    painter->drawLine(pixelPos.x(), pixelPos.y() + s, pixelPos.x(),
                      pixelPos.y());
    painter->drawLine(pixelPos.x() - s, pixelPos.y(), pixelPos.x(),
                      pixelPos.y());
  } break;
  default:
    break;
  }
  painter->restore();
}
