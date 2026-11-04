#include "planrevolvingdoortool.h"
#include "planrevolvingdoorsetupdialog.h"
#include "planarchdata.h"
#include "spacefinish.h"
#include "scenesymbolregistry.h"
#include "wallpositiondialog.h"

#include "arcitem.h"
#include "cadcommands.h"
#include "circleitem.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "lineitem.h"
#include "myarc.h"
#include "mycircle.h"
#include "multilinedata.h"
#include "mygraphicsscene.h"
#include "myvector.h"
#include "plandoortool.h"   // addClosingLinesStatic 재사용
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
// 회전문 단면 치수 상수 (단위: mm)
// ─────────────────────────────────────────────────────────────────────────────
static const double ARC_THICK    = 45.0;   // 외벽호(원형 인클로저) 두께
static const double WING_THICK   = 30.0;   // 날개 두께
static const double MARGIN_50    = 50.0;   // 외벽호 안쪽~날개 끝 여유 / 프레임캡 접선방향 길이
static const double AXIS_DIA     = 30.0;   // 회전축 원 지름
static const double ARC_GAP_ANGLE = 80.0;  // 상/하 트임 각도 — 좌우 호 각각 (180-트임)로 축소
static const double ARC_SPAN      = 180.0 - ARC_GAP_ANGLE; // 좌/우 호 각각의 실제 스윕 각도(100°)

double  PlanRevolvingDoorTool::s_h                = 2400.0;
double  PlanRevolvingDoorTool::s_v                = 200.0;
bool    PlanRevolvingDoorTool::s_useWallThickness = false;
int     PlanRevolvingDoorTool::s_wingCount        = 4;
QString PlanRevolvingDoorTool::s_wallGroupId;
int     PlanRevolvingDoorTool::s_wallPositionMode  = 0;
int     PlanRevolvingDoorTool::s_lastWallPosMode   = WallPositionDialog::Inner;
double  PlanRevolvingDoorTool::s_wallDetectedSpan  = 0.0;
bool    PlanRevolvingDoorTool::s_mlAtP1            = false;
static int s_revolvingDoorSeq = 0;

// ── 그룹 UUID 태깅 (람다 대신 정적 함수) ─────────────────────────────────────
static void tagRevolvingDoorItemWithGroupId(QGraphicsItem *item, const QString &uuid)
{
    item->setData(PD_DATA_GROUP_ID, uuid);
}

// ── 도구 기본값 저장/불러오기 (3-2: 도면 스코프 우선, 전역은 씨앗) ─────────────
//   네임스페이스 "PlanRevolvingDoorTool" = 기존 QSettings 그룹명 그대로.
static void savePlanRevolvingDoorSettings(MyGraphicsScene *scene)
{
    ToolDefaults d("PlanRevolvingDoorTool", scene);
    d.put("H",                PlanRevolvingDoorTool::globalH());
    d.put("V",                PlanRevolvingDoorTool::globalV());
    d.put("UseWallThickness", PlanRevolvingDoorTool::useWallThickness());
    d.put("WingCount",        PlanRevolvingDoorTool::wingCount());
}

static void loadPlanRevolvingDoorSettings(MyGraphicsScene *scene)
{
    DoorLayerSettings::load(scene);
    SpaceFinish::load(scene);
    ToolDefaults d("PlanRevolvingDoorTool", scene);
    PlanRevolvingDoorTool::setGlobalH(d.getDouble("H",                2400.0));
    PlanRevolvingDoorTool::setGlobalV(d.getDouble("V",                200.0));
    PlanRevolvingDoorTool::setUseWallThickness(d.getBool("UseWallThickness", false));
    PlanRevolvingDoorTool::setWingCount(d.getInt("WingCount",      4));
}

