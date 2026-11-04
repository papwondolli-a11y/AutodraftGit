#define CADCOMMANDS_CPP
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "drawsubwindow.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "mygraphicsview.h"
#include "mymath.h"
#include "zoomwindowtool.h"
#include "mirrortool.h"
#include "quadmirrortool.h"
#include "rotatetool.h"
#include "scaletool.h"
#include "stretchtool.h"
#include "inserttool.h"
#include "splinetool.h"

#include <QFileDialog>
#include <QDebug>
#include <QGraphicsItem>
#include <QUndoStack>


AddEntityCommand::AddEntityCommand(MyGraphicsScene *scene, QGraphicsItem *item,
                                   QUndoCommand *parent)
    : QUndoCommand(parent), m_scene(scene), m_item(item), m_ownsItem(false) {
  setText("Add Entity");
  // [신규] 생성 시점에 레이어 이름을 미리 기억해둠
  if (CadItem *ci = dynamic_cast<CadItem *>(m_item)) {
    m_layerName = ci->layerName();
  }
}

AddEntityCommand::~AddEntityCommand() {
  if (m_ownsItem && m_item) {
    delete m_item;
  }
}

void AddEntityCommand::undo() {
 // qDebug() << "AddEntityCommand::undo() - Removing item from scene";
  if (m_scene && m_item) {
    m_scene->removeItem(m_item);
    m_ownsItem = true;
  }
}

void AddEntityCommand::redo() {
 // qDebug() << "AddEntityCommand::redo() - Adding item to scene";
  if (m_scene && m_item) {
    if (!m_layerName.isEmpty()) {
      QGraphicsItem *root = m_scene->getLayerRoot(m_layerName);
      m_item->setParentItem(root); // setParentItem이 씬 추가도 동시에 수행함
    } else {
      m_scene->addItem(m_item);
    }
    m_ownsItem = false;
  }
}

// --- RemoveEntityCommand ---

RemoveEntityCommand::RemoveEntityCommand(MyGraphicsScene *scene,
                                         QGraphicsItem *item,
                                         QUndoCommand *parent)
    : QUndoCommand(parent), m_scene(scene), m_item(item), m_ownsItem(false) {
  setText("Remove Entity");
  if (CadItem *ci = dynamic_cast<CadItem *>(m_item)) {
    m_layerName = ci->layerName();
  }
}

RemoveEntityCommand::~RemoveEntityCommand() {
  if (m_ownsItem && m_item) {
    delete m_item;
  }
}

void RemoveEntityCommand::undo() {
 // qDebug() << "RemoveEntityCommand::undo() - Restoring item to scene";
  if (m_scene && m_item) {
    if (!m_layerName.isEmpty()) {
      QGraphicsItem *root = m_scene->getLayerRoot(m_layerName);
      m_item->setParentItem(root);
    } else {
      m_scene->addItem(m_item);
    }
    m_ownsItem = false;
  }
}

void RemoveEntityCommand::redo() {
 // qDebug() << "RemoveEntityCommand::redo() - Removing item from scene";
  if (m_scene && m_item) {
    m_scene->removeItem(m_item);
    m_ownsItem = true;
  }
}

// --- MoveEntityCommand ---

MoveEntityCommand::MoveEntityCommand(CadItem *item, const MyVector &delta,
                                     QUndoCommand *parent)
    : QUndoCommand(parent), m_item(item), m_delta(delta) {
  setText("Move Entity");
}

void MoveEntityCommand::undo() {
  if (m_item) {
    m_item->move(m_delta * -1.0);
  }
}

void MoveEntityCommand::redo() {
  if (m_item) {
    m_item->move(m_delta);
  }
}

// --- ExtendEntityCommand ---

ExtendEntityCommand::ExtendEntityCommand(CadItem *item, const QPointF &oldPos,
                                         const QPointF &newPos, bool isFirstEnd,
                                         QUndoCommand *parent)
    : QUndoCommand(parent), m_item(item), m_oldPos(oldPos), m_newPos(newPos),
      m_isFirstEnd(isFirstEnd) {
  setText("Extend Entity");
}

void ExtendEntityCommand::undo() {
  if (m_item) {
    m_item->updateEndpoint(m_oldPos, m_isFirstEnd);
  }
}

void ExtendEntityCommand::redo() {
  if (m_item) {
    m_item->updateEndpoint(m_newPos, m_isFirstEnd);
  }
}

// --- StretchEntityCommand ---
StretchEntityCommand::StretchEntityCommand(CadItem *item,
                                           const QList<int> &indices,
                                           const MyVector &delta,
                                           QUndoCommand *parent)
    : QUndoCommand(parent), m_item(item), m_indices(indices), m_delta(delta) {
  setText("Stretch Entity");
}

void StretchEntityCommand::undo() {
  if (m_item) {
    m_item->stretch(m_indices, m_delta * -1.0);
  }
}

