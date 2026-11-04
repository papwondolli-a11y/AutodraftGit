#include "mygraphicsscene.h"
#include "abstracttool.h"
#include "basedimensionitem.h"
#include "caditem.h"
#include "ghostoverlay.h"
#include "iappcontext.h"
#include "mygraphicsview.h"
#include "overlaymanager.h"
#include "thememanager.h"

#include <QDebug>
#include <QGraphicsRectItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPainter>
#include <QPen>
#include <QTimer>
#include <cmath>

MyGraphicsScene::MyGraphicsScene(QObject *parent)
    : QGraphicsScene(parent), m_currentTool(nullptr) {
  m_overlayManager  = new OverlayManager(this);
  m_symbolRegistry  = new SceneSymbolRegistry();
  setSceneRect(-1000000, -1000000, 2000000, 2000000);

  connect(this, &QGraphicsScene::selectionChanged, this,
          &MyGraphicsScene::onSelectionChanged);
}

MyGraphicsScene::~MyGraphicsScene() {
  disconnect(this, &QGraphicsScene::selectionChanged, this,
             &MyGraphicsScene::onSelectionChanged);
  delete m_symbolRegistry;
}

void MyGraphicsScene::setTool(AbstractTool *tool) {
  if (m_currentTool == tool)
    return;

  if (m_currentTool) {
    m_currentTool->deactivate();
  }

  m_currentTool = tool;

  if (m_currentTool) {
    m_currentTool->activate(this);
  }
}

void MyGraphicsScene::mousePressEvent(QGraphicsSceneMouseEvent *event) {
  if (m_currentTool) {
    m_currentTool->mousePress(event->scenePos(), event->button());
    event->accept();
  } else {
    QGraphicsScene::mousePressEvent(event);
  }
}

void MyGraphicsScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
  if (!event)
    return;
  QGraphicsScene::mouseMoveEvent(event);
  if (m_currentTool) {
    m_currentTool->mouseMove(event->scenePos());
  }
}

void MyGraphicsScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
  if (!event)
    return;
  if (m_currentTool) {
    m_currentTool->mouseRelease(event->scenePos(), event->button());
    event->accept();
  } else {
    QGraphicsScene::mouseReleaseEvent(event);
  }
}

void MyGraphicsScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
  if (!event)
    return;
  if (m_currentTool) {
    m_currentTool->mouseDoubleClick(event->scenePos(), event->button());
    event->accept();
  } else {
    QGraphicsScene::mouseDoubleClickEvent(event);
  }
}

void MyGraphicsScene::keyPressEvent(QKeyEvent *event) {
  if (!event)
    return;
  if (event->key() == Qt::Key_Escape) {
    if (m_currentTool) {
      m_currentTool->keyPress(event);
      return;
    }
  }
  if (focusItem() && focusItem()->type() == QGraphicsTextItem::Type) {
    QGraphicsScene::keyPressEvent(event);
    return;
  }
  if (m_currentTool) {
    m_currentTool->keyPress(event);
    event->accept();
  } else {
    QGraphicsScene::keyPressEvent(event);
  }
}

QGraphicsItem *MyGraphicsScene::getLayerRoot(const QString &layerName) {
  if (m_layerRoots.contains(layerName)) {
    return m_layerRoots.value(layerName);
  }
  QGraphicsRectItem *rootItem = new QGraphicsRectItem();
  rootItem->setFlag(QGraphicsItem::ItemHasNoContents);
  addItem(rootItem);
  m_layerRoots.insert(layerName, rootItem);
  return rootItem;
}

void MyGraphicsScene::reparentItemToLayer(QGraphicsItem *item,
                                          const QString &layerName) {
  if (!item)
    return;
  QGraphicsItem *newRoot = getLayerRoot(layerName);
  if (item->parentItem() != newRoot) {
    item->setParentItem(newRoot);
  }
}

void MyGraphicsScene::clearAll() {
  blockSignals(true);
  clear();
  m_layerRoots.clear();
  m_symbolRegistry->clear();
  m_docSettings.clear(); // [3-1] 새 로드 전 도면 스코프 기본값 초기화
  blockSignals(false);
}

SceneSymbolRegistry *MyGraphicsScene::symbolRegistry() const
{
    return m_symbolRegistry;
}

void MyGraphicsScene::setLayerVisible(const QString &layerName, bool visible) {
  if (m_layerRoots.contains(layerName)) {
    m_layerRoots.value(layerName)->setVisible(visible);
  }
}

