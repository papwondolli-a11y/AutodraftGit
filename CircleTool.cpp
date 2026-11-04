#include "circletool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QUndoStack>

#include <QDebug>
#include <QKeyEvent>
#include <QtMath>

CircleTool::CircleTool(QObject *parent)
    : AbstractTool(parent), m_isDrawing(false) {}

CircleTool::~CircleTool() { cancelDrawing(); }

void CircleTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);
  m_isDrawing = false;
  m_isDiameterMode = false; // [신규] 초기화

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("CIRCLE", CommandDock::Command);
    ctx->commandDock()->showInteractivePrompt(tr(" 중심점을 지정 하세요 : "), {});
  }
}

void CircleTool::deactivate() {
  cancelDrawing();
  AbstractTool::deactivate();
}

bool CircleTool::processCommand(const QString &command) {
  QString cmd = command.trimmed().toLower();
  if (cmd.isEmpty())
    return false;

  IAppContext *ctx = getContext();
  if (!ctx)
    return false;

  // 1. 그리는 중인 경우 (반지름 또는 지름 입력)
  if (m_isDrawing) {
      // "D" 직접 입력 또는 프롬프트의 [Diameter(D)] 옵션 클릭 모두 처리
      if (cmd == "d" || cmd == "diameter" || cmd == "diameter(d)") {
          m_isDiameterMode = true; // [신규] 지름 모드로 전환
          if (ctx->commandDock()) {
              ctx->commandDock()->showInteractivePrompt(tr(" 지름(⌀)값 입력 또는 화면에서 지정 하세요 : "), {});
          }
          return true;
      }

      bool ok = false;
      double val = cmd.toDouble(&ok);
      if (ok && val > 0) {
          qreal radius = m_isDiameterMode ? val / 2.0 : val;
          finishCircle(radius);
          return true;
      }
  }
  // 2. 그리는 중이 아닌 경우 (중심점 좌표 입력)
  else {
    if (ctx->parser()) {
      QPointF resultPos;
      if (ctx->parser()->parseCoordinate(cmd, QPointF(0, 0), QPointF(0, 0),
                                         false, resultPos)) {
        mousePress(resultPos, Qt::LeftButton);
        return true;
      }
    }
  }

  return false;
}

void CircleTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;
  if (button != Qt::LeftButton)
    return;

  // 1. 스냅 계산
  QPointF snappedPos = pos;
  IAppContext *ctx = getContext();
  if (ctx && ctx->snapManager()) {
    QPointF base = m_isDrawing ? m_centerPoint : QPointF();
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
  }

  if (!m_isDrawing) {
    // [1단계] 중심점 지정
    m_isDrawing = true;
    m_centerPoint = snappedPos;
    m_currentPos = snappedPos;

    // 시작점이 찍혔으므로 고스트 아이템 설정
    if (m_scene) {
      m_scene->setGhostPath(QPainterPath(), true);
    }

    if (ctx && ctx->commandDock()) {
      QStringList options;
      options << "Diameter(D)";
      ctx->commandDock()->showInteractivePrompt(
          tr(" 반지름(R)값 입력 또는 화면에서 지정 하세요 or [Diameter] : "), options);
    }
  } else {
    // [2단계] 반지름/지름 확정
    m_currentPos = snappedPos;
    if (m_isDiameterMode) {
        qreal diameter = QLineF(m_centerPoint, m_currentPos).length();
        finishCircle(diameter / 2.0);
    } else {
        finishCircle();
    }
  }
}

void CircleTool::mouseMove(QPointF pos) {
  if (!m_scene || m_scene->views().isEmpty())
    return;

  // 1. 스냅 계산
  QPointF snappedPos = pos;
  IAppContext *ctx = getContext();
  if (ctx && ctx->snapManager()) {
    QPointF base = m_isDrawing ? m_centerPoint : QPointF();
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
  }
  m_currentPos = snappedPos;

  // 2. 고스트 업데이트
  if (m_isDrawing && m_scene) {
    qreal dist = QLineF(m_centerPoint, m_currentPos).length();
    qreal radius = m_isDiameterMode ? dist / 2.0 : dist; // [지름 모드] 반영
    QPainterPath path;
    path.addEllipse(m_centerPoint, radius, radius);

    updateGhostPreview(path, currentLayerName());
  }

  // 3. 스냅 마커
  if (m_scene) {
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
      snapType =
          ctx->snapManager()->snapPoint(pos, m_scene, m_centerPoint).type;
    }
    m_scene->setSnapMarker(m_currentPos, snapType);
  }
}

void CircleTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
  Q_UNUSED(pos);
  Q_UNUSED(button);
}

void CircleTool::finishCircle(qreal overrideRadius) {
  if (!m_scene)
    return;

  qreal radius = (overrideRadius > 0)
                     ? overrideRadius
                     : QLineF(m_centerPoint, m_currentPos).length();

  if (radius > 0.001) {
    IAppContext *ctx = getContext();
    if (ctx && ctx->entityFactory() && ctx->layerManager() &&
        ctx->lineTypeManager()) {
      Layer *curLayer = ctx->layerManager()->getCurrentLayer();

      QPen pen(curLayer->color());
      pen.setWidthF(curLayer->weight());

      QString linetypeName = curLayer->linetype();
      if (linetypeName == "Continuous" || linetypeName == "Continous") {
        pen.setStyle(Qt::SolidLine);
      } else {
        pen.setStyle(Qt::CustomDashLine);
        QVector<qreal> pattern =
            ctx->lineTypeManager()->getQtDashPattern(linetypeName);
        if (!pattern.isEmpty()) {
          pen.setDashPattern(pattern);
        } else {
          pen.setStyle(Qt::SolidLine);
        }
      }

      QGraphicsItem *realItem =
          ctx->entityFactory()->createCircle(m_centerPoint, radius, pen);
      if(!realItem) return;

      if (CadItem *ci = dynamic_cast<CadItem *>(realItem)) {
        ci->setLayerName(curLayer->name());
      }

      QGraphicsItem *layerFolder = m_scene->getLayerRoot(curLayer->name());
      realItem->setParentItem(layerFolder);

      if (realItem->scene() == m_scene) {
        m_scene->removeItem(realItem);
      }

      if (ctx && ctx->undoStack()) {
        ctx->undoStack()->push(new AddEntityCommand(m_scene, realItem));
      } else {
        m_scene->addItem(realItem);
      }

      if (ctx->commandDock()) {
        ctx->commandDock()->logMessage("Circle created.", CommandDock::Info);
        ctx->commandDock()->setPrompt(" Command : ");
      }
    }
//    qDebug() << "Circle Created. R=" << radius;
  }

  cancelDrawing();
  emit toolFinished();
}

void CircleTool::cancelDrawing() {
  m_isDrawing = false;
  m_isDiameterMode = false; // [신규] 초기화
  if (m_scene) {
    m_scene->setGhostPath(QPainterPath(), false);
  }
  clearOverlay(); // 스냅 마커 등 정리
}

void CircleTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}

void CircleTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    cancelDrawing();
    emit toolFinished();
  } else {
    AbstractTool::keyPress(event);
  }
}
