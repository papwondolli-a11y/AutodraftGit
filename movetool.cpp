#include "movetool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "gripitem.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "lineitem.h"
#include "multilinedata.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QMap>
#include <QUndoStack>
#include <QUuid>

#include <QDebug>
#include <QKeyEvent>

// MoveTool::MoveTool(QObject* parent)
MoveTool::MoveTool(bool isCopyMode, QObject *parent)
    : AbstractTool(parent), m_currentState(State_Idle)
      //    , m_isCopyMode(false)
      ,
      m_isCopyMode(isCopyMode), m_isBoxSelecting(false) {}

MoveTool::~MoveTool() { clearOverlay(); }

void MoveTool::activate(MyGraphicsScene *scene) {
  // [핵심 가드] 이미 해당 씬에서 활성화된 도구라면 상태 초기화 방지
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);

  if (m_scene->selectedItems().isEmpty()) {
    // 1. 선택된 게 없음 -> "객체 선택 모드"
    m_currentState = State_Selecting;
    m_isBoxSelecting = false; // 박스 선택 초기화
  } else {
    // 2. 이미 선택됨 -> "기준점 대기 모드"
    m_currentState = State_WaitingForBase;

    // [요구사항 1 해결] Move 툴 진입 시 그립 숨기기
    hideGripsOfSelectedItems();
  }

  // [핵심] 커맨드 라인 프롬프트 초기화
  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage(m_isCopyMode ? "COPY" : "MOVE",
                                   CommandDock::Command);
    updatePrompt();
  }
}

void MoveTool::deactivate() {
  clearOverlay();
  m_currentState = State_Idle;
  m_isBoxSelecting = false;
  AbstractTool::deactivate();
}

// [신규 헬퍼] 선택된 아이템의 그립(자식)만 찾아서 숨김
void MoveTool::hideGripsOfSelectedItems() {
  if (!m_scene)
    return;

  QList<QGraphicsItem *> selectedItems = m_scene->selectedItems();
  for (int i = 0; i < selectedItems.size(); ++i) {
    QGraphicsItem *item = selectedItems.at(i);
    QList<QGraphicsItem *> children = item->childItems();
    for (int j = 0; j < children.size(); ++j) {
      QGraphicsItem *child = children.at(j);
      if (child->type() == GripItem::Type) {
        child->setVisible(false);
      }
    }
  }
}

void MoveTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;

  // [상태 1] 객체 선택 중
  if (m_currentState == State_Selecting) {
    handleSelectionPress(pos, button);
    return;
  }

  // [우클릭] 기준점/목적점 단계에서 우클릭 시 도구 종료
  if (button == Qt::RightButton) {
    m_scene->clearSelection();
    emit toolFinished();
    return;
  }

  if (button != Qt::LeftButton)
    return;

  // 스냅 계산
  QPointF snappedPos = pos;
  IAppContext *ctx = getContext();
  if (ctx && ctx->snapManager()) {
    QPointF base =
        (m_currentState == State_WaitingForSecond) ? m_basePoint : QPointF();
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
  }

  // [상태 2] 기준점 찍기
  if (m_currentState == State_WaitingForBase) {
    m_basePoint = snappedPos;
    m_currentState = State_WaitingForSecond;
    updatePrompt();
  }
  // [상태 3] 목적점 찍기
  else if (m_currentState == State_WaitingForSecond) {
    MyVector delta = MyVector(snappedPos) - MyVector(m_basePoint);

    if (m_isCopyMode) {
      // [다중 복사 로직]
      executeMoveOrCopy(delta);
      // 복사 후 종료하지 않고 다음 목적점 대기
      updatePrompt();
    } else {
      // [이동 로직] 기존처럼 종료
      executeMoveOrCopy(delta);
      m_scene->clearSelection();
      emit toolFinished();
    }
  }
}

void MoveTool::mouseMove(QPointF pos) {
  if (!m_scene)
    return;

  // [수정] 마우스의 실제 좌표를 최신화 (거리 입력 시 방향 결정에 사용)
  m_currentMousePos = pos;

  // [상태 1] 객체 선택 중 (박스 그리기)
  if (m_currentState == State_Selecting) {
    handleSelectionMove(pos);
    return;
  }

  // [상태 2, 3] 이동 미리보기
  QPointF snappedPos = pos;
  SnapType type = SnapType::None;
  IAppContext *ctx = getContext();
  if (ctx && ctx->snapManager()) {
    QPointF base =
        (m_currentState == State_WaitingForSecond) ? m_basePoint : QPointF();
    auto result = ctx->snapManager()->snapPoint(pos, m_scene, base);
    snappedPos = result.snappedPos;
    type = result.type;
  }
  m_scene->setSnapMarker(snappedPos, type);

  if (m_currentState == State_WaitingForSecond) {
    MyVector delta = MyVector(snappedPos) - MyVector(m_basePoint);

    QPainterPath combinedGhostPath;
    QList<QGraphicsItem *> selectedItems = m_scene->selectedItems();
    for (int i = 0; i < selectedItems.size(); ++i) {
      QGraphicsItem *item = selectedItems.at(i);
      if (CadItem *cadItem = dynamic_cast<CadItem *>(item)) {
        QPainterPath itemPath = cadItem->getMovedPath(delta);
        if (!itemPath.isEmpty()) {
          combinedGhostPath.addPath(itemPath);
        }
      }
    }

    // [중요] 기준점으로부터 현재 마우스 위치까지의 가상선(Rubber-band) 추가
    combinedGhostPath.moveTo(m_basePoint);
    combinedGhostPath.lineTo(snappedPos);

    updateGhostPreview(combinedGhostPath,
                       getCommonLayerName(selectedItems));
  }
}

void MoveTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
  // 클릭-클릭 방식이므로 Release에서는 아무 작업 안 함
  Q_UNUSED(pos);
  Q_UNUSED(button);
}

// ---------------------------------------------------------
// 선택 로직 (클릭-클릭 방식)
// ---------------------------------------------------------
void MoveTool::handleSelectionPress(QPointF pos, Qt::MouseButton button) {
  // 우클릭 -> 선택 확정/종료
  if (button == Qt::RightButton) {
    finishSelectionStep();
    return;
  }
  if (button != Qt::LeftButton)
    return;

  //  클릭-클릭 로직 구현

  if (!m_isBoxSelecting) {
    // [1단계] 첫 번째 클릭

    // 1. 클릭한 곳에 아이템이 있는지 확인 (단일 선택)
    QGraphicsItem *item = itemAtPos(pos);
    if (item && item->type() != GripItem::Type) {
      item->setSelected(!item->isSelected());

      // 선택된 아이템은 즉시 그립 숨기기
      if (item->isSelected()) {
        // CadItem은 선택되면 자동으로 그립을 띄우므로, 강제로 다시 숨겨야 함
        for (auto child : item->childItems()) {
          if (child->type() == GripItem::Type)
            child->setVisible(false);
        }
      }
      return;
    }

    // 2. 빈 곳이면 박스 선택 시작 (Start Box Selection)
    m_isBoxSelecting = true;
    m_firstCorner = pos;
//    qDebug() << "Selection Box Started (Click 1)";
  } else {
    // [2단계] 두 번째 클릭 (Finish Box Selection)
    processBoxSelection(pos);
    m_isBoxSelecting = false; // 박스 선택 종료
    clearOverlay();           // 박스 잔상 지우기
 //   qDebug() << "Selection Box Finished (Click 2)";
  }
}

void MoveTool::handleSelectionMove(QPointF pos) {
  // 박스 선택 중일 때만 오버레이 그림
  if (m_isBoxSelecting && m_scene) {
    // 선택 박스 영역을 씬 좌표계로 직접 전달
    MyGraphicsScene::SelectionMode mode = (pos.x() > m_firstCorner.x())
                                              ? MyGraphicsScene::Mode_Window
                                              : MyGraphicsScene::Mode_Crossing;

    m_scene->setSelectionRect(m_firstCorner, pos, mode);
  }
}

void MoveTool::processBoxSelection(QPointF p2) {
  bool isWindow = (p2.x() > m_firstCorner.x());

  QList<CadItem *> topItems = isWindow
      ? collectItemsInWindowRect(m_firstCorner, p2)
      : collectItemsInCrossingRect(m_firstCorner, p2);

  // [성능] 대량 setSelected 중 selectionChanged 폭주(O(n^2) + 속성창 재구성) 억제
  m_scene->beginBulkSelectionChange();
  for (int i = 0; i < topItems.size(); ++i) {
    topItems.at(i)->setSelected(true);
  }
  m_scene->endBulkSelectionChange();
  hideGripsOfSelectedItems();
}

void MoveTool::finishSelectionStep() {
  if (m_scene->selectedItems().isEmpty()) {
    emit toolFinished();
  } else {
    m_scene->savePreviousSelection();
    m_currentState = State_WaitingForBase;
    m_isBoxSelecting = false; // 혹시 켜져있었다면 끔
    clearOverlay();
    updatePrompt();
//    qDebug() << "Selection Done -> Goto BasePoint";
  }
}

