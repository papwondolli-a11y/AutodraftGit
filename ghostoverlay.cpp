#include "ghostoverlay.h"
#include <QDebug>
#include <QGraphicsView>
#include <QPainter>

GhostOverlay::GhostOverlay(QWidget *parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
}

void GhostOverlay::setGhostLine(const QLineF &sceneLine, bool visible,
                                const QColor &color,
                                const QVector<qreal> &dashPattern) {
  m_ghostSceneLine = sceneLine;
  m_ghostLineVisible = visible;
  m_ghostLineColor = color;
  m_ghostLineDashPattern = dashPattern;
  update();
}

void GhostOverlay::setGhostPath(const QPainterPath &scenePath, bool visible,
                                const QColor &color, double width,
                                const QVector<qreal> &dashPattern) {
  m_ghostScenePath = scenePath;
  m_ghostPathVisible = visible;
  m_ghostPathColor = color;
  m_ghostPathWidth = width;
  m_ghostPathDashPattern = dashPattern;
  update();
}

void GhostOverlay::setGhostPixmap(const QPixmap &pixmap, const QPointF &pos,
                                  const QRectF &blockRect, double scale,
                                  double rotation, bool visible) {
  m_ghostPixmap = pixmap;
  m_ghostPixmapPos = pos;
  m_ghostPixmapRect = blockRect;
  m_ghostPixmapScale = scale;
  m_ghostPixmapRotation = rotation;
  m_ghostPixmapVisible = visible;
  update();
}

void GhostOverlay::setSnapMarker(const QPointF &scenePos, SnapType type) {
  m_snapScenePos = scenePos;
  m_snapType = type;
  m_snapVisible = (type != SnapType::None);
  update();
}

void GhostOverlay::setSelectionRect(const QPointF &p1, const QPointF &p2,
                                    SelectionMode mode) {
  m_selectionP1 = p1;
  m_selectionP2 = p2;
  m_selectionMode = mode;
  raise(); // Ensure overlay is on top of other widgets/layers
  update();
}

void GhostOverlay::setDashRect(const QPointF &p1, const QPointF &p2,
                               const QColor &color) {
  m_dashP1 = p1;
  m_dashP2 = p2;
  m_dashColor = color;
  m_dashRectVisible = true;
  update();
}

void GhostOverlay::clear() {
  m_ghostLineVisible = false;
  m_ghostPathVisible = false;
  m_ghostScenePath = QPainterPath();
  m_ghostPixmapVisible = false;
  m_snapVisible = false;
  m_selectionMode = Mode_None;
  m_dashRectVisible = false;
  update();
}

