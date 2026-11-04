#include "polylinetool.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "mypolyline.h"
#include "polylineitem.h"
#include "snapmanager.h"
#include <QDebug>
#include <QKeyEvent>
#include <QUndoStack>

PolylineTool::PolylineTool(QObject *parent)
    : AbstractTool(parent), m_isDrawing(false), m_livePolyline(nullptr) {}

PolylineTool::~PolylineTool() { cancelDrawing(); }

void PolylineTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;
  AbstractTool::activate(scene);

  m_isDrawing = false;
  m_vertices.clear();
  m_livePolyline = nullptr;

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("PLINE", CommandDock::Command);
    ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 점을 지정 하세요 : "), {});
  }
}

void PolylineTool::deactivate() {
  cancelDrawing();
  if (m_livePolyline) {
    if (m_scene)
      m_scene->removeItem(m_livePolyline);
    delete m_livePolyline;
    m_livePolyline = nullptr;
  }
  AbstractTool::deactivate();
}

void PolylineTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;
  IAppContext *ctx = getContext();

  if (button == Qt::RightButton) {
    if (m_isDrawing) {
      finishPolyline(false);
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
    QPointF base =
        m_vertices.empty() ? QPointF() : m_vertices.back().toPointF();
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
  }

  MyVector v(snappedPos);
  m_vertices.push_back(v);

  QPen layerPen(Qt::white, 0);
  Layer *curLayer = nullptr;
  if (ctx && ctx->layerManager()) {
    curLayer = ctx->layerManager()->getCurrentLayer();
    layerPen.setColor(curLayer->color());
    layerPen.setWidthF(curLayer->weight());

    if (ctx->lineTypeManager()) {
      QVector<qreal> pattern =
          ctx->lineTypeManager()->getQtDashPattern(curLayer->linetype());
      if (!pattern.isEmpty()) {
        layerPen.setDashPattern(pattern);
      } else {
        layerPen.setStyle(Qt::SolidLine);
      }
    }
  }

  if (!m_isDrawing) {
    m_isDrawing = true;
  } else {
    // 2nd point onward
    if (m_vertices.size() == 2) {
      if (ctx && ctx->entityFactory()) {
        QGraphicsItem *item =
            ctx->entityFactory()->createPolyline(m_vertices, false, layerPen);
        m_livePolyline = dynamic_cast<PolylineItem *>(item);
        if (m_livePolyline && m_scene) {
          m_scene->addItem(m_livePolyline);
          m_livePolyline->setZValue(1000);
        }
      }
    } else if (m_livePolyline) {
      m_livePolyline->setPolyline(MyPolyline(m_vertices, false));
      m_livePolyline->setPen(layerPen);
    }
  }

  if (ctx && ctx->commandDock()) {
    QStringList options;
    options << "Undo";
    if (m_vertices.size() >= 3)
      options << "Close";
    ctx->commandDock()->showInteractivePrompt(tr(" 다음 점을 지정 하세요 or "),options);
  }
}

void PolylineTool::mouseMove(QPointF pos) {
  m_currentMousePos = pos;
  if (!m_scene)
    return;

  QPointF finalPos = pos;
  SnapType snapType = SnapType::None;

  IAppContext *ctx = getContext();
  if (ctx && ctx->snapManager()) {
    QPointF base =
        m_vertices.empty() ? QPointF() : m_vertices.back().toPointF();
    auto result = ctx->snapManager()->snapPoint(pos, m_scene, base);
    finalPos = result.snappedPos;
    snapType = result.type;
  }

  m_scene->setSnapMarker(finalPos, snapType);

  if (m_isDrawing) {
    updateGhostLinePreview(QLineF(m_vertices.back().toPointF(), finalPos), currentLayerName());
  }
}

void PolylineTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    cancelDrawing();
    emit toolFinished();
    return;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if (m_isDrawing && m_vertices.size() >= 2) {
      finishPolyline(false);
    }
    return;
  }
  AbstractTool::keyPress(event);
}

bool PolylineTool::processCommand(const QString &command) {
  QString cmd = command.toLower().trimmed();
  if (cmd == "u" || cmd == "undo") {
    return undoLastVertex();
  } else if (cmd == "c" || cmd == "close") {
    if (m_vertices.size() >= 3) {
      finishPolyline(true);
      return true;
    }
  } else {
    IAppContext *ctx = getContext();
    if (ctx && ctx->parser()) {
      QPointF base =
          m_vertices.empty() ? QPointF(0, 0) : m_vertices.back().toPointF();
      QPointF resultPos;
      bool isOrtho = (ctx->snapManager() && ctx->snapManager()->isOrtho());
      if (ctx->parser()->parseCoordinate(cmd, base, m_currentMousePos, isOrtho,
                                         resultPos)) {
        mousePress(resultPos, Qt::LeftButton);
        return true;
      }
    }
  }
  return false;
}

void PolylineTool::finishPolyline(bool closed) {
  if (m_vertices.size() < 2) {
    cancelDrawing();
    return;
  }

  IAppContext *ctx = getContext();
  if (ctx && ctx->entityFactory()) {
    Layer *curLayer = nullptr;
    QPen pen(Qt::white, 0);
    if (ctx->layerManager()) {
      curLayer = ctx->layerManager()->getCurrentLayer();
      pen.setColor(curLayer->color());
      pen.setWidthF(curLayer->weight());

      if (ctx->lineTypeManager()) {
        QVector<qreal> pattern =
            ctx->lineTypeManager()->getQtDashPattern(curLayer->linetype());
        if (!pattern.isEmpty()) {
          pen.setDashPattern(pattern);
        }
      }
    }

    QGraphicsItem *item = nullptr;
    if (m_livePolyline) {
      item = m_livePolyline;
      m_livePolyline->setPolyline(MyPolyline(m_vertices, closed));
      m_livePolyline->setPen(pen);
      m_livePolyline = nullptr;
    } else {
      item = ctx->entityFactory()->createPolyline(m_vertices, closed, pen);
    }

    if (item && m_scene) {
      if (curLayer) {
        if (PolylineItem *pi = dynamic_cast<PolylineItem *>(item)) {
          pi->setLayerName(curLayer->name());
        }
        QGraphicsItem *folder = m_scene->getLayerRoot(curLayer->name());
        item->setParentItem(folder);
      }
      if (item->scene() == m_scene) {
        m_scene->removeItem(item);
      }

      if (ctx->undoStack()) {
        ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
      } else {
        m_scene->addItem(item);
      }
    }
  }

  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->logMessage("Polyline created.", CommandDock::Info);
    ctx->commandDock()->setPrompt(" Command : ");
  }

  m_vertices.clear();
  m_isDrawing = false;
  clearOverlay();
  emit toolFinished();
}

void PolylineTool::cancelDrawing() {
  m_isDrawing = false;
  m_vertices.clear();
  if (m_livePolyline) {
    if (m_scene)
      m_scene->removeItem(m_livePolyline);
    delete m_livePolyline;
    m_livePolyline = nullptr;
  }
  clearOverlay();
}

bool PolylineTool::undoLastVertex() {
  if (m_vertices.empty())
    return false;
  m_vertices.pop_back();
  if (m_vertices.empty()) {
    cancelDrawing();
  } else {
    if (m_livePolyline) {
      if (m_vertices.size() < 2) {
        if (m_scene)
          m_scene->removeItem(m_livePolyline);
        delete m_livePolyline;
        m_livePolyline = nullptr;
      } else {
        m_livePolyline->setPolyline(MyPolyline(m_vertices, false));
      }
    }
    mouseMove(m_currentMousePos);
  }
  return true;
}

void PolylineTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}
