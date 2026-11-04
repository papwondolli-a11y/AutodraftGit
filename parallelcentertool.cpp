#include "parallelcentertool.h"
#include "parallelcentersetupdialog.h"
#include "drawingsettingscommand.h"

#include "arcitem.h"
#include "cadcommands.h"
#include "circleitem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "lineitem.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "snapmanager.h"
#include "tooldefaults.h"
#include <QKeyEvent>
#include <QUndoStack>
#include <QWidget>
#include <QtMath>

namespace {

// 클릭된 그래픽 아이템(또는 그 부모)이 LineItem/CircleItem/ArcItem 중 하나이면
// 그 아이템을 돌려주고, 아니면 nullptr.
QGraphicsItem *resolveCenterableItem(QGraphicsItem *rawItem) {
    auto tryCast = [](QGraphicsItem *it) -> QGraphicsItem * {
        if (!it)
            return nullptr;
        if (dynamic_cast<LineItem *>(it))
            return it;
        if (dynamic_cast<CircleItem *>(it))
            return it;
        if (dynamic_cast<ArcItem *>(it))
            return it;
        return nullptr;
    };
    if (QGraphicsItem *hit = tryCast(rawItem))
        return hit;
    if (rawItem && rawItem->parentItem())
        return tryCast(rawItem->parentItem());
    return nullptr;
}

bool readCircularInfo(QGraphicsItem *item, ParallelCenterTool::CircularInfo &out) {
    if (CircleItem *c = dynamic_cast<CircleItem *>(item)) {
        MyCircle data = c->getCircle();
        out.center = data.center.toPointF();
        out.radius = data.radius;
        out.isArc = false;
        return true;
    }
    if (ArcItem *a = dynamic_cast<ArcItem *>(item)) {
        MyArc data = a->getArc();
        out.center = data.center.toPointF();
        out.radius = data.radius;
        out.isArc = true;
        out.startAngle = data.startAngle;
        out.spanAngle = data.spanAngle;
        return true;
    }
    return false;
}

} // namespace

double ParallelCenterTool::s_offset = 0.0;

// ── 도구 기본값 저장/불러오기 (3-2: 도면 스코프 우선) ─────────────────────────
//   네임스페이스 "ParallelCenterTool" = 기존 QSettings 그룹명 그대로.
static void saveParallelCenterSettings(MyGraphicsScene *scene) {
    ToolDefaults d("ParallelCenterTool", scene);
    d.put("Offset", ParallelCenterTool::s_offset);
}

static void loadParallelCenterSettings(MyGraphicsScene *scene) {
    ToolDefaults d("ParallelCenterTool", scene);
    ParallelCenterTool::s_offset = d.getDouble("Offset", 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────

ParallelCenterTool::ParallelCenterTool(QObject *parent)
    : AbstractTool(parent), m_state(SelectingFirst),
      m_firstItem(nullptr), m_secondItem(nullptr)
{
    // 생성 시점엔 씬이 없으므로 전역/코드 기본값만. 도면값은 activate()에서 반영.
    loadParallelCenterSettings(nullptr);
}

ParallelCenterTool::~ParallelCenterTool() {}

void ParallelCenterTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;

    AbstractTool::activate(scene);
    m_state = SelectingFirst;
    m_firstItem = nullptr;
    m_secondItem = nullptr;

    // [3-2] 현재 도면의 오프셋 기본값 반영(도면값 없으면 전역/코드 기본값).
    loadParallelCenterSettings(scene);

    if (m_scene) {
        hideGripsOfSelectedItems();
        m_scene->clearSelection();
    }
    updatePrompt();
}

void ParallelCenterTool::deactivate() {
    if (m_scene)
        m_scene->clearSelection();
    AbstractTool::deactivate();
}

// ── onFinish / onCancel ───────────────────────────────────────────────────────
void ParallelCenterTool::onFinish() {
    resetSelection();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
        ctx->commandDock()->setPrompt(" Command : ");
    }
}

