#include "abstracttool.h"
#include "blockitem.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "detailregiondata.h"
#include "gripitem.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mycircle.h"
#include "mydimension.h"
#include "myentity.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QAbstractGraphicsShapeItem>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPen>
#include <QSet>
#include <QTimer>
#include <QtGlobal>

AbstractTool::AbstractTool(QObject *parent)
    : QObject(parent), m_scene(nullptr), m_appContext(nullptr),
      m_blockEnterKey(false), m_picker(nullptr, this) {}

AbstractTool::~AbstractTool() {}

void AbstractTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;

  m_scene = scene;
  m_picker.setScene(scene);  // CadPicker에 scene 주입
  // [핵심] 도구가 새로 시작될 때 명령어를 실행시킨 Enter/Space 키에 의해 즉시 종료되는 현상 방지
  blockEnterKeyTemporarily(250);
}

void AbstractTool::deactivate() {
  if (m_appContext && m_appContext->commandDock()) {
    m_appContext->commandDock()->setPrompt(tr(" Command : "));
    m_appContext->commandDock()->setToolActive(false);
  }
  m_picker.setScene(nullptr);  // 단엌 후 scene 해제
  m_scene = nullptr;
}

void AbstractTool::detachFromScene(MyGraphicsScene *scene) {
  if (scene && m_scene == scene)
    deactivate();
}

void AbstractTool::hideGripsOfSelectedItems() {
  if (!m_scene)
    return;

  // 도구 전환 시 '선택된 객체'의 그립만 숨김 (전체 객체 순회보다 효율적)
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

void AbstractTool::blockEnterKeyTemporarily(int msec) {
  m_blockEnterKey = true;
  // 사용자 규칙 준수: 람다 대신 멤버 함수 포인터 사용
  QTimer::singleShot(msec, this, &AbstractTool::unblockEnterKey);
}

void AbstractTool::unblockEnterKey() {
  m_blockEnterKey = false;
}

void AbstractTool::setContext(IAppContext *ctx) { m_appContext = ctx; }

IAppContext *AbstractTool::getContext() const { return m_appContext; }

bool AbstractTool::processCommand(const QString &command) {
  Q_UNUSED(command);
  return false;
}

void AbstractTool::mousePress(QPointF pos, Qt::MouseButton button) {
  Q_UNUSED(pos);
  Q_UNUSED(button);
}

void AbstractTool::mouseMove(QPointF pos) { Q_UNUSED(pos); }

void AbstractTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
  Q_UNUSED(pos);
  Q_UNUSED(button);
}

void AbstractTool::mouseDoubleClick(QPointF pos, Qt::MouseButton button) {
  Q_UNUSED(pos);
  Q_UNUSED(button);
}

