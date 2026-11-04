#include "selecttool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "gripitem.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "leaderitem.h"
#include "leadertextedittool.h"
#include "textedittool.h"
#include "textitem.h"
#include <QUndoStack>

#include <QApplication>
#include <QDebug>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPainter>
#include <QtMath>

SelectTool::SelectTool(QObject *parent)
    : AbstractTool(parent), m_activeGrip(nullptr), m_boxSelectPending(false) {}

SelectTool::~SelectTool() {}

void SelectTool::activate(MyGraphicsScene *scene) {
  // [핵심 가드] 창 전환이 아닌 동일 창 내 재바인딩이면 상태 유지
  if (m_scene == scene)
    return;

  // [핵심] 창 전환 시 이전 창의 잔상과 상태를 확실히 정리
  if (m_scene) {
    endFastDragRender(); // 이전 창에 저품질 렌더링이 걸려 있었다면 복원
    clearOverlay();
  }

  m_activeGrip = nullptr;
  m_boxSelectPending = false;
  m_hasLastBoxViewPos = false;

  AbstractTool::activate(scene); // 실제 씬 연결

  // [핵심] 다른 툴에서 SelectTool로 복귀할 때, 눌려있던 Enter 키가
  // 뒤늦게 처리되어 이전 명령(예: Insert)이 반복 실행되는 것을 방지합니다.
  blockEnterKeyTemporarily(200);
}

void SelectTool::deactivate() {
  // 진행 중인 작업 취소
  m_activeGrip = nullptr;
  cancelBoxSelection(); // 박스 선택 상태 + 저품질 렌더링 복원

  AbstractTool::deactivate();
}

void SelectTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;
  if (button != Qt::LeftButton)
    return;

  IAppContext *ctx = getContext();
  QPointF snappedPos = pos;
  if (ctx && ctx->snapManager()) {
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene).snappedPos;
  }

  // 1. 그립 편집 확정
  if (m_activeGrip) {
    CadItem *item = dynamic_cast<CadItem *>(m_activeGrip->parentItem());
    
    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager() && item) {
       QPointF basePoint;
       MyVector baseVec;
       if (item->getOrthoBasePoint(m_activeGrip->gripIndex(), baseVec)) {
           basePoint = baseVec.toPointF();
       }
       snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, basePoint).snappedPos;
    }

    if (item) {
      item->applyGripMove(m_activeGrip->gripIndex(), snappedPos);
    }
    m_activeGrip = nullptr;
    clearOverlay();
    return;
  }

  // 2. 박스 선택 확정
  if (m_boxSelectPending) {
    finishBoxSelection(pos);
    return;
  }

  // 3. 새로운 작업 시작
  // 크로스헤어 중앙 픽박스 안에 그립이 있으면 도형보다 그립을 우선 선택한다.
  if (GripItem *grip = findGripAt(pos)) {
    m_activeGrip = grip;
    m_lastPos = pos;
    return;
  }

  QGraphicsItem *targetItem = itemAtPos(pos);
  if (targetItem) {
    // 원래 설계대로 단일 클릭은 누적 다중 선택 + 토글 방식:
    // 선택 안 된 객체를 클릭하면 기존 선택에 추가, 이미 선택된 객체를
    // 다시 클릭하면 선택 해제. (전체 해제는 빈 곳 클릭 또는 Esc)
    // 참고: 인접한 호(Arc)에서 이전 선택 항목의 그립이 다른 도형 클릭을
    // 가로채던 문제는 위 그립 판정(altCad != gripOwner 이면 도형 선택 우선)
    // 에서 처리하므로 여기서 이전 선택을 지울 필요가 없다.
    toggleSelection(targetItem);
  } else {
    // AutoCAD처럼 박스(Window/Crossing) 선택은 기존 선택에 추가한다.
    // 빈 곳 클릭으로 선택을 지우지 않으며, 전체 해제는 Esc로 한다.
    m_boxSelectPending = true;
    m_firstCorner = pos;
    m_hasLastBoxViewPos = false;
    // 참고: 드래그 미리보기는 MyGraphicsView의 "프리즈 오버레이"가 처리하므로
    // (씬 리페인트 자체가 일어나지 않음) 여기서 안티앨리어싱을 끌 필요는 없다.
  }
}