void MyGraphicsScene::updateLayerItems(const QString &layerName,
                                       const QColor &color, double weight,
                                       const QString &linetype,
                                       const QVector<qreal> &dashPattern) {
    Q_UNUSED(linetype)
  m_layerStyleCache[layerName] = {color, weight, dashPattern};
  if (!m_layerRoots.contains(layerName))
    return;
  QGraphicsItem *root = m_layerRoots.value(layerName);
  QPen basePen(color);
  basePen.setWidthF(weight);
  basePen.setCosmetic(true);
  if (!dashPattern.isEmpty()) {
    basePen.setStyle(Qt::CustomDashLine);
    basePen.setDashPattern(dashPattern);
  } else {
    basePen.setStyle(Qt::SolidLine);
  }
  for (QGraphicsItem *item : root->childItems()) {
    if (CadItem *ci = dynamic_cast<CadItem *>(item)) {
      QPen p = basePen;
      ci->setPen(p);
    } else if (QGraphicsTextItem *ti =
                   dynamic_cast<QGraphicsTextItem *>(item)) {
      ti->setDefaultTextColor(color);
    }
  }
}

void MyGraphicsScene::setShowLineWeight(bool show) {
  if (m_showLineWeight != show) {
    m_showLineWeight = show;
    update();
  }
}

bool MyGraphicsScene::showLineWeight() const { return m_showLineWeight; }

void MyGraphicsScene::setShowOrigin(bool show) {
  if (m_showOrigin != show) {
    m_showOrigin = show;
    update();
  }
}

void MyGraphicsScene::setDrawingLimits(const QRectF &limits) {
  if (m_drawingLimits != limits) {
    m_drawingLimits = limits;
    m_overlayManager->setDrawingLimits(limits);
    emit drawingSettingsChanged();
    update();
  }
}

void MyGraphicsScene::setDrawingScale(double scale) {
  if (scale > 0 && m_drawingScale != scale) {
    m_drawingScale = scale;
    // setDimensionStyle과 동일하게 모든 치수선 캐시 무효화 및 재그리기
    const QList<QGraphicsItem *> allItems = items();
    for (int i = 0; i < allItems.size(); ++i) {
      if (BaseDimensionItem *dim = dynamic_cast<BaseDimensionItem *>(allItems[i])) {
        dim->invalidateBoundingRect();
        dim->update();
      }
    }
    emit drawingSettingsChanged();
  }
}

void MyGraphicsScene::setDrawingSizeName(const QString &name) {
  if (m_drawingSizeName != name) {
    m_drawingSizeName = name;
    emit drawingSettingsChanged();
  }
}

void MyGraphicsScene::setDimensionStyle(const DimensionStyle &style) {
  m_dimStyle = style;
  // 모든 치수선 아이템: boudningRect 캐시 무효화 후 재그리기
  // (type() 비교는 EntityType 열거형이 아닌 dynamic_cast로 판별해야 함)
  const QList<QGraphicsItem *> allItems = items();
  for (int i = 0; i < allItems.size(); ++i) {
    if (BaseDimensionItem *dim = dynamic_cast<BaseDimensionItem *>(allItems[i])) {
      dim->invalidateBoundingRect(); // 캐시 무효화 + prepareGeometryChange()
      dim->update();                 // 재그리기 요청
    }
  }
  emit drawingSettingsChanged();
}

// [Phase 2] 도면 길이 단위 — DocumentSettings("units/linear") 백킹.
namespace {
const QString kLinearUnitKey = QStringLiteral("units/linear");
}

LinearUnit MyGraphicsScene::linearUnit() const {
  return UnitConvert::fromString(
      m_docSettings.value(kLinearUnitKey, QStringLiteral("mm")).toString());
}

void MyGraphicsScene::setLinearUnit(LinearUnit u) {
  if (linearUnit() == u)
    return;
  // mm 는 기본값이므로 키를 지운다 → 단위를 건드린 적 없는 도면의 .cdb 는
  // doc_settings 블롭이 그대로 비어 바이트 동일하게 유지된다.
  if (u == LinearUnit::Mm)
    m_docSettings.remove(kLinearUnitKey);
  else
    m_docSettings.setValue(kLinearUnitKey, UnitConvert::toString(u));

  // 표시 단위만 바뀔 뿐 기하(mm)는 그대로. setDimensionStyle 과 동일하게
  // 모든 치수선 캐시를 무효화해 텍스트를 새 단위로 다시 계산하게 한다.
  const QList<QGraphicsItem *> allItems = items();
  for (int i = 0; i < allItems.size(); ++i) {
    if (BaseDimensionItem *dim = dynamic_cast<BaseDimensionItem *>(allItems[i])) {
      dim->invalidateBoundingRect();
      dim->update();
    }
  }
  emit drawingSettingsChanged();
  update();
}