void ParallelCenterTool::onCancel() {
    resetSelection();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage("*Cancel*", CommandDock::Info);
        ctx->commandDock()->setToolActive(false);
        ctx->commandDock()->setPrompt(" Command : ");
    }
}

// ── 프롬프트 ─────────────────────────────────────────────────────────────────
void ParallelCenterTool::updatePrompt() {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;

    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("PARALLELCENTER", CommandDock::Command);

    if (m_state == SelectingFirst)
        ctx->commandDock()->showInteractivePrompt(
            tr(" 첫 번째 직선/원/호를 선택하세요 : "), {"Setup"});
    else
        ctx->commandDock()->showInteractivePrompt(
            tr(" 두 번째 직선/원/호를 선택하세요 : "), {"Setup"});
}

// ── 선택 초기화 ───────────────────────────────────────────────────────────────
void ParallelCenterTool::resetSelection() {
    if (m_firstItem && m_scene)
        m_firstItem->setSelected(false);
    m_firstItem = nullptr;
    m_secondItem = nullptr;
    m_state = SelectingFirst;
    if (m_scene)
        m_scene->clearSelection();
}

// ── processCommand ────────────────────────────────────────────────────────────
bool ParallelCenterTool::processCommand(const QString &command) {
    const QString lower = command.toLower().trimmed();
    if (lower == "s" || lower == "setup") {
        openSetupDialog();
        return true;
    }
    return false;
}

// ── Setup 다이얼로그 (거리만) ──────────────────────────────────────────────────
void ParallelCenterTool::openSetupDialog() {
    IAppContext *ctx = getContext();
    QWidget *parentWin =
        ctx ? dynamic_cast<QWidget *>(ctx->commandDock()) : nullptr;

    ParallelCenterSetupDialog dlg(s_offset, parentWin);
    if (dlg.exec() == QDialog::Accepted) {
        s_offset = dlg.offset();
        saveParallelCenterSettings(m_scene);
    }
    updatePrompt();
}

// ── 입력 이벤트 ───────────────────────────────────────────────────────────────
void ParallelCenterTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene)
        return;

    if (button == Qt::RightButton) {
        onCancel();
        emit toolFinished();
        return;
    }
    if (button != Qt::LeftButton)
        return;

    QGraphicsItem *rawItem = itemAtPos(pos);
    QGraphicsItem *hitItem = resolveCenterableItem(rawItem);
    if (!hitItem)
        return;

    if (m_state == SelectingFirst) {
        m_firstItem = hitItem;
        m_scene->clearSelection();
        m_firstItem->setSelected(true);
        hideGripsOfSelectedItems();
        m_state = SelectingSecond;
        updatePrompt();
    } else {
        if (hitItem == m_firstItem)
            return;
        m_secondItem = hitItem;
        tryCreateCenter();
    }
}

void ParallelCenterTool::mouseMove(QPointF pos) {
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    if (ctx && ctx->snapManager()) {
        auto result = ctx->snapManager()->snapPoint(pos, m_scene, QPointF());
        m_scene->setSnapMarker(result.snappedPos, result.type);
    }
}

void ParallelCenterTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && m_state == SelectingSecond) {
        resetSelection();
        updatePrompt();
        return;
    }
    AbstractTool::keyPress(event);
}

// ── 중심선 생성 ───────────────────────────────────────────────────────────────
void ParallelCenterTool::tryCreateCenter() {
    if (!m_firstItem || !m_secondItem || !m_scene)
        return;

    IAppContext *ctx = getContext();
    if (!ctx) {
        resetSelection();
        updatePrompt();
        return;
    }

    LineItem *firstLine = dynamic_cast<LineItem *>(m_firstItem);
    LineItem *secondLine = dynamic_cast<LineItem *>(m_secondItem);
    if (firstLine && secondLine) {
        createLineCenter(ctx, firstLine, secondLine);
        return;
    }

    CircularInfo first, second;
    if (readCircularInfo(m_firstItem, first) && readCircularInfo(m_secondItem, second)) {
        createCircularCenter(ctx, first, second);
        return;
    }

    if (ctx->commandDock())
        ctx->commandDock()->logMessage(
            tr("직선-직선 또는 원/호-원/호 조합만 선택할 수 있습니다."), CommandDock::Warning);
    resetSelection();
    updatePrompt();
}

