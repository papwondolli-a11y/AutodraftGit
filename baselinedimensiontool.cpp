#include "baselinedimensiontool.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"
#include "lineardimensionitem.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "snapmanager.h"
#include <QUndoStack>
#include <QtMath>


BaselineDimensionTool::BaselineDimensionTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitFirstOrigin), m_isDrawing(false) {}

BaselineDimensionTool::~BaselineDimensionTool() { cancelDrawing(); }

void BaselineDimensionTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;
  AbstractTool::activate(scene);
  m_state = WaitFirstOrigin;
  m_isDrawing = false;
  m_nextOrigins.clear();

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("DIMBASELINE", CommandDock::Command);
    ctx->commandDock()->showInteractivePrompt(
        tr(" 첫번째 치수 보조선 위치점을 지정해 주세요 (종료: 엔터,스페이스,마우스우측) : "), {});
  }
}

void BaselineDimensionTool::deactivate() {
  cancelDrawing();
  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setToolActive(false);
  }
  AbstractTool::deactivate();
}

void BaselineDimensionTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;
  IAppContext *ctx = getContext();

  if (button == Qt::RightButton) {
    if (m_state == WaitNextOrigins && !m_nextOrigins.isEmpty()) {
      m_state = WaitDimLinePoint;
      if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 치수선이 놓일 위치를 지정해 주세요 : "), {});
      }
      return;
    } else {
      onCancel();
      return;
    }
  }

  if (button != Qt::LeftButton)
    return;

  QPointF snappedPos = pos;
  if (ctx && ctx->snapManager()) {
    QPointF base =
        (m_state != WaitFirstOrigin) ? m_firstOrigin.toPointF() : QPointF();
    snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
  }

  if (m_state == WaitFirstOrigin) {
    m_firstOrigin = MyVector(snappedPos);
    m_state = WaitNextOrigins;
    m_isDrawing = true;
    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->showInteractivePrompt(
          tr(" 다음 치수보조선 위치점을 지정해 주세요 : "), {});
    }
  } else if (m_state == WaitNextOrigins) {
    m_nextOrigins.append(MyVector(snappedPos));
    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->showInteractivePrompt(
          tr(" 다음 치수보조선 위치점을 지정해 주세요 (위치점 지정 종료: 엔터,스페이스,마우스우측) : "), {});
    }
  } else if (m_state == WaitDimLinePoint) {
    finalizeDimensions(snappedPos);
    m_state = WaitFirstOrigin;
    m_isDrawing = false;
    m_nextOrigins.clear();
    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->showInteractivePrompt(
          tr(" 첫번재 치수 보조선 위치점을 지정해 주세요 (종료: 엔터,스페이스,마우스우측) : "), {});
    }
  }
}

void BaselineDimensionTool::mouseMove(QPointF pos) {
  m_currentMousePos = pos;
  IAppContext *ctx = getContext();

  QPointF finalPos = pos;
  SnapType snapType = SnapType::None;

  if (ctx && ctx->snapManager()) {
    QPointF base =
        (m_state != WaitFirstOrigin) ? m_firstOrigin.toPointF() : QPointF();
    SnapManager::SnapResult result =
        ctx->snapManager()->snapPoint(pos, m_scene, base);
    finalPos = result.snappedPos;
    snapType = result.type;
  }

  if (m_scene) {
    m_scene->setSnapMarker(finalPos, snapType);
  }

  if (m_isDrawing) {
    updateGhostPreview();
  }
}

void BaselineDimensionTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
  Q_UNUSED(pos);
  Q_UNUSED(button);
}

bool BaselineDimensionTool::processCommand(const QString &command) {
  QString cmd = command.trimmed().toLower();
  IAppContext *ctx = getContext();

  if (m_state == WaitFirstOrigin || m_state == WaitNextOrigins) {
    if (cmd.isEmpty()) {
      // [엔터/스페이스] 우클릭과 동일하게 지정 종료 로직 수행
      if (m_state == WaitNextOrigins && !m_nextOrigins.isEmpty()) {
        m_state = WaitDimLinePoint;
        if (ctx && ctx->commandDock()) {
          ctx->commandDock()->showInteractivePrompt(
              tr(" 치수선이 놓일 위치를 지정해 주세요 : "), {});
        }
      } else {
        onCancel();
      }
      return true;
    }
  }
  return false;
}

void BaselineDimensionTool::onFinish() {
  if (m_state == WaitDimLinePoint) {
    finalizeDimensions(m_currentMousePos);
  }
}

void BaselineDimensionTool::onCancel() {
  cancelDrawing();
  if (getContext() && getContext()->commandDock()) {
    getContext()->commandDock()->setPrompt(" Command : ");
  }
  emit toolFinished();
}

void BaselineDimensionTool::cancelDrawing() {
  m_isDrawing = false;
  m_state = WaitFirstOrigin;
  m_nextOrigins.clear();
  if (m_scene) {
    m_scene->clearOverlay();
  }
}

