#include "plandoorbiswingtool.h"
#include "plandoorsetupdialog.h"   // Setup 다이얼로그는 PlanDoorTool 과 완전히 동일한 필드(H,V,FrameType,Layer)라 재사용
#include "planarchdata.h"
#include "spacefinish.h"
#include "scenesymbolregistry.h"
#include "wallpositiondialog.h"

#include "arcitem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "lineitem.h"
#include "myarc.h"
#include "multilinedata.h"
#include "mygraphicsscene.h"
#include "myvector.h"
#include "polylineitem.h"
#include "snapmanager.h"
#include "tooldefaults.h"
#include "doorlayersettings.h"

#include <QApplication>
#include <QSettings>
#include <QUndoStack>
#include <QUuid>
#include <QWidget>
#include <QtMath>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// 문틀 단면 치수 상수 (단위: mm) — PlanDoorTool 과 동일한 단면 규격을 사용한다.
// ─────────────────────────────────────────────────────────────────────────────

double  PlanDoorBiSwingTool::s_h                = 900.0;
double  PlanDoorBiSwingTool::s_v                = 200.0;
bool    PlanDoorBiSwingTool::s_useWallThickness = false;
int     PlanDoorBiSwingTool::s_frameType        = 3;   // 기본값 Type4
QString PlanDoorBiSwingTool::s_wallGroupId;
int     PlanDoorBiSwingTool::s_wallPositionMode  = 0;
int     PlanDoorBiSwingTool::s_lastWallPosMode   = WallPositionDialog::Inner;
double  PlanDoorBiSwingTool::s_wallDetectedSpan  = 0.0;
bool    PlanDoorBiSwingTool::s_mlAtP1            = false;
static int s_doorBiSwingSeq = 0;

// ── 그룹 UUID 태깅 (람다 대신 정적 함수) ─────────────────────────────────────
static void tagBiSwingDoorItemWithGroupId(QGraphicsItem *item, const QString &uuid)
{
    item->setData(PD_DATA_GROUP_ID, uuid);
}

// ── 도구 기본값 저장/불러오기 (3-2: 도면 스코프 우선, 전역은 씨앗) ─────────────
//   네임스페이스 "PlanDoorBiSwingTool" = 기존 QSettings 그룹명 그대로.
static void savePlanDoorBiSwingSettings(MyGraphicsScene *scene)
{
    ToolDefaults d("PlanDoorBiSwingTool", scene);
    d.put("H",                PlanDoorBiSwingTool::globalH());
    d.put("V",                PlanDoorBiSwingTool::globalV());
    d.put("UseWallThickness", PlanDoorBiSwingTool::useWallThickness());
    d.put("FrameType",        PlanDoorBiSwingTool::frameType());
}

static void loadPlanDoorBiSwingSettings(MyGraphicsScene *scene)
{
    DoorLayerSettings::load(scene);
    SpaceFinish::load(scene);
    ToolDefaults d("PlanDoorBiSwingTool", scene);
    PlanDoorBiSwingTool::setGlobalH(d.getDouble("H",                900.0));
    PlanDoorBiSwingTool::setGlobalV(d.getDouble("V",                200.0));
    PlanDoorBiSwingTool::setUseWallThickness(d.getBool("UseWallThickness", false));
    PlanDoorBiSwingTool::setFrameType(d.getInt("FrameType",      3));
}

double  PlanDoorBiSwingTool::globalH()                        { return s_h; }
void    PlanDoorBiSwingTool::setGlobalH(double v)             { if (v > 0.0) s_h = v; }
double  PlanDoorBiSwingTool::globalV()                        { return s_v; }
void    PlanDoorBiSwingTool::setGlobalV(double v)             { if (v > 0.0) s_v = v; }
bool    PlanDoorBiSwingTool::useWallThickness()               { return s_useWallThickness; }
void    PlanDoorBiSwingTool::setUseWallThickness(bool use)    { s_useWallThickness = use; }
int     PlanDoorBiSwingTool::frameType()                      { return s_frameType; }
void    PlanDoorBiSwingTool::setFrameType(int t)              { s_frameType = qBound(0, t, 3); }
QString PlanDoorBiSwingTool::frameLayer()                     { return DoorLayerSettings::frameLayer(); }
void    PlanDoorBiSwingTool::setFrameLayer(const QString &n)  { DoorLayerSettings::setFrameLayer(n); }
QString PlanDoorBiSwingTool::otherLayer()                         { return DoorLayerSettings::otherLayer(); }
void    PlanDoorBiSwingTool::setOtherLayer(const QString &n)      { DoorLayerSettings::setOtherLayer(n); }
QString PlanDoorBiSwingTool::trimExcludeLayer()                   { return DoorLayerSettings::trimExcludeLayer(); }
void    PlanDoorBiSwingTool::setTrimExcludeLayer(const QString &n){ DoorLayerSettings::setTrimExcludeLayer(n); }

CommandBase *PlanDoorBiSwingTool::createCommand(IAppContext *c)
{
    return new PlanDoorBiSwingCommand(c);
}

// ── 생성자 ───────────────────────────────────────────────────────────────────
PlanDoorBiSwingTool::PlanDoorBiSwingTool(QObject *parent)
    : AbstractTool(parent)
    , m_state(WaitP1)
    , m_currentMousePos(0.0, 0.0)
{}

// ─────────────────────────────────────────────────────────────────────────────
// applySnapForState — 상태별 osnap 자동 전환
//   WaitP1 / WaitP3 : Nearest ON,  Perpendicular OFF
//   WaitP2          : Nearest OFF, Perpendicular ON
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::applySnapForState(State s)
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->snapManager())
        return;

    SnapManager *sm        = ctx->snapManager();
    const bool  wantNearest = (s == WaitP1 || s == WaitP3);
    const bool  wantPerp    = (s == WaitP2);

    if (sm->isSnapNearest()       != wantNearest) sm->toggleSnapNearest();
    if (sm->isSnapPerpendicular() != wantPerp)    sm->toggleSnapPerpendicular();
}

// ── restoreSavedSnap ────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::restoreSavedSnap()
{
    if (!m_snapSaved)
        return;

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->snapManager())
        return;

    SnapManager *sm = ctx->snapManager();
    if (sm->isSnapNearest()       != m_savedNearest) sm->toggleSnapNearest();
    if (sm->isSnapPerpendicular() != m_savedPerp)    sm->toggleSnapPerpendicular();
    m_snapSaved = false;
}

// ── activate ────────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::activate(MyGraphicsScene *scene)
{
    // [3-2] 매 활성화마다 현재 도면의 값으로 다시 채운다(도면 전환 반영).
    loadPlanDoorBiSwingSettings(scene);

    if (m_scene == scene)
        return;

    AbstractTool::activate(scene);
    m_state = WaitP1;

    // 현재 스냅 상태 저장 후 P1 스냅 적용
    IAppContext *ctx = getContext();
    if (ctx && ctx->snapManager()) {
        m_savedNearest = ctx->snapManager()->isSnapNearest();
        m_savedPerp    = ctx->snapManager()->isSnapPerpendicular();
        m_snapSaved    = true;
        applySnapForState(WaitP1);
    }

    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("PLAN-DOOR-BISWING", CommandDock::Command);
    }

    showP1Prompt();
}

// ── deactivate ───────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::deactivate()
{
    restoreSavedSnap();

    m_state = WaitP1;
    if (m_scene)
        m_scene->clearOverlay();
    AbstractTool::deactivate();
}

// ── showP1Prompt ─────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::showP1Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(tr(" P1: 도어가 들어갈 선  지정 (nea) "), {"Setup"});
}

