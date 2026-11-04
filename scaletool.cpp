#include "scaletool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "snapmanager.h"
#include <QKeyEvent>
#include <QList>
#include <QtMath>
#include <cmath>


ScaleTool::ScaleTool(IAppContext *ctx, QObject *parent) : AbstractTool(parent) {
  setContext(ctx);
}

ScaleTool::~ScaleTool() { clearOverlay(); }

void ScaleTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;
  AbstractTool::activate(scene);

  m_selectedItems.clear();
  m_currentState = State_SelectingObjects;
  m_isReferenceMode = false;

  if (m_scene) {
    QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (auto *item : std::as_const(selected)) {
      if (CadItem *ci = dynamic_cast<CadItem *>(item)) {
        m_selectedItems.push_back(ci);
      }
    }
    if (!m_selectedItems.empty()) {
      hideGripsOfSelectedItems();
      m_currentState = State_WaitingForBasePoint;
    }
  }

  if (auto ctx = getContext()) {
    if (auto dock = ctx->commandDock()) {
      dock->setToolActive(true);
      dock->logMessage("SCALE", CommandDock::Command);
    }
  }
  updatePrompt();
}

void ScaleTool::deactivate() {
  clearOverlay();
  if (m_scene) {
    for (CadItem *item : std::as_const(m_selectedItems)) {
      if (item) {
        item->setSelected(false);
        // 그립을 명시적으로 다시 켤 필요 없음 (기본값 유지)
      }
    }
    m_scene->clearSelection();
  }
  m_selectedItems.clear();
  AbstractTool::deactivate();
}

bool ScaleTool::processCommand(const QString &command) {
  QString cmd = command.toLower().trimmed();
  IAppContext *ctx = getContext();
  if (!ctx)
    return false;

  if (cmd == "p" && m_currentState == State_SelectingObjects) {
    if (m_scene && m_scene->applyPreviousSelection()) {
      m_selectedItems.clear();
      QList<CadItem *> prev = getTopLevelCadItems(m_scene->selectedItems());
      for (int i = 0; i < prev.size(); ++i)
        m_selectedItems.push_back(prev[i]);
      hideGripsOfSelectedItems();
    }
    return true;
  }

  if (cmd.isEmpty()) {
    if (m_currentState == State_SelectingObjects && !m_selectedItems.empty()) {
      m_scene->savePreviousSelection();
      m_currentState = State_WaitingForBasePoint;
      updatePrompt();
      return true;
    }
    return false;
  }

  // 축척 비율 또는 참조 모드 입력 처리
  if (m_currentState == State_WaitingForScaleFactor) {
    if (cmd == "r" || cmd == "reference") {
      m_isReferenceMode = true;
      m_currentState = State_WaitingForReferenceLength;
      updatePrompt();
      return true;
    }

    bool ok;
    double factor = cmd.toDouble(&ok);
    if (ok) {
      finishScale(factor);
      return true;
    }
  }

  // 참조 길이 또는 새 길이 입력 처리
  if (m_currentState == State_WaitingForReferenceLength ||
      m_currentState == State_WaitingForNewLength) {
    bool ok;
    double val = cmd.toDouble(&ok);
    if (ok) {
      if (m_currentState == State_WaitingForReferenceLength) {
        m_referenceLength = val;
        m_currentState = State_WaitingForNewLength;
        updatePrompt();
      } else {
        if (!MyMath::isZero(m_referenceLength)) {
          finishScale(val / m_referenceLength);
        }
      }
      return true;
    }
  }

  // 좌표 입력 처리
  QPointF base = (m_currentState == State_WaitingForScaleFactor)
                     ? m_basePoint.toPointF()
                     : QPointF(0, 0);
  QPointF resultPos;
  bool isOrtho = (ctx->snapManager() && ctx->snapManager()->isOrtho());

  if (ctx->parser() && ctx->parser()->parseCoordinate(
                           cmd, base, m_currentMousePos, isOrtho, resultPos)) {
    mousePress(resultPos, Qt::LeftButton);
    return true;
  }

  return false;
}

void ScaleTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (button == Qt::RightButton) {
    if (m_currentState == State_SelectingObjects && !m_selectedItems.empty()) {
      m_currentState = State_WaitingForBasePoint;
      updatePrompt();
      return;
    }
  }

  if (button != Qt::LeftButton)
    return;

  QPointF snappedPos = pos;
  if (auto ctx = getContext()) {
    if (auto snap = ctx->snapManager()) {
      QPointF base = (m_currentState == State_WaitingForScaleFactor)
                         ? m_basePoint.toPointF()
                         : QPointF();
      snappedPos = snap->snapPoint(pos, m_scene, base).snappedPos;
    }
  }

  switch (m_currentState) {
  case State_SelectingObjects: {
    if (auto *item = itemAtPos(pos)) {
      if (CadItem *ci = dynamic_cast<CadItem *>(item)) {
        auto it = std::find(m_selectedItems.begin(), m_selectedItems.end(), ci);
        if (it == m_selectedItems.end()) {
          m_selectedItems.push_back(ci);
          ci->setSelected(true);
          hideGripsOfSelectedItems();
        } else {
          m_selectedItems.erase(it);
          ci->setSelected(false);
        }
      }
    } else {
      m_firstCorner = pos;
      m_currentState = State_SelectingObjects_SecondCorner;
      updatePrompt();
    }
    break;
  }
  case State_SelectingObjects_SecondCorner:
    processBoxSelection(pos);
    m_currentState = State_SelectingObjects;
    updatePrompt();
    break;
  case State_WaitingForBasePoint:
    m_basePoint = snappedPos;
    m_currentState = State_WaitingForScaleFactor;
    m_isReferenceMode = false;
    updatePrompt();
    break;
  case State_WaitingForScaleFactor: {
    // 마우스 클릭 시 배율 결정
    // 기본 참조 길이는 100.0으로 가정하여, 클릭한 곳까지의 거리 비율로 배율
    // 계산
    double dist =
        QLineF(m_basePoint.toPointF(), applyOrtho(snappedPos)).length();
    double factor = dist / 100.0;
    if (factor > 0.0) {
      finishScale(factor);
    }
    break;
  }
  case State_WaitingForReferenceLength:
    m_referencePoint1 = applyOrtho(snappedPos);
    m_currentState = State_WaitingForNewLength;
    m_referenceLength =
        QLineF(m_basePoint.toPointF(), m_referencePoint1).length();
    updatePrompt();
    break;
  case State_WaitingForNewLength: {
    double newLength =
        QLineF(m_basePoint.toPointF(), applyOrtho(snappedPos)).length();
    if (!MyMath::isZero(m_referenceLength)) {
      finishScale(newLength / m_referenceLength);
    }
    break;
  }
  }
}

void ScaleTool::mouseMove(QPointF pos) {
  m_currentMousePos = pos;
  QPointF snappedPos = pos;
  SnapType snapType = SnapType::None;

  if (auto ctx = getContext()) {
    if (auto snap = ctx->snapManager()) {
      QPointF base = (m_currentState == State_WaitingForScaleFactor)
                         ? m_basePoint.toPointF()
                         : QPointF();
      auto result = snap->snapPoint(pos, m_scene, base);
      snappedPos = result.snappedPos;
      snapType = result.type;
    }
  }

  if (m_scene) {
    m_scene->setSnapMarker(snappedPos, snapType);
  }

  if (m_currentState == State_SelectingObjects_SecondCorner) {
    handleSelectionMove(pos);
  } else if (m_currentState == State_WaitingForScaleFactor ||
             m_currentState == State_WaitingForReferenceLength ||
             m_currentState == State_WaitingForNewLength) {
    updateScaleGhost(applyOrtho(snappedPos));
  }
}

void ScaleTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    clearOverlay();
    emit toolFinished();
  } else if ((event->key() == Qt::Key_Return ||
              event->key() == Qt::Key_Enter) ||
             event->key() == Qt::Key_Space) {
    if (m_currentState == State_SelectingObjects) {
      if (!m_selectedItems.empty()) {
        m_scene->savePreviousSelection();
        m_currentState = State_WaitingForBasePoint;
        updatePrompt();
      } else {
        emit toolFinished(); // 선택된 게 없으면 종료 (다른 툴들과 동일)
      }
    }
  } else {
    // F3, F8 등 기능키 처리를 위해 부모 클래스 호출
    AbstractTool::keyPress(event);
  }
}

void ScaleTool::handleSelectionMove(QPointF pos) {
  if (!m_scene)
    return;
  bool isCrossing = (pos.x() < m_firstCorner.x());
  m_scene->setSelectionRect(m_firstCorner, pos,
                            isCrossing ? MyGraphicsScene::Mode_Crossing
                                       : MyGraphicsScene::Mode_Window);
}

void ScaleTool::processBoxSelection(QPointF p2) {
  if (!m_scene) return;

  bool isWindow = (p2.x() > m_firstCorner.x());
  QList<CadItem *> topItems = isWindow
      ? collectItemsInWindowRect(m_firstCorner, p2)
      : collectItemsInCrossingRect(m_firstCorner, p2);

  for (int i = 0; i < (int)topItems.size(); ++i) {
    CadItem *ci = topItems.at(i);
    if (std::find(m_selectedItems.begin(), m_selectedItems.end(), ci) ==
        m_selectedItems.end()) {
      m_selectedItems.push_back(ci);
      ci->setSelected(true);
    }
  }
  hideGripsOfSelectedItems();
  clearOverlay();
}

