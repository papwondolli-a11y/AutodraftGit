#include "areadimensiontool.h"
#include "caditem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"
#include "lineardimensionitem.h"
#include "myline.h"
#include "mypolyline.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QUndoStack>
#include <QtMath>
#include <algorithm>

namespace {
// 좌표 병합/포함 판정에 쓰는 허용 오차
constexpr double kEps = 1e-3;
}

AreaDimensionTool::AreaDimensionTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitSymbolOption), m_isDrawing(false),
      m_useDiameterSymbol(true), m_verticalDims(true) {}

AreaDimensionTool::~AreaDimensionTool() { cancelDrawing(); }

void AreaDimensionTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitSymbolOption;
    m_isDrawing = false;
    m_useDiameterSymbol = true;
    m_verticalDims = true;
    m_slabs.clear();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("AREADIM", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(
            tr(" 파이(Ø) 기호를 사용하시겠습니까? [Y/N] <Y> : "), {"Y", "N"});
    }
}

void AreaDimensionTool::deactivate() {
    cancelDrawing();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setToolActive(false);
    AbstractTool::deactivate();
}

bool AreaDimensionTool::processCommand(const QString &command) {
    if (m_state != WaitSymbolOption) return false;

    QString cmd = command.trimmed().toLower();
    m_useDiameterSymbol = (cmd != "n");

    m_state = WaitP1;
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(
            tr(" 영역의 첫 번째 점을 지정하세요 [P1] : "), {});
    return true;
}

void AreaDimensionTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) { onCancel(); return; }
    if (button != Qt::LeftButton)  return;
    if (m_state == WaitSymbolOption) return; // 텍스트로만 응답

    QPointF snapped = resolvePoint(pos);

    if (m_state == WaitP1) {
        m_p1 = MyVector(snapped);
        m_state = WaitP2;
        m_isDrawing = true;
        if (ctx && ctx->commandDock())
            ctx->commandDock()->showInteractivePrompt(
                tr(" 영역의 두 번째 점을 지정하세요 [P2] : "), {});

    } else if (m_state == WaitP2) {
        if (m_p1.distanceTo(MyVector(snapped)) < kEps) return;
        m_p2 = MyVector(snapped);
        computeSlabs();

        if (m_slabs.isEmpty()) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(
                    tr(" 영역 내에서 단(step) 형상을 찾을 수 없습니다."), CommandDock::Command);
            cancelDrawing();
            return;
        }

        if (ctx && ctx->commandDock()) {
            // [진단용] 검출된 단의 개수/방향/span 값을 출력해
            // 형상 인식이 의도대로 됐는지 확인할 수 있게 한다.
            QStringList spans;
            for (const StepSlab &s : m_slabs)
                spans << QString::number(s.span(), 'f', 3);
            ctx->commandDock()->logMessage(
                tr(" %1개 단 검출됨 [%2] (span: %3)")
                    .arg(m_slabs.size())
                    .arg(m_verticalDims ? tr("세로 치수") : tr("가로 치수"))
                    .arg(spans.join(", ")),
                CommandDock::Command);
        }

        // P2가 확정되면 영역 선택용 점선 박스는 더 이상 필요 없으므로 즉시 지운다.
        if (m_scene) m_scene->clearOverlay();

        m_state = WaitP3;
        if (ctx && ctx->commandDock())
            ctx->commandDock()->showInteractivePrompt(
                tr(" 치수선이 놓일 위치를 지정하세요 [P3] : "), {});

    } else if (m_state == WaitP3) {
        finalizeDimensions(snapped);
    }
}

void AreaDimensionTool::mouseMove(QPointF pos) {
    SnapType snapType = SnapType::None;
    // 미리보기와 클릭 확정이 항상 같은 좌표를 쓰도록 동일한 함수로 해석한다.
    m_currentMousePos = resolvePoint(pos, &snapType);

    if (m_scene) m_scene->setSnapMarker(m_currentMousePos, snapType);
    if (m_isDrawing) updateGhostPreview();
}