double  PlanRevolvingDoorTool::globalH()                        { return s_h; }
void    PlanRevolvingDoorTool::setGlobalH(double v)             { if (v > 0.0) s_h = v; }
double  PlanRevolvingDoorTool::globalV()                        { return s_v; }
void    PlanRevolvingDoorTool::setGlobalV(double v)             { if (v > 0.0) s_v = v; }
bool    PlanRevolvingDoorTool::useWallThickness()               { return s_useWallThickness; }
void    PlanRevolvingDoorTool::setUseWallThickness(bool use)    { s_useWallThickness = use; }
int     PlanRevolvingDoorTool::wingCount()                      { return s_wingCount; }
void    PlanRevolvingDoorTool::setWingCount(int n)              { s_wingCount = (n == 3) ? 3 : 4; }
QString PlanRevolvingDoorTool::frameLayer()                     { return DoorLayerSettings::frameLayer(); }
void    PlanRevolvingDoorTool::setFrameLayer(const QString &n)  { DoorLayerSettings::setFrameLayer(n); }
QString PlanRevolvingDoorTool::otherLayer()                         { return DoorLayerSettings::otherLayer(); }
void    PlanRevolvingDoorTool::setOtherLayer(const QString &n)      { DoorLayerSettings::setOtherLayer(n); }
QString PlanRevolvingDoorTool::trimExcludeLayer()                   { return DoorLayerSettings::trimExcludeLayer(); }
void    PlanRevolvingDoorTool::setTrimExcludeLayer(const QString &n){ DoorLayerSettings::setTrimExcludeLayer(n); }

CommandBase *PlanRevolvingDoorTool::createCommand(IAppContext *c)
{
    return new PlanRevolvingDoorCommand(c);
}

// ── 생성자 ───────────────────────────────────────────────────────────────────
PlanRevolvingDoorTool::PlanRevolvingDoorTool(QObject *parent)
    : AbstractTool(parent)
    , m_state(WaitP1)
    , m_currentMousePos(0.0, 0.0)
{}

// ─────────────────────────────────────────────────────────────────────────────
// applySnapForState — 상태별 osnap 자동 전환
// ─────────────────────────────────────────────────────────────────────────────
void PlanRevolvingDoorTool::applySnapForState(State s)
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
void PlanRevolvingDoorTool::restoreSavedSnap()
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
void PlanRevolvingDoorTool::activate(MyGraphicsScene *scene)
{
    // [3-2] 매 활성화마다 현재 도면의 값으로 다시 채운다(도면 전환 반영).
    loadPlanRevolvingDoorSettings(scene);

    if (m_scene == scene)
        return;

    AbstractTool::activate(scene);
    m_state = WaitP1;

    IAppContext *ctx = getContext();
    if (ctx && ctx->snapManager()) {
        m_savedNearest = ctx->snapManager()->isSnapNearest();
        m_savedPerp    = ctx->snapManager()->isSnapPerpendicular();
        m_snapSaved    = true;
        applySnapForState(WaitP1);
    }

    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("PLAN-REVOLVING-DOOR", CommandDock::Command);
    }

    showP1Prompt();
}

// ── deactivate ───────────────────────────────────────────────────────────────
void PlanRevolvingDoorTool::deactivate()
{
    restoreSavedSnap();

    m_state = WaitP1;
    if (m_scene)
        m_scene->clearOverlay();
    AbstractTool::deactivate();
}

// ── showP1Prompt ─────────────────────────────────────────────────────────────
void PlanRevolvingDoorTool::showP1Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(tr(" P1: 도어가 들어갈 선  지정 (nea) "), {"Setup"});
}

// ── showP1DistancePrompt ─────────────────────────────────────────────────────
void PlanRevolvingDoorTool::showP1DistancePrompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(tr(" 거리를 입력하세요 : "), {});
}

// ── computeP1FromAnchor ──────────────────────────────────────────────────────
QPointF PlanRevolvingDoorTool::computeP1FromAnchor(double dist) const
{
    QPointF dir = m_p1WallDir;
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
void PlanRevolvingDoorTool::showP2Prompt() const
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
void PlanRevolvingDoorTool::showP3Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(
        tr(" P3: 방향점 지정 (nea) — 문틀 진행 방향 "), {});
}

// ── mouseMove ────────────────────────────────────────────────────────────────
void PlanRevolvingDoorTool::mouseMove(QPointF pos)
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
        QPointF dir = m_p1WallDir;
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
void PlanRevolvingDoorTool::mousePress(QPointF pos, Qt::MouseButton button)
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
            LineItem *ml = findNearestMultilineAt(snapped);
            if (ml) {
                MyLine line = ml->getLine();
                MyVector dir = (line.end - line.start).normalized();
                m_p1WallDir = QPointF(dir.x, dir.y);
            } else {
                m_p1WallDir = QPointF(0, 0);
            }
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
void PlanRevolvingDoorTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/) {}