// ── showP1DistancePrompt ─────────────────────────────────────────────────────
void PlanDoorBiSwingTool::showP1DistancePrompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(tr(" 거리를 입력하세요 : "), {});
}

// captureP1WallDirs: collect the (non-parallel) directions of every multiline
// segment passing through the anchor. At a corner both directions become candidates.
void PlanDoorBiSwingTool::captureP1WallDirs(const QPointF &pos)
{
    m_p1WallDirs.clear();
    m_p1WallDir = QPointF(0, 0);
    if (!m_scene) return;

    const double kPerpTol = 15.0;
    const double kExtend  = 50.0;

    const QList<QGraphicsItem *> allItems = m_scene->items();
    for (QGraphicsItem *gi : allItems) {
        LineItem *li = dynamic_cast<LineItem *>(gi);
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString().isEmpty()) continue;

        const MyLine  ln  = li->getLine();
        const QPointF lp1(ln.start.x, ln.start.y);
        const QPointF lp2(ln.end.x,   ln.end.y);
        const QPointF seg = lp2 - lp1;
        const double  len = std::hypot(seg.x(), seg.y());
        if (len < 1e-10) continue;

        const QPointF dn = seg / len;
        const QPointF d  = pos - lp1;
        const double  t  = dn.x() * d.x() + dn.y() * d.y();
        if (t < -kExtend || t > len + kExtend) continue;
        if (qAbs(dn.x() * d.y() - dn.y() * d.x()) > kPerpTol) continue;

        bool dup = false;
        for (const QPointF &e : m_p1WallDirs) {
            if (qAbs(e.x() * dn.y() - e.y() * dn.x()) < 0.05) { dup = true; break; }
        }
        if (!dup) m_p1WallDirs.append(dn);
    }
    if (!m_p1WallDirs.isEmpty())
        m_p1WallDir = m_p1WallDirs.first();
}

// resolveP1WallDir: among the candidates, pick the one closest to the anchor->mouse direction.
QPointF PlanDoorBiSwingTool::resolveP1WallDir() const
{
    if (m_p1WallDirs.size() <= 1)
        return m_p1WallDir;
    const QPointF d = m_currentMousePos - m_p1Anchor;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1e-9)
        return m_p1WallDir;
    QPointF best = m_p1WallDirs.first();
    double bestDot = -1.0;
    for (const QPointF &c : m_p1WallDirs) {
        const double v = qAbs(c.x() * d.x() + c.y() * d.y()) / len;
        if (v > bestDot) { bestDot = v; best = c; }
    }
    return best;
}

// ── computeP1FromAnchor ──────────────────────────────────────────────────────
QPointF PlanDoorBiSwingTool::computeP1FromAnchor(double dist) const
{
    QPointF dir = resolveP1WallDir();
    if (dir.isNull()) {
        QPointF d = m_currentMousePos - m_p1Anchor;
        double len = std::hypot(d.x(), d.y());
        if (len < 1e-9) return m_p1Anchor;
        dir = QPointF(d.x() / len, d.y() / len);
    } else {
        QPointF toMouse = m_currentMousePos - m_p1Anchor;
        double dot = toMouse.x() * dir.x() + toMouse.y() * dir.y();
        if (dot < 0.0) dir = QPointF(-dir.x(), -dir.y());
    }
    return m_p1Anchor + dir * dist;
}

// ── showP2Prompt ─────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::showP2Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    if (s_useWallThickness)
        ctx->commandDock()->showInteractivePrompt(
            tr(" P2: 반대편 선  지정 (per) — 두께 자동 측정 "), {});
    else
        ctx->commandDock()->showInteractivePrompt(
            tr(" P2: 반대편 선 지정 (per) "), {});
}

// ── showP3Prompt ─────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::showP3Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(
        tr(" P3: 방향점 지정 (nea) — 문틀 진행 방향 "), {});
}

// ── mouseMove ────────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::mouseMove(QPointF pos)
{
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    QPointF   finalPos = pos;
    SnapType  snapType = SnapType::None;

    if (ctx && ctx->snapManager()) {
        if (m_state == WaitP2) {
            auto result = ctx->snapManager()->snapPoint(pos, m_scene, m_p1);
            finalPos = result.snappedPos;
            snapType = result.type;
        } else {
            auto result = ctx->snapManager()->snapPoint(pos, m_scene);
            finalPos = result.snappedPos;
            snapType = result.type;
        }
    }

    m_currentMousePos = finalPos;
    m_scene->setSnapMarker(finalPos, snapType);

    if (m_state == WaitP1Distance) {
        QPointF dir = resolveP1WallDir();
        if (dir.isNull()) {
            QPointF d = finalPos - m_p1Anchor;
            double len = std::hypot(d.x(), d.y());
            if (len > 1e-9) dir = QPointF(d.x() / len, d.y() / len);
            else dir = QPointF(1, 0);
        } else {
            QPointF toMouse = finalPos - m_p1Anchor;
            double dot = toMouse.x() * dir.x() + toMouse.y() * dir.y();
            if (dot < 0.0) dir = QPointF(-dir.x(), -dir.y());
        }
        double projLen = (finalPos - m_p1Anchor).x() * dir.x()
                       + (finalPos - m_p1Anchor).y() * dir.y();
        if (projLen < 0) projLen = 0;
        QPainterPath path;
        path.moveTo(m_p1Anchor);
        path.lineTo(m_p1Anchor + dir * projLen);
        m_scene->setGhostPath(path, true, QColor(255, 200, 0));

    } else if (m_state == WaitP2) {
        QPainterPath path;
        path.moveTo(m_p1);
        path.lineTo(finalPos);
        updateGhostPreview(path, currentLayerName());

    } else if (m_state == WaitP3) {
        double angleDeg, V;
        bool   flipY;
        if (computeFrom3Pts(m_p1, m_p2, finalPos, angleDeg, V, flipY))
            updateGhostDoor(m_p1, angleDeg, V, flipY);
        else
            m_scene->clearOverlay();
    }
}

// ── mousePress ───────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        if (m_state == WaitP1Distance) {
            m_state = WaitP1;
            m_scene->clearOverlay();
            applySnapForState(WaitP1);
            showP1Prompt();
        } else if (m_state == WaitP1) {
            onCancel();
        } else if (m_state == WaitP2) {
            m_state = WaitP1;
            m_scene->clearOverlay();
            applySnapForState(WaitP1);
            showP1Prompt();
        } else {
            m_state = WaitP2;
            QPainterPath path;
            path.moveTo(m_p1);
            path.lineTo(m_currentMousePos);
            updateGhostPreview(path, currentLayerName());
            applySnapForState(WaitP2);
            showP2Prompt();
        }
        return;
    }

    if (button != Qt::LeftButton)
        return;

    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        if (m_state == WaitP2)
            snapped = ctx->snapManager()->snapPoint(pos, m_scene, m_p1).snappedPos;
        else
            snapped = ctx->snapManager()->snapPoint(pos, m_scene).snappedPos;
    }

    if (m_state == WaitP1) {
        Qt::KeyboardModifiers mods = QApplication::queryKeyboardModifiers();
        if (mods & Qt::ShiftModifier) {
            m_p1Anchor = snapped;
            captureP1WallDirs(snapped);
            m_state = WaitP1Distance;
            m_scene->clearOverlay();
            showP1DistancePrompt();
            return;
        }

        m_p1 = snapped;
        {
            LineItem *ml   = findNearestMultilineAt(m_p1);
            s_wallGroupId  = ml ? ml->data(ML_DATA_GROUP_ID).toString() : QString();
            s_wallPositionMode = 0;
        }
        s_mlAtP1 = !s_wallGroupId.isEmpty();

        m_state = WaitP2;
        applySnapForState(WaitP2);
        QPainterPath path;
        path.moveTo(m_p1);
        path.lineTo(m_currentMousePos);
        updateGhostPreview(path, currentLayerName());
        showP2Prompt();

    } else if (m_state == WaitP2) {
        m_p2 = snapped;
        checkWallPositionIfNeeded();
        m_state = WaitP3;
        applySnapForState(WaitP3);
        double angleDeg, V;
        bool   flipY;
        if (computeFrom3Pts(m_p1, m_p2, m_currentMousePos, angleDeg, V, flipY))
            updateGhostDoor(m_p1, angleDeg, V, flipY);
        showP3Prompt();

    } else {
        double angleDeg, V;
        bool   flipY;
        if (!computeFrom3Pts(m_p1, m_p2, snapped, angleDeg, V, flipY)) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(
                    tr("P1·P2·P3 가 너무 가깝거나 일직선입니다."), CommandDock::Info);
            return;
        }
        m_scene->clearOverlay();
        placeDoor(m_p1, angleDeg, V, flipY);
        m_state = WaitP1;
        applySnapForState(WaitP1);
        showP1Prompt();
    }
}

