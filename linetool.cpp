#include "linetool.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "lineitem.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QUndoStack>

#include <QDebug>
#include <QKeyEvent>
#include <QPen>
#include <QTimer>
#include <QtMath>

LineTool::LineTool(QObject *parent)
    : AbstractTool(parent), m_isDrawing(false) // 초기화
{}

LineTool::~LineTool() { cancelDrawing(); }

// 1. 툴이 시작될 때 (Activate)
void LineTool::activate(MyGraphicsScene *scene) {

  // [핵심 가드] 이미 해당 씬에서 활성화된 도구라면 초기화 건너뜜 (마우스 이동
  // 시 상태 보존)
  if (m_scene == scene) {
    return;
  }

  AbstractTool::activate(scene);
  m_isDrawing = false;
  m_addedItems.clear();

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("LINE", CommandDock::Command);
    ctx->commandDock()->showInteractivePrompt(tr(" 시작 점을 지정하세요 : "), {});
  } // else {
  //}
}

// 2. 툴이 종료될 때 (Deactivate)
void LineTool::deactivate() {
  cancelDrawing();
  AbstractTool::deactivate();
}

bool LineTool::processCommand(const QString &command) {
  QString cmd = command.toLower().trimmed();
  bool success = false;

  // (1) Undo / Close 처리
  if (cmd == "u" || cmd == "undo") {
    success = undoLastSegment();
  } else if (cmd == "c" || cmd == "close") {
    closeLoop();
    success = true;
  }
  // (2) 좌표 처리
  else {
    IAppContext *ctx = getContext();
    if (ctx && ctx->parser()) {
      QPointF base = m_isDrawing ? m_startPoint : QPointF(0, 0);
      QPointF resultPos;
      bool isOrtho = (ctx->snapManager() && ctx->snapManager()->isOrtho());

      if (ctx->parser()->parseCoordinate(cmd, base, m_currentMousePos, isOrtho,
                                         resultPos)) {
        mousePress(resultPos, Qt::LeftButton);
        success = true;
      }
    }
  }

  if (success) {
    // AbstractTool의 공통 타이머 함수 호출 (엔터키 0.2초 차단)
    blockEnterKeyTemporarily(200);
    return true;
  }
  return false;
}

void LineTool::onFinish() {
  if (m_isDrawing) {
    cancelDrawing();
    if (getContext() && getContext()->commandDock()) {
      getContext()->commandDock()->logMessage("Line tool finished.",
                                              CommandDock::Info);
      getContext()->commandDock()->setPrompt(" Command : ");
    }
  }
}

void LineTool::onCancel() {
  cancelDrawing();
  if (getContext() && getContext()->commandDock()) {
    getContext()->commandDock()->logMessage("*Cancel*", CommandDock::Info);
    getContext()->commandDock()->setPrompt(" Command : ");
  }
}

bool LineTool::undoLastSegment() {
  if (m_addedItems.isEmpty()) {
    cancelDrawing();
    return false; // 더 이상 지울 게 없으면 실패 리턴 -> 전역 Undo로 넘어감
  }

  QGraphicsItem *lastItem = m_addedItems.takeLast();

  if (LineItem *li = dynamic_cast<LineItem *>(lastItem)) {
    m_startPoint = li->getLine().start.toPointF();
    m_endPoint = m_startPoint;
  }

  if (m_scene) {
    m_scene->removeItem(lastItem);
  }
  delete lastItem;

  // Undo 후 마우스가 가만히 있어도 고스트 라인이 갱신되도록 호출
  mouseMove(m_currentMousePos);

  if (getContext() && getContext()->commandDock()) {
    getContext()->commandDock()->logMessage("Undo.", CommandDock::Info);
  }
  return true;
}

void LineTool::closeLoop() {
  if (m_addedItems.isEmpty() || !m_isDrawing) {
    return;
  }

  // 첫 번째 점과 현재 시작점(직전의 끝점) 사이를 잇는 마지막 선 생성
  if (QLineF(m_startPoint, m_firstPoint).length() > 0.001) {
    m_endPoint = m_firstPoint;
    finishLine();
  }

  if (getContext() && getContext()->commandDock()) {
    getContext()->commandDock()->logMessage("Loop closed.", CommandDock::Info);
    getContext()->commandDock()->setPrompt(" Command : ");
  }
  emit toolFinished();
}

void LineTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;
  IAppContext *ctx = getContext();

  // 우클릭: 종료
  if (button == Qt::RightButton) {
    if (m_isDrawing) {
      cancelDrawing();
      if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage("Function finished.", CommandDock::Info);
        ctx->commandDock()->setPrompt("  Command : ");
      }
      emit toolFinished();
    }
    return;
  }

  if (button != Qt::LeftButton)
    return;

  // 스냅 계산
  QPointF snappedPos = pos;
  if (ctx && ctx->snapManager()) {
    QPointF base = m_isDrawing ? m_startPoint : QPointF();
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
  }

  if (!m_isDrawing) {
    // [1단계] 시작점 지정
    m_isDrawing = true;
    m_startPoint = snappedPos;
    m_endPoint = snappedPos;
    m_firstPoint = snappedPos;
    m_currentMousePos = snappedPos;

    if (m_scene) {
      updateGhostLinePreview(QLineF(m_startPoint, m_endPoint), currentLayerName());
    }

    if (ctx && ctx->commandDock()) {
      QStringList options;
      options << "Undo ";
      ctx->commandDock()->showInteractivePrompt(tr(" 다음 점을 지정 하세요 또는 "), options);
    }
  } else {
    // [2단계] 끝점 지정 -> 선 생성
    m_endPoint = snappedPos;
    finishLine();

    // 연속 그리기 설정
    m_startPoint = snappedPos;

    if (m_scene) {
      updateGhostLinePreview(QLineF(m_startPoint, m_currentMousePos), currentLayerName());
    }

    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->logMessage("Line created.", CommandDock::Info);
      QStringList options;
      options << "Undo";
      if (m_addedItems.size() >= 2)
        options << "Close";

      ctx->commandDock()->showInteractivePrompt(tr(" 다음 점을 지정 하세요 또는 "),
                                                options);
    }
  }
}

void LineTool::mouseMove(QPointF pos) {
  if (!m_scene)
    return;

  m_currentMousePos = pos;

  // 1. 스냅 계산
  QPointF finalPos = pos;
  SnapType snapType = SnapType::None;

  IAppContext *ctx = getContext();
  if (ctx && ctx->snapManager()) {
    QPointF base = m_isDrawing ? m_startPoint : QPointF();
    SnapManager::SnapResult result =
        ctx->snapManager()->snapPoint(pos, m_scene, base);
    finalPos = result.snappedPos;
    snapType = result.type;
  }

  m_endPoint = finalPos;

  m_endPoint = finalPos;

  if (m_isDrawing && m_scene) {
    updateGhostLinePreview(QLineF(m_startPoint, m_endPoint),
                           currentLayerName());
  }

  if (m_scene) {
    m_scene->setSnapMarker(finalPos, snapType);
  }
}

void LineTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
  Q_UNUSED(pos);
  Q_UNUSED(button);
}

void LineTool::finishLine() {
  if (!m_scene)
    return;

  if (QLineF(m_startPoint, m_endPoint).length() > 0.001) {
    IAppContext *ctx = getContext();
    QGraphicsItem *realItem = nullptr;

    if (ctx && ctx->layerManager() && ctx->lineTypeManager()) {
      Layer *curLayer = ctx->layerManager()->getCurrentLayer();

      QPen layerPen(curLayer->color());
      layerPen.setWidthF(curLayer->weight());

      QString linetypeName = curLayer->linetype();

      // [패턴 적용 시작]
      // "Continuous"나 "Continous" (오타)인 경우 솔리드 라인으로 처리
      if (linetypeName == "Continuous" || linetypeName == "Continous") {
        layerPen.setStyle(Qt::SolidLine);
      } else {
        // 1. 펜의 스타일을 사용자 지정 점선(CustomDashLine) 모드로 변경
        layerPen.setStyle(Qt::CustomDashLine);

        // 2. 매니저에게 부탁해서 파싱된 패턴 배열을 받아오기
        QVector<qreal> pattern =
            ctx->lineTypeManager()->getQtDashPattern(linetypeName);

        // 3. 펜에 패턴 장착!
        if (!pattern.isEmpty()) {
          layerPen.setDashPattern(pattern);
        } else {
          layerPen.setStyle(Qt::SolidLine); // 파싱 실패 시 안전장치
        }
      }

      realItem = ctx->entityFactory()->createLine(QLineF(m_startPoint, m_endPoint), layerPen);
      if(!realItem) return;
      if (LineItem *li = dynamic_cast<LineItem *>(realItem)) {
        li->setLayerName(curLayer->name());
      }

      QGraphicsItem *layerFolder = m_scene->getLayerRoot(curLayer->name());
      realItem->setParentItem(layerFolder);

      // setParentItem이 씬에 추가했을 수 있으므로, 명령으로 관리하기
      // 위해 잠시 제거
      if (realItem->scene() == m_scene) {
        m_scene->removeItem(realItem);
      }

      if (ctx && ctx->undoStack()) {
        ctx->undoStack()->push(new AddEntityCommand(m_scene, realItem));
      } else {
        m_scene->addItem(realItem);
      }
      // =========================================================
    }
    //        m_scene->addItem(realItem);
    m_addedItems.append(realItem);
    emit lineCreated();
  }
}

void LineTool::cancelDrawing() {
  m_isDrawing = false;

  if (m_scene) {
    m_scene->setGhostLine(QLineF(), false);
    m_scene->clearOverlay();
  }
}