GripItem *SelectTool::findGripAt(const QPointF &pos) const {
  if (!m_scene)
    return nullptr;

  // itemAtPos()와 동일한 픽박스 반경(5px)을 씬 좌표로 환산
  double scale = 1.0;
  QTransform vt;
  if (!m_scene->views().isEmpty()) {
    vt = m_scene->views().constFirst()->viewportTransform();
    if (std::abs(vt.m11()) > 0.0001)
      scale = std::abs(vt.m11());
  }
  const double tol = 5.0 / scale;
  const QRectF box(pos.x() - tol, pos.y() - tol, tol * 2.0, tol * 2.0);

  // 그립은 ItemIgnoresTransformations 이므로 뷰 변환을 함께 넘겨야 정확히 검색된다.
  const QList<QGraphicsItem *> found =
      m_scene->items(box, Qt::IntersectsItemBoundingRect, Qt::DescendingOrder, vt);

  GripItem *best = nullptr;
  double bestDist = 0.0;
  for (int i = 0; i < found.size(); ++i) {
    GripItem *g = dynamic_cast<GripItem *>(found.at(i));
    if (!g || !g->isVisible())
      continue;
    const QPointF d = g->scenePos() - pos;
    const double dist = d.x() * d.x() + d.y() * d.y();
    if (!best || dist < bestDist) {
      best = g;
      bestDist = dist;
    }
  }
  return best;
}

void SelectTool::mouseMove(QPointF pos) {
  IAppContext *ctx = getContext(); // [수정] parent() 대신 getContext() 사용

  // =========================================================
  // Case A: 그립 편집 중 (이동 미리보기)
  // =========================================================
  if (m_activeGrip) {
    CadItem *item = dynamic_cast<CadItem *>(m_activeGrip->parentItem());

    // 1. 스냅 계산
    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager() && item) {
      QPointF basePoint;
      MyVector baseVec;
      if (item->getOrthoBasePoint(m_activeGrip->gripIndex(), baseVec)) {
          basePoint = baseVec.toPointF();
      }

      auto result = ctx->snapManager()->snapPoint(pos, m_scene, basePoint);
      snappedPos = result.snappedPos;

      // 스냅 마커 표시 (이제 씬 좌표로 직접 전달)
      m_scene->setSnapMarker(snappedPos, result.type);
    }

    // 2. 고스트 형상 요청
    if (item) {
      QPainterPath ghostPath =
          item->getGhostShape(m_activeGrip->gripIndex(), snappedPos);

      // 3. 오버레이에 그리기 (아이템 레이어 색상 반영)
      updateGhostPreview(ghostPath, item->layerName());
    }
    return;
  }

  // =========================================================
  // Case B: 박스 선택 미리보기
  // =========================================================
  if (m_boxSelectPending && m_scene) {
    // [성능] 뷰포트 기준 몇 픽셀 이상 움직였을 때만 선택 사각형을 갱신한다.
    // 마우스 이동 이벤트마다 부분 리페인트가 도는 것을 억제한다.
    if (!m_scene->views().isEmpty()) {
      QGraphicsView *v = m_scene->views().first();
      QPoint vp = v->mapFromScene(pos);
      if (m_hasLastBoxViewPos &&
          (vp - m_lastBoxViewPos).manhattanLength() < 3)
        return;
      m_lastBoxViewPos = vp;
      m_hasLastBoxViewPos = true;
    }

    // 방향 판별
    MyGraphicsScene::SelectionMode mode;
    if (pos.x() > m_firstCorner.x())
      mode = MyGraphicsScene::Mode_Window;
    else
      mode = MyGraphicsScene::Mode_Crossing;

    m_scene->setSelectionRect(m_firstCorner, pos, mode);
  }
}