// 중심선 레이어/펜을 계산한다 (전역 중심선 레이어 사용).
static QPen buildCenterLinePen(IAppContext *ctx, Layer *&targetLayer) {
    targetLayer = nullptr;
    if (ctx->layerManager()) {
        const QString &globalLayer = DrawingSettingsCommand::s_centerLineLayer;
        if (!globalLayer.isEmpty())
            targetLayer = ctx->layerManager()->getLayer(globalLayer);
        if (!targetLayer)
            targetLayer = ctx->layerManager()->getCurrentLayer();
    }

    QPen pen;
    if (targetLayer) {
        pen = QPen(targetLayer->color());
        pen.setWidthF(targetLayer->weight());
        QString ltName = targetLayer->linetype();
        if (ltName != "Continuous" && ltName != "Continous" && ctx->lineTypeManager()) {
            pen.setStyle(Qt::CustomDashLine);
            QVector<qreal> pattern = ctx->lineTypeManager()->getQtDashPattern(ltName);
            if (!pattern.isEmpty())
                pen.setDashPattern(pattern);
            else
                pen.setStyle(Qt::SolidLine);
        }
    }
    return pen;
}

// 새로 생성된 중심선/중심원/중심호 아이템을 레이어에 배치하고 undo에 등록한다.
void ParallelCenterTool::finishNewCenterItem(IAppContext *ctx, Layer *targetLayer,
                                              QGraphicsItem *newItem) {
    if (!newItem) {
        resetSelection();
        emit toolFinished();
        return;
    }

    if (CadItem *ci = dynamic_cast<CadItem *>(newItem)) {
        if (targetLayer)
            ci->setLayerName(targetLayer->name());
    }

    if (targetLayer) {
        QGraphicsItem *layerFolder = m_scene->getLayerRoot(targetLayer->name());
        newItem->setParentItem(layerFolder);
        if (newItem->scene() == m_scene)
            m_scene->removeItem(newItem);
    }

    if (ctx->undoStack())
        ctx->undoStack()->push(new AddEntityCommand(m_scene, newItem));
    else
        m_scene->addItem(newItem);

    if (ctx->commandDock())
        ctx->commandDock()->logMessage(tr("평행 중심선 생성 완료."), CommandDock::Info);

    resetSelection();
    updatePrompt();
}

void ParallelCenterTool::createLineCenter(IAppContext *ctx, LineItem *firstLine,
                                           LineItem *secondLine) {
    MyLine l1 = firstLine->getLine();
    MyLine l2 = secondLine->getLine();

    QPointF a1 = l1.start.toPointF();
    QPointF a2 = l1.end.toPointF();
    QPointF b1 = l2.start.toPointF();
    QPointF b2 = l2.end.toPointF();

    double lenA = QLineF(a1, a2).length();
    double lenB = QLineF(b1, b2).length();

    if (lenA < 1e-9 || lenB < 1e-9) {
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(tr("선분 길이가 너무 짧습니다."), CommandDock::Warning);
        resetSelection();
        updatePrompt();
        return;
    }

    QPointF dirA((a2 - a1).x() / lenA, (a2 - a1).y() / lenA);
    QPointF dirB((b2 - b1).x() / lenB, (b2 - b1).y() / lenB);

    double cross = dirA.x() * dirB.y() - dirA.y() * dirB.x();
    if (qAbs(cross) > 0.01) {
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(tr("두 직선이 평행하지 않습니다."), CommandDock::Warning);
        resetSelection();
        updatePrompt();
        return;
    }

    QPointF dir = dirA;
    QPointF norm(-dir.y(), dir.x());

    auto dot = [](const QPointF &v, const QPointF &u) {
        return v.x() * u.x() + v.y() * u.y();
    };

    double pa1 = dot(a1, dir), pa2 = dot(a2, dir);
    double pb1 = dot(b1, dir), pb2 = dot(b2, dir);
    double na1 = dot(a1, norm);
    double nb1 = dot(b1, norm);

    double centerN;
    if (s_offset <= 0.0) {
        centerN = (na1 + nb1) / 2.0;
    } else {
        double direction = (nb1 >= na1) ? 1.0 : -1.0;
        centerN = na1 + direction * s_offset;
    }

    double spanMin = qMin(qMin(pa1, pa2), qMin(pb1, pb2));
    double spanMax = qMax(qMax(pa1, pa2), qMax(pb1, pb2));

    QPointF startPt(centerN * norm.x() + spanMin * dir.x(),
                    centerN * norm.y() + spanMin * dir.y());
    QPointF endPt(centerN * norm.x() + spanMax * dir.x(),
                  centerN * norm.y() + spanMax * dir.y());

    if (!ctx->entityFactory()) {
        resetSelection();
        emit toolFinished();
        return;
    }

    Layer *targetLayer = nullptr;
    QPen pen = buildCenterLinePen(ctx, targetLayer);

    QGraphicsItem *newItem = ctx->entityFactory()->createLine(QLineF(startPt, endPt), pen);
    finishNewCenterItem(ctx, targetLayer, newItem);
}

