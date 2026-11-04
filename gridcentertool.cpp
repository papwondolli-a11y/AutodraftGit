#include "gridcentertool.h"
#include "gridcentersetupdialog.h"
#include "gridbubblesetupdialog.h"
#include "drawingsettingscommand.h"

#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "lineardimensionitem.h"
#include "lineitem.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "mytext.h"
#include "inputparser.h"
#include "snapmanager.h"
#include "textitem.h"
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainterPath>
#include "tooldefaults.h"
#include <QUndoStack>
#include <QWidget>
#include <QtMath>

namespace {
// "3600,3600,4500" 형식의 간격 목록을 파싱. 실패 시 ok=false.
QVector<double> parseSpacingList(const QString &text, bool &ok) {
    QVector<double> result;
    const QStringList tokens = text.split(',', Qt::SkipEmptyParts);
    for (const QString &rawTok : tokens) {
        bool tokOk = false;
        double v = rawTok.trimmed().toDouble(&tokOk);
        if (!tokOk || v <= 0.0) {
            ok = false;
            return {};
        }
        result.append(v);
    }
    ok = true;
    return result;
}

// 3-2: 도면 스코프 우선(ToolDefaults). 네임스페이스 "GridCenterTool" = 기존 그룹명.
void saveGridCenterSettings(MyGraphicsScene *scene) {
    ToolDefaults d("GridCenterTool", scene);
    d.put("XSpacing", GridCenterTool::s_xSpacingText);
    d.put("YSpacing", GridCenterTool::s_ySpacingText);
    d.put("DimTop", GridCenterTool::s_dimTop);
    d.put("DimBottom", GridCenterTool::s_dimBottom);
    d.put("DimLeft", GridCenterTool::s_dimLeft);
    d.put("DimRight", GridCenterTool::s_dimRight);
    d.put("DimTotal", GridCenterTool::s_dimTotal);
    d.put("BubbleTop", GridCenterTool::s_bubbleTop);
    d.put("BubbleBottom", GridCenterTool::s_bubbleBottom);
    d.put("BubbleLeft", GridCenterTool::s_bubbleLeft);
    d.put("BubbleRight", GridCenterTool::s_bubbleRight);
    d.put("BubbleTypeH", GridCenterTool::s_bubbleTypeH);
    d.put("BubbleTypeV", GridCenterTool::s_bubbleTypeV);
}

void loadGridCenterSettings(MyGraphicsScene *scene) {
    ToolDefaults d("GridCenterTool", scene);
    GridCenterTool::s_xSpacingText = d.getString("XSpacing", QString());
    GridCenterTool::s_ySpacingText = d.getString("YSpacing", QString());
    GridCenterTool::s_dimTop    = d.getBool("DimTop", false);
    GridCenterTool::s_dimBottom = d.getBool("DimBottom", false);
    GridCenterTool::s_dimLeft   = d.getBool("DimLeft", false);
    GridCenterTool::s_dimRight  = d.getBool("DimRight", false);
    GridCenterTool::s_dimTotal  = d.getBool("DimTotal", false);
    GridCenterTool::s_bubbleTop    = d.getBool("BubbleTop", false);
    GridCenterTool::s_bubbleBottom = d.getBool("BubbleBottom", false);
    GridCenterTool::s_bubbleLeft   = d.getBool("BubbleLeft", false);
    GridCenterTool::s_bubbleRight  = d.getBool("BubbleRight", false);
    GridCenterTool::s_bubbleTypeH  = d.getInt("BubbleTypeH", 2);
    GridCenterTool::s_bubbleTypeV  = d.getInt("BubbleTypeV", 2);
}

// 사용자가 클릭한 좌표(coord)가 negTip/posTip 중 어느 쪽에 더 가까운지에 따라
// 그 팁으로부터 얼마나 튀어나온 위치를 클릭했는지를 간격(gap)으로 환산한다.
double gapFromClick(double coord, double negTip, double posTip) {
    double mid = (negTip + posTip) / 2.0;
    double gap = (coord >= mid) ? (coord - posTip) : (negTip - coord);
    return qMax(1.0, gap);
}
}

QString GridCenterTool::s_xSpacingText;
QString GridCenterTool::s_ySpacingText;
bool GridCenterTool::s_dimTop    = false;
bool GridCenterTool::s_dimBottom = false;
bool GridCenterTool::s_dimLeft   = false;
bool GridCenterTool::s_dimRight  = false;
bool GridCenterTool::s_dimTotal  = false;
bool GridCenterTool::s_bubbleTop    = false;
bool GridCenterTool::s_bubbleBottom = false;
bool GridCenterTool::s_bubbleLeft   = false;
bool GridCenterTool::s_bubbleRight  = false;
int  GridCenterTool::s_bubbleTypeH  = 2;
int  GridCenterTool::s_bubbleTypeV  = 2;

GridCenterTool::GridCenterTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitingP1)
{
    // 생성 시점엔 씬이 없으므로 전역/코드 기본값만. 도면값은 activate()에서 반영.
    loadGridCenterSettings(nullptr);
}

GridCenterTool::~GridCenterTool() {}

void GridCenterTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;

    AbstractTool::activate(scene);

    // [3-2] 현재 도면의 그리드센터 기본값 반영(도면값 없으면 전역/코드 기본값).
    loadGridCenterSettings(scene);

    m_state = WaitingP1;
    m_p1 = QPointF();
    m_currentMousePos = QPointF();
    m_xPositions.clear();
    m_yPositions.clear();

    if (m_scene) {
        hideGripsOfSelectedItems();
        m_scene->clearSelection();
    }
    updatePrompt();
}