void AbstractTool::keyPress(QKeyEvent *event) {
  IAppContext *ctx = getContext();

  if (!ctx || !ctx->snapManager())
    return;

  class SnapManager *snap = ctx->snapManager();

  if (event->key() == Qt::Key_F3) {
    snap->toggleOSnap();
    ctx->showStatusText(
        QString("Object Snap: %1").arg(snap->isOSnap() ? "ON" : "OFF"));
    return;
  } else if (event->key() == Qt::Key_F8) {
    snap->toggleOrtho();
    ctx->showStatusText(
        QString("Ortho Mode: %1").arg(snap->isOrtho() ? "ON" : "OFF"));
    return;
  }

  if (event->modifiers() & Qt::AltModifier) {
    switch (event->key()) {
    case Qt::Key_E:
      snap->toggleSnapEndpoint();
      ctx->showStatusText(QString("Snap Endpoint: %1")
                              .arg(snap->isSnapEndpoint() ? "ON" : "OFF"));
      break;
    case Qt::Key_M:
      snap->toggleSnapMidpoint();
      ctx->showStatusText(QString("Snap Midpoint: %1")
                              .arg(snap->isSnapMidpoint() ? "ON" : "OFF"));
      break;
    case Qt::Key_C:
      snap->toggleSnapCenter();
      ctx->showStatusText(
          QString("Snap Center: %1").arg(snap->isSnapCenter() ? "ON" : "OFF"));
      break;
    case Qt::Key_I:
      snap->toggleSnapIntersection();
      ctx->showStatusText(QString("Snap Intersection: %1")
                              .arg(snap->isSnapIntersection() ? "ON" : "OFF"));
      break;
    case Qt::Key_Q:
      snap->toggleSnapQuadrant();
      ctx->showStatusText(QString("Snap Quadrant: %1")
                              .arg(snap->isSnapQuadrant() ? "ON" : "OFF"));
      break;
    case Qt::Key_N:
      snap->toggleSnapNearest();
      ctx->showStatusText(QString("Snap Nearest: %1")
                              .arg(snap->isSnapNearest() ? "ON" : "OFF"));
      break;
    case Qt::Key_P:
      snap->toggleSnapPerpendicular();
      ctx->showStatusText(QString("Snap Perpendicular: %1")
                              .arg(snap->isSnapPerpendicular() ? "ON" : "OFF"));
      break;
    default:
      break;
    }
  }

  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
      event->key() == Qt::Key_Space) {
    if (m_blockEnterKey)
      return;

    // [핵심 보완] 엔터/스페이스 시 먼저 도구에게 명령어로 처리할 기회를 줌 (기본값 수락 등)
    if (processCommand("")) {
      return; // 도구가 빈 입력을 내부적으로 처리했으면 종료하지 않음
    }

    onFinish();
    emit toolFinished();
    return;
  }

  if (event->key() == Qt::Key_Escape) {
    onCancel();
    emit toolFinished();
    return;
  }
}

void AbstractTool::updateGhostPreview(const QPainterPath &path,
                                      const QString &layerName) {
  if (!m_scene)
    return;

  if (layerName.isEmpty()) {
    m_scene->setGhostPath(path, true, m_scene->defaultGhostColor(), 1.0, {});
  } else {
    // 특정 레이어 스타일 적용
    LayerStyle style = m_scene->getLayerStyle(layerName);
    QColor ghostColor = style.color;
    ghostColor.setAlpha(127); // 50% 투명도
    m_scene->setGhostPath(path, true, ghostColor, 1.0, style.dashPattern);
  }
}

void AbstractTool::updateGhostLinePreview(const QLineF &line,
                                          const QString &layerName) {
  if (!m_scene)
    return;

  if (layerName.isEmpty()) {
    m_scene->setGhostLine(line, true, m_scene->defaultGhostColor(), {});
  } else {
    LayerStyle style = m_scene->getLayerStyle(layerName);
    QColor ghostColor = style.color;
    ghostColor.setAlpha(127);
    m_scene->setGhostLine(line, true, ghostColor, style.dashPattern);
  }
}

QString AbstractTool::currentLayerName() const {
  IAppContext *ctx = getContext();
  if (!ctx)
    return "";
  auto lm = ctx->layerManager();
  if (lm && lm->getCurrentLayer()) {
    return lm->getCurrentLayer()->name();
  }
  return "Default";
}

QPen AbstractTool::currentLayerPen() const {
  return layerPenFor(getContext(), QString());
}

QPen AbstractTool::getLayerPen(const QString &layerName) const {
  if (layerName.isEmpty())
    return QPen(Qt::white, 0);
  return layerPenFor(getContext(), layerName);
}

QPen AbstractTool::layerPenFor(IAppContext *ctx, const QString &layerName) {
  if (!ctx || !ctx->layerManager())
    return QPen(Qt::white, 0);

  Layer *layer = layerName.isEmpty()
                     ? nullptr
                     : ctx->layerManager()->getLayer(layerName);
  if (!layer)
    layer = ctx->layerManager()->getCurrentLayer();
  if (!layer)
    return QPen(Qt::white, 0);

  QPen pen(layer->color());
  pen.setWidthF(layer->weight());

  if (ctx->lineTypeManager()) {
    QVector<qreal> pattern =
        ctx->lineTypeManager()->getQtDashPattern(layer->linetype());
    if (!pattern.isEmpty()) {
      pen.setStyle(Qt::CustomDashLine);
      pen.setDashPattern(pattern);
    } else {
      pen.setStyle(Qt::SolidLine);
    }
  }
  pen.setCosmetic(true);
  return pen;
}

