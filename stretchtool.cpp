#include "stretchtool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "gripitem.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QDebug>
#include <QKeyEvent>
#include <QUndoStack>

StretchTool::StretchTool(IAppContext *ctx, QObject *parent)
    : AbstractTool(parent), m_currentState(State_Selecting), m_ctx(ctx),
      m_isBoxSelecting(false) {
  setContext(ctx);
}

StretchTool::~StretchTool() { clearOverlay(); }

void StretchTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);
  m_currentState = State_Selecting;
  m_isBoxSelecting = false;
  m_stretchMap.clear();

  if (m_ctx && m_ctx->commandDock()) {
    m_ctx->commandDock()->setToolActive(true);
    m_ctx->commandDock()->logMessage("STRETCH", CommandDock::Command);
    updatePrompt();
  }
}

void StretchTool::deactivate() {
  clearOverlay();
  m_currentState = State_Selecting;
  m_isBoxSelecting = false;
  AbstractTool::deactivate();
}

void StretchTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;

  if (m_currentState == State_Selecting) {
    handleSelectionPress(pos, button);
    return;
  }

  if (button != Qt::LeftButton)
    return;

  QPointF snappedPos = pos;
  if (m_ctx && m_ctx->snapManager()) {
    QPointF base =
        (m_currentState == State_WaitingForSecond) ? m_basePoint : QPointF();
    snappedPos = m_ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
  }

  if (m_currentState == State_WaitingForBase) {
    m_basePoint = snappedPos;
    m_currentState = State_WaitingForSecond;
    updatePrompt();
  } else if (m_currentState == State_WaitingForSecond) {
    executeStretch(snappedPos);
    m_scene->clearSelection();
    emit toolFinished();
  }
}

void StretchTool::mouseMove(QPointF pos) {
  if (!m_scene)
    return;

  m_currentMousePos = pos;

  if (m_currentState == State_Selecting) {
    handleSelectionMove(pos);
    return;
  }

  QPointF snappedPos = pos;
  SnapType type = SnapType::None;
  if (m_ctx && m_ctx->snapManager()) {
    QPointF base =
        (m_currentState == State_WaitingForSecond) ? m_basePoint : QPointF();
    auto result = m_ctx->snapManager()->snapPoint(pos, m_scene, base);
    snappedPos = result.snappedPos;
    type = result.type;
  }
  m_scene->setSnapMarker(snappedPos, type);

  if (m_currentState == State_WaitingForSecond) {
    MyVector delta = MyVector(snappedPos) - MyVector(m_basePoint);

    // 모든 아이템의 고스트 패스를 담을 통합 패스 (씬 좌표계 기준)
    QPainterPath combinedGhostPath;

    // 각 아이템별로 신축된 경로 생성 및 통합
    QMapIterator<CadItem *, QList<int>> it(m_stretchMap);
    while (it.hasNext()) {
      it.next();
      CadItem *item = it.key();
      QList<int> indices = it.value();

      // 아이템 로컬 좌표계 기준의 고스트 경로 획득
      QPainterPath localPath = item->getStretchedPath(indices, delta);

      // 씬 좌표계로 변환하여 통합 (위치 오차 방지)
      combinedGhostPath.addPath(item->mapToScene(localPath));
    }

    // 통합된 고스트 패스를 한 번에 렌더링 (대표적으로 첫 번째 아이템의 레이어
    // 스타일 사용)
    if (!m_stretchMap.isEmpty()) {
      CadItem *firstItem = m_stretchMap.begin().key();
      LayerStyle style = m_scene->getLayerStyle(firstItem->layerName());
      QColor ghostColor = style.color;
      ghostColor.setAlpha(127);

      m_scene->setGhostPath(combinedGhostPath, true, ghostColor, 1.0,
                            style.dashPattern);
    }
  }
}

void StretchTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    onCancel();
    emit toolFinished();
  } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if (m_currentState == State_Selecting) {
      finishSelectionStep();
    }
  } else {
    AbstractTool::keyPress(event);
  }
}

bool StretchTool::processCommand(const QString &command) {
  QString cmd = command.toLower().trimmed();
  if (cmd.isEmpty()) {
    if (m_currentState == State_Selecting) {
      finishSelectionStep();
      return true;
    }
    return false;
  }

  QPointF base =
      (m_currentState == State_WaitingForSecond) ? m_basePoint : QPointF(0, 0);
  QPointF resultPos;
  bool isOrtho = (m_ctx->snapManager() && m_ctx->snapManager()->isOrtho());

  if (m_ctx->parser()->parseCoordinate(cmd, base, m_currentMousePos, isOrtho,
                                       resultPos)) {
    mousePress(resultPos, Qt::LeftButton);
    return true;
  }
  return false;
}