void GridCenterTool::deactivate() {
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection();
    }
    AbstractTool::deactivate();
}

void GridCenterTool::onFinish() {
    if (m_scene)
        m_scene->clearOverlay();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
        ctx->commandDock()->setPrompt(" Command : ");
    }
}

void GridCenterTool::onCancel() {
    if (m_scene)
        m_scene->clearOverlay();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage("*Cancel*", CommandDock::Info);
        ctx->commandDock()->setToolActive(false);
        ctx->commandDock()->setPrompt(" Command : ");
    }
}

void GridCenterTool::updatePrompt() {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;

    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("CENTERLINE", CommandDock::Command);

    switch (m_state) {
    case WaitingP1:
        ctx->commandDock()->showInteractivePrompt(
            tr(" 첫 번째 구석점(P1)을 지정하세요 : "), {"Setup"});
        break;
    case WaitingP2:
        ctx->commandDock()->showInteractivePrompt(
            tr(" 반대편 구석점(P2)을 지정하세요 : "), {"Setup"});
        break;
    case WaitingHDimPos:
        ctx->commandDock()->showInteractivePrompt(
            tr(" 상단/하단 치수선 놓일 위치를 지정하세요 : "), {"Setup"});
        break;
    case WaitingVDimPos:
        ctx->commandDock()->showInteractivePrompt(
            tr(" 좌측/우측 치수선 놓일 위치를 지정하세요 : "), {"Setup"});
        break;
    }
}

bool GridCenterTool::processCommand(const QString &command) {
    IAppContext *ctx = getContext();
    if (!ctx)
        return false;

    const QString lower = command.trimmed().toLower();
    if (lower == "s" || lower == "setup") {
        openSetupDialog();
        return true;
    }

    if (m_state == WaitingHDimPos || m_state == WaitingVDimPos) {
        QString text = command.trimmed();
        QPointF pos = m_currentMousePos;
        if (!text.isEmpty()) {
            if (!ctx->parser())
                return false;
            bool isOrtho = ctx->snapManager() && ctx->snapManager()->isOrtho();
            QPointF result;
            if (!ctx->parser()->parseCoordinate(text, QPointF(), m_currentMousePos, isOrtho, result))
                return false;
            pos = result;
        }
        if (m_state == WaitingHDimPos)
            finishHDimPos(pos);
        else
            finishVDimPos(pos);
        blockEnterKeyTemporarily(200);
        return true;
    }

    if (!ctx->parser())
        return false;

    QPointF base = (m_state == WaitingP2) ? m_p1 : QPointF();
    bool isOrtho = ctx->snapManager() && ctx->snapManager()->isOrtho();
    QPointF result;

    if (ctx->parser()->parseCoordinate(command, base, m_currentMousePos, isOrtho, result)) {
        mousePress(result, Qt::LeftButton);
        blockEnterKeyTemporarily(200);
        return true;
    }
    return false;
}

// P2가 확정된 직후 호출된다. 별도의 프롬프트 없이 Setup 다이얼로그 "중심선 간격"에
// 저장된 s_xSpacingText/s_ySpacingText 값을 그대로 사용해 m_xPositions/m_yPositions를
// 채우고 바로 finalizeGrid()를 호출한다. 값이 비어있거나 형식이 잘못된 경우 해당 축은
// 중앙에 1개만 생성한다.
void GridCenterTool::computeGridPositions() {
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    double x1 = m_p1.x(), y1 = m_p1.y();
    double x2 = m_currentMousePos.x(), y2 = m_currentMousePos.y();
    double cx = (x1 + x2) / 2.0, cy = (y1 + y2) / 2.0;
    bool hasWidth  = qAbs(x2 - x1) > 1e-6;
    bool hasHeight = qAbs(y2 - y1) > 1e-6;
    // P1을 기준으로 P2 방향으로 누적되도록 부호 결정 (P1이 항상 시작점)
    double xDir = (x2 >= x1) ? 1.0 : -1.0;
    double yDir = (y2 >= y1) ? 1.0 : -1.0;

    m_xPositions.clear();
    m_yPositions.clear();

    if (!hasWidth && !hasHeight) {
        // P1 == P2: 생성할 선이 없음, 초기 상태로 복귀
        m_state = WaitingP1;
        m_scene->clearOverlay();
        updatePrompt();
        return;
    }

    if (hasHeight) {
        QString text = s_xSpacingText.trimmed();
        bool ok = false;
        QVector<double> spacings = text.isEmpty() ? QVector<double>() : parseSpacingList(text, ok);
        if (!text.isEmpty() && !ok) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(
                    tr("Setup의 가로축 간격 값이 올바르지 않아 중앙 1개로 생성합니다."), CommandDock::Warning);
            spacings.clear();
        }
        if (text.isEmpty() || !ok) {
            m_xPositions = { cx };
        } else {
            double cur = x1;
            m_xPositions.append(cur);
            for (double s : spacings) {
                cur += xDir * s;
                m_xPositions.append(cur);
            }
        }
    }

    if (hasWidth) {
        QString text = s_ySpacingText.trimmed();
        bool ok = false;
        QVector<double> spacings = text.isEmpty() ? QVector<double>() : parseSpacingList(text, ok);
        if (!text.isEmpty() && !ok) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(
                    tr("Setup의 세로축 간격 값이 올바르지 않아 중앙 1개로 생성합니다."), CommandDock::Warning);
            spacings.clear();
        }
        if (text.isEmpty() || !ok) {
            m_yPositions = { cy };
        } else {
            double cur = y1;
            m_yPositions.append(cur);
            for (double s : spacings) {
                cur += yDir * s;
                m_yPositions.append(cur);
            }
        }
    }

    finalizeGrid();
}

void GridCenterTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene)
        return;

    if (button == Qt::RightButton) {
        if (m_state == WaitingP2) {
            // P2 대기 중 우클릭 → P1 선택으로 복귀
            m_state = WaitingP1;
            m_scene->clearOverlay();
            updatePrompt();
        } else {
            onCancel();
            emit toolFinished();
        }
        return;
    }

    if (button != Qt::LeftButton)
        return;

    if (m_state == WaitingHDimPos || m_state == WaitingVDimPos) {
        QPointF snapped = pos;
        IAppContext *ctx = getContext();
        if (ctx && ctx->snapManager())
            snapped = ctx->snapManager()->snapPoint(pos, m_scene, QPointF()).snappedPos;

        if (m_state == WaitingHDimPos)
            finishHDimPos(snapped);
        else
            finishVDimPos(snapped);
        return;
    }

    // 스냅 적용
    QPointF snapped = pos;
    IAppContext *ctx = getContext();
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitingP2) ? m_p1 : QPointF();
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitingP1) {
        m_p1 = snapped;
        m_currentMousePos = snapped;
        m_state = WaitingP2;
        updateGhostBox(snapped);
        updatePrompt();
    } else {
        // P2 확정 → Setup에 저장된 간격값으로 즉시 중심선 생성
        m_currentMousePos = snapped;
        computeGridPositions();
    }
}

void GridCenterTool::mouseMove(QPointF pos) {
    if (!m_scene)
        return;

    if (m_state == WaitingP2) {
        IAppContext *ctx = getContext();
        QPointF snapped = pos;
        if (ctx && ctx->snapManager()) {
            auto res = ctx->snapManager()->snapPoint(pos, m_scene, m_p1);
            snapped = res.snappedPos;
            m_scene->setSnapMarker(snapped, res.type);
        }
        m_currentMousePos = snapped;
        updateGhostBox(snapped);
        return;
    }

    if (m_state == WaitingHDimPos || m_state == WaitingVDimPos) {
        IAppContext *ctx = getContext();
        QPointF snapped = pos;
        if (ctx && ctx->snapManager()) {
            auto res = ctx->snapManager()->snapPoint(pos, m_scene, QPointF());
            snapped = res.snappedPos;
            m_scene->setSnapMarker(snapped, res.type);
        }
        m_currentMousePos = snapped;
        updateDimGhost(snapped);
        return;
    }
}

void GridCenterTool::updateGhostBox(const QPointF &p2) {
    if (!m_scene)
        return;

    double x1 = m_p1.x(), y1 = m_p1.y();
    double x2 = p2.x(),   y2 = p2.y();

    // P1 == P2: 아무것도 그리지 않음
    if (qAbs(x2 - x1) < 1e-6 && qAbs(y2 - y1) < 1e-6) {
        m_scene->clearOverlay();
        return;
    }

    double cx = (x1 + x2) / 2.0;
    double cy = (y1 + y2) / 2.0;
    bool hasWidth  = qAbs(x2 - x1) > 1e-6;
    bool hasHeight = qAbs(y2 - y1) > 1e-6;

    QPainterPath path;
    // 박스 윤곽 (두 차원이 모두 있을 때만)
    if (hasWidth && hasHeight) {
        path.moveTo(x1, y1);
        path.lineTo(x2, y1);
        path.lineTo(x2, y2);
        path.lineTo(x1, y2);
        path.closeSubpath();
    }
    // 가로 중심선 미리보기 (너비가 있을 때) — 박스 밖으로 약간 연장
    if (hasWidth) {
        double ext = qMax(2.0, qAbs(x2 - x1) * 0.05);
        double minX = qMin(x1, x2), maxX = qMax(x1, x2);
        path.moveTo(minX - ext, cy);
        path.lineTo(maxX + ext, cy);
    }
    // 세로 중심선 미리보기 (높이가 있을 때) — 박스 밖으로 약간 연장
    if (hasHeight) {
        double ext = qMax(2.0, qAbs(y2 - y1) * 0.05);
        double minY = qMin(y1, y2), maxY = qMax(y1, y2);
        path.moveTo(cx, minY - ext);
        path.lineTo(cx, maxY + ext);
    }

    updateGhostPreview(path, currentLayerName());
}