void GhostOverlay::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);

  // [핵심] 뷰를 찾아 실시간 매핑 수행
  QWidget *vp = parentWidget();
  if (!vp)
    return;
  QGraphicsView *view = qobject_cast<QGraphicsView *>(vp->parentWidget());
  if (!view)
    return;

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // 1. 고스트 라인 (Scene -> View 매핑)
  if (m_ghostLineVisible) {
    QPen pen(m_ghostLineColor, 1);
    if (m_ghostLineDashPattern.isEmpty()) {
      pen.setStyle(Qt::DashLine);
    } else {
      pen.setStyle(Qt::CustomDashLine);
      pen.setDashPattern(m_ghostLineDashPattern);
    }
    painter.setPen(pen);

    QPointF p1 = view->mapFromScene(m_ghostSceneLine.p1());
    QPointF p2 = view->mapFromScene(m_ghostSceneLine.p2());
    painter.drawLine(QLineF(p1, p2));
  }

  // 2. 고스트 패스 (Scene -> View 매핑)
  if (m_ghostPathVisible) {
    QPen pen(m_ghostPathColor, m_ghostPathWidth);
    if (m_ghostPathDashPattern.isEmpty()) {
      pen.setStyle(Qt::DashLine);
    } else {
      pen.setStyle(Qt::CustomDashLine);
      pen.setDashPattern(m_ghostPathDashPattern);
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // [핵심] 패스 전체를 뷰 매핑 엔진에 위임하여 정밀 변환
    QPainterPath viewPath = view->mapFromScene(m_ghostScenePath);
    painter.drawPath(viewPath);
  }

  // 3. 고스트 픽스맵(블록 미리보기) — 잠시 뷰의 실제 변환(Y축 반전 포함)으로
  // 전환해서 그린다. viewportTransform()은 뷰가 실제로 쓰는 살아있는 변환이라
  // 줌/팬/GL 여부와 무관하게 항상 정확하다.
  if (m_ghostPixmapVisible && !m_ghostPixmap.isNull()) {
    painter.save();
    painter.setTransform(view->viewportTransform());
    painter.setOpacity(0.5);
    painter.translate(m_ghostPixmapPos);
    painter.rotate(m_ghostPixmapRotation);
    painter.scale(m_ghostPixmapScale, -m_ghostPixmapScale);
    double x = m_ghostPixmapRect.left();
    double y = -m_ghostPixmapRect.bottom();
    double w = m_ghostPixmapRect.width();
    double h = m_ghostPixmapRect.height();
    painter.drawPixmap(QRectF(x, y, w, h), m_ghostPixmap, m_ghostPixmap.rect());
    painter.restore();
  }

  // 4. 선택 박스 (Scene -> View 매핑 - 표준 함수 사용)
  if (m_selectionMode != Mode_None) {
    QColor fillColor;
    QColor borderColor;

    if (m_selectionMode == Mode_Window) {
      fillColor = QColor(0, 0, 255, 60);
      borderColor = QColor(0, 0, 255);
    } else {
      fillColor = QColor(0, 255, 0, 60);
      borderColor = QColor(0, 255, 0);
    }

    painter.setPen(QPen(borderColor, 2, Qt::SolidLine));
    painter.setBrush(fillColor);

    // [핵심] 뷰의 표준 매핑 함수를 사용하여 화면상의 픽셀 위치를 정확히 계산
    QPointF viewP1 = view->mapFromScene(m_selectionP1);
    QPointF viewP2 = view->mapFromScene(m_selectionP2);

    painter.drawRect(QRectF(viewP1, viewP2).normalized());
  }

  // 5. 줌 윈도우용 점선 박스 (Scene -> View 매핑)
  if (m_dashRectVisible) {
    QPen pen(m_dashColor, 1, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QPointF viewP1 = view->mapFromScene(m_dashP1);
    QPointF viewP2 = view->mapFromScene(m_dashP2);

    painter.drawRect(QRectF(viewP1, viewP2).normalized());
  }

  // 6. 스냅 마커 (Scene -> View 매핑)
  if (m_snapVisible) {
    QPointF viewPos = view->mapFromScene(m_snapScenePos);
    drawSnapMarker(painter, viewPos, m_snapType);
  }
}

void GhostOverlay::drawSnapMarker(QPainter &painter, const QPointF &center,
                                  SnapType type) {
  // OverlayManager::drawSnapMarker와 동일한 모양/크기 (overlaymanager.cpp
  // 참고) — 여기서는 center가 이미 뷰 픽셀 좌표이므로 별도 변환이 필요 없다.
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPen pen(Qt::red, 1.5);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  double s = 7.0;

  switch (type) {
  case SnapType::Endpoint:
    painter.drawRect(QRectF(center.x() - s, center.y() - s, s * 2, s * 2));
    break;
  case SnapType::Midpoint: {
    QPolygonF tri;
    tri << QPointF(center.x(), center.y() - s)
        << QPointF(center.x() - s, center.y() + s)
        << QPointF(center.x() + s, center.y() + s);
    painter.drawPolygon(tri);
  } break;
  case SnapType::Center:
    painter.drawEllipse(center, s, s);
    break;
  case SnapType::Intersection:
    painter.drawLine(center.x() - s, center.y() - s, center.x() + s,
                     center.y() + s);
    painter.drawLine(center.x() - s, center.y() + s, center.x() + s,
                     center.y() - s);
    break;
  case SnapType::Quadrant: {
    QPolygonF dia;
    dia << QPointF(center.x(), center.y() - s)
        << QPointF(center.x() + s, center.y())
        << QPointF(center.x(), center.y() + s)
        << QPointF(center.x() - s, center.y());
    painter.drawPolygon(dia);
  } break;
  case SnapType::Nearest: {
    QPolygonF hg;
    hg << QPointF(center.x() - s, center.y() - s)
       << QPointF(center.x() + s, center.y() - s)
       << QPointF(center.x() - s, center.y() + s)
       << QPointF(center.x() + s, center.y() + s)
       << QPointF(center.x() - s, center.y() - s);
    painter.drawPolyline(hg);
  } break;
  case SnapType::Perpendicular: {
    painter.drawLine(center.x() - s, center.y() - s, center.x() - s,
                     center.y() + s);
    painter.drawLine(center.x() - s, center.y() + s, center.x() + s,
                     center.y() + s);
    painter.drawLine(center.x(), center.y() + s, center.x(), center.y());
    painter.drawLine(center.x() - s, center.y(), center.x(), center.y());
  } break;
  default:
    break;
  }
  painter.restore();
}
