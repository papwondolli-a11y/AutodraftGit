#include "arc3pointtool.h"
#include "arcitem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "myvector.h"
#include "snapmanager.h"
#include <QKeyEvent>
#include <QUndoStack>
#include <QtMath>

Arc3PointTool::Arc3PointTool(QObject *parent)
    : AbstractTool(parent), m_step(0) {}

Arc3PointTool::~Arc3PointTool() { cancelDrawing(); }

void Arc3PointTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);
  m_step = 0;

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("ARC (3-Point)", CommandDock::Command);
    ctx->commandDock()->showInteractivePrompt(tr(" 호의 첫 번째 점을 지정하세요 : "), {});
  }
}

void Arc3PointTool::deactivate() {
  cancelDrawing();
  AbstractTool::deactivate();
}

bool Arc3PointTool::processCommand(const QString &cmd) {
  IAppContext *ctx = getContext();
  if (!ctx)
    return false;

  QString input = cmd.trimmed();
  if (input.isEmpty())
    return false;

  QPointF base = QPointF();
  if (m_step == 1) {
    base = m_p1;
  } else if (m_step == 2) {
    base = m_p2;
  }

  QPointF resultPos;
  bool isOrtho = (ctx->snapManager() && ctx->snapManager()->isOrtho());
  if (ctx->parser() &&
      ctx->parser()->parseCoordinate(input, base, m_currentMousePos,
                                     isOrtho, resultPos)) {
    mousePress(resultPos, Qt::LeftButton);
    return true;
  }

  return false;
}

void Arc3PointTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;

  if (button == Qt::RightButton) {
    cancelDrawing();
    emit toolFinished();
    return;
  }
  if (button != Qt::LeftButton)
    return;

  QPointF snappedPos = pos;
  IAppContext *ctx = getContext();
  if (ctx && ctx->snapManager()) {
    QPointF base = QPointF();
    if (m_step == 1) {
      base = m_p1;
    } else if (m_step == 2) {
      base = m_p2;
    }
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
  }

  if (m_step == 0) {
    m_p1 = snappedPos;
    m_step = 1;
    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->showInteractivePrompt(tr(" 호의 두 번째 점을 지정하세요 : "), {});
    }
  } else if (m_step == 1) {
    if (QLineF(m_p1, snappedPos).length() < 0.001)
      return;
    m_p2 = snappedPos;
    m_step = 2;
    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->showInteractivePrompt(tr(" 호의 세 번째 점을 지정하세요 : "), {});
    }
  } else if (m_step == 2) {
    if (QLineF(m_p2, snappedPos).length() < 0.001 || QLineF(m_p1, snappedPos).length() < 0.001)
      return;
    m_p3 = snappedPos;
    finishArc();
  }
}

void Arc3PointTool::mouseMove(QPointF pos) {
  if (!m_scene)
    return;

  QPointF snappedPos = pos;
  SnapType snapType = SnapType::None;
  IAppContext *ctx = getContext();

  if (ctx && ctx->snapManager()) {
    QPointF base = QPointF();
    if (m_step == 1) {
      base = m_p1;
    } else if (m_step == 2) {
      base = m_p2;
    }
    SnapManager::SnapResult result = ctx->snapManager()->snapPoint(pos, m_scene, base);
    snappedPos = result.snappedPos;
    snapType = result.type;
  }
  m_currentMousePos = snappedPos;

  // 1. Snap Marker 설정
  m_scene->setSnapMarker(m_currentMousePos, snapType);

  // 2. Ghost Updates
  if (m_step == 1) {
    // 1단계에서는 첫 번째 점에서 현재 마우스 위치까지 직선 가이드 표시
    updateGhostLinePreview(QLineF(m_p1, m_currentMousePos), currentLayerName());
    m_scene->setGhostPath(QPainterPath(), false);
  } else if (m_step == 2) {
    // 2단계에서는 P1, P2, 현재 마우스 위치를 통과하는 가상 호 표시
    m_scene->setGhostLine(QLineF(), false);
    QPointF center;
    qreal radius, startAngle, spanAngle;
    if (calculateArc(center, radius, startAngle, spanAngle)) {
      qreal cross = (m_p2.x() - m_p1.x()) * (m_currentMousePos.y() - m_p2.y()) -
                    (m_p2.y() - m_p1.y()) * (m_currentMousePos.x() - m_p2.x());
      QPointF startPt = (cross > 0.0) ? m_p1 : m_currentMousePos;
      QPointF endPt = (cross > 0.0) ? m_currentMousePos : m_p1;

      MyArc tempArc = ArcItem::createFrom3Points(MyVector(center), MyVector(startPt), MyVector(endPt));
      QPainterPath path = ArcItem::createQtPath(tempArc);
      updateGhostPreview(path, currentLayerName());
    } else {
      m_scene->setGhostPath(QPainterPath(), false);
    }
  } else {
    // 0단계일 때는 고스트 가이드를 끄고 스냅 마커 유지
    m_scene->setGhostLine(QLineF(), false);
    m_scene->setGhostPath(QPainterPath(), false);
  }
}