QPointF ScaleTool::applyOrtho(const QPointF &pos) {
  auto ctx = getContext();
  if (!ctx || !ctx->snapManager() || !ctx->snapManager()->isOrtho())
    return pos;

  if (m_currentState == State_WaitingForScaleFactor) {
    double dx = pos.x() - m_basePoint.x;
    double dy = pos.y() - m_basePoint.y;
    if (std::abs(dx) > std::abs(dy)) {
      return QPointF(pos.x(), m_basePoint.y);
    } else {
      return QPointF(m_basePoint.x, pos.y());
    }
  } else if (m_currentState == State_WaitingForReferenceLength ||
             m_currentState == State_WaitingForNewLength) {
    double dx = pos.x() - m_basePoint.x;
    double dy = pos.y() - m_basePoint.y;
    if (std::abs(dx) > std::abs(dy)) {
      return QPointF(pos.x(), m_basePoint.y);
    } else {
      return QPointF(m_basePoint.x, pos.y());
    }
  }
  return pos;
}

void ScaleTool::updatePrompt() {
  if (auto ctx = getContext()) {
    if (auto dock = ctx->commandDock()) {
      if (m_currentState == State_SelectingObjects) {
        dock->setPrompt(tr("축척할 객체 선택 (완료하려면 Enter)"));
      } else if (m_currentState == State_SelectingObjects_SecondCorner) {
        dock->setPrompt(tr("다음 구석 지정"));
      } else if (m_currentState == State_WaitingForBasePoint) {
        dock->setPrompt(tr("기준점 지정:"));
      } else if (m_currentState == State_WaitingForScaleFactor) {
        dock->setPrompt(tr("축척 비율 지정 또는 [참조(R)]:"));
      } else if (m_currentState == State_WaitingForReferenceLength) {
        dock->setPrompt(tr("참조 길이 지정 <1.0>:"));
      } else if (m_currentState == State_WaitingForNewLength) {
        dock->setPrompt(tr("새 길이 지정:"));
      }
    }
  }
}

void ScaleTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}

void ScaleTool::updateScaleGhost(const QPointF &currentPos) {
  clearOverlay();
  if (!m_scene || m_selectedItems.empty())
    return;

  double factor = 1.0;
  QPointF basePt = m_basePoint.toPointF();

  if (m_currentState == State_WaitingForScaleFactor) {
    // 거리 기반 임시 배율 (기준 거리 100.0)
    double dist = QLineF(basePt, currentPos).length();
    factor = dist / 100.0;
    updateGhostLinePreview(QLineF(basePt, currentPos), "");
  } else if (m_currentState == State_WaitingForReferenceLength) {
    // 참조 길이를 정하는 중: 현재 점까지의 거리를 보여줌
    updateGhostLinePreview(QLineF(basePt, currentPos), "");
    // 미리보기는 원본 크기 유지
    factor = 1.0;
  } else if (m_currentState == State_WaitingForNewLength) {
    // 새 길이를 정하는 중
    double currentDist = QLineF(basePt, currentPos).length();
    if (!MyMath::isZero(m_referenceLength)) {
      factor = currentDist / m_referenceLength;
    }
    // 기준점에서 참조 지점까지 표시 (참조선)
    updateGhostLinePreview(QLineF(basePt, m_referencePoint1), "Default");
    // 기준점에서 현재 마우스까지 표시
    updateGhostLinePreview(QLineF(basePt, currentPos), "");
  }

  if (factor < 0.001)
    factor = 0.001;

  // 선택된 객체들의 소속 레이어 파악
  QString commonLayer = getCommonLayerName(
      QList<QGraphicsItem *>(m_selectedItems.begin(), m_selectedItems.end()));

  QPainterPath combinedPath;
  for (CadItem *item : std::as_const(m_selectedItems)) {
    if (!item)
      continue;
    combinedPath.addPath(item->getScaledPath(m_basePoint, factor));
  }

  // Ghost 스타일 적용
  updateGhostPreview(combinedPath, commonLayer);
}

void ScaleTool::finishScale(double scaleFactor) {
  if (auto ctx = getContext()) {
    if (auto stack = ctx->undoStack()) {
      stack->beginMacro(tr("Scale %1 items").arg(m_selectedItems.size()));
      for (CadItem *item : std::as_const(m_selectedItems)) {
        stack->push(new ScaleEntityCommand(item, m_basePoint, scaleFactor));
      }
      stack->endMacro();
    }
  }
  if (m_scene) {
    m_scene->clearSelection();
  }
  clearOverlay();
  emit toolFinished();
}