QString AbstractTool::resolveLayerName(IAppContext *ctx,
                                       const QString &layerName) {
  if (!ctx || !ctx->layerManager())
    return layerName.isEmpty() ? QString("Default") : layerName;

  LayerManager *lm = ctx->layerManager();
  if (!layerName.isEmpty() && lm->getLayer(layerName))
    return layerName;
  if (lm->getCurrentLayer())
    return lm->getCurrentLayer()->name();
  return layerName.isEmpty() ? QString("Default") : layerName;
}

QString
AbstractTool::getCommonLayerName(const QList<QGraphicsItem *> &items) const {
  if (items.isEmpty())
    return "";

  QString commonName = "";
  bool first = true;

  for (int i = 0; i < items.size(); ++i) {
    QGraphicsItem *item = items.at(i);
    if (CadItem *ci = dynamic_cast<CadItem *>(item)) {
      if (first) {
        commonName = ci->layerName();
        first = false;
      } else if (ci->layerName() != commonName) {
        return ""; // 레이어 섞임
      }
    }
  }
  return commonName;
}

QGraphicsItem *AbstractTool::itemAtPos(const QPointF &pos) {
  if (!m_scene)
    return nullptr;

  double tolerance = 5.0;
  if (!m_scene->views().isEmpty()) {
    auto view = m_scene->views().constFirst();
    double scale = view->viewportTransform().m11();
    if (std::abs(scale) > 0.0001) {
      tolerance = 5.0 / std::abs(scale);
    }
  }

  // m_picker에 scene이 주입되어 있으므로 인스턴스 메서드로 호출·
  // 타입 우선순위(HatchItem 최하위)와 거리 기준으로 최적의 객체를 선택합니다.
  return m_picker.pick(pos, tolerance);
}

QList<CadItem *>
AbstractTool::getTopLevelCadItems(const QList<QGraphicsItem *> &items) const {
  QList<CadItem *> result;
  // [성능] 예전에는 result.contains()(선형 탐색)로 중복을 걸러 선택 객체가
  // 많을 때 O(n^2)이 되어, 도면을 여러 개 복사해 둔 상태에서 move/copy 선택이
  // 감당 불가로 느려졌다. QSet으로 O(n) 중복 제거.
  QSet<CadItem *> seen;
  seen.reserve(items.size());
  for (int i = 0; i < items.size(); ++i) {
    QGraphicsItem *item = items.at(i);
    if (!item || !item->isVisible() || item->type() == GripItem::Type)
      continue;

    QGraphicsItem *topItem = item;
    while (topItem->parentItem() &&
           dynamic_cast<CadItem *>(topItem->parentItem())) {
      topItem = topItem->parentItem();
    }

    if (CadItem *ci = dynamic_cast<CadItem *>(topItem)) {
      if (!seen.contains(ci)) {
        seen.insert(ci);
        result.append(ci);
      }
    }
  }
  return result;
}

// ──────────────────────────────────────────────────────────────
// [성능] 사각형에 걸치는 후보를 "유일한 최상위 CadItem"으로 먼저 접는다.
// items()는 블록 자식 수천 개를 전부 돌려주므로, 기하 검사(geometryPath/shape)
// 전에 블록 단위로 축약해야 확정 순간이 도면 개수에 비례해 느려지지 않는다.
// ──────────────────────────────────────────────────────────────
QList<CadItem *>
AbstractTool::topLevelCandidatesInRect(const QRectF &sceneRect) const {
  QList<CadItem *> tops;
  if (!m_scene)
    return tops;

  const QList<QGraphicsItem *> raw =
      m_scene->items(sceneRect, Qt::IntersectsItemBoundingRect);

  QSet<CadItem *> seen;
  seen.reserve(64);
  for (int i = 0; i < raw.size(); ++i) {
    QGraphicsItem *it = raw.at(i);
    if (!it || !it->isVisible() || it->type() == GripItem::Type)
      continue;

    // 최상위 CadItem으로 올라감 (블록 자식 → 블록)
    QGraphicsItem *topItem = it;
    while (topItem->parentItem() &&
           dynamic_cast<CadItem *>(topItem->parentItem())) {
      topItem = topItem->parentItem();
    }
    CadItem *ci = dynamic_cast<CadItem *>(topItem);
    if (!ci || seen.contains(ci))
      continue;
    seen.insert(ci);
    tops.append(ci);
  }
  return tops;
}