// ── mouseRelease ─────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/) {}

// ── processCommand ────────────────────────────────────────────────────────────
bool PlanDoorBiSwingTool::processCommand(const QString &command)
{
    IAppContext *ctx = getContext();
    const QString cmd = command.trimmed();

    if (m_state == WaitP1Distance) {
        bool ok;
        double dist = cmd.toDouble(&ok);
        if (ok && dist > 0.0) {
            m_p1 = computeP1FromAnchor(dist);
            {
                LineItem *ml  = findNearestMultilineAt(m_p1);
                s_wallGroupId = ml ? ml->data(ML_DATA_GROUP_ID).toString() : QString();
                s_wallPositionMode = 0;
            }
            s_mlAtP1 = !s_wallGroupId.isEmpty();
            m_state = WaitP2;
            applySnapForState(WaitP2);
            if (m_scene) m_scene->clearOverlay();
            QPainterPath path;
            path.moveTo(m_p1);
            path.lineTo(m_currentMousePos);
            updateGhostPreview(path, currentLayerName());
            showP2Prompt();
            return true;
        }
        return false;
    }

    if (m_state == WaitP1) {
        const QString lower = cmd.toLower();
        if (lower == "s" || lower == "setup") {
            QWidget *parentWin =
                ctx ? dynamic_cast<QWidget *>(ctx->commandDock()) : nullptr;

            // Setup 필드(H·V·FrameType·Layer)가 PlanDoorTool 과 완전히 동일하므로
            // 별도 다이얼로그를 새로 만들지 않고 PlanDoorSetupDialog 를 그대로 재사용한다.
            PlanDoorSetupDialog dlg(s_h, s_v, s_useWallThickness, s_frameType,
                                    parentWin);
            dlg.setSpaceFinishType(SpaceFinish::type());
            if (dlg.exec() == QDialog::Accepted) {
                SpaceFinish::setType(dlg.spaceFinishType());
                SpaceFinish::save(m_scene);
                setGlobalH(dlg.h());
                setGlobalV(dlg.v());
                setUseWallThickness(dlg.useWallThickness());
                setFrameType(dlg.frameType());
                savePlanDoorBiSwingSettings(m_scene);
            }
            showP1Prompt();
            return true;
        }

        if (ctx && ctx->parser()) {
            QPointF resultPos;
            const bool isOrtho = ctx->snapManager() && ctx->snapManager()->isOrtho();
            if (ctx->parser()->parseCoordinate(
                    cmd, m_currentMousePos, m_currentMousePos, isOrtho, resultPos)) {
                m_p1 = resultPos;
                {
                    LineItem *ml   = findNearestMultilineAt(m_p1);
                    s_wallGroupId  = ml ? ml->data(ML_DATA_GROUP_ID).toString() : QString();
                    s_wallPositionMode = 0;
                }
                s_mlAtP1 = !s_wallGroupId.isEmpty();
                m_state = WaitP2;
                applySnapForState(WaitP2);
                showP2Prompt();
                return true;
            }
        }
    }

    if (m_state == WaitP2) {
        if (ctx && ctx->parser()) {
            QPointF resultPos;
            const bool isOrtho = ctx->snapManager() && ctx->snapManager()->isOrtho();
            if (ctx->parser()->parseCoordinate(
                    cmd, m_p1, m_currentMousePos, isOrtho, resultPos)) {
                m_p2 = resultPos;
                checkWallPositionIfNeeded();
                m_state = WaitP3;
                applySnapForState(WaitP3);
                showP3Prompt();
                return true;
            }
        }
    }

    if (m_state == WaitP3) {
        if (ctx && ctx->parser()) {
            QPointF resultPos;
            const bool isOrtho = ctx->snapManager() && ctx->snapManager()->isOrtho();
            if (ctx->parser()->parseCoordinate(
                    cmd, m_p2, m_currentMousePos, isOrtho, resultPos)) {
                double angleDeg, V;
                bool   flipY;
                if (computeFrom3Pts(m_p1, m_p2, resultPos, angleDeg, V, flipY)) {
                    if (m_scene) m_scene->clearOverlay();
                    placeDoor(m_p1, angleDeg, V, flipY);
                    m_state = WaitP1;
                    applySnapForState(WaitP1);
                    showP1Prompt();
                }
                return true;
            }
        }
    }

    return false;
}

