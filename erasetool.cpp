#include "erasetool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "gripitem.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QDebug>
#include <QKeyEvent>
#include <QUndoStack>


EraseTool::EraseTool(QObject *parent)
    : AbstractTool(parent), m_isBoxSelecting(false) {}

EraseTool::~EraseTool() { clearOverlay(); }

void EraseTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);
  m_isBoxSelecting = false;

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("ERASE", CommandDock::Command);
    ctx->commandDock()->showInteractivePrompt(tr(" 지울 객체를 선택 하세요 : "), {});
  }

  // 기존 선택된 아이템의 그립 숨기기
  hideGripsOfSelectedItems();
}

void EraseTool::deactivate() {
  clearOverlay();
  m_isBoxSelecting = false;
  AbstractTool::deactivate();
}

void EraseTool::mousePress(QPointF pos, Qt::MouseButton button) {
  handleSelectionPress(pos, button);
}

void EraseTool::mouseMove(QPointF pos) { handleSelectionMove(pos); }

bool EraseTool::processCommand(const QString &command) {
  QString cmd = command.toLower().trimmed();
  if (cmd == "p") {
    if (m_scene && m_scene->applyPreviousSelection()) {
      hideGripsOfSelectedItems();
    }
    return true;
  }
  if (cmd.isEmpty()) {
    executeErase();
    return true;
  }
  return false;
}

void EraseTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
  Q_UNUSED(pos);
  Q_UNUSED(button);
}

void EraseTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    if (m_scene)
      m_scene->clearSelection();
    emit toolFinished();
  } else {
    AbstractTool::keyPress(event);
  }
}

void EraseTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}

void EraseTool::handleSelectionPress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;

  if (button == Qt::RightButton) {
    executeErase();
    return;
  }

  if (button != Qt::LeftButton)
    return;

  if (!m_isBoxSelecting) {
    // 1. 개별 아이템 클릭 체크 (토글)
    QGraphicsItem *item = itemAtPos(pos);
    if (item) {
      // 그립 아이템은 무시
      if (item->type() == GripItem::Type) {
        if (item->parentItem())
          item = item->parentItem();
      }

      if (item && item->type() != GripItem::Type) {
        item->setSelected(!item->isSelected());
        hideGripsOfSelectedItems();
        return;
      }
    }

    // 2. 빈 공간 클릭 시 박스 선택 시작
    m_isBoxSelecting = true;
    m_firstCorner = pos;
  } else {
    // 3. 박스 선택 완료
    processBoxSelection(pos);
    m_isBoxSelecting = false;
    clearOverlay();
  }
}

void EraseTool::handleSelectionMove(QPointF pos) {
  if (m_isBoxSelecting && m_scene) {
    MyGraphicsScene::SelectionMode mode = (pos.x() > m_firstCorner.x())
                                              ? MyGraphicsScene::Mode_Window
                                              : MyGraphicsScene::Mode_Crossing;
    m_scene->setSelectionRect(m_firstCorner, pos, mode);
  }
}

void EraseTool::processBoxSelection(QPointF p2) {
  bool isWindow = (p2.x() > m_firstCorner.x());
  QList<CadItem *> topItems = isWindow
      ? collectItemsInWindowRect(m_firstCorner, p2)
      : collectItemsInCrossingRect(m_firstCorner, p2);

  for (int i = 0; i < topItems.size(); ++i) {
    topItems.at(i)->setSelected(true);
  }
  hideGripsOfSelectedItems();
}

void EraseTool::executeErase() {
  if (!m_scene) {
    emit toolFinished();
    return;
  }

  m_scene->savePreviousSelection();
  QList<QGraphicsItem *> selected = m_scene->selectedItems();
  QList<CadItem *> topSelected = getTopLevelCadItems(selected);
  if (topSelected.isEmpty()) {
    emit toolFinished();
    return;
  }

  IAppContext *ctx = getContext();
  if (ctx && ctx->undoStack()) {
    ctx->undoStack()->beginMacro(tr("Erase %1 items").arg(topSelected.size()));
    for (auto *item : std::as_const(topSelected)) {
      ctx->undoStack()->push(new RemoveEntityCommand(m_scene, item));
    }
    ctx->undoStack()->endMacro();
  } else {
    for (auto *item : std::as_const(topSelected)) {
      m_scene->removeItem(item);
      delete item;
    }
  }

  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->logMessage(
        tr("%1 items erased.").arg(topSelected.size()), CommandDock::Info);
    ctx->commandDock()->setPrompt(" Command : ");
  }

  m_scene->clearSelection();
  emit toolFinished();
}
