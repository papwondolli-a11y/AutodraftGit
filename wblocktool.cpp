#include "wblocktool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "filemanager.h"
#include "gripitem.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QFileDialog>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMessageBox>
#include <QUndoStack>

WBlockTool::WBlockTool(QObject *parent)
    : AbstractTool(parent), m_currentState(State_Idle),
      m_isBoxSelecting(false) {}

WBlockTool::~WBlockTool() {}

void WBlockTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);

  if (m_scene->selectedItems().isEmpty()) {
    m_currentState = State_Selecting;
  } else {
    m_currentState = State_WaitingForBase;
    hideGripsOfSelectedItems();
  }

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("WBLOCK", CommandDock::Command);
    updatePrompt();
  }
}

void WBlockTool::deactivate() {
  clearOverlay();
  m_currentState = State_Idle;
  m_isBoxSelecting = false;
  AbstractTool::deactivate();
}

void WBlockTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;

  if (m_currentState == State_Selecting) {
    handleSelectionPress(pos, button);
    return;
  }

  if (button != Qt::LeftButton)
    return;

  QPointF snappedPos = pos;
  IAppContext *ctx = getContext();
  if (ctx && ctx->snapManager()) {
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene).snappedPos;
  }

  if (m_currentState == State_WaitingForBase) {
    m_basePoint = snappedPos;
    executeWBlock();
  }
}

void WBlockTool::mouseMove(QPointF pos) {
  m_currentMousePos = pos;
  if (!m_scene)
    return;

  if (m_currentState == State_Selecting) {
    handleSelectionMove(pos);
    return;
  }

  QPointF snappedPos = pos;
  SnapType type = SnapType::None;
  IAppContext *ctx = getContext();
  if (ctx && ctx->snapManager()) {
    auto result = ctx->snapManager()->snapPoint(pos, m_scene);
    snappedPos = result.snappedPos;
    type = result.type;
  }
  m_scene->setSnapMarker(snappedPos, type);
}

void WBlockTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
  Q_UNUSED(pos);
  Q_UNUSED(button);
}

bool WBlockTool::processCommand(const QString &command) {
  QString cmd = command.toLower().trimmed();
  IAppContext *ctx = getContext();
  if (!ctx || !ctx->parser())
    return false;

  if (cmd.isEmpty()) {
    if (m_currentState == State_Selecting) {
      finishSelectionStep();
      return true;
    }
    if (m_currentState == State_WaitingForBase) {
      return true; // 빈 엔터 소비 — AbstractTool이 toolFinished를 발동하지 않도록
    }
    return false;
  }

  QPointF resultPos;
  bool isOrtho = (ctx->snapManager() && ctx->snapManager()->isOrtho());

  if (ctx->parser()->parseCoordinate(cmd, QPointF(0, 0), m_currentMousePos,
                                     isOrtho, resultPos)) {
    mousePress(resultPos, Qt::LeftButton);
    return true;
  }

  return false;
}

void WBlockTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    m_scene->clearSelection();
    emit toolFinished();
  } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if (m_currentState == State_Selecting) {
      finishSelectionStep();
    }
  } else {
    AbstractTool::keyPress(event);
  }
}

void WBlockTool::updatePrompt() {
  IAppContext *ctx = getContext();
  if (!ctx || !ctx->commandDock())
    return;

  switch (m_currentState) {
  case State_Selecting:
    ctx->commandDock()->showInteractivePrompt(tr(" 블록으로 만들 객체를 선택 하세요 : "),
                                              {});
    break;
  case State_WaitingForBase:
    ctx->commandDock()->showInteractivePrompt(tr(" 기준점을 지정 하세요 : "), {});
    break;
  default:
    break;
  }
}