void GridCenterTool::finalizeGrid() {
    if (!m_scene)
        return;

    double x1 = m_p1.x(), y1 = m_p1.y();
    double x2 = m_currentMousePos.x(), y2 = m_currentMousePos.y();
    double minX = qMin(x1, x2), maxX = qMax(x1, x2);
    double minY = qMin(y1, y2), maxY = qMax(y1, y2);
    bool hasWidth  = qAbs(x2 - x1) > 1e-6;
    bool hasHeight = qAbs(y2 - y1) > 1e-6;

    // 세로 중심선의 길이(Y방향)는 가로축(행) 간격이 실제 입력된 경우 그 합계 범위를 따르고,
    // 아니면 박스 높이를 사용한다.
    double vLineMinY = minY, vLineMaxY = maxY;
    if (m_yPositions.size() > 1) {
        vLineMinY = qMin(m_yPositions.first(), m_yPositions.last());
        vLineMaxY = qMax(m_yPositions.first(), m_yPositions.last());
    }
    // 가로 중심선의 길이(X방향)는 세로축(열) 간격이 실제 입력된 경우 그 합계 범위를 따르고,
    // 아니면 박스 너비를 사용한다.
    double hLineMinX = minX, hLineMaxX = maxX;
    if (m_xPositions.size() > 1) {
        hLineMinX = qMin(m_xPositions.first(), m_xPositions.last());
        hLineMaxX = qMax(m_xPositions.first(), m_xPositions.last());
    }

    IAppContext *ctx = getContext();
    if (!ctx)
        return;

    Layer *layer = nullptr;
    if (ctx->layerManager()) {
        const QString &globalLayer = DrawingSettingsCommand::s_centerLineLayer;
        if (!globalLayer.isEmpty())
            layer = ctx->layerManager()->getLayer(globalLayer);
        if (!layer)
            layer = ctx->layerManager()->getCurrentLayer();
    }

    QPen pen;
    if (layer) {
        pen = QPen(layer->color());
        pen.setWidthF(layer->weight());
        QString ltName = layer->linetype();
        if (ltName != "Continuous" && ltName != "Continous" && ctx->lineTypeManager()) {
            pen.setStyle(Qt::CustomDashLine);
            QVector<qreal> pattern = ctx->lineTypeManager()->getQtDashPattern(ltName);
            if (!pattern.isEmpty())
                pen.setDashPattern(pattern);
            else
                pen.setStyle(Qt::SolidLine);
        } else {
            pen.setStyle(Qt::SolidLine);
        }
    }

    auto applyLayer = [&](QGraphicsItem *item) {
        if (!item) return;
        if (LineItem *li = dynamic_cast<LineItem *>(item))
            if (layer) li->setLayerName(layer->name());
        if (layer) {
            QGraphicsItem *folder = m_scene->getLayerRoot(layer->name());
            item->setParentItem(folder);
            if (item->scene() == m_scene)
                m_scene->removeItem(item);
        }
    };

    QList<QGraphicsItem *> created;
    double extV = qMax(2.0, (vLineMaxY - vLineMinY) * 0.05);
    double extH = qMax(2.0, (hLineMaxX - hLineMinX) * 0.05);

    // 세로 중심선들: x = m_xPositions 각각, 가로축 간격 합계 범위 밖으로 약간 연장
    if (hasHeight && !m_xPositions.isEmpty()) {
        for (int i = 0; i < m_xPositions.size(); ++i) {
            double x = m_xPositions.at(i);
            QGraphicsItem *item = ctx->entityFactory()->createLine(
                QLineF(x, vLineMinY - extV, x, vLineMaxY + extV), pen);
            applyLayer(item);
            created.append(item);
        }
    }

    // 가로 중심선들: y = m_yPositions 각각, 세로축 간격 합계 범위 밖으로 약간 연장
    if (hasWidth && !m_yPositions.isEmpty()) {
        for (int i = 0; i < m_yPositions.size(); ++i) {
            double y = m_yPositions.at(i);
            QGraphicsItem *item = ctx->entityFactory()->createLine(
                QLineF(hLineMinX - extH, y, hLineMaxX + extH, y), pen);
            applyLayer(item);
            created.append(item);
        }
    }

    if (!created.isEmpty()) {
        if (ctx->undoStack()) {
            ctx->undoStack()->beginMacro(tr("중심선 생성"));
            for (int i = 0; i < created.size(); ++i) {
                QGraphicsItem *item = created.at(i);
                ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
            }
            ctx->undoStack()->endMacro();
        } else {
            for (int i = 0; i < created.size(); ++i) {
                QGraphicsItem *item = created.at(i);
                m_scene->addItem(item);
            }
        }

        if (ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("중심선 %1개 생성 완료.").arg(created.size()), CommandDock::Info);
    }

    // 치수선/주열번호 배치 단계에서 계속 사용할 그리드 범위 정보를 보관
    m_vLineMinY = vLineMinY;
    m_vLineMaxY = vLineMaxY;
    m_hLineMinX = hLineMinX;
    m_hLineMaxX = hLineMaxX;
    m_extV = extV;
    m_extH = extH;
    m_hasWidth = hasWidth;
    m_hasHeight = hasHeight;

    // 중심선의 실제 끝점(오버슈트 포함) - 치수선이 없을 때 주열번호가 참조하는 기준점
    m_topLineY = vLineMaxY + extV;
    m_bottomLineY = vLineMinY - extV;
    m_leftLineX = hLineMinX - extH;
    m_rightLineX = hLineMaxX + extH;
    m_topDimSet = m_bottomDimSet = m_leftDimSet = m_rightDimSet = false;

    beginDimensionFlow();
}

// Setup 다이얼로그에서 치수선 방향이 체크되어 있으면, 연속치수(ContinuedimensionTool)처럼
// 사용자가 직접 치수선이 놓일 위치를 지정하도록 WaitingHDimPos/WaitingVDimPos 단계로 진입한다.
// 체크된 방향이 없으면 곧바로 초기 상태로 복귀한다.
void GridCenterTool::beginDimensionFlow() {
    bool needH = m_hasHeight && m_xPositions.size() >= 2 && (s_dimTop || s_dimBottom);
    bool needV = m_hasWidth && m_yPositions.size() >= 2 && (s_dimLeft || s_dimRight);

    m_needVDimPos = needV;

    if (needH) {
        m_state = WaitingHDimPos;
        updatePrompt();
    } else if (needV) {
        m_state = WaitingVDimPos;
        updatePrompt();
    } else {
        finishDimensionFlow();
    }
}