// ── processCommand ────────────────────────────────────────────────────────────
bool PlanRevolvingDoorTool::processCommand(const QString &command)
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

            PlanRevolvingDoorSetupDialog dlg(s_h, s_v, s_useWallThickness, s_wingCount,
                                             parentWin);
            dlg.setSpaceFinishType(SpaceFinish::type());
            if (dlg.exec() == QDialog::Accepted) {
                SpaceFinish::setType(dlg.spaceFinishType());
                SpaceFinish::save(m_scene);
                setGlobalH(dlg.h());
                setGlobalV(dlg.v());
                setUseWallThickness(dlg.useWallThickness());
                setWingCount(dlg.wingCount());
                savePlanRevolvingDoorSettings(m_scene);
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
void PlanRevolvingDoorTool::onFinish()
{
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

// ── onCancel ─────────────────────────────────────────────────────────────────
void PlanRevolvingDoorTool::onCancel()
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
// computeFrom3Pts — PlanDoorTool 과 완전히 동일한 계산 규칙
// ─────────────────────────────────────────────────────────────────────────────
bool PlanRevolvingDoorTool::computeFrom3Pts(
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
    const double nx = -uy;
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
struct RevDoorPtCalc {
    double ox, oy, c, s2;
    bool   flipY;
    MyVector operator()(double dx, double dy) const {
        if (!flipY)
            return MyVector(ox + dx*c - dy*s2, oy + dx*s2 + dy*c);
        else
            return MyVector(ox + dx*c + dy*s2, oy + dx*s2 - dy*c);
    }
};

static QPointF revDoorPointF(const RevDoorPtCalc &pt, double dx, double dy)
{
    const MyVector v = pt(dx, dy);
    return QPointF(v.x, v.y);
}

// 회전축 중심(cx,cy) 기준 극좌표(반경 r, 각도 θ)의 로컬 (dx,dy) 계산
// static void revDoorPolarLocal(double cx, double cy, double angleDeg, double r,
//                               double &outDx, double &outDy)
// {
//     const double rad = qDegreesToRadians(angleDeg);
//     outDx = cx + r * std::cos(rad);
//     outDy = cy + r * std::sin(rad);
// }

// 극좌표 스포크(반경 rInner~rOuter, 각도 angleDeg, 접선방향 두께 thickness) 4점(로컬 dx,dy)
// — 날개·외벽호 프레임캡 공용. out[4] = { (rInner,-half), (rOuter,-half), (rOuter,+half), (rInner,+half) }
static void revDoorPolarRectLocal(double cx, double cy, double angleDeg,
                                  double rInner, double rOuter, double thickness,
                                  QPointF out[4])
{
    const double rad   = qDegreesToRadians(angleDeg);
    const double dirX  = std::cos(rad), dirY = std::sin(rad);
    const double perpX = -dirY,          perpY = dirX;
    const double halfT = thickness / 2.0;
    out[0] = QPointF(cx + dirX*rInner + perpX*halfT, cy + dirY*rInner + perpY*halfT);
    out[1] = QPointF(cx + dirX*rOuter + perpX*halfT, cy + dirY*rOuter + perpY*halfT);
    out[2] = QPointF(cx + dirX*rOuter - perpX*halfT, cy + dirY*rOuter - perpY*halfT);
    out[3] = QPointF(cx + dirX*rInner - perpX*halfT, cy + dirY*rInner - perpY*halfT);
}

static void appendClosedPolygon(QPainterPath &path, const std::vector<QPointF> &pts)
{
    if (pts.empty()) return;
    path.moveTo(pts.front());
    for (std::size_t i = 1; i < pts.size(); ++i)
        path.lineTo(pts[i]);
    path.closeSubpath();
}

// ── 세계좌표 ↔ 로컬좌표 변환 (트림 함수 공용, 람다 대체) ─────────────────────
static QPointF revDoorWorldToLocal(double ox, double oy, double c, double s2,
                                   const MyVector &p)
{
    const double dx = p.x - ox, dy = p.y - oy;
    return QPointF(dx * c + dy * s2, dy * c - dx * s2);
}

static MyVector revDoorLocalToWorld(double ox, double oy, double c, double s2,
                                    double lx, double ly)
{
    return MyVector(ox + lx * c - ly * s2, oy + lx * s2 + ly * c);
}

static MyVector revDoorInterp(double ox, double oy, double c, double s2,
                              double lx1, double ly1, double lx2, double ly2,
                              double cutX)
{
    const double span = lx2 - lx1;
    const double t    = span > 1e-9 ? (cutX - lx1) / span : 0.0;
    return revDoorLocalToWorld(ox, oy, c, s2, cutX, ly1 + t * (ly2 - ly1));
}

static void copyRevDoorLineAppearance(LineItem *dst, const LineItem *src)
{
    dst->setLayerName(src->layerName());
    dst->setPen(src->pen());
}

static void copyRevDoorLineMlTags(LineItem *dst, const LineItem *src)
{
    copyRevDoorLineAppearance(dst, src);
    dst->setData(ML_DATA_GROUP_ID, src->data(ML_DATA_GROUP_ID));
    dst->setData(ML_DATA_ROLE,     src->data(ML_DATA_ROLE));
    const double sp = src->data(ML_DATA_SPACING).toDouble();
    if (sp > 0.0) dst->setData(ML_DATA_SPACING, sp);
}

// ─────────────────────────────────────────────────────────────────────────────
// updateGhostDoor — 회전문 고스트 미리보기
//
// 회전축 중심 = local(H/2, V/2). 문틀(Type4 고정) 양쪽 → 그 안쪽 외벽호(두께45,
// 좌/우 180°씩) → 그 안쪽 날개(N개, 두께30, 회전축~R_wing).
// ─────────────────────────────────────────────────────────────────────────────
void PlanRevolvingDoorTool::updateGhostDoor(
    const QPointF &p1, double angleDeg, double V, bool flipY)
{
    if (!m_scene)
        return;

    const double H  = s_h;
    const double J  = DoorLayerSettings::jambOuter();
    const double CX = H / 2.0;
    const double CY = V / 2.0;
    const double R_outer = CX - J;
    const double R_inner = R_outer - ARC_THICK;
    const double R_wing  = R_inner - MARGIN_50;
    const double R_axis  = AXIS_DIA / 2.0;

    double ox = p1.x(), oy = p1.y();
    const double c  = std::cos(qDegreesToRadians(angleDeg));
    const double s2 = std::sin(qDegreesToRadians(angleDeg));

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

    const RevDoorPtCalc pt { ox, oy, c, s2, flipY };

    QPainterPath path;

    // ── 문틀(Type4 고정, 45mm 사각) 좌·우 ──────────────────────────────────
    appendClosedPolygon(path, { revDoorPointF(pt,0,0), revDoorPointF(pt,0,V),
                                 revDoorPointF(pt,J,V), revDoorPointF(pt,J,0) });
    appendClosedPolygon(path, { revDoorPointF(pt,H,0), revDoorPointF(pt,H,V),
                                 revDoorPointF(pt,H-J,V), revDoorPointF(pt,H-J,0) });

    // ── 외벽호(원형 인클로저, 좌/우 각 ARC_SPAN(=180-트임80=100)°, 두께 45mm) ──
    // 좌측 호는 local 180°, 우측 호는 local 0° 를 중심으로 대칭 — 상/하에 트임(80°) 발생.
    {
        MyVector center = pt(CX, CY);
        QPointF  cenF(center.x, center.y);
        QRectF   rectOuter(cenF.x() - R_outer, cenF.y() - R_outer, R_outer*2.0, R_outer*2.0);
        QRectF   rectInner(cenF.x() - R_inner, cenF.y() - R_inner, R_inner*2.0, R_inner*2.0);
        const double halfSpan = ARC_SPAN / 2.0;
        // 좌측: CAD start=angleDeg+180-halfSpan, span=+ARC_SPAN → Qt(start,span)=(-CADstart,-CADspan)
        double drawStartL = -(angleDeg + 180.0 - halfSpan);
        path.arcMoveTo(rectOuter, drawStartL); path.arcTo(rectOuter, drawStartL, -ARC_SPAN);
        path.arcMoveTo(rectInner, drawStartL); path.arcTo(rectInner, drawStartL, -ARC_SPAN);
        // 우측: CAD start=angleDeg-halfSpan, span=+ARC_SPAN
        double drawStartR = -(angleDeg - halfSpan);
        path.arcMoveTo(rectOuter, drawStartR); path.arcTo(rectOuter, drawStartR, -ARC_SPAN);
        path.arcMoveTo(rectInner, drawStartR); path.arcTo(rectInner, drawStartR, -ARC_SPAN);
    }

    // ── 외벽호 프레임캡(45x50, 좌/우 호 각각의 양끝단+중앙 = 총 6곳) ───────
    {
        const double halfSpan = ARC_SPAN / 2.0;
        const double capAngles[6] = { -halfSpan, 0.0, halfSpan,                       // 우측 호: 양끝+중앙
                                       180.0-halfSpan, 180.0, 180.0+halfSpan };        // 좌측 호: 양끝+중앙
        for (int i = 0; i < 6; ++i) {
            QPointF localPts[4];
            revDoorPolarRectLocal(CX, CY, capAngles[i], R_inner, R_outer, MARGIN_50, localPts);
            std::vector<QPointF> worldPts;
            for (int k = 0; k < 4; ++k)
                worldPts.push_back(revDoorPointF(pt, localPts[k].x(), localPts[k].y()));
            appendClosedPolygon(path, worldPts);
        }
    }

    // ── 날개(N개, 두께 30mm, 회전축~R_wing) ───────────────────────────────
    {
        const double startAngle = 45.0;
        const double step = 360.0 / double(s_wingCount);
        for (int i = 0; i < s_wingCount; ++i) {
            const double a = startAngle + step * i;
            QPointF localPts[4];
            revDoorPolarRectLocal(CX, CY, a, R_axis, R_wing, WING_THICK, localPts);
            std::vector<QPointF> worldPts;
            for (int k = 0; k < 4; ++k)
                worldPts.push_back(revDoorPointF(pt, localPts[k].x(), localPts[k].y()));
            appendClosedPolygon(path, worldPts);
        }
    }

    // ── 회전축 원 (지름 30mm) ───────────────────────────────────────────────
    {
        MyVector center = pt(CX, CY);
        path.addEllipse(QPointF(center.x, center.y), R_axis, R_axis);
    }

    updateGhostPreview(path, currentLayerName());
}

// ── buildGeometry ─────────────────────────────────────────────────────────────
QList<QGraphicsItem*> PlanRevolvingDoorTool::buildGeometry(const RevolvingDoorBuildParams &pIn, IAppContext *ctx)
{
    // 지정 레이어가 도면에 없거나 비어 있으면 현재 레이어로 대체 (속성 편집 재생성 경로 포함)
    RevolvingDoorBuildParams p = pIn;
    p.frameLayer = AbstractTool::resolveLayerName(ctx, p.frameLayer);
    p.otherLayer = AbstractTool::resolveLayerName(ctx, p.otherLayer);

    const double H  = p.H;
    const double V  = p.V;
    const double J  = DoorLayerSettings::jambOuter();
    const double CX = H / 2.0;
    const double CY = V / 2.0;
    const double R_outer = CX - J;
    const double R_inner = R_outer - ARC_THICK;
    const double R_wing  = R_inner - MARGIN_50;
    const double R_axis  = AXIS_DIA / 2.0;

    const double c  = std::cos(qDegreesToRadians(p.angleDeg));
    const double s2 = std::sin(qDegreesToRadians(p.angleDeg));
    const RevDoorPtCalc pt { p.ox, p.oy, c, s2, p.flipY };

    // 레이어 색·두께·선종류 적용, 레이어가 없으면 현재 레이어 → 흰색 순으로 대체
    const QPen framePen = AbstractTool::layerPenFor(ctx, p.frameLayer);
    const QPen otherPen = AbstractTool::layerPenFor(ctx, p.otherLayer);

    QList<QGraphicsItem*> result;

    // ── 문틀(Type4 고정) 좌·우 ────────────────────────────────────────────
    std::vector<MyVector> leftPts  = { pt(0,0), pt(0,V), pt(J,V), pt(J,0) };
    std::vector<MyVector> rightPts = { pt(H,0), pt(H,V), pt(H-J,V), pt(H-J,0) };
    PolylineItem *leftJamb  = new PolylineItem(leftPts,  true);
    leftJamb->setLayerName(p.frameLayer);  leftJamb->setPen(framePen);
    PolylineItem *rightJamb = new PolylineItem(rightPts, true);
    rightJamb->setLayerName(p.frameLayer); rightJamb->setPen(framePen);
    result << leftJamb << rightJamb;

    // ── 외벽호(원형 인클로저, 좌/우 각 ARC_SPAN(=180-트임80=100)°, 두께 45mm) ──
    // 좌측 호는 local 180°, 우측 호는 local 0° 를 중심으로 대칭 — 상/하에 트임(80°) 발생.
    const MyVector center = pt(CX, CY);
    const double halfSpan = ARC_SPAN / 2.0;
    ArcItem *leftOuterArc  = new ArcItem(MyArc(center, R_outer, p.angleDeg + 180.0 - halfSpan, ARC_SPAN));
    ArcItem *leftInnerArc  = new ArcItem(MyArc(center, R_inner, p.angleDeg + 180.0 - halfSpan, ARC_SPAN));
    ArcItem *rightOuterArc = new ArcItem(MyArc(center, R_outer, p.angleDeg - halfSpan, ARC_SPAN));
    ArcItem *rightInnerArc = new ArcItem(MyArc(center, R_inner, p.angleDeg - halfSpan, ARC_SPAN));
    leftOuterArc->setLayerName(p.otherLayer);  leftOuterArc->setPen(otherPen);
    leftInnerArc->setLayerName(p.otherLayer);  leftInnerArc->setPen(otherPen);
    rightOuterArc->setLayerName(p.otherLayer); rightOuterArc->setPen(otherPen);
    rightInnerArc->setLayerName(p.otherLayer); rightInnerArc->setPen(otherPen);
    result << leftOuterArc << leftInnerArc << rightOuterArc << rightInnerArc;

    // ── 외벽호 프레임캡(45x50, 좌/우 호 각각의 양끝단+중앙 = 총 6곳) ───────
    const double capAngles[6] = { -halfSpan, 0.0, halfSpan,
                                   180.0-halfSpan, 180.0, 180.0+halfSpan };
    for (int i = 0; i < 6; ++i) {
        QPointF localPts[4];
        revDoorPolarRectLocal(CX, CY, capAngles[i], R_inner, R_outer, MARGIN_50, localPts);
        std::vector<MyVector> capPts;
        for (int k = 0; k < 4; ++k)
            capPts.push_back(pt(localPts[k].x(), localPts[k].y()));
        PolylineItem *cap = new PolylineItem(capPts, true);
        cap->setLayerName(p.frameLayer); cap->setPen(framePen);
        result << cap;
    }

    // ── 날개(N개, 두께 30mm, 회전축~R_wing) ───────────────────────────────
    const double startAngle = 45.0;
    const double step = 360.0 / double(p.wingCount);
    for (int i = 0; i < p.wingCount; ++i) {
        const double a = startAngle + step * i;
        QPointF localPts[4];
        revDoorPolarRectLocal(CX, CY, a, R_axis, R_wing, WING_THICK, localPts);
        std::vector<MyVector> wingPts;
        for (int k = 0; k < 4; ++k)
            wingPts.push_back(pt(localPts[k].x(), localPts[k].y()));
        PolylineItem *wing = new PolylineItem(wingPts, true);
        wing->setLayerName(p.frameLayer); wing->setPen(framePen);
        result << wing;
    }

    // ── 회전축 원 (지름 30mm) ───────────────────────────────────────────────
    CircleItem *axisCircle = new CircleItem(center, R_axis);
    axisCircle->setLayerName(p.otherLayer); axisCircle->setPen(otherPen);
    result << axisCircle;

    return result;
}

// placeDoor
// ─────────────────────────────────────────────────────────────────────────────
void PlanRevolvingDoorTool::placeDoor(
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

    RevolvingDoorBuildParams buildP;
    buildP.ox = ox; buildP.oy = oy; buildP.angleDeg = angleDeg;
    buildP.H = H; buildP.V = V; buildP.flipY = flipY;
    buildP.wingCount = s_wingCount;
    buildP.frameLayer = frameLyr; buildP.otherLayer = otherLyr;

    QList<QGraphicsItem*> newItems = buildGeometry(buildP, ctx);

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
    const QString doorName = tr("회전문-%1").arg(++s_revolvingDoorSeq);

    for (int i = 0; i < newItems.size(); ++i)
        tagRevolvingDoorItemWithGroupId(newItems.at(i), doorUuid);

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
    doorInfo.type                = "PlanRevolvingDoor";
    doorInfo.name                = doorName;
    doorInfo.props["H"]          = H;
    doorInfo.props["V"]          = V;
    doorInfo.props["WingCount"]  = s_wingCount;
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

    ctx->undoStack()->beginMacro(tr("회전문"));
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
            tr("회전문 배치 — P1(%1,%2)  H=%3  V=%4  날개=%5개  각도=%6°")
                .arg(ox,0,'f',1).arg(oy,0,'f',1)
                .arg(H,0,'f',1).arg(V,0,'f',1)
                .arg(s_wingCount)
                .arg(angleDeg,0,'f',1),
            CommandDock::Info);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// findNearestMultilineAt
// ─────────────────────────────────────────────────────────────────────────────
LineItem *PlanRevolvingDoorTool::findNearestMultilineAt(const QPointF &pos)
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
LineItem *PlanRevolvingDoorTool::findNearestMLWithDir(
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
void PlanRevolvingDoorTool::trimWallInDoorOpening(
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
        QPointF      loc1 = revDoorWorldToLocal(ox, oy, c, s2, ln.start);
        QPointF      loc2 = revDoorWorldToLocal(ox, oy, c, s2, ln.end);

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
            MyVector cutLv = revDoorInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL);
            MyVector cutRv = revDoorInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR);
            LineItem *lp = new LineItem(wSmall, cutLv);
            LineItem *rp = new LineItem(cutRv, wLarge);
            copyRevDoorLineMlTags(lp, li);
            copyRevDoorLineMlTags(rp, li);
            stack->push(new RemoveEntityCommand(m_scene, li));
            stack->push(new AddEntityCommand(m_scene, lp));
            stack->push(new AddEntityCommand(m_scene, rp));
        }
        else if (lx1 < cutL) {
            const bool     isP1 = swapped;
            const MyVector mv   = revDoorInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        }
        else {
            const bool     isP1 = !swapped;
            const MyVector mv   = revDoorInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR);
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
void PlanRevolvingDoorTool::trimGeneralLines(
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
        QPointF      loc1 = revDoorWorldToLocal(ox, oy, c, s2, ln.start);
        QPointF      loc2 = revDoorWorldToLocal(ox, oy, c, s2, ln.end);

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
            MyVector cutLv = revDoorInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL);
            MyVector cutRv = revDoorInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR);
            LineItem *lp = new LineItem(wSmall, cutLv);
            LineItem *rp = new LineItem(cutRv, wLarge);
            copyRevDoorLineAppearance(lp, li);
            copyRevDoorLineAppearance(rp, li);
            stack->push(new RemoveEntityCommand(m_scene, li));
            stack->push(new AddEntityCommand(m_scene, lp));
            stack->push(new AddEntityCommand(m_scene, rp));
        } else if (lx1 < cutL) {
            const bool     isP1 = swapped;
            const MyVector mv   = revDoorInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        } else {
            const bool     isP1 = !swapped;
            const MyVector mv   = revDoorInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR);
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
void PlanRevolvingDoorTool::checkWallPositionIfNeeded()
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
bool PlanRevolvingDoorTool::isMLCompatibleDirection(
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
double PlanRevolvingDoorTool::computeWallPositionDelta(
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
double PlanRevolvingDoorTool::computeWallPositionDeltaFromSpan(double &outNearLy, double &outFarLy)
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
// addWallClosingLines — 마감선은 문 형태와 무관한 순수 벽-트림 로직이므로
// PlanDoorTool 의 정적 함수(addClosingLinesStatic)를 그대로 재사용한다.
// ─────────────────────────────────────────────────────────────────────────────
void PlanRevolvingDoorTool::addWallClosingLines(
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
// PlanRevolvingDoorCommand
// ─────────────────────────────────────────────────────────────────────────────
PlanRevolvingDoorCommand::PlanRevolvingDoorCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent)
    , m_ctx(ctx)
{}

void PlanRevolvingDoorCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new PlanRevolvingDoorTool());
}
