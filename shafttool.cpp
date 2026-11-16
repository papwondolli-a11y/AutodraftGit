#include "shafttool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "drawingsettingscommand.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "myvector.h"
#include "shaftbuilder.h"
#include "shaftdialog.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QKeyEvent>
#include <QPen>
#include <QTimer>
#include <QUndoStack>

namespace {
const char *const kToolNamespace = "ShaftTool";
const char *const kLayerBody   = "SHAFT";
const char *const kLayerCenter = "SHAFT-CEN";
const char *const kLayerDim    = "SHAFT-DIM";
const char *const kLayerThread = "SHAFT-THREAD"; // 나사 골지름선 (가는 선)
}

ShaftTool::ShaftTool(QObject *parent)
    : AbstractTool(parent)
{}

void ShaftTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    // 마지막으로 사용한 값을 다이얼로그 초기값으로 복원한다(없으면 예시 축).
    ToolDefaults defaults(kToolNamespace, scene);
    const ShaftParams saved = ShaftParams::fromJsonString(
        defaults.getString(QStringLiteral("Params"), QString()), ShaftParams::sample());

    ShaftDialog dlg(getContext());
    dlg.setParams(saved);
    if (dlg.exec() != QDialog::Accepted) {
        // 취소: activate 시점엔 toolFinished 가 아직 연결 전이라 바로 emit 하면 무시된다.
        // 입력을 막고 이벤트 루프로 종료를 미룬다.
        m_cancelled = true;
        QTimer::singleShot(0, this, &ShaftTool::finishTool);
        return;
    }

    m_params = dlg.params();
    defaults.put(QStringLiteral("Params"), m_params.toJsonString());

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            tr("축 (구간 %1개, 전체 길이 %2)")
                .arg(m_params.segments.size()).arg(m_params.totalLength()),
            CommandDock::Command);
    }
    showPositionPrompt();
}

void ShaftTool::deactivate() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    AbstractTool::deactivate();
}

void ShaftTool::showPositionPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 축 왼쪽 끝면 중심 위치 지정 (Enter=종료): "), {});
    }
}

void ShaftTool::finishTool() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    emit toolFinished();
}