void Arc3PointTool::finishArc() {
  if (!m_scene)
    return;

  QPointF center;
  qreal radius, startAngle, spanAngle;
  if (!calculateArc(center, radius, startAngle, spanAngle)) {
    cancelDrawing();
    emit toolFinished();
    return;
  }

  IAppContext *ctx = getContext();
  if (ctx && ctx->entityFactory() && ctx->layerManager() &&
      ctx->lineTypeManager()) {
    Layer *curLayer = ctx->layerManager()->getCurrentLayer();
    if (!curLayer)
      return;

    QPen pen(curLayer->color());
    pen.setWidthF(curLayer->weight());

    QString linetype = curLayer->linetype();
    if (linetype != "Continuous" && linetype != "Continous") {
      pen.setStyle(Qt::CustomDashLine);
      QVector<qreal> pattern =
          ctx->lineTypeManager()->getQtDashPattern(linetype);
      if (!pattern.isEmpty())
        pen.setDashPattern(pattern);
      else
        pen.setStyle(Qt::SolidLine);
    } else {
      pen.setStyle(Qt::SolidLine);
    }

    // CCW 방향이면 P1 -> P3, CW 방향이면 P3 -> P1로 통과
    qreal cross = (m_p2.x() - m_p1.x()) * (m_p3.y() - m_p2.y()) -
                  (m_p2.y() - m_p1.y()) * (m_p3.x() - m_p2.x());
    QPointF startPoint = (cross > 0.0) ? m_p1 : m_p3;
    QPointF endPoint = (cross > 0.0) ? m_p3 : m_p1;

    QGraphicsItem *realItem = ctx->entityFactory()->createArc(
        center, startPoint, endPoint, pen);

    if (!realItem)
      return;

    if (ArcItem *ai = dynamic_cast<ArcItem *>(realItem)) {
      ai->setLayerName(curLayer->name());
    }

    QGraphicsItem *layerFolder = m_scene->getLayerRoot(curLayer->name());
    realItem->setParentItem(layerFolder);

    if (realItem->scene() == m_scene) {
      m_scene->removeItem(realItem);
    }

    if (ctx->undoStack()) {
      ctx->undoStack()->push(new AddEntityCommand(m_scene, realItem));
    } else {
      m_scene->addItem(realItem);
    }

    if (ctx->commandDock()) {
      ctx->commandDock()->logMessage("Arc (3-Point) created.", CommandDock::Info);
      ctx->commandDock()->setPrompt(" Command : ");
    }
  }

  cancelDrawing();
  emit toolFinished();
}

bool Arc3PointTool::calculateArc(QPointF &center, qreal &radius, qreal &startAngle, qreal &spanAngle) const {
  qreal x1 = m_p1.x(), y1 = m_p1.y();
  qreal x2 = m_p2.x(), y2 = m_p2.y();
  qreal x3 = m_currentMousePos.x(), y3 = m_currentMousePos.y();

  if (m_step == 2) {
    x3 = m_p3.x();
    y3 = m_p3.y();
  }

  // 외적 기반 분모 계산 (A)
  qreal A = x1 * (y2 - y3) - y1 * (x2 - x3) + x2 * y3 - x3 * y2;

  // 세 점이 거의 일직선상에 있는 경우 판단
  if (qAbs(A) < 1e-6) {
    return false;
  }

  qreal sq1 = x1 * x1 + y1 * y1;
  qreal sq2 = x2 * x2 + y2 * y2;
  qreal sq3 = x3 * x3 + y3 * y3;

  qreal xc = (sq1 * (y2 - y3) + sq2 * (y3 - y1) + sq3 * (y1 - y2)) / (2.0 * A);
  qreal yc = (sq1 * (x3 - x2) + sq2 * (x1 - x3) + sq3 * (x2 - x1)) / (2.0 * A);

  center = QPointF(xc, yc);
  radius = qSqrt((x1 - xc) * (x1 - xc) + (y1 - yc) * (y1 - yc));

  if (radius < 1e-4) {
    return false;
  }

  // 각도 구하기
  qreal a1 = qRadiansToDegrees(std::atan2(y1 - yc, x1 - xc));
 // qreal a2 = qRadiansToDegrees(std::atan2(y2 - yc, x2 - xc));
  qreal a3 = qRadiansToDegrees(std::atan2(y3 - yc, x3 - xc));

  auto normalize = [](qreal angle) {
    while (angle < 0.0) angle += 360.0;
    while (angle >= 360.0) angle -= 360.0;
    return angle;
  };

  a1 = normalize(a1);
//  a2 = normalize(a2);
  a3 = normalize(a3);

  // 세 점의 방향성(CCW/CW) 판단
  qreal cross = (x2 - x1) * (y3 - y2) - (y2 - y1) * (x3 - x2);

  if (cross > 0.0) { // CCW
    startAngle = a1;
    qreal span = a3 - a1;
    if (span < 0.0) span += 360.0;
    spanAngle = span;
  } else { // CW
    startAngle = a3;
    qreal span = a1 - a3;
    if (span < 0.0) span += 360.0;
    spanAngle = span;
  }

  return true;
}

void Arc3PointTool::cancelDrawing() {
  m_step = 0;
  clearOverlay();
}

void Arc3PointTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}

void Arc3PointTool::mouseRelease(QPointF, Qt::MouseButton) {}

void Arc3PointTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    cancelDrawing();
    emit toolFinished();
  } else {
    AbstractTool::keyPress(event);
  }
}