// ── onFinish ─────────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::onFinish()
{
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

// ── onCancel ─────────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::onCancel()
{
    m_state = WaitP1;
    if (m_scene)
        m_scene->clearOverlay();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage("*Cancel*", CommandDock::Info);
    onFinish();
}

// ─────────────────────────────────────────────────────────────────────────────
// computeFrom3Pts — PlanDoorTool 과 완전히 동일한 계산 규칙.
// flipY 는 문틀(잼브)·벽 두께 계산에 그대로 쓰이지만, 스윙호 자체는 좌우 대칭이라
// flipY 부호와 무관하게 동일한 반원을 그린다 (updateGhostDoor/buildGeometry 참고).
// ─────────────────────────────────────────────────────────────────────────────
bool PlanDoorBiSwingTool::computeFrom3Pts(
    const QPointF &p1, const QPointF &p2, const QPointF &p3,
    double &outAngleDeg, double &outV, bool &outFlipY) const
{
    const double dx3  = p3.x() - p1.x();
    const double dy3  = p3.y() - p1.y();
    const double len3 = std::sqrt(dx3 * dx3 + dy3 * dy3);
    if (len3 < 1.0)
        return false;

    const double ux = dx3 / len3;
    const double uy = dy3 / len3;
    const double nx = -uy;      // 90°CCW
    const double ny =  ux;

    const double dx2     = p2.x() - p1.x();
    const double dy2     = p2.y() - p1.y();
    const double vSigned = dx2 * nx + dy2 * ny;

    const double minV = s_useWallThickness ? 10.0 : 1.0;
    if (std::abs(vSigned) < minV)
        return false;

    outAngleDeg = qRadiansToDegrees(std::atan2(uy, ux));
    outV        = s_useWallThickness ? std::abs(vSigned) + 20.0 : s_v;
    outFlipY    = (vSigned < 0.0);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ── 좌표 변환 헬퍼 (람다 대체용 함수 객체 — PlanDoorTool 의 DoorPtCalc 패턴과 동일) ──
// ─────────────────────────────────────────────────────────────────────────────
struct DoorBiSwingPtCalc {
    double ox, oy, c, s2;
    bool   flipY;
    MyVector operator()(double dx, double dy) const {
        if (!flipY)
            return MyVector(ox + dx*c - dy*s2, oy + dx*s2 + dy*c);
        else
            return MyVector(ox + dx*c + dy*s2, oy + dx*s2 - dy*c);
    }
};

static QPointF doorBiSwingPointF(const DoorBiSwingPtCalc &pt, double dx, double dy)
{
    const MyVector v = pt(dx, dy);
    return QPointF(v.x, v.y);
}

static void appendClosedPolygon(QPainterPath &path, const std::vector<QPointF> &pts)
{
    if (pts.empty()) return;
    path.moveTo(pts.front());
    for (std::size_t i = 1; i < pts.size(); ++i)
        path.lineTo(pts[i]);
    path.closeSubpath();
}

// static void appendOpenPolyline(QPainterPath &path, const std::vector<QPointF> &pts)
// {
//     if (pts.empty()) return;
//     path.moveTo(pts.front());
//     for (std::size_t i = 1; i < pts.size(); ++i)
//         path.lineTo(pts[i]);
// }


// ── 세계좌표 ↔ 로컬좌표 변환 (트림 함수 공용, 람다 대체) ─────────────────────
static QPointF doorBiSwingWorldToLocal(double ox, double oy, double c, double s2,
                                       const MyVector &p)
{
    const double dx = p.x - ox, dy = p.y - oy;
    return QPointF(dx * c + dy * s2, dy * c - dx * s2);
}

static MyVector doorBiSwingLocalToWorld(double ox, double oy, double c, double s2,
                                        double lx, double ly)
{
    return MyVector(ox + lx * c - ly * s2, oy + lx * s2 + ly * c);
}

static MyVector doorBiSwingInterp(double ox, double oy, double c, double s2,
                                  double lx1, double ly1, double lx2, double ly2,
                                  double cutX)
{
    const double span = lx2 - lx1;
    const double t    = span > 1e-9 ? (cutX - lx1) / span : 0.0;
    return doorBiSwingLocalToWorld(ox, oy, c, s2, cutX, ly1 + t * (ly2 - ly1));
}

static void copyDoorBiSwingLineAppearance(LineItem *dst, const LineItem *src)
{
    dst->setLayerName(src->layerName());
    dst->setPen(src->pen());
}

static void copyDoorBiSwingLineMlTags(LineItem *dst, const LineItem *src)
{
    copyDoorBiSwingLineAppearance(dst, src);
    dst->setData(ML_DATA_GROUP_ID, src->data(ML_DATA_GROUP_ID));
    dst->setData(ML_DATA_ROLE,     src->data(ML_DATA_ROLE));
    const double sp = src->data(ML_DATA_SPACING).toDouble();
    if (sp > 0.0) dst->setData(ML_DATA_SPACING, sp);
}

// ─────────────────────────────────────────────────────────────────────────────
// updateGhostDoor — 양방향 여닫이(자재문) 고스트 미리보기
//
// 문틀(잼브)·연결선은 PlanDoorTool 과 동일. 경첩은 벽 두께 정중앙(local Y=V/2)에
// 위치하며, 문짝은 경첩을 지나는 직선 하나로 근접면 바깥(-R)·원접면 바깥(+R)
// 양쪽 극단까지 뻗는다. 스윙호는 그 두 극단을, 닫힘 기준선(local +X, 문틀 진행
// 방향)을 지나며 잇는 180° 호 하나로 표현한다.
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::updateGhostDoor(
    const QPointF &p1, double angleDeg, double V, bool flipY)
{
    if (!m_scene)
        return;

    const double H  = s_h;
    const double J  = DoorLayerSettings::jambOuter();
    const double S  = DoorLayerSettings::stopWidth();
    const double K  = DoorLayerSettings::stepHeight();
    const double HX = J;                        // 경첩 local X 위치 — 근접 잼브 두께(J)만큼 안쪽으로 이동
    const double R  = H - 2.0 * J;             // 문짝 스윙 반경 — 경첩(HX=J)에서 원접 잼브 안쪽 면(H-J)까지
    const double HY = V / 2.0;                 // 경첩 local Y 위치 (벽 두께 정중앙)

    double ox = p1.x(), oy = p1.y();
    const double c  = std::cos(qDegreesToRadians(angleDeg));
    const double s2 = std::sin(qDegreesToRadians(angleDeg));

    // 벽두께 모드: 내/외벽 10mm 돌출 원점 보정 (실내 방향)
    if (s_useWallThickness) {
        if (!flipY) { ox += 10.0 * s2; oy -= 10.0 * c; }
        else        { ox -= 10.0 * s2; oy += 10.0 * c; }
    } else if (s_wallPositionMode != 0) {
        double nearLy, farLy, delta;
        if (!s_wallGroupId.isEmpty() && s_mlAtP1)
            delta = computeWallPositionDelta(c, s2, p1, flipY, nearLy, farLy);
        else
            delta = computeWallPositionDeltaFromSpan(nearLy, farLy);
        if (!flipY) { ox -= delta * s2; oy += delta * c; }
        else        { ox += delta * s2; oy -= delta * c; }
    }

    const DoorBiSwingPtCalc pt { ox, oy, c, s2, flipY };

    QPainterPath path;

    // 문틀-문틀 연결선(connLines)은 그리지 않는다 — 좌우 잼브만 표시.
    switch (s_frameType) {
    case 1:
        appendClosedPolygon(path, { doorBiSwingPointF(pt,0,0), doorBiSwingPointF(pt,0,V),
                                     doorBiSwingPointF(pt,S,V), doorBiSwingPointF(pt,S,V-K),
                                     doorBiSwingPointF(pt,J,V-K), doorBiSwingPointF(pt,J,0) });
        appendClosedPolygon(path, { doorBiSwingPointF(pt,H,0), doorBiSwingPointF(pt,H,V),
                                     doorBiSwingPointF(pt,H-S,V), doorBiSwingPointF(pt,H-S,V-K),
                                     doorBiSwingPointF(pt,H-J,V-K), doorBiSwingPointF(pt,H-J,0) });
        break;
    case 2:
        appendClosedPolygon(path, { doorBiSwingPointF(pt,0,0), doorBiSwingPointF(pt,0,V),
                                     doorBiSwingPointF(pt,S,V), doorBiSwingPointF(pt,S,V-K),
                                     doorBiSwingPointF(pt,J,V-K), doorBiSwingPointF(pt,J,K),
                                     doorBiSwingPointF(pt,S,0) });
        appendClosedPolygon(path, { doorBiSwingPointF(pt,H,0), doorBiSwingPointF(pt,H,V),
                                     doorBiSwingPointF(pt,H-S,V), doorBiSwingPointF(pt,H-S,V-K),
                                     doorBiSwingPointF(pt,H-J,V-K), doorBiSwingPointF(pt,H-J,K),
                                     doorBiSwingPointF(pt,H-S,0) });
        break;
    case 3:
        appendClosedPolygon(path, { doorBiSwingPointF(pt,0,0), doorBiSwingPointF(pt,0,V),
                                     doorBiSwingPointF(pt,J,V), doorBiSwingPointF(pt,J,0) });
        appendClosedPolygon(path, { doorBiSwingPointF(pt,H,0), doorBiSwingPointF(pt,H,V),
                                     doorBiSwingPointF(pt,H-J,V), doorBiSwingPointF(pt,H-J,0) });
        break;
    default:
        appendClosedPolygon(path, { doorBiSwingPointF(pt,0,0), doorBiSwingPointF(pt,0,V),
                                     doorBiSwingPointF(pt,S,V), doorBiSwingPointF(pt,S,V-K),
                                     doorBiSwingPointF(pt,J,V-K), doorBiSwingPointF(pt,J,K),
                                     doorBiSwingPointF(pt,S,K), doorBiSwingPointF(pt,S,0) });
        appendClosedPolygon(path, { doorBiSwingPointF(pt,H,0), doorBiSwingPointF(pt,H,V),
                                     doorBiSwingPointF(pt,H-S,V), doorBiSwingPointF(pt,H-S,V-K),
                                     doorBiSwingPointF(pt,H-J,V-K), doorBiSwingPointF(pt,H-J,K),
                                     doorBiSwingPointF(pt,H-S,K), doorBiSwingPointF(pt,H-S,0) });
        break;
    }

    // 문짝: 경첩(HX,HY)을 지나 양쪽 극단(HY-R, HY+R)까지 뻗는 얇은 사각형.
    // 문짝 두께 폭(HX~HX+J)이 잼브 폭(0~J)과 정확히 일치하도록 정렬한다.
    appendClosedPolygon(path, { doorBiSwingPointF(pt, HX,   HY - R),
                                 doorBiSwingPointF(pt, HX+J, HY - R),
                                 doorBiSwingPointF(pt, HX+J, HY + R),
                                 doorBiSwingPointF(pt, HX,   HY + R) });

    // 경첩 표시선: 문짝 중앙(local Y=HY, 벽 두께 정중앙)에서 문짝 왼쪽·오른쪽을 잇는 짧은 가로선
    path.moveTo(doorBiSwingPointF(pt, HX,   HY));
    path.lineTo(doorBiSwingPointF(pt, HX+J, HY));

    // 스윙호: 두 극단을 닫힘 기준선(local +X) 경유하여 잇는 180° 반원.
    // 180° 는 시작·끝점이 정반대(대칭)라 회전 방향(부호)과 무관하게 동일한 호를 그린다.
    {
        QPointF cen = doorBiSwingPointF(pt, HX, HY);
        QRectF  rect(cen.x() - R, cen.y() - R, R * 2.0, R * 2.0);
        const double drawStart = -(angleDeg - 90.0);
        const double drawSpan  = -180.0;   // Qt 각도는 CAD 와 부호 반대: CAD +180°(CCW, +X 경유) = Qt -180°
        path.arcMoveTo(rect, drawStart);
        path.arcTo(rect, drawStart, drawSpan);
    }

    updateGhostPreview(path, currentLayerName());
}

// ── buildGeometry ─────────────────────────────────────────────────────────────
// DoorBuildParams 는 PlanDoorTool 과 공용 (plandoortool.h 에 정의됨). h1 필드는 미사용.
QList<QGraphicsItem*> PlanDoorBiSwingTool::buildGeometry(const DoorBuildParams &pIn, IAppContext *ctx)
{
    // 지정 레이어가 도면에 없거나 비어 있으면 현재 레이어로 대체 (속성 편집 재생성 경로 포함)
    DoorBuildParams p = pIn;
    p.frameLayer = AbstractTool::resolveLayerName(ctx, p.frameLayer);
    p.otherLayer = AbstractTool::resolveLayerName(ctx, p.otherLayer);

    const double H  = p.H;
    const double V  = p.V;
    const double J  = DoorLayerSettings::jambOuter();
    const double S  = DoorLayerSettings::stopWidth();
    const double K  = DoorLayerSettings::stepHeight();
    const double HX = J;                        // 경첩 local X 위치 — 근접 잼브 두께(J)만큼 안쪽으로 이동
    const double R  = H - 2.0 * J;             // 문짝 스윙 반경 — 경첩(HX=J)에서 원접 잼브 안쪽 면(H-J)까지
    const double HY = V / 2.0;                 // 경첩 local Y 위치 (벽 두께 정중앙)

    const double c  = std::cos(qDegreesToRadians(p.angleDeg));
    const double s2 = std::sin(qDegreesToRadians(p.angleDeg));
    const DoorBiSwingPtCalc pt { p.ox, p.oy, c, s2, p.flipY };

    // 레이어 색·두께·선종류 적용, 레이어가 없으면 현재 레이어 → 흰색 순으로 대체
    const QPen framePen = AbstractTool::layerPenFor(ctx, p.frameLayer);
    const QPen otherPen = AbstractTool::layerPenFor(ctx, p.otherLayer);

    // ── 좌·우 잼브 (PlanDoorTool 과 동일 단면) ────────────────────────────────
    std::vector<MyVector> leftPts, rightPts;
    switch (p.frameType) {
    case 1:
        leftPts  = { pt(0,0), pt(0,V), pt(S,V), pt(S,V-K), pt(J,V-K), pt(J,0) };
        rightPts = { pt(H,0), pt(H,V), pt(H-S,V), pt(H-S,V-K), pt(H-J,V-K), pt(H-J,0) };
        break;
    case 2:
        leftPts  = { pt(0,0), pt(0,V), pt(S,V), pt(S,V-K), pt(J,V-K), pt(J,K), pt(S,0) };
        rightPts = { pt(H,0), pt(H,V), pt(H-S,V), pt(H-S,V-K), pt(H-J,V-K), pt(H-J,K), pt(H-S,0) };
        break;
    case 3:
        leftPts  = { pt(0,0), pt(0,V), pt(J,V), pt(J,0) };
        rightPts = { pt(H,0), pt(H,V), pt(H-J,V), pt(H-J,0) };
        break;
    default:
        leftPts  = { pt(0,0), pt(0,V), pt(S,V), pt(S,V-K), pt(J,V-K), pt(J,K), pt(S,K), pt(S,0) };
        rightPts = { pt(H,0), pt(H,V), pt(H-S,V), pt(H-S,V-K), pt(H-J,V-K), pt(H-J,K), pt(H-S,K), pt(H-S,0) };
        break;
    }

    PolylineItem *leftJamb  = new PolylineItem(leftPts,  true);
    leftJamb->setLayerName(p.frameLayer);  leftJamb->setPen(framePen);
    PolylineItem *rightJamb = new PolylineItem(rightPts, true);
    rightJamb->setLayerName(p.frameLayer); rightJamb->setPen(framePen);

    // 문틀-문틀 연결선(connLines)은 그리지 않는다 — 좌우 잼브만 표시.

    // ── 문짝(얇은 사각형) : 경첩(HX)을 지나 양쪽 극단(HY±R)까지 뻗음.
    // 근접 잼브(0~J) 바로 안쪽(J~2J)으로 잼브 두께만큼 이동되어 있어 잼브와 겹치지 않는다.
    const std::vector<MyVector> leafPts = { pt(HX, HY - R), pt(HX+J, HY - R),
                                             pt(HX+J, HY + R), pt(HX, HY + R) };
    PolylineItem *leafLine = new PolylineItem(leafPts, true);
    leafLine->setLayerName(p.frameLayer); leafLine->setPen(framePen);

    // 경첩 표시선: 문짝 중앙(local Y=HY, 벽 두께 정중앙)에서 문짝 왼쪽·오른쪽을 잇는 짧은 가로선
    LineItem *hingeMark = new LineItem(pt(HX, HY), pt(HX+J, HY));
    hingeMark->setLayerName(p.frameLayer); hingeMark->setPen(framePen);

    // ── 스윙호(180°) : 닫힘 기준선(local +X, angleDeg) 을 지나는 반원.
    // 좌우 대칭이라 span 부호는 임의로 고정(+180)해도 항상 같은 호를 그린다.
    const MyVector arcCenter = pt(HX, HY);
    ArcItem *swingArc = new ArcItem(MyArc(arcCenter, R, p.angleDeg - 90.0, 180.0));
    swingArc->setLayerName(p.otherLayer); swingArc->setPen(otherPen);

    QList<QGraphicsItem*> result;
    result << leftJamb << rightJamb << leafLine << hingeMark << swingArc;
    return result;
}

// placeDoor
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::placeDoor(
    const QPointF &p1, double angleDeg, double V, bool flipY)
{
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->undoStack())
        return;

    const double H  = s_h;

    double ox = p1.x(), oy = p1.y();
    const double c  = std::cos(qDegreesToRadians(angleDeg));
    const double s2 = std::sin(qDegreesToRadians(angleDeg));

    // 원점 보정 (두 모드 상호 배타)
    double wallNearLy = 0.0, wallFarLy = V;
    if (!s_useWallThickness && s_wallPositionMode != 0) {
        double delta;
        if (!s_wallGroupId.isEmpty() && s_mlAtP1)
            delta = computeWallPositionDelta(c, s2, p1, flipY, wallNearLy, wallFarLy);
        else
            delta = computeWallPositionDeltaFromSpan(wallNearLy, wallFarLy);

        if (!flipY) {
            ox -= delta * s2;
            oy += delta * c;
        } else {
            ox += delta * s2;
            oy -= delta * c;
        }
    } else if (s_useWallThickness) {
        if (!flipY) { ox += 10.0 * s2; oy -= 10.0 * c; }
        else        { ox -= 10.0 * s2; oy += 10.0 * c; }
    }

    const QString frameLyr = resolveLayerName(getContext(), DoorLayerSettings::frameLayer());
    const QString otherLyr = resolveLayerName(getContext(), DoorLayerSettings::otherLayer());

    DoorBuildParams buildP;
    buildP.ox = ox; buildP.oy = oy; buildP.angleDeg = angleDeg;
    buildP.H = H; buildP.V = V; buildP.flipY = flipY;
    buildP.frameType = s_frameType;
    buildP.frameLayer = frameLyr; buildP.otherLayer = otherLyr;

    QList<QGraphicsItem*> newItems = buildGeometry(buildP, ctx);

    // 트림 후보를 매크로 시작 전에 수집 (프레임 선이 후보에 포함되지 않도록)
    QList<LineItem *> trimCandidates;
    {
        const QList<QGraphicsItem *> allItems = m_scene->items();
        for (QGraphicsItem *gi : allItems) {
            LineItem *li = dynamic_cast<LineItem *>(gi);
            if (!li) continue;
            if (!li->data(ML_DATA_GROUP_ID).toString().isEmpty()) continue;
            if (!DoorLayerSettings::trimExcludeLayer().isEmpty() && li->layerName() == DoorLayerSettings::trimExcludeLayer()) continue;
            trimCandidates << li;
        }
    }

    const QString doorUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString doorName = tr("양방-%1").arg(++s_doorBiSwingSeq);

    for (int i = 0; i < newItems.size(); ++i)
        tagBiSwingDoorItemWithGroupId(newItems.at(i), doorUuid);

    double trimYLow, trimYHigh;
    if (s_useWallThickness) {
        const double trimP1ly = (p1.y() - oy) * c - (p1.x() - ox) * s2;
        const double trimP2ly = (m_p2.y() - oy) * c - (m_p2.x() - ox) * s2;
        trimYLow  = std::min(trimP1ly, trimP2ly);
        trimYHigh = std::max(trimP1ly, trimP2ly);
    } else {
        trimYLow  = flipY ? -wallFarLy  : wallNearLy;
        trimYHigh = flipY ? -wallNearLy : wallFarLy;
    }

    // 공간벽 마감형태: 양쪽(P1-P2 가상선 / 너비 H 만큼 떨어진 가상선)의 space 선 수집 → 끊기/연결선 계획
    SpaceFinish::Plan finishL, finishR;
    SpaceFinish::planBoth(m_scene, s_wallGroupId, p1, m_p2, ox, oy, c, s2, H, finishL, finishR);

    SymbolInfo doorInfo;
    doorInfo.type                = "PlanDoorBiSwing";
    doorInfo.name                = doorName;
    doorInfo.props["H"]          = H;
    doorInfo.props["V"]          = V;
    doorInfo.props["FrameType"]  = s_frameType;
    doorInfo.props["ox"]         = ox;
    doorInfo.props["oy"]         = oy;
    doorInfo.props["angleDeg"]   = angleDeg;
    doorInfo.props["flipY"]      = flipY ? 1 : 0;
    doorInfo.props["FrameLayer"] = frameLyr;
    doorInfo.props["OtherLayer"] = otherLyr;
    doorInfo.props["wallGroupId"] = s_wallGroupId;
    doorInfo.props["trimYLow"]    = trimYLow;
    doorInfo.props["trimYHigh"]   = trimYHigh;
    doorInfo.props["wallNearLy"]  = wallNearLy;
    doorInfo.props["wallFarLy"]   = wallFarLy;
    SpaceFinish::storeProps(doorInfo.props, finishL, finishR);

    ctx->undoStack()->beginMacro(tr("양방향 여닫이 Type%1").arg(s_frameType + 1));
    ctx->undoStack()->push(new RegisterSymbolCommand(m_scene->symbolRegistry(), doorUuid, doorInfo));
    for (int i = 0; i < newItems.size(); ++i)
        ctx->undoStack()->push(new AddEntityCommand(m_scene, newItems.at(i)));

    if (!s_wallGroupId.isEmpty()) {
        trimWallInDoorOpening(ox, oy, c, s2, trimYLow, trimYHigh, ctx->undoStack(),
                              finishL.extItems, finishR.extItems, SpaceFinish::extLength());
        SpaceFinish::addConnectors(m_scene, finishL, finishR, doorUuid, ctx->undoStack());
        if (!s_useWallThickness && s_wallPositionMode != 0)
            addWallClosingLines(ox, oy, c, s2, wallNearLy, wallFarLy, flipY,
                                doorUuid, ctx->undoStack());
    }
    trimGeneralLines(trimCandidates, ox, oy, c, s2, trimYLow, trimYHigh, ctx->undoStack());

    ctx->undoStack()->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            tr("양방향 여닫이 배치 — P1(%1,%2)  H=%3  V=%4  각도=%5°")
                .arg(ox,0,'f',1).arg(oy,0,'f',1)
                .arg(H,0,'f',1).arg(V,0,'f',1)
                .arg(angleDeg,0,'f',1),
            CommandDock::Info);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// findNearestMultilineAt
// ─────────────────────────────────────────────────────────────────────────────
LineItem *PlanDoorBiSwingTool::findNearestMultilineAt(const QPointF &pos)
{
    if (!m_scene)
        return nullptr;

    const double kPerpTol = 15.0;
    const double kExtend  = 50.0;

    LineItem *best     = nullptr;
    double    bestDist = kPerpTol;

    const QList<QGraphicsItem *> allItems = m_scene->items();
    for (QGraphicsItem *gi : allItems) {
        LineItem *li = dynamic_cast<LineItem *>(gi);
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString().isEmpty()) continue;

        const MyLine  ln  = li->getLine();
        const QPointF lp1(ln.start.x, ln.start.y);
        const QPointF lp2(ln.end.x,   ln.end.y);
        const QPointF seg = lp2 - lp1;
        const double  len = std::sqrt(seg.x() * seg.x() + seg.y() * seg.y());
        if (len < 1e-10) continue;

        const QPointF dn = seg / len;
        const QPointF d  = pos - lp1;

        const double t = dn.x() * d.x() + dn.y() * d.y();
        if (t < -kExtend || t > len + kExtend) continue;

        const double perp = qAbs(dn.x() * d.y() - dn.y() * d.x());
        if (perp < bestDist) {
            bestDist = perp;
            best     = li;
        }
    }
    return best;
}