void BaselineDimensionTool::finalizeDimensions(const QPointF &lastPos) {
  if (!m_scene || m_nextOrigins.isEmpty())
    return;

  IAppContext *ctx = getContext();
  if (!ctx || !ctx->layerManager())
    return;

  DimensionStyle style = m_scene->dimensionStyle();
  Layer *curLayer = ctx->layerManager()->getCurrentLayer();

  QString targetLayerName = style.dimLineLayer;
  if (targetLayerName.isEmpty() ||
      !ctx->layerManager()->getLayer(targetLayerName)) {
    targetLayerName = curLayer->name();
  }
  Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);

  QPen pen(targetLayer->color());
  pen.setWidthF(targetLayer->weight());

  MyVector p1 = m_firstOrigin;
  MyVector p2_base = m_nextOrigins[0];
  double angle_base = MyMath::linearDimAngle(p1, p2_base, MyVector(lastPos));

  MyVector dir_base(qCos(qDegreesToRadians(angle_base)),
                    qSin(qDegreesToRadians(angle_base)));
  MyVector normal_base(-dir_base.y, dir_base.x);

  MyVector L = MyVector(lastPos);
  double h_start = MyMath::dotProduct(L - p1, normal_base);
  double spacing = style.baselineSpacing * m_scene->drawingScale();

  // 인덱스 기반 루프 사용
  for (int i = 0; i < m_nextOrigins.size(); ++i) {
    MyVector p2 = m_nextOrigins[i];

    double h = h_start;
    if (h_start >= 0) {
      h += i * spacing;
    } else {
      h -= i * spacing;
    }

    MyDimension data;
    data.dimensionType = DimensionType::Linear;
    data.start = p1;
    data.end = p2;
    data.dimLine = p1 + normal_base * h;
    data.useFixedAngle = true;
    data.fixedAngle = angle_base;
    data.hideExt1 = (i != m_nextOrigins.size() - 1);

    // 새 치수 생성 시 전역 스타일의 제로 억제 설정을 상속받음
    data.precision = style.precision;
    data.suppressTrailingZeros = style.suppressTrailingZeros;
    data.suppressTrailingZerosTol = style.suppressTrailingZerosTol;

    // Factory 패턴 대신 직접 LinearDimensionItem 객체 생성
    LinearDimensionItem *item = new LinearDimensionItem(data);
    item->setLayerName(targetLayer->name());
    item->setPen(pen);
    item->setGripsVisible(false);

    QGraphicsItem *layerRoot = m_scene->getLayerRoot(targetLayer->name());
    item->setParentItem(layerRoot);

    if (ctx->undoStack()) {
      ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
    } else {
      m_scene->addItem(item);
    }
  }

  m_scene->clearOverlay();
}

void BaselineDimensionTool::updateGhostPreview() {
  if (!m_isDrawing || !m_scene)
    return;

  QPainterPath combinedPath;
  DimensionStyle style = m_scene->dimensionStyle();
  double spacing = style.baselineSpacing * m_scene->drawingScale();

  MyVector p1 = m_firstOrigin;
  MyVector currentPos = MyVector(m_currentMousePos);

  if (m_state == WaitNextOrigins) {
    MyDimension ghostData(p1, currentPos, currentPos);
    ghostData.dimensionType = DimensionType::Linear;
    // 실시간 미리보기: 점과 마우스 위치에 따른 수평/수직 판단
    double dx = qAbs(m_currentMousePos.x() - p1.x);
    double dy = qAbs(m_currentMousePos.y() - p1.y);
    ghostData.useFixedAngle = true;
    ghostData.fixedAngle = (dy > dx) ? 0.0 : 90.0;
    LinearDimensionItem ghost(ghostData);
    combinedPath.addPath(ghost.geometryPath());

    // Qt Container Range-loop 경고 회피를 위한 인덱스 순회
    for (int i = 0; i < m_nextOrigins.size(); ++i) {
      MyVector p = m_nextOrigins[i];
      MyDimension d(p1, p, p);
      d.dimensionType = DimensionType::Linear;
      double dx = qAbs(m_currentMousePos.x() - p1.x);
      double dy = qAbs(m_currentMousePos.y() - p1.y);
      d.useFixedAngle = true;
      d.fixedAngle = (dy > dx) ? 0.0 : 90.0;
      LinearDimensionItem di(d);
      combinedPath.addPath(di.geometryPath());
    }
  } else if (m_state == WaitDimLinePoint) {
    if (!m_nextOrigins.isEmpty()) {
      MyVector p2_base = m_nextOrigins[0];
      double angle_base = MyMath::linearDimAngle(p1, p2_base, MyVector(m_currentMousePos));

      MyVector dir_base(qCos(qDegreesToRadians(angle_base)),
                        qSin(qDegreesToRadians(angle_base)));
      MyVector normal_base(-dir_base.y, dir_base.x);
      double h_start = MyMath::dotProduct(currentPos - p1, normal_base);

      for (int i = 0; i < m_nextOrigins.size(); ++i) {
        MyVector p2 = m_nextOrigins[i];
        double h =
            (h_start >= 0) ? (h_start + i * spacing) : (h_start - i * spacing);

        MyDimension d(p1, p2, p1 + normal_base * h);
        d.dimensionType = DimensionType::Linear;
        d.useFixedAngle = true;
        d.fixedAngle = angle_base;
        d.hideExt1 = (i != m_nextOrigins.size() - 1);

        LinearDimensionItem di(d);
        combinedPath.addPath(di.geometryPath());
      }
    }
  }

  m_scene->setGhostPath(combinedPath, true, Qt::yellow, 1.0);
}