#include "arccentertool.h"
#include "arcitem.h"
#include "cadcommands.h"
//#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "myvector.h"
#include "snapmanager.h"
#include <QDebug>
#include <QKeyEvent>
#include <QUndoStack>
#include <QtMath>

ArcCenterTool::ArcCenterTool(QObject *parent)
    : AbstractTool(parent), m_step(0) {}

ArcCenterTool::~ArcCenterTool() { cancelDrawing(); }

void ArcCenterTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);
  m_step = 0;

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("ARC (Center)", CommandDock::Command);
    ctx->commandDock()->showInteractivePrompt(tr(" 호의 중심점을 지정 하세요 : "), {});
  }
}

void ArcCenterTool::deactivate() {
  cancelDrawing();
  AbstractTool::deactivate();
}

bool ArcCenterTool::processCommand(const QString &cmd) {
  IAppContext *ctx = getContext();
  if (!ctx)
    return false;

  QString input = cmd.trimmed();
  if (input.isEmpty())
    return false;

  if (m_step == 1) { // Center picked, radius input
    QPointF resultPos;
    bool isOrtho = (ctx->snapManager() && ctx->snapManager()->isOrtho());
    if (ctx->parser() &&
        ctx->parser()->parseCoordinate(input, m_center, m_currentMousePos,
                                       isOrtho, resultPos)) {
      m_startPoint = resultPos;
      m_step = 2;

      if (ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 다음점을 지정하세요  ( 또는 각도숫자 ex: 45 ) : "), {});
      }
      return true;
    }
  } else if (m_step == 2) { // Start point picked, angle or next point input
    bool ok = false;
    double angleDeg = input.toDouble(&ok);
    if (ok) {
      QLineF baseLine(m_center, m_startPoint);
      qreal baseAngle = baseLine.angle();
      // CAD 표준: 반시계 방향(+), 시계 방향(-)
      // Qt 화면 좌표계(Y-Down)에서는 기본이 CW(+)이므로, CCW를 위해 차감
      qreal targetAngle = baseAngle - angleDeg;

      QLineF endLine = baseLine;
      endLine.setAngle(targetAngle);
      m_endPoint = endLine.p2();

      finishArc();
      return true;
    } else {
      // 숫자가 아닌 경우 좌표 입력 시도
      QPointF resultPos;
      bool isOrtho = (ctx->snapManager() && ctx->snapManager()->isOrtho());
      if (ctx->parser() &&
          ctx->parser()->parseCoordinate(input, m_center, m_currentMousePos,
                                         isOrtho, resultPos)) {
        m_endPoint = resultPos;
        finishArc();
        return true;
      }
    }
  } else if (m_step == 0) { // Specify center
    QPointF resultPos;
    if (ctx->parser() &&
        ctx->parser()->parseCoordinate(input, QPointF(0, 0), QPointF(0, 0),
                                       false, resultPos)) {
      mousePress(resultPos, Qt::LeftButton);
      return true;
    }
  }

  return false;
}

void ArcCenterTool::mousePress(QPointF pos, Qt::MouseButton button) {
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
    QPointF base = (m_step > 0) ? m_center : QPointF();
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
  }

  if (m_step == 0) {
    m_center = snappedPos;
    m_step = 1;
    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->showInteractivePrompt(tr(" 반지름(R) 점을 지정 하세요 (또는 반지름값 입력 ex: 30, 30<45) : "), {});
    }
  } else if (m_step == 1) {
    if (QLineF(m_center, snappedPos).length() < 0.001)
      return;
    m_startPoint = snappedPos;
    m_step = 2;
    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->showInteractivePrompt(
          tr("  다음점을 지정하세요  ( 또는 각도숫자 ex: 45 ) : "), {});
    }
  } else if (m_step == 2) {
    m_endPoint = snappedPos;
    finishArc();
  }
}

void ArcCenterTool::mouseMove(QPointF pos) {
  if (!m_scene)
    return;

  QPointF snappedPos = pos;
  SnapType snapType = SnapType::None;
  IAppContext *ctx = getContext();

  // 1. 스냅 계산
  if (ctx && ctx->snapManager()) {
    QPointF base = (m_step > 0) ? m_center : QPointF();
    SnapManager::SnapResult result = ctx->snapManager()->snapPoint(pos, m_scene, base);
    snappedPos = result.snappedPos;
    snapType = result.type;
  }
  m_currentMousePos = snappedPos;

  // 2. Snap Marker 설정
  m_scene->setSnapMarker(m_currentMousePos, snapType);

  // 3. Ghost Updates
  if (m_step == 1) {
    updateGhostLinePreview(QLineF(m_center, m_currentMousePos), currentLayerName());
  } else if (m_step == 2) {
    MyArc tempArc =
        ArcItem::createFrom3Points(MyVector(m_center), MyVector(m_startPoint),
                                   MyVector(m_currentMousePos));
    QPainterPath path = ArcItem::createQtPath(tempArc);
    updateGhostPreview(path, currentLayerName());
  } else {
    // m_step == 0 일 때 clearOverlay() 대신 고스트 오버레이만 끄도록 수정하여 스냅 마커 유지
    m_scene->setGhostLine(QLineF(), false);
    m_scene->setGhostPath(QPainterPath(), false);
  }
}

void ArcCenterTool::finishArc() {
  if (!m_scene)
    return;

  IAppContext *ctx = getContext();
  if (ctx && ctx->entityFactory() && ctx->layerManager() &&
      ctx->lineTypeManager()) {
    Layer *curLayer = ctx->layerManager()->getCurrentLayer();
    if (!curLayer)
      return; // [방어 코드] 레이어 객체 널 체크
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

    QGraphicsItem *realItem = ctx->entityFactory()->createArc(
        m_center, m_startPoint, m_endPoint, pen);

    if(!realItem) return;
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
      ctx->commandDock()->logMessage("Arc (Center) created.",
                                     CommandDock::Info);
      ctx->commandDock()->setPrompt(" Command : ");
    }
  }

  cancelDrawing();
  emit toolFinished();
}

void ArcCenterTool::cancelDrawing() {
  m_step = 0;
  clearOverlay();
}

void ArcCenterTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}

void ArcCenterTool::mouseRelease(QPointF, Qt::MouseButton) {}

void ArcCenterTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    cancelDrawing();
    emit toolFinished();
  } else {
    AbstractTool::keyPress(event);
  }
}