void StretchEntityCommand::redo() {
  if (m_item) {
    m_item->stretch(m_indices, m_delta);
  }
}

// --- ChangePropertyCommand ---

ChangePropertyCommand::ChangePropertyCommand(CadItem *item, const QString &key,
                                             const QVariant &oldValue,
                                             const QVariant &newValue,
                                             QUndoCommand *parent)
    : QUndoCommand(parent), m_item(item), m_key(key), m_oldValue(oldValue),
      m_newValue(newValue) {
  setText(QString("Property Change: %1").arg(key));
}

void ChangePropertyCommand::undo() {
  if (m_item) {
    m_item->setProperty(m_key, m_oldValue);
  }
}

void ChangePropertyCommand::redo() {
  if (m_item) {
    m_item->setProperty(m_key, m_newValue);
  }
}

// --- ChangeLayerCommand ---

ChangeLayerCommand::ChangeLayerCommand(MyGraphicsScene *scene, CadItem *item,
                                       const QString &oldLayer,
                                       const QString &newLayer,
                                       const QPen &oldPen, const QPen &newPen,
                                       QUndoCommand *parent)
    : QUndoCommand(parent), m_scene(scene), m_item(item), m_oldLayer(oldLayer),
      m_newLayer(newLayer), m_oldPen(oldPen), m_newPen(newPen) {
  setText("Change Layer");
}

void ChangeLayerCommand::undo() {
  if (m_item && m_scene) {
    m_item->setLayerName(m_oldLayer);
    m_item->setPen(m_oldPen);
    m_scene->reparentItemToLayer(m_item, m_oldLayer);
  }
}

void ChangeLayerCommand::redo() {
  if (m_item && m_scene) {
    m_item->setLayerName(m_newLayer);
    m_item->setPen(m_newPen);
    m_scene->reparentItemToLayer(m_item, m_newLayer);
  }
}

// --- GlobalUndoCommand ---
GlobalUndoCommand::GlobalUndoCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void GlobalUndoCommand::execute() {
 // qDebug() << "GlobalUndoCommand::execute() called";
  if (m_ctx && m_ctx->undoStack()) {
    m_ctx->undoStack()->undo();
  } else {
//    qDebug() << "GlobalUndoCommand::execute() failed: ctx or undoStack is null";
  }
}

// --- GlobalRedoCommand ---
GlobalRedoCommand::GlobalRedoCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void GlobalRedoCommand::execute() {
  //qDebug() << "GlobalRedoCommand::execute() called";
  if (m_ctx && m_ctx->undoStack()) {
    m_ctx->undoStack()->redo();
  } else {
 //   qDebug() << "GlobalRedoCommand::execute() failed: ctx or undoStack is null";
  }
}

// --- Zoom Commands ---
ZoomAllCommand::ZoomAllCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void ZoomAllCommand::execute() {
  if (m_ctx && m_ctx->activeDrawWindow() && m_ctx->activeDrawWindow()->view()) {
    if (m_ctx->commandDock())
      m_ctx->commandDock()->logMessage(tr("모든 객체로 화면을 맞춥니다."));
    m_ctx->activeDrawWindow()->view()->zoomAll();
  }
}

ZoomExtendCommand::ZoomExtendCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void ZoomExtendCommand::execute() {
  if (m_ctx && m_ctx->activeDrawWindow() && m_ctx->activeDrawWindow()->view()) {
    if (m_ctx->commandDock())
      m_ctx->commandDock()->logMessage(tr("도면 범위로 화면을 맞춥니다."));
    m_ctx->activeDrawWindow()->view()->zoomExtend();
  }
}

MirrorEntityCommand::MirrorEntityCommand(CadItem *item, const MyVector &p1,
                                         const MyVector &p2,
                                         QUndoCommand *parent)
    : QUndoCommand(parent), m_item(item), m_p1(p1), m_p2(p2) {
  setText(QObject::tr("Mirror Entity"));
}

void MirrorEntityCommand::undo() {
  if (m_item)
    m_item->mirror(m_p1, m_p2); // Reflection is its own inverse
}

void MirrorEntityCommand::redo() {
  if (m_item)
    m_item->mirror(m_p1, m_p2);
}

MirrorCommand::MirrorCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void MirrorCommand::execute() {
  if (m_ctx && m_ctx->activeDrawWindow()) {
    m_ctx->activeDrawWindow()->setTool(new MirrorTool(m_ctx));
  }
}

// --- QuadMirrorCommand ---

QuadMirrorCommand::QuadMirrorCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void QuadMirrorCommand::execute() {
  if (m_ctx && m_ctx->activeDrawWindow()) {
    m_ctx->activeDrawWindow()->setTool(new QuadMirrorTool(m_ctx));
  }
}

// --- RotateEntityCommand ---

RotateEntityCommand::RotateEntityCommand(CadItem *item,
                                         const MyVector &basePoint,
                                         double angle, QUndoCommand *parent)
    : QUndoCommand(parent), m_item(item), m_basePoint(basePoint),
      m_angle(angle) {
  setText(QObject::tr("Rotate Entity"));
}