// ─────────────────────────────────────────────────────────────────────────────
// findNearestMLWithDir
// ─────────────────────────────────────────────────────────────────────────────
LineItem *PlanDoorBiSwingTool::findNearestMLWithDir(
    const QPointF &pos, double nx, double ny)
{
    if (!m_scene) return nullptr;

    const double kPerpTol = 15.0;
    const double kExtend  = 50.0;

    LineItem *best     = nullptr;
    double    bestDist = kPerpTol;

    const QList<QGraphicsItem *> allItems = m_scene->items();
    for (QGraphicsItem *gi : allItems) {
        LineItem *li = dynamic_cast<LineItem *>(gi);
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString().isEmpty()) continue;
        if (li->data(ML_DATA_ROLE).toString() == "center") continue;

        const MyLine  ln  = li->getLine();
        const QPointF lp1(ln.start.x, ln.start.y);
        const QPointF lp2(ln.end.x,   ln.end.y);
        const QPointF seg = lp2 - lp1;
        const double  len = std::sqrt(seg.x() * seg.x() + seg.y() * seg.y());
        if (len < 1e-10) continue;

        const QPointF dn = seg / len;

        const double dp = std::abs(dn.x() * nx + dn.y() * ny);
        if (dp >= 0.5) continue;

        const QPointF d = pos - lp1;
        const double  t = dn.x() * d.x() + dn.y() * d.y();
        if (t < -kExtend || t > len + kExtend) continue;

        const double perp = qAbs(dn.x() * d.y() - dn.y() * d.x());
        if (perp < bestDist) {
            bestDist = perp;
            best     = li;
        }
    }
    return best;
}