bool MoveTool::processCommand(const QString &command) {
  QString cmd = command.toLower().trimmed();
  IAppContext *ctx = getContext();
  if (!ctx || !ctx->parser())
    return false;

  // 1. 객체 선택 상태에서 P -> 이전 선택 복원
  if (cmd == "p" && m_currentState == State_Selecting) {
    if (m_scene && m_scene->applyPreviousSelection()) {
      hideGripsOfSelectedItems();
    }
    return true;
  }

  // 2. 객체 선택 상태에서 엔터 -> 선택 확정
  if (cmd.isEmpty()) {
    if (m_currentState == State_Selecting) {
      finishSelectionStep();
      return true;
    }
      // [다중 복사 중 엔터] 복사 모드에서 목적점 대기 중 엔터 -> 도구 종료
    if (m_isCopyMode && m_currentState == State_WaitingForSecond) {
      m_scene->clearSelection();
      emit toolFinished();
      return true;
    }
    return false;
  }

  // 2. 좌표 파싱
  QPointF base =
      (m_currentState == State_WaitingForSecond) ? m_basePoint : QPointF(0, 0);
  QPointF resultPos;
  bool isOrtho = (ctx->snapManager() && ctx->snapManager()->isOrtho());

  if (ctx->parser()->parseCoordinate(cmd, base, m_currentMousePos, isOrtho,
                                     resultPos)) {
    mousePress(resultPos, Qt::LeftButton);
    return true;
  }

  return false;
}

void MoveTool::updatePrompt() {
  IAppContext *ctx = getContext();
  if (!ctx || !ctx->commandDock())
    return;

  switch (m_currentState) {
  case State_Selecting:
    ctx->commandDock()->showInteractivePrompt(tr(" 객체를 선택 하세요 : "), {});
    break;
  case State_WaitingForBase:
    ctx->commandDock()->showInteractivePrompt(tr(" 기준점을 지정 하세요 : "), {});
    break;
  case State_WaitingForSecond:
    ctx->commandDock()->showInteractivePrompt(tr(" 목표점을 지정 하세요 : "), {});
    break;
  default:
    break;
  }
}

void MoveTool::executeMoveOrCopy(const MyVector &delta) {
  if (!m_scene)
    return;
  QList<QGraphicsItem *> selected = m_scene->selectedItems();
  QList<CadItem *> topItems = getTopLevelCadItems(selected);
  IAppContext *ctx = getContext();

  // [성능] 아래 루프에서 항목마다 setSelected가 불려 selectionChanged가
  // 폭주(O(n^2) + 속성창 재구성)한다. 구간 전체를 감싸 끝에서 한 번만 갱신.
  m_scene->beginBulkSelectionChange();

  // [수정] 이동 모드일 때만 자동 선택 해제 (복사는 원본 유지를 위해 해제 안 함)
  if (!m_isCopyMode) {
    m_scene->clearSelection();
  }

  if (ctx && ctx->undoStack()) {
    ctx->undoStack()->beginMacro(m_isCopyMode ? "Copy Items" : "Move Items");
  }

  // 복사 시 다중선 그룹 ID 재매핑 — 원본과 동일 ID 공유 시 코너 툴 오작동 방지
  QMap<QString, QString> gidRemap;
  if (m_isCopyMode) {
    for (CadItem *ci : topItems) {
      LineItem *li = dynamic_cast<LineItem *>(ci);
      if (!li) continue;
      const QString gid = li->data(ML_DATA_GROUP_ID).toString();
      if (!gid.isEmpty() && !gidRemap.contains(gid))
        gidRemap.insert(gid, QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
  }

  for (int i = 0; i < topItems.size(); ++i) {
    CadItem *cadItem = topItems.at(i);
    if (!cadItem)
      continue;

    if (m_isCopyMode) {
      // [복사 모드]
      CadItem *newItem = cadItem->clone();
      if (newItem) {
        newItem->move(delta);
        // 다중선 그룹 ID 재매핑 적용 (복사본은 새 UUID 사용)
        if (LineItem *li = dynamic_cast<LineItem *>(newItem)) {
          const QString gid = li->data(ML_DATA_GROUP_ID).toString();
          if (!gid.isEmpty() && gidRemap.contains(gid))
            li->setData(ML_DATA_GROUP_ID, gidRemap.value(gid));
        }
        if (ctx && ctx->undoStack()) {
          ctx->undoStack()->push(new AddEntityCommand(m_scene, newItem));
        } else {
          m_scene->addItem(newItem);
        }
        // 다중 복사 시 원본 선택 유지를 위해 새 객체는 선택하지 않음
        newItem->setSelected(false);
      }
    } else {
      // [이동 모드]
      if (ctx && ctx->undoStack()) {
        ctx->undoStack()->push(new MoveEntityCommand(cadItem, delta));
      } else {
        cadItem->move(delta);
      }
      cadItem->setSelected(true);
    }
  }

  if (ctx && ctx->undoStack()) {
    ctx->undoStack()->endMacro();
  }

  m_scene->endBulkSelectionChange();
}

void MoveTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}

void MoveTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    m_scene->clearSelection();
    emit toolFinished();
  } else {
    AbstractTool::keyPress(event);
  }
}