// ── 중심원/중심호 생성 (동심 원·호) ─────────────────────────────────────────────
void ParallelCenterTool::createCircularCenter(IAppContext *ctx, const CircularInfo &first,
                                               const CircularInfo &second) {
    const double centerDist = QLineF(first.center, second.center).length();
    if (centerDist > 1e-6) {
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(tr("두 원/호의 중심이 같아야 합니다."), CommandDock::Warning);
        resetSelection();
        updatePrompt();
        return;
    }

    double centerRadius;
    if (s_offset <= 0.0) {
        centerRadius = (first.radius + second.radius) / 2.0;
    } else {
        double direction = (second.radius >= first.radius) ? 1.0 : -1.0;
        centerRadius = first.radius + direction * s_offset;
    }

    if (centerRadius <= 1e-9) {
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(tr("계산된 반지름이 0보다 커야 합니다."), CommandDock::Warning);
        resetSelection();
        updatePrompt();
        return;
    }

    // 각도 범위: 원-원이면 완전한 원, 원-호/호-호면 호의 각도 범위(합집합)를 사용.
    bool asArc = first.isArc || second.isArc;
    double startAngle = 0.0;
    double spanAngle = 360.0;

    if (first.isArc && second.isArc) {
        // 두 번째 호의 시작각을 첫 번째 호와 가장 가까운 방향(±180도 이내)으로 맞춘 뒤
        // 두 호가 덮는 구간의 합집합(min~max)을 새 호의 범위로 삼는다.
        double delta = MyMath::normalizeAngle(second.startAngle - first.startAngle);
        if (delta > 180.0)
            delta -= 360.0;
        double start2 = first.startAngle + delta;
        double end1 = first.startAngle + first.spanAngle;
        double end2 = start2 + second.spanAngle;

        double unionStart = qMin(first.startAngle, start2);
        double unionEnd = qMax(end1, end2);
        startAngle = MyMath::normalizeAngle(unionStart);
        spanAngle = unionEnd - unionStart;
    } else if (first.isArc) {
        startAngle = first.startAngle;
        spanAngle = first.spanAngle;
    } else if (second.isArc) {
        startAngle = second.startAngle;
        spanAngle = second.spanAngle;
    }

    if (asArc && spanAngle >= 359.999)
        asArc = false; // 합집합이 사실상 완전한 원

    if (!ctx->entityFactory()) {
        resetSelection();
        emit toolFinished();
        return;
    }

    Layer *targetLayer = nullptr;
    QPen pen = buildCenterLinePen(ctx, targetLayer);

    QGraphicsItem *newItem = asArc
        ? ctx->entityFactory()->createArc(first.center, centerRadius, startAngle, spanAngle, pen)
        : ctx->entityFactory()->createCircle(first.center, centerRadius, pen);

    finishNewCenterItem(ctx, targetLayer, newItem);
}