// ─────────────────────────────────────────────────────────────────────────────
// 상태별 입력점 해석.
// P1/P2(영역 모서리): 스냅/직교를 적용하지 않는다. 객체 스냅이 근처의 끝점·
//   중간점·중심선 끝점 등으로 모서리를 끌어가거나, 직교(F8/Shift)가 P2를 P1과
//   같은 높이/폭으로 눌러 영역이 의도보다 작아지면(두께 0 포함) 바깥 윤곽선이
//   영역 필터에서 빠져 "단을 찾을 수 없음"이 간헐적으로 발생했다. 화면의 점선
//   박스(원 좌표)와 실제 계산 영역(스냅 좌표)이 달랐던 것도 같은 원인이다.
// P3(치수선 위치): 객체 스냅만 허용하고, P1 기준 직교는 적용하지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
QPointF AreaDimensionTool::resolvePoint(const QPointF &pos, SnapType *outType) const {
    if (outType) *outType = SnapType::None;
    if (m_state != WaitP3 || !m_scene) return pos;

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->snapManager()) return pos;

    SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene, QPointF());
    if (!res.isValid) return pos;
    if (outType) *outType = res.type;
    return res.snappedPos;
}

void AreaDimensionTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos); Q_UNUSED(button);
}

void AreaDimensionTool::onFinish() {
    if (m_state == WaitP3)
        finalizeDimensions(m_currentMousePos);
}

