#include "quadmirrortool.h"
#include "blockitem.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "multilinetagutil.h"
#include "myvector.h"
#include "quadmirrordialog.h"
#include "snapmanager.h"

#include <QGraphicsView>
#include <QKeyEvent>
#include <QUndoStack>
#include <QWidget>
#include <algorithm>

QuadMirrorTool::QuadMirrorTool(IAppContext *ctx, QObject *parent)
    : AbstractTool(parent), m_currentState(State_SelectingObjects),
      m_isMoveMode(true) {
  setContext(ctx);
}

QuadMirrorTool::~QuadMirrorTool() {}

void QuadMirrorTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;
  AbstractTool::activate(scene);

  // 도구 활성화 시점에 이미 선택되어 있던 객체들을 그대로 대상으로 사용
  m_selectedItems.clear();
  if (m_scene) {
    QList<CadItem *> topItems = getTopLevelCadItems(m_scene->selectedItems());
    for (int i = 0; i < topItems.size(); ++i) {
      m_selectedItems.push_back(topItems[i]);
    }
  }

  if (auto ctx = getContext()) {
    if (auto dock = ctx->commandDock()) {
      dock->setToolActive(true);
      dock->logMessage("QUADMIRROR", CommandDock::Command);
    }
  }

  if (!m_selectedItems.empty()) {
    // 이미 선택된 객체가 있으면 바로 다이얼로그를 띄운다.
    // 취소되었거나 처리할 대상이 없으면 도구를 즉시 종료한다.
    if (!showDialogAndPrepareClones()) {
      emit toolFinished();
    }
    return;
  }

  m_currentState = State_SelectingObjects;
  updatePrompt();
}

void QuadMirrorTool::deactivate() {
  clearOverlay();
  // 배치되지 못한 채 남은 임시 복제본 정리 (Esc 등으로 중간에 취소된 경우)
  discardPendingClones();
  // [수정] 다이얼로그 취소/Esc 등 어떤 경로로 도구가 종료되든 이 지점을
  // 항상 거치므로, 남아있는 선택 표시를 여기서 한 번에 정리한다.
  // (기존에는 취소 시 선택이 그대로 남아 Esc를 두 번 눌러야 했음)
  if (m_scene)
    m_scene->clearSelection();
  m_selectedItems.clear();
  AbstractTool::deactivate();
}

void QuadMirrorTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (m_currentState == State_SelectingObjects) {
    if (button != Qt::LeftButton)
      return;

    if (CadItem *item = dynamic_cast<CadItem *>(itemAtPos(pos))) {
      // 클릭한 객체를 선택 목록에 토글 추가/제거
      auto it = std::find(m_selectedItems.begin(), m_selectedItems.end(), item);
      if (it == m_selectedItems.end()) {
        m_selectedItems.push_back(item);
        item->setSelected(true);
        hideGripsOfSelectedItems();
      } else {
        m_selectedItems.erase(it);
        item->setSelected(false);
      }
    } else {
      // 빈 공간 클릭 -> 박스 선택 시작
      m_firstCorner = pos;
      m_currentState = State_SelectingObjects_SecondCorner;
      clearOverlay();
    }
  } else if (m_currentState == State_SelectingObjects_SecondCorner) {
    if (button == Qt::LeftButton) {
      processBoxSelection(pos);
      m_currentState = State_SelectingObjects;
    }
  } else if (m_currentState == State_PickingBasePoint) {
    if (button != Qt::LeftButton)
      return;

    QPointF snappedPos = pos;
    if (auto ctx = getContext()) {
      if (auto sm = ctx->snapManager()) {
        snappedPos = sm->snapPoint(pos, m_scene, QPointF()).snappedPos;
      }
    }
    m_basePoint = snappedPos;
    m_currentState = State_PickingDestinationPoint;
    updatePrompt();
  } else if (m_currentState == State_PickingDestinationPoint) {
    if (button != Qt::LeftButton)
      return;

    QPointF snappedPos = pos;
    if (auto ctx = getContext()) {
      if (auto sm = ctx->snapManager()) {
        snappedPos = sm->snapPoint(pos, m_scene, m_basePoint).snappedPos;
      }
    }

    // [복사 모드 전용] 원본과 정확히 겹치는 배치(이동량 0)를 막는다.
    // 이동 모드는 원본이 삭제되므로 겹칠 대상이 없어 이 제한이 필요 없다.
    if (!m_isMoveMode &&
        MyVector(m_basePoint).distanceTo(MyVector(snappedPos)) < 1e-6) {
      return; // 같은 상태를 유지해 다른 위치를 다시 클릭하도록 유도
    }

    MyVector delta = MyVector(snappedPos) - MyVector(m_basePoint);
    finalizePlacement(delta);
    emit toolFinished();
  }
}

