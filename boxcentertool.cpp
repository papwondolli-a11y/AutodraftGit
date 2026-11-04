#include "boxcentertool.h"
#include "drawingsettingscommand.h"

#include "cadcommands.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "lineitem.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "inputparser.h"
#include "snapmanager.h"
#include <QKeyEvent>
#include <QPainterPath>
#include <QUndoStack>
#include <QtMath>

BoxCenterTool::BoxCenterTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitingP1) {}

BoxCenterTool::~BoxCenterTool() {}

void BoxCenterTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;

    AbstractTool::activate(scene);
    m_state = WaitingP1;
    m_p1 = QPointF();
    m_currentMousePos = QPointF();

    if (m_scene) {
        hideGripsOfSelectedItems();
        m_scene->clearSelection();
    }
    updatePrompt();
}

void BoxCenterTool::deactivate() {
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection();
    }
    AbstractTool::deactivate();
}

void BoxCenterTool::onFinish() {
    if (m_scene)
        m_scene->clearOverlay();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
        ctx->commandDock()->setPrompt(" Command : ");
    }
}

void BoxCenterTool::onCancel() {
    if (m_scene)
        m_scene->clearOverlay();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage("*Cancel*", CommandDock::Info);
        ctx->commandDock()->setToolActive(false);
        ctx->commandDock()->setPrompt(" Command : ");
    }
}

void BoxCenterTool::updatePrompt() {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;

    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("BOXCENTER", CommandDock::Command);

    if (m_state == WaitingP1)
        ctx->commandDock()->showInteractivePrompt(
            tr(" 첫 번째 구석점(P1)을 지정하세요 : "), {});
    else
        ctx->commandDock()->showInteractivePrompt(
            tr(" 반대편 구석점(P2)을 지정하세요 : "), {});
}

bool BoxCenterTool::processCommand(const QString &command) {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->parser())
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

void BoxCenterTool::mousePress(QPointF pos, Qt::MouseButton button) {
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
        // P2 확정 → 중심선 생성
        m_currentMousePos = snapped;
        createCenterLines();
        // 연속 실행: 초기 상태로
        m_state = WaitingP1;
        m_scene->clearOverlay();
        updatePrompt();
    }
}

void BoxCenterTool::mouseMove(QPointF pos) {
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitingP2) ? m_p1 : QPointF();
        auto res = ctx->snapManager()->snapPoint(pos, m_scene, base);
        snapped = res.snappedPos;
        m_scene->setSnapMarker(snapped, res.type);
    }

    m_currentMousePos = snapped;

    if (m_state == WaitingP2)
        updateGhostBox(snapped);
}

void BoxCenterTool::updateGhostBox(const QPointF &p2) {
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
    // 가로 중심선 (너비가 있을 때) — 박스 밖으로 약간 연장
    if (hasWidth) {
        double ext = qMax(2.0, qAbs(x2 - x1) * 0.05);
        double minX = qMin(x1, x2), maxX = qMax(x1, x2);
        path.moveTo(minX - ext, cy);
        path.lineTo(maxX + ext, cy);
    }
    // 세로 중심선 (높이가 있을 때) — 박스 밖으로 약간 연장
    if (hasHeight) {
        double ext = qMax(2.0, qAbs(y2 - y1) * 0.05);
        double minY = qMin(y1, y2), maxY = qMax(y1, y2);
        path.moveTo(cx, minY - ext);
        path.lineTo(cx, maxY + ext);
    }

    updateGhostPreview(path, currentLayerName());
}

void BoxCenterTool::createCenterLines() {
    if (!m_scene)
        return;

    double x1 = m_p1.x(),              y1 = m_p1.y();
    double x2 = m_currentMousePos.x(), y2 = m_currentMousePos.y();

    bool hasWidth  = qAbs(x2 - x1) > 1e-6;
    bool hasHeight = qAbs(y2 - y1) > 1e-6;

    // P1 == P2: 생성할 선이 없음
    if (!hasWidth && !hasHeight)
        return;

    double cx = (x1 + x2) / 2.0;
    double cy = (y1 + y2) / 2.0;

    IAppContext *ctx = getContext();
    Layer *layer = nullptr;
    if (ctx && ctx->layerManager()) {
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

    // 가로 중심선: y=cy, x1~x2 — 박스 밖으로 약간 연장
    QGraphicsItem *hLine = nullptr;
    if (hasWidth) {
        double ext  = qMax(2.0, qAbs(x2 - x1) * 0.05);
        double minX = qMin(x1, x2), maxX = qMax(x1, x2);
        hLine = ctx->entityFactory()->createLine(QLineF(minX - ext, cy, maxX + ext, cy), pen);
        applyLayer(hLine);
    }

    // 세로 중심선: x=cx, y1~y2 — 박스 밖으로 약간 연장
    QGraphicsItem *vLine = nullptr;
    if (hasHeight) {
        double ext  = qMax(2.0, qAbs(y2 - y1) * 0.05);
        double minY = qMin(y1, y2), maxY = qMax(y1, y2);
        vLine = ctx->entityFactory()->createLine(QLineF(cx, minY - ext, cx, maxY + ext), pen);
        applyLayer(vLine);
    }

    if (ctx->undoStack()) {
        ctx->undoStack()->beginMacro(tr("상자 중심선"));
        if (hLine) ctx->undoStack()->push(new AddEntityCommand(m_scene, hLine));
        if (vLine) ctx->undoStack()->push(new AddEntityCommand(m_scene, vLine));
        ctx->undoStack()->endMacro();
    } else {
        if (hLine) m_scene->addItem(hLine);
        if (vLine) m_scene->addItem(vLine);
    }

    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(tr("상자 중심선 생성 완료."), CommandDock::Info);
}