void GridCenterTool::updateDimGhost(const QPointF &pos) {
    if (!m_scene)
        return;

    QPainterPath path;
    if (m_state == WaitingHDimPos) {
        path.moveTo(m_hLineMinX - m_extH, pos.y());
        path.lineTo(m_hLineMaxX + m_extH, pos.y());
    } else if (m_state == WaitingVDimPos) {
        path.moveTo(pos.x(), m_vLineMinY - m_extV);
        path.lineTo(pos.x(), m_vLineMaxY + m_extV);
    }
    updateGhostPreview(path, currentLayerName());
}

Layer *GridCenterTool::resolveDimLayer(QPen &pen) {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->layerManager() || !m_scene)
        return nullptr;

    DimensionStyle style = m_scene->dimensionStyle();
    Layer *curLayer = ctx->layerManager()->getCurrentLayer();
    QString targetLayerName = style.dimLineLayer;
    if (targetLayerName.isEmpty() || !ctx->layerManager()->getLayer(targetLayerName))
        targetLayerName = curLayer ? curLayer->name() : QString();
    Layer *targetLayer =
        targetLayerName.isEmpty() ? nullptr : ctx->layerManager()->getLayer(targetLayerName);

    if (targetLayer) {
        pen = QPen(targetLayer->color());
        pen.setWidthF(targetLayer->weight());
    }
    return targetLayer;
}

// 연결/연속 치수선 도구(ContinuedimensionTool)의 MyDimension 구성 방식(start/end/dimLine,
// 체인의 hideExt1 처리)을 그대로 따라 상단/하단 치수선을 생성한다.
// m_topDimY/m_bottomDimY는 finishHDimPos()에서 미리 계산되어 있어야 한다.
QList<QGraphicsItem *> GridCenterTool::buildRowDimensions() {
    QList<QGraphicsItem *> created;
    if (m_xPositions.size() < 2 || !(s_dimTop || s_dimBottom))
        return created;

    QPen pen;
    Layer *targetLayer = resolveDimLayer(pen);

    // 합계 치수선과 일반 치수선 사이의 간격은 연속치수선(ContinuedimensionTool)과 동일하게
    // 치수 스타일의 baselineSpacing을 따른다 (사용자가 클릭해 지정한 gap과는 별개).
    DimensionStyle style = m_scene ? m_scene->dimensionStyle() : DimensionStyle();
    double totalSpacing = qMax(1.0, style.baselineSpacing * (m_scene ? m_scene->drawingScale() : 1.0));

    auto makeItem = [&](const MyDimension &data) {
        LinearDimensionItem *item = new LinearDimensionItem(data);
        if (targetLayer) {
            item->setLayerName(targetLayer->name());
            item->setPen(pen);
        }
        item->setGripsVisible(false);
        if (targetLayer)
            item->setParentItem(m_scene->getLayerRoot(targetLayer->name()));
        created.append(item);
    };

    auto buildRow = [&](double lineY, double dimY, double *totalDimYOut) {
        for (int i = 0; i + 1 < m_xPositions.size(); ++i) {
            MyDimension d;
            d.dimensionType = DimensionType::Linear;
            d.start = MyVector(m_xPositions[i], lineY);
            d.end   = MyVector(m_xPositions[i + 1], lineY);
            d.dimLine = MyVector(m_xPositions[i], dimY);
            d.useFixedAngle = true;
            d.fixedAngle = 0.0;
            d.hideExt1 = (i > 0);
            d.hideExt2 = false;
            makeItem(d);
        }
        if (s_dimTotal) {
            double totalDimY = dimY + ((dimY >= lineY) ? totalSpacing : -totalSpacing);
            if (totalDimYOut)
                *totalDimYOut = totalDimY;
            MyDimension d;
            d.dimensionType = DimensionType::Linear;
            d.start = MyVector(m_xPositions.first(), lineY);
            d.end   = MyVector(m_xPositions.last(), lineY);
            d.dimLine = MyVector(m_xPositions.first(), totalDimY);
            d.useFixedAngle = true;
            d.fixedAngle = 0.0;
            d.hideExt1 = false;
            d.hideExt2 = false;
            makeItem(d);
        }
    };

    if (s_dimTop)
        buildRow(m_topLineY, m_topDimY, &m_topTotalDimY);
    if (s_dimBottom)
        buildRow(m_bottomLineY, m_bottomDimY, &m_bottomTotalDimY);

    return created;
}