// ──────────────────────────────────────────────────────────────
// Window 선택 공통 헬퍼 (SelectTool.finishBoxSelection과 동일한 기준)
// ──────────────────────────────────────────────────────────────
QList<CadItem *> AbstractTool::collectItemsInWindowRect(const QPointF &p1,
                                                        const QPointF &p2) const {
  if (!m_scene) return QList<CadItem *>();

  QRectF selRect = QRectF(p1, p2).normalized();

  // ① 후보를 유일 최상위 CadItem으로 축약
  const QList<CadItem *> tops = topLevelCandidatesInRect(selRect);

  // ② 완전 포함 판정. [성능] mapToScene(전체 geometryPath)는 해치처럼 경로가
  //    수천 요소일 때 항목마다 새 경로를 할당한다. 로컬 boundingRect를 먼저
  //    구한 뒤 그 사각형만 변환하면 할당 없이 동일한 판정이 된다.
  QList<CadItem *> result;
  for (int i = 0; i < tops.size(); ++i) {
    CadItem *cadItem = tops.at(i);
    QRectF localBBox = cadItem->geometryPath().boundingRect();
    QRectF itemRect = cadItem->sceneTransform().mapRect(localBBox);
    // geometryPath는 선 두께를 포함하지 않으므로 여유 없이 판정한다.
    // (예전 ±0.5 여유는 짧은 사선이 선택 상자보다 작아도 탈락시켰음)
    // 부동소수 오차만 허용해 상자 경계에 정확히 걸친 객체도 선택.
    const double eps = 1e-6;
    if (itemRect.left() >= selRect.left() - eps &&
        itemRect.right() <= selRect.right() + eps &&
        itemRect.top() >= selRect.top() - eps &&
        itemRect.bottom() <= selRect.bottom() + eps)
      result.append(cadItem);
  }
  return result;
}

// 선분 a-b가 사각형 r과 만나는지 (Liang-Barsky 클리핑)
static bool segmentIntersectsRect(const QPointF &a, const QPointF &b,
                                  const QRectF &r) {
  double t0 = 0.0, t1 = 1.0;
  const double dx = b.x() - a.x();
  const double dy = b.y() - a.y();
  const double p[4] = {-dx, dx, -dy, dy};
  const double q[4] = {a.x() - r.left(), r.right() - a.x(),
                       a.y() - r.top(), r.bottom() - a.y()};
  for (int i = 0; i < 4; ++i) {
    if (qFuzzyIsNull(p[i])) {
      if (q[i] < 0.0)
        return false;
    } else {
      const double t = q[i] / p[i];
      if (p[i] < 0.0) {
        if (t > t1) return false;
        if (t > t0) t0 = t;
      } else {
        if (t < t0) return false;
        if (t < t1) t1 = t;
      }
    }
  }
  return true;
}