// [성능] MyGraphicsView가 붙어 있으면 고스트/스냅/선택/대시 오버레이는 전부
// 뷰의 투명 오버레이 위젯(GhostOverlay)으로 직접 전달한다. 이 위젯만
// 갱신되고 QGraphicsScene/QGraphicsView는 전혀 건드리지 않으므로, 도면 크기와
// 무관하게 항상 가볍다. MyGraphicsView가 없는 폴백(인쇄 미리보기 등 일반
// QGraphicsView)은 성능이 민감하지 않으므로 기존 OverlayManager 경로를 그대로
// 쓴다.
static GhostOverlay *findGhostOverlay(const QList<QGraphicsView *> &views) {
  for (QGraphicsView *v : views) {
    if (MyGraphicsView *mv = qobject_cast<MyGraphicsView *>(v))
      return mv->ghostOverlay();
  }
  return nullptr;
}

void MyGraphicsScene::setGhostLine(const QLineF &sceneLine, bool visible,
                                   const QColor &color,
                                   const QVector<qreal> &dashPattern) {
  if (GhostOverlay *go = findGhostOverlay(views())) {
    go->setGhostLine(sceneLine, visible, color, dashPattern);
    return;
  }
  m_overlayManager->setGhostLine(sceneLine, visible, color, dashPattern);
  update();
}

void MyGraphicsScene::setGhostPath(const QPainterPath &scenePath, bool visible,
                                   const QColor &color, double width,
                                   const QVector<qreal> &dashPattern) {
  if (GhostOverlay *go = findGhostOverlay(views())) {
    go->setGhostPath(scenePath, visible, color, width, dashPattern);
    return;
  }
  m_overlayManager->setGhostPath(scenePath, visible, color, width, dashPattern);
  update();
}

void MyGraphicsScene::setGhostPixmap(const QPixmap &pixmap,
                                     const QPointF &scenePos,
                                     const QRectF &blockRect, double scale,
                                     double rotation, bool visible) {
  if (GhostOverlay *go = findGhostOverlay(views())) {
    go->setGhostPixmap(pixmap, scenePos, blockRect, scale, rotation, visible);
    return;
  }
  m_overlayManager->setGhostPixmap(pixmap, scenePos, blockRect, scale, rotation,
                                   visible);
  update();
}

void MyGraphicsScene::setSnapMarker(const QPointF &scenePos, SnapType type) {
  if (GhostOverlay *go = findGhostOverlay(views())) {
    go->setSnapMarker(scenePos, type);
    return;
  }
  m_overlayManager->setSnapMarker(scenePos, type);
  update();
}

void MyGraphicsScene::setSelectionRect(const QPointF &p1, const QPointF &p2,
                                       SelectionMode mode) {
  const QList<QGraphicsView *> vs = views();
  if (GhostOverlay *go = findGhostOverlay(vs)) {
    go->setSelectionRect(p1, p2, static_cast<GhostOverlay::SelectionMode>(mode));
    return;
  }

  // 폴백: 일반 QGraphicsView(프린트 미리보기 등) — 전경 레이어 부분 무효화.
  m_overlayManager->setSelectionRect(
      p1, p2, static_cast<OverlayManager::SelectionMode>(mode));
  QRectF dirty = m_overlayManager->selectionDirtyRect();
  double margin = 4.0;
  if (!vs.isEmpty()) {
    double sx = std::abs(vs.first()->transform().m11());
    if (sx > 1e-9)
      margin = 4.0 / sx;
  }
  invalidate(dirty.adjusted(-margin, -margin, margin, margin),
             QGraphicsScene::ForegroundLayer);
}

void MyGraphicsScene::setDashRect(const QPointF &p1, const QPointF &p2,
                                  const QColor &color) {
  if (GhostOverlay *go = findGhostOverlay(views())) {
    go->setDashRect(p1, p2, color);
    return;
  }
  m_overlayManager->setDashRect(p1, p2, color);
  update();
}

void MyGraphicsScene::clearOverlay() {
  if (GhostOverlay *go = findGhostOverlay(views())) {
    go->clear();
  }
  m_overlayManager->clear();
  update();
}

void MyGraphicsScene::drawForeground(QPainter *painter, const QRectF &rect) {
  Q_UNUSED(rect);
  m_overlayManager->draw(painter, m_showOrigin);
}

LayerStyle MyGraphicsScene::getLayerStyle(const QString &layerName) const {
  if (m_layerStyleCache.contains(layerName)) {
    return m_layerStyleCache.value(layerName);
  }
  
  // [개선] 대소문자 구분 없이 재검색 (DXF 호환성)
  auto it = m_layerStyleCache.begin();
  while (it != m_layerStyleCache.end()) {
      if (it.key().compare(layerName, Qt::CaseInsensitive) == 0) {
          return it.value();
      }
      ++it;
  }

  return {Qt::gray, 1.0, {}};
}