// ─────────────────────────────────────────────────────────────────────────────
// trimWallInDoorOpening
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::trimWallInDoorOpening(
    double ox, double oy, double c, double s2,
    double yLow, double yHigh, QUndoStack *stack,
    const QList<LineItem *> &extLeftItems, const QList<LineItem *> &extRightItems,
    double extLen)
{
    if (!m_scene || s_wallGroupId.isEmpty())
        return;

    const double baseCutL = 0.0;
    const double baseCutR = s_h;
    const double EPS  = 1e-6;

    QList<LineItem *> wallLines;
    const QList<QGraphicsItem *> sceneItems = m_scene->items();
    for (int i = 0; i < sceneItems.size(); ++i) {
        QGraphicsItem *gi = sceneItems[i];
        LineItem *li = dynamic_cast<LineItem *>(gi);
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString() != s_wallGroupId) continue;
        if (li->data(ML_DATA_ROLE).toString() == "center") continue;
        if (!DoorLayerSettings::trimExcludeLayer().isEmpty() && li->layerName() == DoorLayerSettings::trimExcludeLayer()) continue; // 트림 제외 레이어
        const MyLine lnD = li->getLine();
        const double sdxD = lnD.end.x - lnD.start.x;
        const double sdyD = lnD.end.y - lnD.start.y;
        const double slenD = std::sqrt(sdxD * sdxD + sdyD * sdyD);
        if (slenD < 1e-9) continue;
        if (std::abs((sdxD / slenD) * c + (sdyD / slenD) * s2) < 0.5) continue;
        wallLines << li;
    }

    for (int j = 0; j < wallLines.size(); ++j) {
        LineItem *li = wallLines[j];
        // 공간벽 마감형태 대상 space 선은 개구부 양쪽 바깥(-X / +X)으로 extLen 만큼 더 끊는다
        const double cutL = extLeftItems.contains(li)  ? baseCutL - extLen : baseCutL;
        const double cutR = extRightItems.contains(li) ? baseCutR + extLen : baseCutR;
        const MyLine ln   = li->getLine();
        QPointF      loc1 = doorBiSwingWorldToLocal(ox, oy, c, s2, ln.start);
        QPointF      loc2 = doorBiSwingWorldToLocal(ox, oy, c, s2, ln.end);

        bool swapped = false;
        if (loc1.x() > loc2.x()) { qSwap(loc1, loc2); swapped = true; }

        const double lx1 = loc1.x(), ly1 = loc1.y();
        const double lx2 = loc2.x(), ly2 = loc2.y();

        if (lx2 <= cutL + EPS || lx1 >= cutR - EPS)
            continue;

        const double midLy = (ly1 + ly2) / 2.0;
        const double yTol  = 15.0;
        if (midLy < yLow - yTol || midLy > yHigh + yTol)
            continue;

        const MyVector wSmall = swapped ? ln.end   : ln.start;
        const MyVector wLarge = swapped ? ln.start : ln.end;

        if (lx1 >= cutL - EPS && lx2 <= cutR + EPS) {
            stack->push(new RemoveEntityCommand(m_scene, li));
        }
        else if (lx1 < cutL && lx2 > cutR) {
            MyVector cutLv = doorBiSwingInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL);
            MyVector cutRv = doorBiSwingInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR);
            LineItem *lp = new LineItem(wSmall, cutLv);
            LineItem *rp = new LineItem(cutRv, wLarge);
            copyDoorBiSwingLineMlTags(lp, li);
            copyDoorBiSwingLineMlTags(rp, li);
            stack->push(new RemoveEntityCommand(m_scene, li));
            stack->push(new AddEntityCommand(m_scene, lp));
            stack->push(new AddEntityCommand(m_scene, rp));
        }
        else if (lx1 < cutL) {
            const bool     isP1 = swapped;
            const MyVector mv   = doorBiSwingInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        }
        else {
            const bool     isP1 = !swapped;
            const MyVector mv   = doorBiSwingInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// trimGeneralLines
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::trimGeneralLines(
    const QList<LineItem *> &candidates,
    double ox, double oy, double c, double s2,
    double yLow, double yHigh,
    QUndoStack *stack)
{
    if (!m_scene || !stack || candidates.isEmpty())
        return;

    const double cutL = 0.0;
    const double cutR = s_h;
    const double EPS  = 1e-6;
    const double yTol = 10.0;

    for (LineItem *li : candidates) {
        if (!li->scene())
            continue;

        const MyLine ln   = li->getLine();
        QPointF      loc1 = doorBiSwingWorldToLocal(ox, oy, c, s2, ln.start);
        QPointF      loc2 = doorBiSwingWorldToLocal(ox, oy, c, s2, ln.end);

        bool swapped = false;
        if (loc1.x() > loc2.x()) { qSwap(loc1, loc2); swapped = true; }

        const double lx1 = loc1.x(), ly1 = loc1.y();
        const double lx2 = loc2.x(), ly2 = loc2.y();

        if (lx2 <= cutL + EPS || lx1 >= cutR - EPS)
            continue;

        const double lyMin = std::min(ly1, ly2);
        const double lyMax = std::max(ly1, ly2);
        if (lyMax < yLow - yTol || lyMin > yHigh + yTol)
            continue;

        const MyVector wSmall = swapped ? ln.end   : ln.start;
        const MyVector wLarge = swapped ? ln.start : ln.end;

        if (lx1 >= cutL - EPS && lx2 <= cutR + EPS) {
            stack->push(new RemoveEntityCommand(m_scene, li));
        } else if (lx1 < cutL && lx2 > cutR) {
            MyVector cutLv = doorBiSwingInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL);
            MyVector cutRv = doorBiSwingInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR);
            LineItem *lp = new LineItem(wSmall, cutLv);
            LineItem *rp = new LineItem(cutRv, wLarge);
            copyDoorBiSwingLineAppearance(lp, li);
            copyDoorBiSwingLineAppearance(rp, li);
            stack->push(new RemoveEntityCommand(m_scene, li));
            stack->push(new AddEntityCommand(m_scene, lp));
            stack->push(new AddEntityCommand(m_scene, rp));
        } else if (lx1 < cutL) {
            const bool     isP1 = swapped;
            const MyVector mv   = doorBiSwingInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        } else {
            const bool     isP1 = !swapped;
            const MyVector mv   = doorBiSwingInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// checkWallPositionIfNeeded
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::checkWallPositionIfNeeded()
{
    s_wallPositionMode = 0;
    s_wallDetectedSpan = 0.0;

    if (s_useWallThickness)
        return;

    const double dx   = m_p2.x() - m_p1.x();
    const double dy   = m_p2.y() - m_p1.y();
    const double dist = std::sqrt(dx * dx + dy * dy);
    s_wallDetectedSpan = dist;

    if (dist < 1e-6)
        return;

    const double nx = dx / dist;
    const double ny = dy / dist;

    if (!s_wallGroupId.isEmpty() && !isMLCompatibleDirection(s_wallGroupId, nx, ny)) {
        s_wallGroupId.clear();
        s_mlAtP1 = false;
    }

    if (s_wallGroupId.isEmpty()) {
        LineItem *ml = findNearestMLWithDir(m_p1, nx, ny);
        if (ml) {
            s_wallGroupId = ml->data(ML_DATA_GROUP_ID).toString();
            s_mlAtP1 = true;
        }
    }

    if (s_wallGroupId.isEmpty()) {
        LineItem *ml = findNearestMLWithDir(m_p2, nx, ny);
        if (ml)
            s_wallGroupId = ml->data(ML_DATA_GROUP_ID).toString();
    }

    if (s_v >= dist)
        return;

    IAppContext *ctx = getContext();
    QWidget *parentWin = ctx ? dynamic_cast<QWidget *>(ctx->commandDock()) : nullptr;

    WallPositionDialog dlg(
        static_cast<WallPositionDialog::Mode>(s_lastWallPosMode), parentWin);
    if (dlg.exec() != QDialog::Accepted) {
        s_wallGroupId.clear();
        return;
    }

    s_wallPositionMode = static_cast<int>(dlg.mode());
    s_lastWallPosMode  = s_wallPositionMode;
}

// ─────────────────────────────────────────────────────────────────────────────
// isMLCompatibleDirection
// ─────────────────────────────────────────────────────────────────────────────
bool PlanDoorBiSwingTool::isMLCompatibleDirection(
    const QString &groupId, double nx, double ny) const
{
    if (!m_scene || groupId.isEmpty()) return false;
    const QList<QGraphicsItem *> items = m_scene->items();
    for (QGraphicsItem *gi : items) {
        LineItem *li = dynamic_cast<LineItem *>(gi);
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString() != groupId) continue;
        if (li->data(ML_DATA_ROLE).toString() == "center") continue;
        const MyLine ln = li->getLine();
        const double sdx = ln.end.x - ln.start.x;
        const double sdy = ln.end.y - ln.start.y;
        const double slen = std::sqrt(sdx * sdx + sdy * sdy);
        if (slen < 1e-6) continue;
        const double dp = std::abs((sdx / slen) * nx + (sdy / slen) * ny);
        if (dp < 0.5) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// computeWallPositionDelta
// ─────────────────────────────────────────────────────────────────────────────
double PlanDoorBiSwingTool::computeWallPositionDelta(
    double c, double s2, const QPointF &rawOrigin, bool flipY,
    double &outNearLy, double &outFarLy)
{
    outNearLy = 0.0;
    outFarLy  = s_v;

    if (!m_scene || s_wallGroupId.isEmpty() || s_wallPositionMode == 0)
        return 0.0;

    double y_near =  1e18;
    double y_far  = -1e18;

    const double dp2x0 = m_p2.x() - rawOrigin.x();
    const double dp2y0 = m_p2.y() - rawOrigin.y();
    double p2ly0 = dp2y0 * c - dp2x0 * s2;
    if (flipY) p2ly0 = -p2ly0;
    const double maxLy = std::abs(p2ly0) + 10.0;

    const QList<QGraphicsItem *> sceneItems = m_scene->items();
    for (QGraphicsItem *gi : sceneItems) {
        LineItem *li = dynamic_cast<LineItem *>(gi);
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString() != s_wallGroupId) continue;
        if (li->data(ML_DATA_ROLE).toString() == "center") continue;

        const MyLine ln = li->getLine();
        const double sdx = ln.end.x - ln.start.x;
        const double sdy = ln.end.y - ln.start.y;
        const double slen = std::sqrt(sdx * sdx + sdy * sdy);
        if (slen < 1e-9) continue;
        if (std::abs((sdx / slen) * c + (sdy / slen) * s2) < 0.5) continue;

        const double mx = (ln.start.x + ln.end.x) / 2.0;
        const double my = (ln.start.y + ln.end.y) / 2.0;
        const double dx = mx - rawOrigin.x();
        const double dy = my - rawOrigin.y();
        double ly = dy * c - dx * s2;
        if (flipY) ly = -ly;

        if (std::abs(ly) > maxLy) continue;

        if (ly < y_near) y_near = ly;
        if (ly > y_far)  y_far  = ly;
    }

    if (y_near >= y_far)
        return 0.0;

    double delta = 0.0;
    switch (s_wallPositionMode) {
    case WallPositionDialog::Inner:
        delta = y_near - 10.0;
        break;
    case WallPositionDialog::Center:
        delta = (y_near + y_far) / 2.0 - s_v / 2.0;
        break;
    case WallPositionDialog::Outer:
        delta = y_far - (s_v - 10.0);
        break;
    }

    outNearLy = y_near - delta;
    outFarLy  = y_far  - delta;
    return delta;
}

// ─────────────────────────────────────────────────────────────────────────────
// computeWallPositionDeltaFromSpan
// ─────────────────────────────────────────────────────────────────────────────
double PlanDoorBiSwingTool::computeWallPositionDeltaFromSpan(double &outNearLy, double &outFarLy)
{
    const double span = s_wallDetectedSpan;
    outNearLy = 0.0;
    outFarLy  = span;

    if (s_wallPositionMode == 0 || span <= 0.0)
        return 0.0;

    double delta = 0.0;
    switch (s_wallPositionMode) {
    case WallPositionDialog::Inner:
        delta = 0.0;
        break;
    case WallPositionDialog::Center:
        delta = (span - s_v) / 2.0;
        break;
    case WallPositionDialog::Outer:
        delta = span - s_v;
        break;
    }

    outNearLy = -delta;
    outFarLy  = span - delta;
    return delta;
}

// ─────────────────────────────────────────────────────────────────────────────
// addWallClosingLines — 마감선은 문짝 형태와 무관한 순수 벽-트림 로직이므로
// PlanDoorTool 의 정적 함수(addClosingLinesStatic)를 그대로 재사용한다.
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorBiSwingTool::addWallClosingLines(
    double ox, double oy, double c, double s2,
    double wallNearLy, double wallFarLy, bool flipY,
    const QString &doorUuid, QUndoStack *stack)
{
    if (!m_scene || !stack) return;

    IAppContext *ctx       = getContext();
    const QString otherLyr = resolveLayerName(getContext(), DoorLayerSettings::otherLayer());

    PlanDoorTool::addClosingLinesStatic(m_scene, ctx,
                                        ox, oy, c, s2, flipY,
                                        s_h, s_v,
                                        wallNearLy, wallFarLy,
                                        otherLyr, doorUuid, stack);
}

// ─────────────────────────────────────────────────────────────────────────────
// PlanDoorBiSwingCommand
// ─────────────────────────────────────────────────────────────────────────────
PlanDoorBiSwingCommand::PlanDoorBiSwingCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent)
    , m_ctx(ctx)
{}

void PlanDoorBiSwingCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new PlanDoorBiSwingTool());
}