// m_leftDimX/m_rightDimX는 finishVDimPos()에서 미리 계산되어 있어야 한다.
QList<QGraphicsItem *> GridCenterTool::buildColumnDimensions() {
    QList<QGraphicsItem *> created;
    if (m_yPositions.size() < 2 || !(s_dimLeft || s_dimRight))
        return created;

    QPen pen;
    Layer *targetLayer = resolveDimLayer(pen);

    // 합계 치수선과 일반 치수선 사이의 간격은 연속치수선(ContinuedimensionTool)과 동일하게
    // 치수 스타일의 baselineSpacing을 따른다 (사용자가 클릭해 지정한 gap과는 별개).
    DimensionStyle style = m_scene ? m_scene->dimensionStyle() : DimensionStyle();
    double totalSpacing = qMax(1.0, style.baselineSpacing * (m_scene ? m_scene->drawingScale() : 1.0));

    auto makeItem = [&](const MyDimension &data) {
        LinearDimensionItem *item = new LinearDimensionItem(data);
        if (targetLayer) {
            item->setLayerName(targetLayer->name());
            item->setPen(pen);
        }
        item->setGripsVisible(false);
        if (targetLayer)
            item->setParentItem(m_scene->getLayerRoot(targetLayer->name()));
        created.append(item);
    };

    auto buildColumn = [&](double lineX, double dimX, double *totalDimXOut) {
        for (int i = 0; i + 1 < m_yPositions.size(); ++i) {
            MyDimension d;
            d.dimensionType = DimensionType::Linear;
            d.start = MyVector(lineX, m_yPositions[i]);
            d.end   = MyVector(lineX, m_yPositions[i + 1]);
            d.dimLine = MyVector(dimX, m_yPositions[i]);
            d.useFixedAngle = true;
            d.fixedAngle = 90.0;
            d.hideExt1 = (i > 0);
            d.hideExt2 = false;
            makeItem(d);
        }
        if (s_dimTotal) {
            double totalDimX = dimX + ((dimX >= lineX) ? totalSpacing : -totalSpacing);
            if (totalDimXOut)
                *totalDimXOut = totalDimX;
            MyDimension d;
            d.dimensionType = DimensionType::Linear;
            d.start = MyVector(lineX, m_yPositions.first());
            d.end   = MyVector(lineX, m_yPositions.last());
            d.dimLine = MyVector(totalDimX, m_yPositions.first());
            d.useFixedAngle = true;
            d.fixedAngle = 90.0;
            d.hideExt1 = false;
            d.hideExt2 = false;
            makeItem(d);
        }
    };

    if (s_dimLeft)
        buildColumn(m_leftLineX, m_leftDimX, &m_leftTotalDimX);
    if (s_dimRight)
        buildColumn(m_rightLineX, m_rightDimX, &m_rightTotalDimX);

    return created;
}

void GridCenterTool::pushDimensionUndo(const QList<QGraphicsItem *> &items) {
    if (items.isEmpty() || !m_scene)
        return;

    IAppContext *ctx = getContext();
    if (!ctx)
        return;

    if (ctx->undoStack()) {
        ctx->undoStack()->beginMacro(tr("치수선 생성"));
        for (QGraphicsItem *item : items)
            ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
        ctx->undoStack()->endMacro();
    } else {
        for (QGraphicsItem *item : items)
            m_scene->addItem(item);
    }

    if (ctx->commandDock())
        ctx->commandDock()->logMessage(
            tr("치수선 %1개 생성 완료.").arg(items.size()), CommandDock::Info);
}

void GridCenterTool::finishHDimPos(const QPointF &pos) {
    double gap = gapFromClick(pos.y(), m_bottomLineY, m_topLineY);

    if (s_dimTop) {
        m_topDimY = m_topLineY + gap;
        m_topDimSet = true;
    }
    if (s_dimBottom) {
        m_bottomDimY = m_bottomLineY - gap;
        m_bottomDimSet = true;
    }

    pushDimensionUndo(buildRowDimensions());

    if (m_needVDimPos) {
        m_state = WaitingVDimPos;
        if (m_scene)
            m_scene->clearOverlay();
        updatePrompt();
    } else {
        finishDimensionFlow();
    }
}

void GridCenterTool::finishVDimPos(const QPointF &pos) {
    double gap = gapFromClick(pos.x(), m_leftLineX, m_rightLineX);

    if (s_dimLeft) {
        m_leftDimX = m_leftLineX - gap;
        m_leftDimSet = true;
    }
    if (s_dimRight) {
        m_rightDimX = m_rightLineX + gap;
        m_rightDimSet = true;
    }

    pushDimensionUndo(buildColumnDimensions());

    finishDimensionFlow();
}

void GridCenterTool::finishDimensionFlow() {
    generateGridBubbles();

    // 연속 실행: 초기 상태로
    m_xPositions.clear();
    m_yPositions.clear();
    m_state = WaitingP1;
    if (m_scene)
        m_scene->clearOverlay();
    ;   updatePrompt();
        onFinish();
         emit toolFinished();

}