void WBlockTool::handleSelectionPress(QPointF pos, Qt::MouseButton button) {
  if (button == Qt::RightButton) {
    finishSelectionStep();
    return;
  }
  if (button != Qt::LeftButton)
    return;

  if (!m_isBoxSelecting) {
    // 1. 클릭한 곳에 아이템이 있는지 확인
    QGraphicsItem *item = itemAtPos(pos);
    if (item && item->type() != GripItem::Type) {
      item->setSelected(!item->isSelected());
      if (item->isSelected()) {
        for (auto child : item->childItems()) {
          if (child->type() == GripItem::Type)
            child->setVisible(false);
        }
      }
      return;
    }

    m_isBoxSelecting = true;
    m_firstCorner = pos;
  } else {
    processBoxSelection(pos);
    m_isBoxSelecting = false;
    clearOverlay();
  }
}

void WBlockTool::handleSelectionMove(QPointF pos) {
  if (m_isBoxSelecting && m_scene) {
    MyGraphicsScene::SelectionMode mode = (pos.x() > m_firstCorner.x())
                                              ? MyGraphicsScene::Mode_Window
                                              : MyGraphicsScene::Mode_Crossing;
    m_scene->setSelectionRect(m_firstCorner, pos, mode);
  }
}

void WBlockTool::processBoxSelection(QPointF p2) {
  bool isWindow = (p2.x() > m_firstCorner.x());
  QList<CadItem *> topItems = isWindow
      ? collectItemsInWindowRect(m_firstCorner, p2)
      : collectItemsInCrossingRect(m_firstCorner, p2);

  for (int i = 0; i < topItems.size(); ++i) {
    topItems.at(i)->setSelected(true);
  }
  hideGripsOfSelectedItems();
}

void WBlockTool::finishSelectionStep() {
  if (m_scene->selectedItems().isEmpty()) {
    emit toolFinished();
  } else {
    m_currentState = State_WaitingForBase;
    m_isBoxSelecting = false;
    clearOverlay();
    updatePrompt();
  }
}

void WBlockTool::hideGripsOfSelectedItems() {
  if (!m_scene)
    return;
  for (auto item : m_scene->selectedItems()) {
    for (auto child : item->childItems()) {
      if (child->type() == GripItem::Type) {
        child->setVisible(false);
      }
    }
  }
}

void WBlockTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}

void WBlockTool::executeWBlock() {
  IAppContext *ctx = getContext();
  if (!ctx || !m_scene || !ctx->fileManager()) {
    emit toolFinished();
    return;
  }

  QList<QGraphicsItem *> selectedItems = m_scene->selectedItems();
  if (selectedItems.isEmpty()) {
    emit toolFinished();
    return;
  }

  QWidget *parentWidget = nullptr;
  if (!m_scene->views().isEmpty()) {
    parentWidget = m_scene->views().first();
  }

  // 다이얼로그 표시 중에는 Enter가 AbstractTool::keyPress를 통해
  // toolFinished를 조기 발동하지 못하도록 차단
  m_blockEnterKey = true;

  QString filePath = QFileDialog::getSaveFileName(parentWidget, "Save Block As",
                                                  "", "CAD Database (*.cdb)");
  if (filePath.isEmpty()) {
    m_blockEnterKey = false;
    return;
  }

  if (ctx->fileManager()->exportSelectedItems(filePath, selectedItems,
                                              m_basePoint)) {
    qDebug() << "WBlockTool: Block saved successfully.";
    auto res = QMessageBox::question(parentWidget, tr("블록만들기"),
                                     tr("블록이 저장 되었습니다.\n선택한 객체를 삭제  할까? "),
                                     QMessageBox::Yes | QMessageBox::No,
                                     QMessageBox::No);

    if (res == QMessageBox::Yes) {
      if (ctx->undoStack()) {
        ctx->undoStack()->beginMacro("WBLOCK: Delete Source Entities");
        for (auto item : std::as_const(selectedItems)) {
          if (item && item->scene() == m_scene) {
            ctx->undoStack()->push(new RemoveEntityCommand(m_scene, item));
          }
        }
        ctx->undoStack()->endMacro();
      }
    }
  } else {
    QMessageBox::critical(parentWidget, tr("블록만들기"), tr("만들기 실패 했습니다."));
  }

  m_blockEnterKey = false;

  if (m_scene)
    m_scene->clearSelection();
  emit toolFinished();
}
