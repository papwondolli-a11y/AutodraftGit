#include "splinetool.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "myspline.h"
#include "splineitem.h"
#include "snapmanager.h"
#include <QKeyEvent>
#include <QUndoStack>

SplineTool::SplineTool(IAppContext *ctx, QObject *parent)
    : AbstractTool(parent), m_isDrawing(false), m_liveSpline(nullptr) {
    setContext(ctx);
}

SplineTool::~SplineTool() { cancelDrawing(); }

void SplineTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;
  AbstractTool::activate(scene);

  m_isDrawing = false;
  m_vertices.clear();
  m_liveSpline = nullptr;

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("SPLINE", CommandDock::Command);
    ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 제어점을 지정 하세요 : "), {});
  }
}

void SplineTool::deactivate() {
  cancelDrawing();
  AbstractTool::deactivate();
}

void SplineTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;
  IAppContext *ctx = getContext();

  if (button == Qt::RightButton) {
    if (m_isDrawing) {
      finishSpline(false);
    } else {
      if (ctx)
        ctx->finishCurrentTool();
    }
    return;
  }

  if (button != Qt::LeftButton)
    return;

  QPointF snappedPos = pos;
  if (ctx && ctx->snapManager()) {
    QPointF base = m_vertices.empty() ? QPointF() : m_vertices.back().toPointF();
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
  }

  m_vertices.push_back(MyVector(snappedPos));

  QPen layerPen(Qt::white, 0);
  Layer *curLayer = nullptr;
  if (ctx && ctx->layerManager()) {
    curLayer = ctx->layerManager()->getCurrentLayer();
    layerPen.setColor(curLayer->color());
    layerPen.setWidthF(curLayer->weight());
  }

  if (!m_isDrawing) {
    m_isDrawing = true;
  } else {
    if (m_vertices.size() == 2) {
      if (ctx && ctx->entityFactory()) {
        QGraphicsItem *item = ctx->entityFactory()->createSpline(m_vertices, 3, false, layerPen);
        m_liveSpline = dynamic_cast<SplineItem *>(item);
        if (m_liveSpline && m_scene) {
          m_scene->addItem(m_liveSpline);
          m_liveSpline->setZValue(1000);
        }
      }
    } else if (m_liveSpline) {
      // Simplified: replace with new item or update in-place
      SplineItem* newItem = new SplineItem(m_vertices, 3, false);
      newItem->setPen(layerPen);
      m_scene->removeItem(m_liveSpline);
      delete m_liveSpline;
      m_liveSpline = newItem;
      m_scene->addItem(m_liveSpline);
      m_liveSpline->setZValue(1000);
    }
  }

  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->showInteractivePrompt(tr(" 다음 제어점을 지정 하세요 (종료: Enter/RightClick) : "), {});
  }
}

void SplineTool::mouseMove(QPointF pos) {
  m_currentMousePos = pos;
  if (!m_scene)
    return;

  QPointF finalPos = pos;
  SnapType snapType = SnapType::None;

  IAppContext *ctx = getContext();
  if (ctx && ctx->snapManager()) {
    QPointF base = m_vertices.empty() ? QPointF() : m_vertices.back().toPointF();
    auto result = ctx->snapManager()->snapPoint(pos, m_scene, base);
    finalPos = result.snappedPos;
    snapType = result.type;
  }

  m_scene->setSnapMarker(finalPos, snapType);

  if (m_isDrawing) {
    updateGhostLinePreview(QLineF(m_vertices.back().toPointF(), finalPos), currentLayerName());
  }
}

void SplineTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    cancelDrawing();
    emit toolFinished();
    return;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if (m_isDrawing && m_vertices.size() >= 2) {
      finishSpline(false);
    }
    return;
  }
  AbstractTool::keyPress(event);
}

bool SplineTool::processCommand(const QString &command) {
  QString cmd = command.toLower().trimmed();
  IAppContext *ctx = getContext();
  if (ctx && ctx->parser()) {
    QPointF base = m_vertices.empty() ? QPointF(0, 0) : m_vertices.back().toPointF();
    QPointF resultPos;
    bool isOrtho = (ctx->snapManager() && ctx->snapManager()->isOrtho());
    if (ctx->parser()->parseCoordinate(cmd, base, m_currentMousePos, isOrtho, resultPos)) {
      mousePress(resultPos, Qt::LeftButton);
      return true;
    }
  }
  return false;
}

void SplineTool::finishSpline(bool closed) {
  if (m_vertices.size() < 2) {
    cancelDrawing();
    return;
  }

  IAppContext *ctx = getContext();
  if (ctx && ctx->entityFactory()) {
    Layer *curLayer = ctx->layerManager() ? ctx->layerManager()->getCurrentLayer() : nullptr;
    QPen pen(Qt::white, 0);
    if (curLayer) {
      pen.setColor(curLayer->color());
      pen.setWidthF(curLayer->weight());
    }

    QGraphicsItem *item = ctx->entityFactory()->createSpline(m_vertices, 3, closed, pen);
    if (item && m_scene) {
      if (curLayer) {
        if (SplineItem *si = dynamic_cast<SplineItem *>(item)) {
          si->setLayerName(curLayer->name());
        }
        item->setParentItem(m_scene->getLayerRoot(curLayer->name()));
      }
      if (item->scene() == m_scene) m_scene->removeItem(item);

      if (ctx->undoStack()) {
        ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
      } else {
        m_scene->addItem(item);
      }
    }
  }

  if (m_liveSpline) {
    m_scene->removeItem(m_liveSpline);
    delete m_liveSpline;
    m_liveSpline = nullptr;
  }

  m_vertices.clear();
  m_isDrawing = false;
  clearOverlay();
  emit toolFinished();
}

void SplineTool::cancelDrawing() {
  m_isDrawing = false;
  m_vertices.clear();
  if (m_liveSpline) {
    if (m_scene) m_scene->removeItem(m_liveSpline);
    delete m_liveSpline;
    m_liveSpline = nullptr;
  }
  clearOverlay();
}

void SplineTool::clearOverlay() {
  if (m_scene) m_scene->clearOverlay();
}