// ──────────────────────────────────────────────────────────────
// Crossing 선택 공통 헬퍼
// ──────────────────────────────────────────────────────────────
QList<CadItem *> AbstractTool::collectItemsInCrossingRect(const QPointF &p1,
                                                          const QPointF &p2) const {
  if (!m_scene) return QList<CadItem *>();

  QRectF selRect = QRectF(p1, p2).normalized();

  // ① 후보를 유일 최상위 CadItem으로 축약 (이미 boundingRect 교차로 걸러짐)
  const QList<CadItem *> tops = topLevelCandidatesInRect(selRect);

  // ② [성능] 크로싱 = "조금이라도 걸치면 선택"이므로 boundingRect 교차만으로
  //    충분하다. 예전에는 후보 수천 개마다 shape()(치수·스플라인·해치는 매번
  //    스트로크) + mapToScene(전체 경로) + QPainterPath::intersects(경로-경로
  //    교차)를 돌려, 도면을 여러 개 배치하면 확정에 7초+가 걸렸다.
  //    유일한 예외: 선택 상자가 아이템 경계상자 "안에" 완전히 들어간 경우
  //    (속 빈 큰 도형은 상자가 아무것도 안 스칠 수 있음)만 정밀 검사한다.
  //    이런 항목은 보통 0~수 개뿐이라 비용이 무시할 수준이다.
  QList<CadItem *> result;
  result.reserve(tops.size());
  QPainterPath selPath;
  bool selPathReady = false;
  for (int i = 0; i < tops.size(); ++i) {
    CadItem *cadItem = tops.at(i);
    const QRectF r = cadItem->sceneBoundingRect();

    // 직선 요소만으로 이뤄진 소형 경로(선, 폴리라인, 사각형 등)는 경계상자가
    // 아니라 실제 선분과 선택 상자의 교차로 판정한다. (사선은 경계상자가 커서
    // 상자가 선에 닿지 않아도 걸치므로 이웃 사선까지 선택되던 문제 방지)
    {
      const QPainterPath gp = cadItem->geometryPath();
      const int n = gp.elementCount();
      bool lineOnly = (n >= 2 && n <= 64);
      for (int k = 0; lineOnly && k < n; ++k) {
        const QPainterPath::Element e = gp.elementAt(k);
        if (e.type == QPainterPath::CurveToElement ||
            e.type == QPainterPath::CurveToDataElement)
          lineOnly = false;
      }
      if (lineOnly) {
        const QTransform tr = cadItem->sceneTransform();
        bool hit = false;
        QPointF prev;
        for (int k = 0; k < n && !hit; ++k) {
          const QPainterPath::Element e = gp.elementAt(k);
          const QPointF cur = tr.map(QPointF(e.x, e.y));
          if (e.type != QPainterPath::MoveToElement &&
              segmentIntersectsRect(prev, cur, selRect))
            hit = true;
          prev = cur;
        }
        if (hit)
          result.append(cadItem);
        continue;
      }
    }

    if (!r.contains(selRect)) {
      // 경계상자가 선택 상자와 부분 겹침/교차 → 채택 (광범위 크로싱)
      result.append(cadItem);
      continue;
    }

    // 선택 상자가 아이템 경계상자에 완전히 포함됨 → 정밀 교차 검사
    if (!selPathReady) {
      selPath.addRect(selRect);
      selPathReady = true;
    }
    const QPainterPath sp = cadItem->mapToScene(cadItem->shape());
    if (sp.intersects(selPath))
      result.append(cadItem);
  }
  return result;
}

// ──────────────────────────────────────────────────────────────
// 상세도 영역(경계원) 안에서 생성되는 치수의 측정 배율 자동 설정
// ──────────────────────────────────────────────────────────────
bool AbstractTool::applyDetailRegionScale(MyDimension &dim) const {
  if (!m_scene)
    return false;

  QList<QPointF> pts;
  pts << dim.start.toPointF() << dim.end.toPointF();
  if (dim.dimensionType == DimensionType::Angular)
    pts << dim.end2.toPointF();

  const QList<QGraphicsItem *> all = m_scene->items();
  for (int i = 0; i < all.size(); ++i) {
    CadItem *ci = dynamic_cast<CadItem *>(all.at(i));
    if (!ci)
      continue;
    const QVariant sv = ci->data(DETAIL_REGION_SCALE);
    if (!sv.isValid())
      continue;
    const double regScale = sv.toDouble();
    if (regScale <= 0.0)
      continue;
    const MyEntity *e = ci->getEntityData();
    if (!e || e->type() != EntityType::Circle)
      continue;
    const MyCircle *c = static_cast<const MyCircle *>(e);
    const double r2 = c->radius * c->radius + c->radius * 1e-4;

    bool allIn = true;
    for (int k = 0; k < pts.size(); ++k) {
      const double dx = pts.at(k).x() - c->center.x;
      const double dy = pts.at(k).y() - c->center.y;
      if (dx * dx + dy * dy > r2) {
        allIn = false;
        break;
      }
    }
    if (allIn) {
      dim.measureScale = 1.0 / regScale;
      return true;
    }
  }
  return false;
}