void StretchTool::handleSelectionPress(QPointF pos, Qt::MouseButton button) {
  if (button == Qt::RightButton) {
    finishSelectionStep();
    return;
  }
  if (button != Qt::LeftButton)
    return;

  if (!m_isBoxSelecting) {
    m_isBoxSelecting = true;
    m_firstCorner = pos;
  } else {
    processBoxSelection(m_firstCorner, pos);
    m_isBoxSelecting = false;
    clearOverlay();
    // [개선] 선택된 객체들의 그립 숨기기 (선택 완료 시점)
    hideGripsOfSelectedItems();
    // [개선] 박스 선택 완료 후 즉시 다음 단계(기준점 지정)로 진행
    finishSelectionStep();
  }
}

void StretchTool::handleSelectionMove(QPointF pos) {
  if (m_isBoxSelecting && m_scene) {
    MyGraphicsScene::SelectionMode mode = (pos.x() > m_firstCorner.x())
                                              ? MyGraphicsScene::Mode_Window
                                              : MyGraphicsScene::Mode_Crossing;
    m_scene->setSelectionRect(m_firstCorner, pos, mode);
  }
}

void StretchTool::processBoxSelection(QPointF p1, QPointF p2) {
  QRectF rect = QRectF(p1, p2).normalized();

  bool isWindow = (p2.x() > p1.x());
  QList<CadItem *> cadItems = isWindow
      ? collectItemsInWindowRect(p1, p2)
      : collectItemsInCrossingRect(p1, p2);

  for (int i = 0; i < cadItems.size(); ++i) {
    CadItem *cadItem = cadItems.at(i);
    cadItem->setSelected(true);

    // 해당 아이템의 어떤 정점이 영역 안에 있는가?
    QList<int> insideIndices;
    QList<SnapPoint> snp = cadItem->getSnapPoints();
    for (int j = 0; j < snp.size(); ++j) {
      if (rect.contains(snp[j].pos.toPointF())) {
        insideIndices.append(j);
      }
    }

    if (!insideIndices.isEmpty()) {
      m_stretchMap[cadItem] = insideIndices;
    }
  }
}

void StretchTool::finishSelectionStep() {
  if (m_stretchMap.isEmpty()) {
    emit toolFinished();
  } else {
    m_currentState = State_WaitingForBase;
    updatePrompt();
    hideGripsOfSelectedItems();
    clearOverlay();
  }
}

void StretchTool::updatePrompt() {
  if (!m_ctx || !m_ctx->commandDock())
    return;

  switch (m_currentState) {
  case State_Selecting:
    m_ctx->commandDock()->showInteractivePrompt(
        tr(" 스트레칭 객체를 선택 하세요 (Crossing): "), {});
    break;
  case State_WaitingForBase:
    m_ctx->commandDock()->showInteractivePrompt(tr(" 기준점을 지정 하세요 : "), {});
    break;
  case State_WaitingForSecond:
    m_ctx->commandDock()->showInteractivePrompt(tr(" 목표점을 지정 하세요 : "), {});
    break;
  }
}

void StretchTool::executeStretch(const QPointF &targetPoint) {
  if (m_stretchMap.isEmpty())
    return;

  MyVector delta = MyVector(targetPoint) - MyVector(m_basePoint);

  if (m_ctx && m_ctx->undoStack()) {
    m_ctx->undoStack()->beginMacro("Stretch Items");
    QMapIterator<CadItem *, QList<int>> it(m_stretchMap);
    while (it.hasNext()) {
      it.next();
      m_ctx->undoStack()->push(
          new StretchEntityCommand(it.key(), it.value(), delta));
    }
    m_ctx->undoStack()->endMacro();
  } else {
    QMapIterator<CadItem *, QList<int>> it(m_stretchMap);
    while (it.hasNext()) {
      it.next();
      it.key()->stretch(it.value(), delta);
    }
  }
}

void StretchTool::clearOverlay() {
  if (m_scene)
    m_scene->clearOverlay();
}

void StretchTool::onFinish() { clearOverlay(); }
void StretchTool::onCancel() { clearOverlay(); }