void QuadMirrorTool::mouseMove(QPointF pos) {
  if (m_currentState == State_SelectingObjects_SecondCorner) {
    handleSelectionMove(pos);
    return;
  }

  if (m_currentState != State_PickingBasePoint &&
      m_currentState != State_PickingDestinationPoint)
    return;

  QPointF snappedPos = pos;
  SnapType snapType = SnapType::None;
  if (auto ctx = getContext()) {
    if (auto sm = ctx->snapManager()) {
      QPointF base = (m_currentState == State_PickingDestinationPoint)
                         ? m_basePoint
                         : QPointF();
      SnapManager::SnapResult res = sm->snapPoint(pos, m_scene, base);
      snappedPos = res.snappedPos;
      snapType = res.type;
    }
  }
  if (m_scene)
    m_scene->setSnapMarker(snappedPos, snapType);

  if (m_currentState == State_PickingDestinationPoint) {
    MyVector delta = MyVector(snappedPos) - MyVector(m_basePoint);

    // 대기 중인 복제본들을 delta만큼 이동한 모습 + 기준점-목적점 안내선(고무줄)
    QPainterPath combinedGhostPath;
    for (size_t i = 0; i < m_pendingClones.size(); ++i) {
      CadItem *clone = m_pendingClones[i];
      if (clone)
        combinedGhostPath.addPath(clone->getMovedPath(delta));
    }
    combinedGhostPath.moveTo(m_basePoint);
    combinedGhostPath.lineTo(snappedPos);

    updateGhostPreview(combinedGhostPath, "");
  }
}

void QuadMirrorTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
      event->key() == Qt::Key_Space) {
    if (m_currentState == State_SelectingObjects && !m_selectedItems.empty()) {
      if (!showDialogAndPrepareClones()) {
        emit toolFinished();
      }
    }
  } else if (event->key() == Qt::Key_Escape) {
    emit toolFinished();
  } else {
    AbstractTool::keyPress(event);
  }
}

void QuadMirrorTool::updatePrompt() {
  auto ctx = getContext();
  if (!ctx || !ctx->commandDock())
    return;
  auto dock = ctx->commandDock();

  switch (m_currentState) {
  case State_SelectingObjects:
  case State_SelectingObjects_SecondCorner:
    dock->showInteractivePrompt(
        tr("4거울상 - 대칭 복사할 객체를 선택 하세요 : "), {});
    break;
  case State_PickingBasePoint:
    dock->showInteractivePrompt(tr("기준점을 지정하세요 : "), {});
    break;
  case State_PickingDestinationPoint:
    dock->showInteractivePrompt(tr("이동할 위치를 지정하세요 : "), {});
    break;
  }
}

void QuadMirrorTool::clearOverlay() {
  if (m_scene)
    m_scene->clearOverlay();
}

void QuadMirrorTool::handleSelectionMove(QPointF pos) {
  if (m_currentState != State_SelectingObjects_SecondCorner || !m_scene)
    return;
  bool isCrossing = (pos.x() < m_firstCorner.x());
  m_scene->setSelectionRect(m_firstCorner, pos,
                            isCrossing ? MyGraphicsScene::Mode_Crossing
                                       : MyGraphicsScene::Mode_Window);
}

void QuadMirrorTool::processBoxSelection(QPointF p2) {
  clearOverlay();
  if (!m_scene)
    return;

  bool isWindow = (p2.x() > m_firstCorner.x());
  QList<CadItem *> topItems = isWindow
      ? collectItemsInWindowRect(m_firstCorner, p2)
      : collectItemsInCrossingRect(m_firstCorner, p2);

  for (int i = 0; i < topItems.size(); ++i) {
    CadItem *ci = topItems[i];
    if (std::find(m_selectedItems.begin(), m_selectedItems.end(), ci) ==
        m_selectedItems.end()) {
      m_selectedItems.push_back(ci);
      ci->setSelected(true);
    }
  }
  hideGripsOfSelectedItems();
}