void MyGraphicsScene::renameLayerStyle(const QString &oldName,
                                       const QString &newName) {
  if (m_layerStyleCache.contains(oldName)) {
    m_layerStyleCache[newName] = m_layerStyleCache.take(oldName);
  }
}

void MyGraphicsScene::removeLayerStyle(const QString &layerName) {
  m_layerStyleCache.remove(layerName);
}

AbstractTool *MyGraphicsScene::currentTool() const { return m_currentTool; }

void MyGraphicsScene::savePreviousSelection() {
  m_previousSelection.clear();
  QList<QGraphicsItem *> sel = selectedItems();
  for (int i = 0; i < sel.size(); ++i) {
    if (CadItem *ci = dynamic_cast<CadItem *>(sel[i]))
      m_previousSelection.append(ci);
  }
}

bool MyGraphicsScene::applyPreviousSelection() {
  if (m_previousSelection.isEmpty())
    return false;

  // 현재 씬에 있는 아이템만 복원 (삭제된 아이템 제외)
  QList<QGraphicsItem *> allItems = items();
  QSet<QGraphicsItem *> itemSet(allItems.begin(), allItems.end());

  // 기존 선택은 유지하고 직전 선택 객체를 추가 (이후 추가/해제 선택 가능)
  bool anySelected = false;
  for (int i = 0; i < m_previousSelection.size(); ++i) {
    CadItem *ci = m_previousSelection[i];
    if (itemSet.contains(ci)) {
      ci->setSelected(true);
      anySelected = true;
    }
  }
  return anySelected;
}

void MyGraphicsScene::beginBulkSelectionChange() { ++m_bulkSelectionDepth; }

void MyGraphicsScene::endBulkSelectionChange() {
  if (m_bulkSelectionDepth > 0 && --m_bulkSelectionDepth == 0) {
    // [성능] Qt는 setSelected() 호출마다 selectionChanged를 emit한다. 구독자가
    // 여럿(이 클래스의 onSelectionChanged, DrawSubWindow::onSelectionChanged →
    // SelectionManager → PropertyDock 재구성)이라 대량 선택 시 O(n^2) + 속성창
    // 수천 회 재빌드가 된다. 대량 구간(isBulkSelectionChange()==true)에서는
    // 구독자들이 스스로 건너뛰고, 여기서 딱 한 번 통지한다.
    flushSelectionChanged();  // 자체 그립 갱신
    emit selectionChanged();  // 외부 구독자(DrawSubWindow 등)에게 1회 통지
  }
}

void MyGraphicsScene::onSelectionChanged() {
  // [성능] 대량 setSelected() 루프 중에는 항목마다 이 슬롯이 불린다. 예전에는
  // 매번 getTopLevelCadItems(selectedItems())를 돌려 O(n^2)이 되어, 도면을
  // 여러 개 복사해 둔 상태에서 move/copy/erase 등의 박스 선택이 감당 불가로
  // 느려졌다. 이제 실제 처리는 flushSelectionChanged()로 미뤄, 한 이벤트 루프
  // 턴에 몰린 신호를 딱 한 번만 처리한다.
  if (m_bulkSelectionDepth > 0)
    return; // begin/endBulkSelectionChange 구간: end에서 한 번 처리
  if (m_selectionFlushPending)
    return; // 이미 예약됨
  m_selectionFlushPending = true;
  QTimer::singleShot(0, this, &MyGraphicsScene::flushSelectionChanged);
}

void MyGraphicsScene::flushSelectionChanged() {
  m_selectionFlushPending = false;

  bool allowGrips = false;
  QList<CadItem *> topItems;
  if (m_currentTool) {
    topItems = m_currentTool->getTopLevelCadItems(selectedItems());
    if (m_currentTool->inherits("SelectTool")) {
      if (topItems.size() <= 5)
        allowGrips = true;
    }
  }
  for (CadItem *item : std::as_const(topItems)) {
    if (item)
      item->setGripsVisible(allowGrips && item->gripsEnabled());
  }
}
void MyGraphicsScene::setContext(IAppContext *ctx) {
    m_context = ctx;
}

IAppContext *MyGraphicsScene::context() const {
    return m_context;
}

void MyGraphicsScene::updateOverlayTheme(ThemeManager *tm) {
  if (!tm) return;
  bool isDark = (tm->currentTheme() == ThemeManager::ThemeType::Dark);
  QColor windowColor   = isDark ? QColor(100, 180, 255) : QColor(0, 80, 255);
  QColor crossingColor = isDark ? QColor(80, 255, 150)  : QColor(0, 160, 0);
  m_overlayManager->setSelectionColors(windowColor, crossingColor);
  m_defaultGhostColor = tm->ghostColor();
}