void RotateEntityCommand::undo() {
  if (m_item)
    m_item->rotate(m_basePoint, -m_angle);
}

void RotateEntityCommand::redo() {
  if (m_item)
    m_item->rotate(m_basePoint, m_angle);
}

// --- ScaleEntityCommand ---
ScaleEntityCommand::ScaleEntityCommand(CadItem *item, const MyVector &basePoint,
                                       double scaleFactor, QUndoCommand *parent)
    : QUndoCommand(parent), m_item(item), m_basePoint(basePoint),
      m_scaleFactor(scaleFactor) {
  setText(QObject::tr("Scale Entity"));
}

void ScaleEntityCommand::undo() {
  if (m_item) {
    if (!MyMath::isZero(m_scaleFactor)) {
      m_item->scale(m_basePoint, 1.0 / m_scaleFactor);
    }
  }
}

void ScaleEntityCommand::redo() {
  if (m_item)
    m_item->scale(m_basePoint, m_scaleFactor);
}

RotateCommand::RotateCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void RotateCommand::execute() {
  if (m_ctx && m_ctx->activeDrawWindow()) {
    m_ctx->activeDrawWindow()->setTool(new RotateTool(m_ctx));
  }
}

ScaleCommand::ScaleCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void ScaleCommand::execute() {
  if (m_ctx && m_ctx->activeDrawWindow()) {
    m_ctx->activeDrawWindow()->setTool(new ScaleTool(m_ctx));
  }
}

ZoomWindowCommand::ZoomWindowCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void ZoomWindowCommand::execute() {
  if (m_ctx && m_ctx->activeDrawWindow()) {
    m_ctx->activeDrawWindow()->setTool(new ZoomWindowTool(m_ctx));
  }
}

StretchCommand::StretchCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void StretchCommand::execute() {
  if (m_ctx && m_ctx->activeDrawWindow()) {
    m_ctx->activeDrawWindow()->setTool(new StretchTool(m_ctx));
  }
}

// --- InsertBlockCommand ---
InsertBlockCommand::InsertBlockCommand(MyGraphicsScene *scene,
                                       const QList<CadItem *> &items,
                                       QUndoCommand *parent)
    : QUndoCommand(parent), m_scene(scene), m_items(items), m_ownsItems(true) {
  setText(QObject::tr("블록 삽입"));
}

InsertBlockCommand::~InsertBlockCommand() {
  if (m_ownsItems) {
    qDeleteAll(m_items);
  }
}

void InsertBlockCommand::undo() {
  if (!m_scene)
    return;
  for (CadItem *item : std::as_const(m_items)) {
    m_scene->removeItem(item);
  }
  m_ownsItems = true;
}

void InsertBlockCommand::redo() {
  if (!m_scene)
    return;
  for (CadItem *item : std::as_const(m_items)) {
    m_scene->reparentItemToLayer(item, item->layerName());
  }
  m_ownsItems = false;
}

InsertCallCommand::InsertCallCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void InsertCallCommand::execute() {
  if (!m_ctx)
    return;

  // QFileDialog 대신 블록 팔레트 도크를 표시합니다.
  m_ctx->showBlockPalette();

    if (m_ctx->commandDock()) {
        m_ctx->commandDock()->logMessage(
            QObject::tr("블록 팔레트에서 삽입할 블록을 선택하세요."));
    }
}

SplineCommand::SplineCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void SplineCommand::execute() {
    if (m_ctx && m_ctx->activeDrawWindow()) {
        m_ctx->activeDrawWindow()->setTool(new SplineTool(m_ctx));
    }
}

BlockLibraryCommand::BlockLibraryCommand(IAppContext *ctx,
                                         const QString &subFolder,
                                         QObject *parent)
    : CommandBase(parent), m_ctx(ctx), m_subFolder(subFolder) {}

void BlockLibraryCommand::execute() {
    if (m_ctx)
        m_ctx->activateBlockLibrary(m_subFolder);
}

// ─────────────────────────────────────────────────────────────────────────────
// ChangeDimStyleCommand
// ─────────────────────────────────────────────────────────────────────────────
ChangeDimStyleCommand::ChangeDimStyleCommand(MyGraphicsScene      *scene,
                                             const DimensionStyle &oldStyle,
                                             const DimensionStyle &newStyle,
                                             QUndoCommand         *parent)
    : QUndoCommand(parent)
    , m_scene(scene)
    , m_oldStyle(oldStyle)
    , m_newStyle(newStyle)
{
    setText(QObject::tr("치수 스타일 변경"));
}

void ChangeDimStyleCommand::undo()
{
    if (m_scene)
        m_scene->setDimensionStyle(m_oldStyle);
}

void ChangeDimStyleCommand::redo()
{
    if (m_scene)
        m_scene->setDimensionStyle(m_newStyle);
}