// GridBubbleTool과 동일한 방식(꼬리선 + 원 + 텍스트)으로 그리드 라인마다 주열번호를 생성한다.
// 생성 위치: 치수선이 생성된 방향은 그 치수보조선 끝점(dimLine + extExtend)에서
// extOffset만큼 더 떨어진 위치, 치수선이 없는 방향은 중심선의 실제 끝점(오버슈트 포함)에서
// extOffset만큼 떨어진 위치. 크기/레이어/기울기 등은 GridBubbleTool에 저장된 설정을 그대로 사용한다.
void GridCenterTool::generateGridBubbles() {
    if (!m_scene)
        return;
    if (!(s_bubbleTop || s_bubbleBottom || s_bubbleLeft || s_bubbleRight))
        return;

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->entityFactory() || !ctx->layerManager() || !ctx->lineTypeManager())
        return;

    // GridBubbleTool의 Setup에서 저장된 설정을 그대로 공유해서 사용한다.
    //   (3-2) 도면 스코프 우선으로 조회 → 같은 도면의 그리드버블 설정과 일관됨.
    QString bubbleLayer, bubbleLabelLayer;
    bool tiltEnabled;
    double labelSize, diameter;
    {
        ToolDefaults gb("GridBubbleTool", m_scene);
        bubbleLayer      = gb.getString("Layer", QString());
        bubbleLabelLayer = gb.getString("LabelLayer", QString());
        tiltEnabled      = gb.getBool("TiltEnabled", false);
        labelSize        = gb.getDouble("LabelSize", 4.0);
        diameter         = gb.getDouble("Diameter", 10.0);
    }

    double scale = m_scene->drawingScale();
    if (scale <= 0.0) scale = 1.0;
    DimensionStyle dimStyle = m_scene->dimensionStyle();
    double extOffset = dimStyle.extOffset * scale;
    double extExtend = dimStyle.extExtend * scale;

    QList<QGraphicsItem *> created;

    auto typeFromInt = [](int v) {
        switch (v) {
        case 0: return GridBubbleSetupDialog::TypeX;
        case 1: return GridBubbleSetupDialog::TypeY;
        case 3: return GridBubbleSetupDialog::TypeNum;
        default: return GridBubbleSetupDialog::TypeA;
        }
    };

    auto buildLabels = [](int count, GridBubbleSetupDialog::LabelType type) {
        QStringList labels;
        int counter = (type == GridBubbleSetupDialog::TypeA) ? 0 : 1;
        for (int i = 0; i < count; ++i) {
            QString label;
            switch (type) {
            case GridBubbleSetupDialog::TypeX:
                label = QStringLiteral("X") + QString::number(counter);
                break;
            case GridBubbleSetupDialog::TypeY:
                label = QStringLiteral("Y") + QString::number(counter);
                break;
            case GridBubbleSetupDialog::TypeA: {
                int n = counter;
                QString s;
                do {
                    s.prepend(QChar('A' + (n % 26)));
                    n = n / 26 - 1;
                } while (n >= 0);
                label = s;
                break;
            }
            case GridBubbleSetupDialog::TypeNum:
                label = QString::number(counter);
                break;
            }
            labels.append(label);
            ++counter;
        }
        return labels;
    };

    // GridBubbleTool::finalizeBubble()과 동일한 방식(꼬리선 + 원 + 텍스트)으로 항목을 생성.
    auto addBubble = [&](const MyVector &pt1, const MyVector &dirVec, const QString &label) {
        Layer *curLayer = bubbleLayer.isEmpty()
                          ? ctx->layerManager()->getCurrentLayer()
                          : ctx->layerManager()->getLayer(bubbleLayer);
        if (!curLayer)
            curLayer = ctx->layerManager()->getCurrentLayer();
        if (!curLayer)
            return;

        QPen pen(curLayer->color());
        pen.setWidthF(curLayer->weight());
        QString ltName = curLayer->linetype();
        if (ltName == "Continuous" || ltName == "Continous") {
            pen.setStyle(Qt::SolidLine);
        } else {
            pen.setStyle(Qt::CustomDashLine);
            QVector<qreal> pattern = ctx->lineTypeManager()->getQtDashPattern(ltName);
            pen.setDashPattern(!pattern.isEmpty() ? pattern : QVector<qreal>());
            if (pattern.isEmpty()) pen.setStyle(Qt::SolidLine);
        }

        const double radius = (diameter / 2.0) * scale;
        const double fontSize = labelSize * scale;

        MyVector bc(pt1.x + dirVec.x * 2.0 * radius, pt1.y + dirVec.y * 2.0 * radius);
        MyVector tailEnd(pt1.x + dirVec.x * radius, pt1.y + dirVec.y * radius);

        Layer *labelLayerPtr = bubbleLabelLayer.isEmpty()
                               ? curLayer
                               : ctx->layerManager()->getLayer(bubbleLabelLayer);
        if (!labelLayerPtr) labelLayerPtr = curLayer;

        QPen labelPen(labelLayerPtr->color());
        labelPen.setWidthF(labelLayerPtr->weight());
        QString ltNameL = labelLayerPtr->linetype();
        if (ltNameL == "Continuous" || ltNameL == "Continous") {
            labelPen.setStyle(Qt::SolidLine);
        } else {
            labelPen.setStyle(Qt::CustomDashLine);
            QVector<qreal> pat = ctx->lineTypeManager()->getQtDashPattern(ltNameL);
            labelPen.setDashPattern(!pat.isEmpty() ? pat : QVector<qreal>());
            if (pat.isEmpty()) labelPen.setStyle(Qt::SolidLine);
        }

        auto finishItem = [&](QGraphicsItem *item, Layer *lay) {
            if (!item) return;
            if (CadItem *ci = dynamic_cast<CadItem *>(item))
                ci->setLayerName(lay->name());
            item->setParentItem(m_scene->getLayerRoot(lay->name()));
            created.append(item);
        };

        finishItem(ctx->entityFactory()->createLine(
            QLineF(pt1.x, pt1.y, tailEnd.x, tailEnd.y), pen), curLayer);
        finishItem(ctx->entityFactory()->createCircle(QPointF(bc.x, bc.y), radius, pen), curLayer);

        QFont tmpFont("Arial");
        tmpFont.setPointSizeF(fontSize);
        QFontMetricsF fm(tmpFont);
        double vertOffset = fm.ascent() / 2.0;

        double tiltAngle = 0.0;
        if (tiltEnabled)
            tiltAngle = qRadiansToDegrees(qAtan2(dirVec.y, dirVec.x)) - 90.0;
        double tiltRad = qDegreesToRadians(tiltAngle);
        MyVector textPos(bc.x + vertOffset * qSin(tiltRad), bc.y - vertOffset * qCos(tiltRad));

        MyText textData;
        textData.pos = textPos;
        textData.text = label;
        textData.fontFamily = "Arial";
        textData.fontSize = fontSize;
        textData.rotation = tiltAngle;
        textData.alignH = 1;

        TextItem *ti = new TextItem(textData);
        ti->setPen(labelPen);
        finishItem(ti, labelLayerPtr);
    };

    // 상단/하단: m_xPositions를 따라 배치. 합계 치수선이 있으면 그 치수보조선 끝점+extOffset,
    // 합계 없이 일반 치수선만 있으면 그 치수보조선 끝점+extOffset, 치수선이 아예 없으면
    // 중심선 실제 끝점+extOffset.
    if (!m_xPositions.isEmpty() && (s_bubbleTop || s_bubbleBottom)) {
        QStringList labels = buildLabels(m_xPositions.size(), typeFromInt(s_bubbleTypeH));
        bool topHasTotal = m_topDimSet && s_dimTotal;
        bool bottomHasTotal = m_bottomDimSet && s_dimTotal;
        double topTip = m_topDimSet ? ((topHasTotal ? m_topTotalDimY : m_topDimY) + extExtend) : m_topLineY;
        double bottomTip = m_bottomDimSet ? ((bottomHasTotal ? m_bottomTotalDimY : m_bottomDimY) - extExtend) : m_bottomLineY;
        for (int i = 0; i < m_xPositions.size(); ++i) {
            if (s_bubbleTop)
                addBubble(MyVector(m_xPositions[i], topTip + extOffset), MyVector(0.0, 1.0), labels[i]);
            if (s_bubbleBottom)
                addBubble(MyVector(m_xPositions[i], bottomTip - extOffset), MyVector(0.0, -1.0), labels[i]);
        }
    }

    // 좌측/우측: m_yPositions를 따라 배치. 합계 치수선 우선 적용은 상단/하단과 동일.
    if (!m_yPositions.isEmpty() && (s_bubbleLeft || s_bubbleRight)) {
        QStringList labels = buildLabels(m_yPositions.size(), typeFromInt(s_bubbleTypeV));
        bool leftHasTotal = m_leftDimSet && s_dimTotal;
        bool rightHasTotal = m_rightDimSet && s_dimTotal;
        double leftTip = m_leftDimSet ? ((leftHasTotal ? m_leftTotalDimX : m_leftDimX) - extExtend) : m_leftLineX;
        double rightTip = m_rightDimSet ? ((rightHasTotal ? m_rightTotalDimX : m_rightDimX) + extExtend) : m_rightLineX;
        for (int i = 0; i < m_yPositions.size(); ++i) {
            if (s_bubbleLeft)
                addBubble(MyVector(leftTip - extOffset, m_yPositions[i]), MyVector(-1.0, 0.0), labels[i]);
            if (s_bubbleRight)
                addBubble(MyVector(rightTip + extOffset, m_yPositions[i]), MyVector(1.0, 0.0), labels[i]);
        }
    }

    if (!created.isEmpty()) {
        if (ctx->undoStack()) {
            ctx->undoStack()->beginMacro(tr("주열번호 생성"));
            for (QGraphicsItem *item : created)
                ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
            ctx->undoStack()->endMacro();
        } else {
            for (QGraphicsItem *item : created)
                m_scene->addItem(item);
        }

        if (ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("주열번호 %1개 생성 완료.").arg(created.size() / 3), CommandDock::Info);
    }
}