void AreaDimensionTool::onCancel() {
    cancelDrawing();
    m_state = WaitSymbolOption;
    if (getContext() && getContext()->commandDock())
        getContext()->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

void AreaDimensionTool::cancelDrawing() {
    m_isDrawing = false;
    m_slabs.clear();
    if (m_scene) m_scene->clearOverlay();
}

// ─────────────────────────────────────────────────────────────────────────────
// boundarySegs(단 경계 후보 선분들)의 pos 값으로 슬랩 경계를 나누고, 각 슬랩
// 중앙에서 measureSegs(측정용 선분들)가 덮는 범위의 최소/최대값을 그 슬랩의
// 치수값(measLo~measHi)으로 삼는다.
// regionBoundLo/Hi(사용자가 지정한 영역 자체의 경계)도 항상 슬랩 경계 후보에
// 포함시켜, 실제 객체의 가장 바깥쪽 경계선이 영역 밖으로 이어지거나 영역
// 경계와 정확히 겹쳐 검출되지 않는 경우에도 가장 바깥 단의 치수를 만들 수
// 있게 한다.
// ─────────────────────────────────────────────────────────────────────────────
QList<AreaDimensionTool::StepSlab> AreaDimensionTool::buildSlabsFromAxis(
    const QList<AxisSeg> &boundarySegs, const QList<AxisSeg> &measureSegs,
    double regionBoundLo, double regionBoundHi) const {

    QList<StepSlab> result;
    if (boundarySegs.isEmpty() || measureSegs.isEmpty()) return result;

    QList<double> positions;
    for (const AxisSeg &b : boundarySegs) positions.append(b.pos);
    positions.append(regionBoundLo);
    positions.append(regionBoundHi);
    std::sort(positions.begin(), positions.end());

    QList<double> boundaries;
    for (double p : positions) {
        if (boundaries.isEmpty() || p - boundaries.last() > kEps)
            boundaries.append(p);
    }
    if (boundaries.size() < 2) return result;

    QList<StepSlab> raw;
    for (int i = 0; i + 1 < boundaries.size(); ++i) {
        double lo = boundaries[i];
        double hi = boundaries[i + 1];
        double mid = (lo + hi) * 0.5;

        double measLo = 0.0, measHi = 0.0;
        bool found = false;
        for (const AxisSeg &m : measureSegs) {
            if (mid < m.lo - kEps || mid > m.hi + kEps) continue;
            if (!found) { measLo = measHi = m.pos; found = true; }
            else { measLo = qMin(measLo, m.pos); measHi = qMax(measHi, m.pos); }
        }
        if (!found || (measHi - measLo) <= kEps) continue;

        raw.append({lo, hi, measLo, measHi});
    }

    // 동일한 치수값(measLo,measHi)을 갖는 슬랩은 인접 여부와 무관하게 하나로
    // 병합한다. 중첩/적층된(예: 계단식·웨딩케이크형) 형상은 대칭 구조상 같은
    // 높이의 단이 좌우 여러 구간에서 반복 검출되는데, 이를 병합하지 않으면
    // 동일한 값의 치수선이 여러 개 겹쳐 생성된다.
    for (const StepSlab &s : raw) {
        bool merged = false;
        for (StepSlab &existing : result) {
            if (std::abs(existing.measLo - s.measLo) < kEps &&
                std::abs(existing.measHi - s.measHi) < kEps) {
                existing.boundLo = qMin(existing.boundLo, s.boundLo);
                existing.boundHi = qMax(existing.boundHi, s.boundHi);
                merged = true;
                break;
            }
        }
        if (!merged) result.append(s);
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// P1~P2 영역 안의 선분(Line/Polyline)을 모아 단(step) 구간을 계산한다.
// 단이 X축으로 배열된 형상(세로 치수)과 Y축으로 배열된 형상(가로 치수)을
// 모두 시도해본 뒤, 유효한 단이 더 많이(동률이면 span 합이 더 크게) 검출되는
// 쪽을 최종적으로 채택한다.
// ─────────────────────────────────────────────────────────────────────────────
void AreaDimensionTool::computeSlabs() {
    m_slabs.clear();
    m_verticalDims = true;
    if (!m_scene) return;

    // Window(완전 포함) 대신 Crossing(교차) 선택을 사용한다.
    // 완전 포함 판정은 ±0.5 보정 때문에 선택 영역 경계에 정확히 걸친 선(가장
    // 바깥쪽 단의 외곽선 등)이 제외되는 문제가 있었다.
    QList<CadItem *> items = collectItemsInCrossingRect(m_p1.toPointF(), m_p2.toPointF());
    if (items.isEmpty()) return;

    QList<AxisSeg> verticals;   // x = pos, y 범위 [lo,hi]
    QList<AxisSeg> horizontals; // y = pos, x 범위 [lo,hi]

    auto addSegment = [&](const MyVector &a, const MyVector &b) {
        if (std::abs(a.x - b.x) < kEps && std::abs(a.y - b.y) >= kEps) {
            verticals.append({a.x, qMin(a.y, b.y), qMax(a.y, b.y)});
        } else if (std::abs(a.y - b.y) < kEps && std::abs(a.x - b.x) >= kEps) {
            horizontals.append({a.y, qMin(a.x, b.x), qMax(a.x, b.x)});
        }
    };

    for (CadItem *item : items) {
        const MyEntity *ent = item->getEntityData();
        if (!ent) continue;

        if (ent->type() == EntityType::Line) {
            const MyLine *line = static_cast<const MyLine *>(ent);
            addSegment(line->start, line->end);
        } else if (ent->type() == EntityType::Polyline) {
            const MyPolyline *poly = static_cast<const MyPolyline *>(ent);
            const int n = static_cast<int>(poly->vertices.size());
            for (int i = 0; i + 1 < n; ++i)
                addSegment(poly->vertices[i], poly->vertices[i + 1]);
            if (poly->isClosed && n > 1)
                addSegment(poly->vertices[n - 1], poly->vertices[0]);
        }
    }

    // Crossing 선택은 영역을 살짝 벗어나는 선분도 주워올 수 있으므로,
    // 실제로 영역의 X/Y 범위 안(약간의 여유 포함)에 있는 것만 남긴다.
    QRectF regionRect = QRectF(m_p1.toPointF(), m_p2.toPointF()).normalized();
    const double margin = qMax(kEps, (regionRect.width() + regionRect.height()) * 0.001);
    {
        QList<AxisSeg> filteredV, filteredH;
        for (const AxisSeg &v : verticals)
            if (v.pos >= regionRect.left() - margin && v.pos <= regionRect.right() + margin)
                filteredV.append(v);
        for (const AxisSeg &h : horizontals)
            if (h.pos >= regionRect.top() - margin && h.pos <= regionRect.bottom() + margin)
                filteredH.append(h);
        verticals = filteredV;
        horizontals = filteredH;
    }

    if (verticals.isEmpty() || horizontals.isEmpty()) return;

    // 후보 1: 단이 X축으로 배열 -> 세로선(verticals)이 슬랩 경계, 가로선(horizontals)이 치수값
    QList<StepSlab> candidateVertical =
        buildSlabsFromAxis(verticals, horizontals, regionRect.left(), regionRect.right());
    // 후보 2: 단이 Y축으로 배열 -> 가로선(horizontals)이 슬랩 경계, 세로선(verticals)이 치수값
    QList<StepSlab> candidateHorizontal =
        buildSlabsFromAxis(horizontals, verticals, regionRect.top(), regionRect.bottom());

    auto totalSpan = [](const QList<StepSlab> &list) {
        double sum = 0.0;
        for (const StepSlab &s : list) sum += s.span();
        return sum;
    };

    bool chooseHorizontal = false;
    if (candidateHorizontal.size() > candidateVertical.size()) {
        chooseHorizontal = true;
    } else if (candidateHorizontal.size() == candidateVertical.size() &&
               !candidateHorizontal.isEmpty() &&
               totalSpan(candidateHorizontal) > totalSpan(candidateVertical)) {
        chooseHorizontal = true;
    }

    if (chooseHorizontal) {
        m_slabs = candidateHorizontal;
        m_verticalDims = false;
    } else {
        m_slabs = candidateVertical;
        m_verticalDims = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 슬랩(span 오름차순)을 P3 위치에서 시작해 바깥쪽으로 baselineSpacing 만큼
// 스택하며 지름 치수(Ø)를 생성한다. m_verticalDims 값에 따라 세로/가로
// 치수선을 만든다.
// ─────────────────────────────────────────────────────────────────────────────
void AreaDimensionTool::finalizeDimensions(const QPointF &p3Pos) {
    if (!m_scene || m_slabs.isEmpty()) { onCancel(); return; }
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->layerManager()) { onCancel(); return; }

    QRectF regionRect = QRectF(m_p1.toPointF(), m_p2.toPointF()).normalized();
    int direction;
    if (m_verticalDims) {
        double regionCenterX = regionRect.center().x();
        direction = (p3Pos.x() < regionCenterX) ? -1 : 1;
    } else {
        double regionCenterY = regionRect.center().y();
        direction = (p3Pos.y() < regionCenterY) ? -1 : 1;
    }

    DimensionStyle style = m_scene->dimensionStyle();
    QString targetLayerName = style.dimLineLayer;
    if (targetLayerName.isEmpty() || !ctx->layerManager()->getLayer(targetLayerName))
        targetLayerName = ctx->layerManager()->getCurrentLayer()->name();
    Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);

    QPen pen(targetLayer->color());
    pen.setWidthF(targetLayer->weight());

    QList<StepSlab> ordered = m_slabs;
    std::sort(ordered.begin(), ordered.end(),
              [](const StepSlab &a, const StepSlab &b) { return a.span() < b.span(); });

    double spacing = style.baselineSpacing * m_scene->drawingScale();
    double curPos = m_verticalDims ? p3Pos.x() : p3Pos.y();

    for (const StepSlab &slab : ordered) {
        double origin = (direction > 0) ? slab.boundHi : slab.boundLo;

        MyDimension data;
        data.dimensionType = DimensionType::Linear;
        data.text          = "";
        data.prefix        = m_useDiameterSymbol ? QString(QChar(0x00D8)) : QString();
        data.precision                 = style.precision;
        data.suppressTrailingZeros     = style.suppressTrailingZeros;
        data.suppressTrailingZerosTol  = style.suppressTrailingZerosTol;
        if (m_verticalDims) {
            data.start   = MyVector(origin, slab.measLo);
            data.end     = MyVector(origin, slab.measHi);
            data.dimLine = MyVector(curPos, slab.measLo);
        } else {
            data.start   = MyVector(slab.measLo, origin);
            data.end     = MyVector(slab.measHi, origin);
            data.dimLine = MyVector(slab.measLo, curPos);
        }
        data.useFixedAngle = false;
        data.fixedAngle    = 0.0;
        data.hideExt1      = false;
        data.hideExt2      = false;

        LinearDimensionItem *item = new LinearDimensionItem(data);
        item->setLayerName(targetLayer->name());
        item->setPen(pen);
        item->setGripsVisible(false);
        item->setParentItem(m_scene->getLayerRoot(targetLayer->name()));

        if (ctx->undoStack())
            ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
        else
            m_scene->addItem(item);

        curPos += direction * spacing;
    }

    m_scene->clearOverlay();
    m_slabs.clear();
    m_isDrawing = false;
    m_state = WaitP1;
    if (ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(
            tr(" 영역의 첫 번째 점을 지정하세요 [P1] : "), {});
}

void AreaDimensionTool::buildDimPreview(const QPointF &p3Pos, QPainterPath &outPath) const {
    if (m_slabs.isEmpty()) return;

    QRectF regionRect = QRectF(m_p1.toPointF(), m_p2.toPointF()).normalized();
    int direction;
    if (m_verticalDims) {
        double regionCenterX = regionRect.center().x();
        direction = (p3Pos.x() < regionCenterX) ? -1 : 1;
    } else {
        double regionCenterY = regionRect.center().y();
        direction = (p3Pos.y() < regionCenterY) ? -1 : 1;
    }

    MyGraphicsScene *scenePtr = m_scene;
    DimensionStyle style = scenePtr ? scenePtr->dimensionStyle() : DimensionStyle();
    double drawingScale = scenePtr ? scenePtr->drawingScale() : 1.0;

    QList<StepSlab> ordered = m_slabs;
    std::sort(ordered.begin(), ordered.end(),
              [](const StepSlab &a, const StepSlab &b) { return a.span() < b.span(); });

    double spacing = style.baselineSpacing * drawingScale;
    double curPos = m_verticalDims ? p3Pos.x() : p3Pos.y();

    for (const StepSlab &slab : ordered) {
        double origin = (direction > 0) ? slab.boundHi : slab.boundLo;

        MyDimension data;
        data.dimensionType = DimensionType::Linear;
        data.prefix        = m_useDiameterSymbol ? QString(QChar(0x00D8)) : QString();
        if (m_verticalDims) {
            data.start   = MyVector(origin, slab.measLo);
            data.end     = MyVector(origin, slab.measHi);
            data.dimLine = MyVector(curPos, slab.measLo);
        } else {
            data.start   = MyVector(slab.measLo, origin);
            data.end     = MyVector(slab.measHi, origin);
            data.dimLine = MyVector(slab.measLo, curPos);
        }
        data.useFixedAngle = false;

        LinearDimensionItem di(data);
        outPath.addPath(di.geometryPath());

        curPos += direction * spacing;
    }
}

void AreaDimensionTool::updateGhostPreview() {
    if (!m_isDrawing || !m_scene) return;

    if (m_state == WaitP2) {
        m_scene->setDashRect(m_p1.toPointF(), m_currentMousePos, QColor(Qt::magenta));
        return;
    }

    if (m_state == WaitP3) {
        // P2 확정 시점에 점선 박스는 이미 지워졌으므로 여기서는 다시 그리지 않는다.
        QPainterPath path;
        buildDimPreview(m_currentMousePos, path);
        m_scene->setGhostPath(path, true, Qt::yellow, 1.0);
    }
}