void ShaftTool::ensureLayer(IAppContext *ctx, const QString &name, const QColor &color,
                            const QString &linetype, double weight) {
    if (!ctx || !ctx->layerManager() || name.isEmpty()) return;
    if (ctx->layerManager()->getLayer(name)) return;
    Layer layer(name, color, weight, linetype, false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

QGraphicsItem *ShaftTool::finalizeItem(QGraphicsItem *item, const QString &layer) {
    if (!item) return nullptr;
    IAppContext *ctx = getContext();
    if (CadItem *cad = dynamic_cast<CadItem *>(item)) cad->setLayerName(layer);
    item->setParentItem(m_scene->getLayerRoot(layer));
    if (item->scene() == m_scene) m_scene->removeItem(item);
    if (ctx && ctx->undoStack()) ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
    else                         m_scene->addItem(item);
    return item;
}

void ShaftTool::placeAt(const QPointF &origin) {
    IAppContext *ctx = getContext();
    if (m_cancelled) return;
    if (!m_scene || !ctx || !ctx->entityFactory() || !ctx->layerManager()) return;

    const ShaftGeometry geo = ShaftBuilder::build(m_params, origin);
    if (geo.outline.empty()) return;

    // 축 외형 레이어: 다이얼로그에서 고른 레이어(없으면 기본 SHAFT, 필요 시 생성)
    QString bodyLayer = m_params.bodyLayer;
    if (bodyLayer.isEmpty() || !ctx->layerManager()->getLayer(bodyLayer)) {
        bodyLayer = QString::fromLatin1(kLayerBody);
        ensureLayer(ctx, bodyLayer, QColor(255, 255, 255), QStringLiteral("Continuous"));
    }
    // 중심선 레이어: 도면 설정의 중심선 레이어, 치수 레이어: 치수 스타일의 치수선 레이어.
    // 설정이 비어 있거나 도면에 없는 레이어면 기본(SHAFT-CEN / SHAFT-DIM)을 쓰고 필요 시 만든다.
    QString centerLayer = DrawingSettingsCommand::s_centerLineLayer;
    if (centerLayer.isEmpty() || !ctx->layerManager()->getLayer(centerLayer)) {
        centerLayer = QString::fromLatin1(kLayerCenter);
        ensureLayer(ctx, centerLayer, QColor(255, 0, 0), QStringLiteral("CENTER"));
    }
    QString dimLayer = m_scene->dimensionStyle().dimLineLayer;
    if (dimLayer.isEmpty() || !ctx->layerManager()->getLayer(dimLayer)) {
        dimLayer = QString::fromLatin1(kLayerDim);
        ensureLayer(ctx, dimLayer, QColor(0, 255, 255), QStringLiteral("Continuous"));
    }

    if (!geo.threadLines.isEmpty())
        ensureLayer(ctx, QString::fromLatin1(kLayerThread), QColor(255, 255, 255), QStringLiteral("Continuous"),
                    0.09);

    const QString threadLayer = QString::fromLatin1(kLayerThread);
    const QPen bodyPen   = AbstractTool::layerPenFor(ctx, bodyLayer);
    const QPen centerPen = AbstractTool::layerPenFor(ctx, centerLayer);
    const QPen dimPen    = AbstractTool::layerPenFor(ctx, dimLayer);
    EntityFactory *ef = ctx->entityFactory();

    QUndoStack *undo = ctx->undoStack();
    if (undo) undo->beginMacro(tr("축"));

    finalizeItem(ef->createPolyline(geo.outline, true, bodyPen), bodyLayer);
    for (int i = 0; i < geo.edgeLines.size(); ++i)
        finalizeItem(ef->createLine(geo.edgeLines.at(i), bodyPen), bodyLayer);
    for (int i = 0; i < geo.keyways.size(); ++i)
        finalizeItem(ef->createPolyline(geo.keyways.at(i).pts, false, bodyPen, geo.keyways.at(i).bulges),
                     bodyLayer);
    for (int i = 0; i < geo.breakLines.size(); ++i)
        finalizeItem(ef->createPolyline(geo.breakLines.at(i).pts, false, bodyPen), bodyLayer);
    // 해칭 레이어: "그리기" 탭 해칭 툴에 설정된 레이어(없거나 삭제됐으면 현재 레이어, 그다음 축 레이어)
    QString hatchLayer = ToolDefaults("Hatch", m_scene).getString(QStringLiteral("Layer"), QString());
    if (hatchLayer.isEmpty() || !ctx->layerManager()->getLayer(hatchLayer)) {
        Layer *cur = ctx->layerManager()->getCurrentLayer();
        hatchLayer = cur ? cur->name() : bodyLayer;
    }
    const QPen hatchPen = AbstractTool::layerPenFor(ctx, hatchLayer);
    for (int i = 0; i < geo.hatchPaths.size(); ++i)
        finalizeItem(ef->createHatch(geo.hatchPaths.at(i), QStringLiteral("ANSI31"), 0.75, 0.0, hatchPen),
                     hatchLayer);
    if (!geo.threadLines.isEmpty()) {
        const QPen threadPen = AbstractTool::layerPenFor(ctx, threadLayer);
        for (int i = 0; i < geo.threadLines.size(); ++i)
            finalizeItem(ef->createLine(geo.threadLines.at(i), threadPen), threadLayer);
    }
    if (geo.hasCenterLine)
        finalizeItem(ef->createLine(geo.centerLine, centerPen), centerLayer);

    for (int i = 0; i < geo.diameterDims.size(); ++i)
        finalizeItem(ef->createDimension(geo.diameterDims.at(i), dimPen), dimLayer);
    for (int i = 0; i < geo.lengthDims.size(); ++i)
        finalizeItem(ef->createDimension(geo.lengthDims.at(i), dimPen), dimLayer);

    if (undo) undo->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            tr("축 생성 완료 (치수 %1개).").arg(geo.diameterDims.size() + geo.lengthDims.size()),
            CommandDock::Info);
    }
    showPositionPrompt();
}

bool ShaftTool::processCommand(const QString &command) {
    if (m_cancelled) return true;
    const QString cmd = command.trimmed();
    IAppContext *ctx = getContext();
    if (!ctx) return false;

    // 아무것도 입력하지 않은 Enter: 툴 종료
    if (cmd.isEmpty()) {
        finishTool();
        return true;
    }

    // 좌표 입력으로 기준점 지정
    if (ctx->parser()) {
        QPointF resultPos;
        if (ctx->parser()->parseCoordinate(cmd, QPointF(0, 0), QPointF(0, 0), false, resultPos)) {
            placeAt(resultPos);
            return true;
        }
    }
    return false;
}

void ShaftTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene || m_cancelled) return;

    if (button == Qt::RightButton) {
        finishTool();
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager()) {
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, QPointF()).snappedPos;
    }
    placeAt(snappedPos);
}

void ShaftTool::mouseMove(QPointF pos) {
    if (!m_scene || m_cancelled) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        const SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene, QPointF());
        snappedPos = res.snappedPos;
        snapType = res.type;
    }

    m_scene->setSnapMarker(snappedPos, snapType);
    updateGhostPreview(ShaftBuilder::buildPath(m_params, snappedPos), currentLayerName());
}

void ShaftTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        finishTool();
        return;
    }
    AbstractTool::keyPress(event);
}