void GridCenterTool::openSetupDialog() {
    IAppContext *ctx = getContext();
    QWidget *parentWin =
        ctx ? dynamic_cast<QWidget *>(ctx->commandDock()) : nullptr;

    GridDimFlags dimFlags;
    dimFlags.top    = s_dimTop;
    dimFlags.bottom = s_dimBottom;
    dimFlags.left   = s_dimLeft;
    dimFlags.right  = s_dimRight;
    dimFlags.total  = s_dimTotal;

    GridBubbleFlags bubbleFlags;
    bubbleFlags.top        = s_bubbleTop;
    bubbleFlags.bottom     = s_bubbleBottom;
    bubbleFlags.left       = s_bubbleLeft;
    bubbleFlags.right      = s_bubbleRight;
    bubbleFlags.hLabelType = s_bubbleTypeH;
    bubbleFlags.vLabelType = s_bubbleTypeV;

    GridCenterSetupDialog dlg(s_ySpacingText, s_xSpacingText, dimFlags, bubbleFlags, parentWin);
    if (dlg.exec() == QDialog::Accepted) {
        s_ySpacingText = dlg.ySpacing();
        s_xSpacingText = dlg.xSpacing();
        GridDimFlags newFlags = dlg.dimFlags();
        s_dimTop    = newFlags.top;
        s_dimBottom = newFlags.bottom;
        s_dimLeft   = newFlags.left;
        s_dimRight  = newFlags.right;
        s_dimTotal  = newFlags.total;

        GridBubbleFlags newBubbleFlags = dlg.bubbleFlags();
        s_bubbleTop      = newBubbleFlags.top;
        s_bubbleBottom   = newBubbleFlags.bottom;
        s_bubbleLeft     = newBubbleFlags.left;
        s_bubbleRight    = newBubbleFlags.right;
        s_bubbleTypeH    = newBubbleFlags.hLabelType;
        s_bubbleTypeV    = newBubbleFlags.vLabelType;

        saveGridCenterSettings(m_scene);
    }
    updatePrompt();
}