bool QuadMirrorTool::showDialogAndPrepareClones() {
  if (m_selectedItems.empty() || !m_scene)
    return false;

  // 선택된 모든 객체를 감싸는 씬 좌표 바운딩 박스 계산 (미러 축 계산 기준)
  QRectF bbox;
  for (size_t i = 0; i < m_selectedItems.size(); ++i) {
    CadItem *item = m_selectedItems[i];
    if (!item)
      continue;
    bbox = bbox.isNull() ? item->sceneBoundingRect()
                         : bbox.united(item->sceneBoundingRect());
  }
  if (bbox.isNull() || (bbox.width() <= 0.0 && bbox.height() <= 0.0))
    return false; // 미러 축을 계산할 수 없는 경우 (점 하나뿐인 경우 등)

  // 다이얼로그의 부모 위젯으로 현재 씬을 보여주는 뷰를 사용 (모달로 표시)
  QWidget *parentWidget =
      m_scene->views().isEmpty() ? nullptr : m_scene->views().first();

  QuadMirrorDialog dialog(m_selectedItems, bbox, parentWidget);
  if (dialog.exec() != QDialog::Accepted)
    return false;

  QuadMirrorDialog::Direction dir = dialog.selectedDirection();
  m_isMoveMode = (dialog.selectedMode() == QuadMirrorDialog::Mode_Move);

  QPointF vAxisP1, vAxisP2, hAxisP1, hAxisP2;
  dialog.getVerticalAxis(vAxisP1, vAxisP2);
  dialog.getHorizontalAxis(hAxisP1, hAxisP2);
  MyVector vP1(vAxisP1), vP2(vAxisP2), hP1(hAxisP1), hP2(hAxisP2);

  // 선택된 방향으로 변환한 복제본을 미리 만들어 둔다.
  // (아직 씬에는 추가하지 않고, 기준점->목적점을 받은 뒤 배치한다)
  discardPendingClones(); // 안전을 위해 이전 잔여물이 있다면 먼저 정리
  // 복사 모드에서는 복제본의 다중선 그룹 ID 를 새로 부여 (원본과 ID 공유 방지)
  QMap<QString, QString> gidRemap;
  for (size_t i = 0; i < m_selectedItems.size(); ++i) {
    CadItem *item = m_selectedItems[i];
    if (!item)
      continue;

    CadItem *clone = item->clone();
    if (dir == QuadMirrorDialog::Direction_MirrorVertical) {
      clone->mirror(vP1, vP2);
    } else if (dir == QuadMirrorDialog::Direction_MirrorHorizontal) {
      clone->mirror(hP1, hP2);
    } else if (dir == QuadMirrorDialog::Direction_Rotate180) {
      // 세로축 대칭 -> 가로축 대칭을 순서대로 적용 = 180도 회전과 동일
      // (텍스트 항목은 CadItem::mirror() 구현상 글자 자체는 뒤집히지 않아
      //  두 번 적용해도 문자 방향이 그대로 유지된다)
      clone->mirror(vP1, vP2);
      clone->mirror(hP1, hP2);
    }
    if (!m_isMoveMode)
      MultilineTagUtil::remapGroupId(clone, gidRemap);
    clone->setLayerName(item->layerName());
    if (!dynamic_cast<BlockItem *>(clone)) {
      clone->setPen(item->pen());
    }
    m_pendingClones.push_back(clone);
  }

  if (m_pendingClones.empty())
    return false;

  m_currentState = State_PickingBasePoint;
  updatePrompt();
  return true;
}

void QuadMirrorTool::finalizePlacement(const MyVector &delta) {
  clearOverlay();

  auto ctx = getContext();
  if (!ctx || !ctx->undoStack() || !m_scene) {
    discardPendingClones();
    return;
  }

  ctx->undoStack()->beginMacro(tr("4거울상"));

  // 변환된 복제본들을 목적점까지 이동시킨 뒤 씬에 추가
  for (size_t i = 0; i < m_pendingClones.size(); ++i) {
    CadItem *clone = m_pendingClones[i];
    if (!clone)
      continue;
    clone->move(delta);
    ctx->undoStack()->push(new AddEntityCommand(m_scene, clone));
  }
  m_pendingClones.clear(); // 소유권이 undo 스택(AddEntityCommand)으로 넘어감

  // 이동 모드일 때만 원본 객체를 삭제
  if (m_isMoveMode) {
    for (size_t i = 0; i < m_selectedItems.size(); ++i) {
      CadItem *item = m_selectedItems[i];
      if (item)
        ctx->undoStack()->push(new RemoveEntityCommand(m_scene, item));
    }
  }

  ctx->undoStack()->endMacro();
  // 선택 해제는 deactivate()에서 일괄 처리한다.
}

void QuadMirrorTool::discardPendingClones() {
  // 아직 씬/undo스택에 소유권이 넘어가지 않은 복제본은 이 도구가 직접 해제
  for (size_t i = 0; i < m_pendingClones.size(); ++i) {
    delete m_pendingClones[i];
  }
  m_pendingClones.clear();
}