void SelectTool::mouseDoubleClick(QPointF pos, Qt::MouseButton button) {
  if (button != Qt::LeftButton || !m_scene)
    return;

  QGraphicsItem *clickedItem = itemAtPos(pos);
  if (clickedItem) {
    if (TextItem *textItem = dynamic_cast<TextItem *>(clickedItem)) {
      IAppContext *ctx = getContext();
      if (ctx) {
        ctx->startTool(new TextEditTool(textItem));
      }
    } else if (LeaderItem *leaderItem = dynamic_cast<LeaderItem *>(clickedItem)) {
      IAppContext *ctx = getContext();
      if (ctx) {
        ctx->startTool(new LeaderTextEditTool(leaderItem));
      }
    }
  }
}

void SelectTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
  Q_UNUSED(pos);
  Q_UNUSED(button);
  // Click-Move-Click 방식이므로 Release에서는 아무것도 하지 않음
}

void SelectTool::finishBoxSelection(const QPointF &secondCorner) {
    if (!m_scene)
        return;

    const bool isWindowMode = (secondCorner.x() > m_firstCorner.x());

    // [성능] 공통 헬퍼 사용 — 후보를 블록 단위로 먼저 접은 뒤 소수 항목만
    // geometryPath()/shape()로 판정한다. 예전에는 여기서 블록 자식 수천 개
    // 전부에 대해 기하 검사를 돌려, 도면을 여러 개 배치하면 확정 순간이
    // 감당 불가로 느려졌다.
    const QList<CadItem *> topItems =
        isWindowMode
            ? collectItemsInWindowRect(m_firstCorner, secondCorner)
            : collectItemsInCrossingRect(m_firstCorner, secondCorner);

    // [성능] 대량 setSelected 중 onSelectionChanged 폭주(O(n^2)) 억제
    m_scene->beginBulkSelectionChange();
    for (int i = 0; i < topItems.size(); ++i) { // 인덱스 기반 루프
        CadItem *item = topItems[i];
        if (item->type() == GripItem::Type)
            continue;
        item->setSelected(true);
    }
    m_scene->endBulkSelectionChange();

    m_boxSelectPending = false;
    m_hasLastBoxViewPos = false;
    endFastDragRender();
    clearOverlay();
}

void SelectTool::toggleSelection(QGraphicsItem *item) {
  if (!item)
    return;
  item->setSelected(!item->isSelected());
}

void SelectTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}

void SelectTool::beginFastDragRender() {
  if (m_fastDragRender || !m_scene)
    return;
  const QList<QGraphicsView *> views = m_scene->views();
  for (int i = 0; i < views.size(); ++i) {
    views[i]->setRenderHint(QPainter::Antialiasing, false);
    views[i]->setRenderHint(QPainter::SmoothPixmapTransform, false);
  }
  m_fastDragRender = true;
}

void SelectTool::endFastDragRender() {
  if (!m_fastDragRender)
    return;
  m_fastDragRender = false;
  if (!m_scene)
    return;
  const QList<QGraphicsView *> views = m_scene->views();
  for (int i = 0; i < views.size(); ++i) {
    views[i]->setRenderHint(QPainter::Antialiasing, true);
    views[i]->setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (views[i]->viewport())
      views[i]->viewport()->update(); // 마지막 저품질 프레임을 고품질로 재도색
  }
}

void SelectTool::cancelBoxSelection() {
  m_boxSelectPending = false;
  m_hasLastBoxViewPos = false;
  endFastDragRender();
  clearOverlay();
}

void SelectTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    if (m_activeGrip) {
      m_activeGrip = nullptr;
      clearOverlay();
    } else if (m_boxSelectPending) {
      cancelBoxSelection();
    } else {
      m_scene->clearSelection();
    }
  } else if (event->key() == Qt::Key_Delete) {
    QList<CadItem *> topSelected =
        getTopLevelCadItems(m_scene->selectedItems());
    if (!topSelected.isEmpty()) {
      IAppContext *ctx = getContext();
      if (ctx && ctx->undoStack()) {
        ctx->undoStack()->beginMacro("Delete Items");
        for (auto item : std::as_const(topSelected)) {
          ctx->undoStack()->push(new RemoveEntityCommand(m_scene, item));
        }
        ctx->undoStack()->endMacro();
      } else {
        qDeleteAll(topSelected);
      }
    }
  }

  // 부모(AbstractTool)에게 키 이벤트 전달 (F3, F8 등 처리를 위해)
  AbstractTool::keyPress(event);
}
