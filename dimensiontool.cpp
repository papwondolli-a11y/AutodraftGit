#include "dimensiontool.h"
#include "arcitem.h"
#include "cadcommands.h"
#include "circleitem.h"
#include "commanddock.h"
#include "dimensionitem.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QUndoStack>


DimensionTool::DimensionTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitStartPoint), m_isDrawing(false) {
  m_data = MyDimension();
}

DimensionTool::~DimensionTool() { cancelDrawing(); }

void DimensionTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;
  AbstractTool::activate(scene);
  m_state = WaitStartPoint;
  m_isDrawing = false;

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("DIM", CommandDock::Command);
    ctx->commandDock()->showInteractivePrompt(
        " Specify first extension line origin or [Select circle/arc] : ", {});
  }
}

void DimensionTool::deactivate() {
  cancelDrawing();
  AbstractTool::deactivate();
}

void DimensionTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;
  IAppContext *ctx = getContext();

  if (button == Qt::RightButton) {
    onCancel();
    return;
  }

  if (button != Qt::LeftButton)
    return;

  QPointF snappedPos = pos;
  if (ctx && ctx->snapManager()) {
    QPointF base =
        (m_state != WaitStartPoint) ? m_data.start.toPointF() : QPointF();
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
  }

  if (m_state == WaitStartPoint) {
    // Check if there's a circle or arc at the clicked position
    if (m_scene) {
      QList<QGraphicsItem *> items = m_scene->items(pos);
      for (QGraphicsItem *item : items) {
        if (CircleItem *circle = dynamic_cast<CircleItem *>(item)) {
          m_data.dimensionType = DimensionType::Diametric;
          m_data.start = circle->getCircle().center;
          m_data.end = circle->getCircle().center +
                       MyVector(circle->getCircle().radius, 0); // Temporary end
          m_state = WaitCircleDimPoint;
          m_isDrawing = true;
          if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(
                " Specify dimension line location : ", {});
          }
          return;
        } else if (ArcItem *arc = dynamic_cast<ArcItem *>(item)) {
          m_data.dimensionType = DimensionType::Radial;
          m_data.start = arc->getArc().center;
          m_data.end = arc->getArc().center +
                       MyVector(arc->getArc().radius, 0); // Temporary end
          m_state = WaitCircleDimPoint;
          m_isDrawing = true;
          if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(
                " Specify dimension line location : ", {});
          }
          return;
        }
      }
    }

    m_data.dimensionType = DimensionType::Linear;
    m_data.start = MyVector(snappedPos);
    m_state = WaitEndPoint;
    m_isDrawing = true;
    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->showInteractivePrompt(
          " Specify second extension line origin : ", {});
    }
  } else if (m_state == WaitEndPoint) {
    m_data.end = MyVector(snappedPos);
    m_data.dimLine = m_data.end; // Initial dim line position
    m_state = WaitDimLinePoint;
    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->showInteractivePrompt(
          " Specify dimension line location : ", {});
    }
  } else if (m_state == WaitDimLinePoint || m_state == WaitCircleDimPoint) {
    m_data.dimLine = MyVector(snappedPos);
    finalizeDimension();
    m_state = WaitStartPoint;
    m_isDrawing = false;
    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->showInteractivePrompt(
          " Specify first extension line origin or [Select circle/arc] : ", {});
    }
  }
}

void DimensionTool::mouseMove(QPointF pos) {
  m_currentMousePos = pos;
  IAppContext *ctx = getContext();

  QPointF finalPos = pos;
  SnapType snapType = SnapType::None;

  if (ctx && ctx->snapManager()) {
    QPointF base =
        (m_state != WaitStartPoint) ? m_data.start.toPointF() : QPointF();
    SnapManager::SnapResult result =
        ctx->snapManager()->snapPoint(pos, m_scene, base);
    finalPos = result.snappedPos;
    snapType = result.type;
  }

  if (m_scene) {
    m_scene->setSnapMarker(finalPos, snapType);
  }

  if (m_isDrawing) {
    if (m_state == WaitEndPoint) {
      m_data.end = MyVector(finalPos);
      m_data.dimLine = m_data.end;
    } else if (m_state == WaitDimLinePoint || m_state == WaitCircleDimPoint) {
      m_data.dimLine = MyVector(finalPos);
    }
    updateGhostPreview();
  }
}

void DimensionTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
  Q_UNUSED(pos);
  Q_UNUSED(button);
}

bool DimensionTool::processCommand(const QString &command) {
  Q_UNUSED(command);
  return false;
}

void DimensionTool::onFinish() { finalizeDimension(); }

void DimensionTool::onCancel() {
  cancelDrawing();
  if (getContext() && getContext()->commandDock()) {
    getContext()->commandDock()->setPrompt(" Command : ");
  }
  emit toolFinished();
}

void DimensionTool::cancelDrawing() {
  m_isDrawing = false;
  m_state = WaitStartPoint;
  m_data = MyDimension(); // 데이터 초기화
  if (m_scene) {
    m_scene->clearOverlay();
  }
}

void DimensionTool::finalizeDimension() {
  if (!m_scene)
    return;
  if (m_data.start.distanceTo(m_data.end) < 0.001)
    return;

  IAppContext *ctx = getContext();
  if (ctx && ctx->entityFactory() && ctx->layerManager()) {
    Layer *curLayer = ctx->layerManager()->getCurrentLayer();
    QPen pen(curLayer->color());
    pen.setWidthF(curLayer->weight());

    applyDetailRegionScale(m_data); // 상세도 영역 안이면 measureScale=1/축척
    QGraphicsItem *item = ctx->entityFactory()->createDimension(m_data, pen);
    if (CadItem *cad = dynamic_cast<CadItem *>(item)) {
      cad->setLayerName(curLayer->name());
    }

    QGraphicsItem *layerRoot = m_scene->getLayerRoot(curLayer->name());
    item->setParentItem(layerRoot);

    if (item->scene() == m_scene) {
      m_scene->removeItem(item);
    }

    // 생성 시 그립 숨기기
    if (CadItem *cad = dynamic_cast<CadItem *>(item)) {
      cad->setGripsVisible(false);
    }

    if (ctx->undoStack()) {
      ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
    } else {
      m_scene->addItem(item);
    }
  }
  m_scene->clearOverlay();
  m_data = MyDimension(); // 완료 후 데이터 초기화
}

void DimensionTool::updateGhostPreview() {
  if (!m_isDrawing || !m_scene)
    return;

  // Use DimensionItem's geometryPath for ghosting
  DimensionItem ghost(m_data);
  m_scene->setGhostPath(ghost.geometryPath(), true, Qt::yellow, 1.0);
}
