#include "plandoortool.h"
#include "plandoorsetupdialog.h"
#include "planarchdata.h"
#include "scenesymbolregistry.h"
#include "spacefinish.h"
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
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// 문틀 단면 치수 상수 (단위: mm)
// ─────────────────────────────────────────────────────────────────────────────

double  PlanDoorTool::s_h               = 900.0;
double  PlanDoorTool::s_v               = 200.0;
bool    PlanDoorTool::s_useWallThickness = false;
int     PlanDoorTool::s_frameType       = 3;   // 기본값 Type4
QString PlanDoorTool::s_wallGroupId;
int     PlanDoorTool::s_wallPositionMode  = 0;
int     PlanDoorTool::s_lastWallPosMode   = WallPositionDialog::Inner;
double  PlanDoorTool::s_wallDetectedSpan  = 0.0;
bool    PlanDoorTool::s_mlAtP1            = false;
static int s_doorSeq = 0;


static void tagItemWithGroupId(QGraphicsItem *item, const QString &uuid)
{
    item->setData(PD_DATA_GROUP_ID, uuid);
}

// ── 도구 기본값 저장/불러오기 (3-1: 도면 스코프 우선, 전역은 씨앗) ──────────────
//   네임스페이스 "PlanDoorTool" 은 기존 QSettings 그룹명 그대로 → 전역에 쌓여
//   있던 사용자 기본값이 그대로 유지된다.
// 문-113(style 1)은 외여닫이와 별도 네임스페이스로 설정값을 따로 관리한다.
static const char *planDoorNamespace(int style)
{
    return style == 1 ? "PlanDoor113Tool" : "PlanDoorTool";
}

static QString spaceFinishNamespace(int style)
{
    return style == 1 ? QStringLiteral("SpaceFinish113") : QStringLiteral("SpaceFinish");
}

static void savePlanDoorSettings(MyGraphicsScene *scene, int style)
{
    ToolDefaults d(planDoorNamespace(style), scene);
    d.put("H",                PlanDoorTool::globalH());
    d.put("V",                PlanDoorTool::globalV());
    d.put("UseWallThickness", PlanDoorTool::useWallThickness());
    d.put("FrameType",        PlanDoorTool::frameType());
}

static void loadPlanDoorSettings(MyGraphicsScene *scene, int style)
{
    DoorLayerSettings::load(scene);
    SpaceFinish::load(scene, spaceFinishNamespace(style));
    ToolDefaults d(planDoorNamespace(style), scene);
    PlanDoorTool::setGlobalH(d.getDouble("H",                900.0));
    PlanDoorTool::setGlobalV(d.getDouble("V",                200.0));
    PlanDoorTool::setUseWallThickness(d.getBool("UseWallThickness", false));
    PlanDoorTool::setFrameType(d.getInt("FrameType",         3));
}

double  PlanDoorTool::globalH()                        { return s_h; }
void    PlanDoorTool::setGlobalH(double v)             { if (v > 0.0) s_h = v; }
double  PlanDoorTool::globalV()                        { return s_v; }
void    PlanDoorTool::setGlobalV(double v)             { if (v > 0.0) s_v = v; }
bool    PlanDoorTool::useWallThickness()               { return s_useWallThickness; }
void    PlanDoorTool::setUseWallThickness(bool use)    { s_useWallThickness = use; }
int     PlanDoorTool::frameType()                      { return s_frameType; }
void    PlanDoorTool::setFrameType(int t)              { s_frameType = qBound(0, t, 3); }
QString PlanDoorTool::frameLayer()                     { return DoorLayerSettings::frameLayer(); }
void    PlanDoorTool::setFrameLayer(const QString &n)  { DoorLayerSettings::setFrameLayer(n); }
QString PlanDoorTool::otherLayer()                         { return DoorLayerSettings::otherLayer(); }
void    PlanDoorTool::setOtherLayer(const QString &n)      { DoorLayerSettings::setOtherLayer(n); }
QString PlanDoorTool::trimExcludeLayer()                   { return DoorLayerSettings::trimExcludeLayer(); }
void    PlanDoorTool::setTrimExcludeLayer(const QString &n){ DoorLayerSettings::setTrimExcludeLayer(n); }

CommandBase *PlanDoorTool::createCommand(IAppContext *c)
{
    return new PlanDoorCommand(c, 0);
}

CommandBase *PlanDoorTool::createCommand113(IAppContext *c)
{
    return new PlanDoorCommand(c, 1);
}

// ── door-113.lsp 문짝 형상 (local 좌표: 경첩 (HX,V), X=문 진행, Y=P2 방향) ────────
//   문짝 = 경첩에서 30° 열린 길이 R·두께 T 의 닫힌 사각형, 호 = 닫힘선(0°)에서 문짝 끝(≈27°)까지
static const double DOOR113_OPEN_DEG = 30.0;

double door113ArcSpanDeg(double R, double T)
{
    const double a = qDegreesToRadians(DOOR113_OPEN_DEG);
    return qRadiansToDegrees(std::atan2(R * std::sin(a) - T * std::cos(a),
                                        R * std::cos(a) + T * std::sin(a)));
}

// 문짝 4점 (door_point, da_1, da_2, da_3) — local 오프셋
void door113LeafLocal(double R, double T, double out[4][2])
{
    const double a  = qDegreesToRadians(DOOR113_OPEN_DEG);
    const double ux = std::cos(a), uy = std::sin(a);     // 문짝 진행
    const double px = std::sin(a), py = -std::cos(a);    // 두께 방향 (벽 쪽)
    out[0][0] = 0.0;               out[0][1] = 0.0;
    out[1][0] = R * ux;            out[1][1] = R * uy;
    out[2][0] = R * ux + T * px;   out[2][1] = R * uy + T * py;
    out[3][0] = T * px;            out[3][1] = T * py;
}

// ── 생성자 ───────────────────────────────────────────────────────────────────
PlanDoorTool::PlanDoorTool(QObject *parent, int doorStyle)
    : AbstractTool(parent)
    , m_style(doorStyle)
    , m_state(WaitP1)
    , m_currentMousePos(0.0, 0.0)
{}

// ─────────────────────────────────────────────────────────────────────────────
// applySnapForState — 상태별 osnap 자동 전환
//   WaitP1 / WaitP3 : Nearest ON,  Perpendicular OFF
//   WaitP2          : Nearest OFF, Perpendicular ON
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorTool::applySnapForState(State s)
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
void PlanDoorTool::restoreSavedSnap()
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
void PlanDoorTool::activate(MyGraphicsScene *scene)
{
    // [3-1] 세션당 1회가 아니라 매 활성화마다 "현재 도면"의 값으로 다시 채운다.
    //   도면(.cdb)에 값이 있으면 그것을, 없으면 전역 QSettings/코드 기본값을 쓴다.
    loadPlanDoorSettings(scene, m_style);

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
        ctx->commandDock()->logMessage("PLAN-DOOR", CommandDock::Command);
    }

    showP1Prompt();
}

// ── deactivate ───────────────────────────────────────────────────────────────
void PlanDoorTool::deactivate()
{
    restoreSavedSnap();

    m_state = WaitP1;
    if (m_scene)
        m_scene->clearOverlay();
    AbstractTool::deactivate();
}

// ── showP1Prompt ─────────────────────────────────────────────────────────────
void PlanDoorTool::showP1Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(tr(" P1: 도어가 들어갈 선  지정 (nea) "), {"Setup"});
}

// ── showP1DistancePrompt ─────────────────────────────────────────────────────
void PlanDoorTool::showP1DistancePrompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(tr(" 거리를 입력하세요 : "), {});
}

// ── captureP1WallDirs ────────────────────────────────────────────────────────
// 앵커를 지나는 모든 다중선 선분의 (서로 평행하지 않은) 방향을 수집.
// 모서리(ㄱ자)에서는 가로/세로 방향이 모두 후보가 되어 마우스 방향으로 선택된다.
void PlanDoorTool::captureP1WallDirs(const QPointF &pos)
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

// ── resolveP1WallDir ─────────────────────────────────────────────────────────
// 후보 방향 중 앵커→마우스 방향과 가장 가까운 것을 반환 (후보 없으면 (0,0)).
QPointF PlanDoorTool::resolveP1WallDir() const
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
// 앵커(m_p1Anchor)에서 벽 방향으로 dist 이동한 최종 P1 좌표 반환
// 벽 방향은 m_p1WallDir (정규화). (0,0)이면 마우스 방향을 대신 사용.
// 마우스 위치로 부호(어느 쪽) 결정.
QPointF PlanDoorTool::computeP1FromAnchor(double dist) const
{
    QPointF dir = resolveP1WallDir();
    if (dir.isNull()) {
        // 벽 미탐지: 마우스 방향 사용
        QPointF d = m_currentMousePos - m_p1Anchor;
        double len = std::hypot(d.x(), d.y());
        if (len < 1e-9) return m_p1Anchor;
        dir = QPointF(d.x() / len, d.y() / len);
    } else {
        // 마우스가 어느 쪽을 향하는지 부호 결정
        QPointF toMouse = m_currentMousePos - m_p1Anchor;
        double dot = toMouse.x() * dir.x() + toMouse.y() * dir.y();
        if (dot < 0.0) dir = QPointF(-dir.x(), -dir.y());
    }
    return m_p1Anchor + dir * dist;
}

// ── showP2Prompt ─────────────────────────────────────────────────────────────
void PlanDoorTool::showP2Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    if (s_useWallThickness)
        ctx->commandDock()->showInteractivePrompt(
            tr(" P2: 반대편 선  지정 (per) — 두께 자동 측정 "), {});
    else
        ctx->commandDock()->showInteractivePrompt(
            tr(" P2: 반대편 선 지정 (per) — 스윙 방향 결정 "), {});
}

// ── showP3Prompt ─────────────────────────────────────────────────────────────
void PlanDoorTool::showP3Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(
        tr(" P3: 방향점 지정 (nea) — 문틀 진행 방향 "), {});
}

// ── mouseMove ────────────────────────────────────────────────────────────────
void PlanDoorTool::mouseMove(QPointF pos)
{
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    QPointF   finalPos = pos;
    SnapType  snapType = SnapType::None;

    if (ctx && ctx->snapManager()) {
        // P2: P1을 기준점으로 넘겨 수직 스냅 활성화
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
        // 앵커에서 벽 방향으로 마우스 투영 (노란색)
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
        // P1 → 마우스 러버밴드
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
void PlanDoorTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        if (m_state == WaitP1Distance) {
            // 거리 입력 취소 → WaitP1 복귀
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

    // 스냅 적용 (P2는 P1 기준 수직 스냅)
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
            // Shift+클릭: 앵커 저장 후 거리 입력 대기
            m_p1Anchor = snapped;
            captureP1WallDirs(snapped);
            m_state = WaitP1Distance;
            m_scene->clearOverlay();
            showP1DistancePrompt();
            return;
        }

        m_p1 = snapped;
        // 다중선 그룹 ID 저장 (트림용) — 위치 다이얼로그는 P2 확정 후 처리
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
        // 현재 마우스 위치로 첫 고스트
        double angleDeg, V;
        bool   flipY;
        if (computeFrom3Pts(m_p1, m_p2, m_currentMousePos, angleDeg, V, flipY))
            updateGhostDoor(m_p1, angleDeg, V, flipY);
        showP3Prompt();

    } else {
        // WaitP3 → 배치
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
void PlanDoorTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/) {}

// ── processCommand ────────────────────────────────────────────────────────────
bool PlanDoorTool::processCommand(const QString &command)
{
    IAppContext *ctx = getContext();
    const QString cmd = command.trimmed();

    // ── WaitP1Distance: 거리 입력 → 최종 P1 확정 ────────────────────────────
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

    // ── WaitP1: Setup 또는 좌표 입력 ─────────────────────────────────────────
    if (m_state == WaitP1) {
        const QString lower = cmd.toLower();
        if (lower == "s" || lower == "setup") {
            QWidget *parentWin =
                ctx ? dynamic_cast<QWidget *>(ctx->commandDock()) : nullptr;

            PlanDoorSetupDialog dlg(s_h, s_v, s_useWallThickness, s_frameType,
                                    parentWin);
            dlg.setSpaceFinishType(SpaceFinish::type());
            if (dlg.exec() == QDialog::Accepted) {
                SpaceFinish::setType(dlg.spaceFinishType());
                SpaceFinish::save(m_scene, spaceFinishNamespace(m_style));
                setGlobalH(dlg.h());
                setGlobalV(dlg.v());
                setUseWallThickness(dlg.useWallThickness());
                setFrameType(dlg.frameType());
                savePlanDoorSettings(m_scene, m_style);
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

    // ── WaitP2: 좌표 입력 ────────────────────────────────────────────────────
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

    // ── WaitP3: 좌표 입력 → 배치 ─────────────────────────────────────────────
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
void PlanDoorTool::onFinish()
{
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

// ── onCancel ─────────────────────────────────────────────────────────────────
void PlanDoorTool::onCancel()
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
// computeFrom3Pts
//
// u        = normalize(P3-P1)        → local X
// n        = 90°CCW(u)               → local Y 후보
// vSigned  = dot(P2-P1, n)           → 부호 있는 두께
//
// 입력값 모드 (s_useWallThickness=false):
//   V = s_v (Setup 값), P2는 방향(부호)만 사용 → 최소 거리 기준 완화
// 벽두께 모드 (s_useWallThickness=true):
//   V = |vSigned|,  최소 10mm 이상이어야 배치
// ─────────────────────────────────────────────────────────────────────────────
bool PlanDoorTool::computeFrom3Pts(
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

    // 입력값 모드: 방향(부호)만 필요 → 1mm 이상이면 충분
    // 벽두께 모드: 실제 거리 사용 → 10mm 이상 필요
    const double minV = s_useWallThickness ? 10.0 : 1.0;
    if (std::abs(vSigned) < minV)
        return false;

    outAngleDeg = qRadiansToDegrees(std::atan2(uy, ux));
    outV        = s_useWallThickness ? std::abs(vSigned) + 20.0 : s_v;
    outFlipY    = (vSigned < 0.0);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// updateGhostDoor
//
// flipY=false: local Y = 90°CCW(X),  스윙 호 CCW → Qt drawSpan=-90
// flipY=true : local Y = 90°CW(X),   스윙 호 CW  → Qt drawSpan=+90
// P1 = 경첩 위치 (local X=0, HX=DoorLayerSettings::stopWidth())
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorTool::updateGhostDoor(
    const QPointF &p1, double angleDeg, double V, bool flipY)
{
    if (!m_scene)
        return;

    const double H  = s_h;
    const double J  = DoorLayerSettings::jambOuter();
    const double S  = DoorLayerSettings::stopWidth();
    const double K  = DoorLayerSettings::stepHeight();
    const double T  = DoorLayerSettings::stepHeight();
    const double R  = H - 2.0 * DoorLayerSettings::stopWidth();
    const double HX = S;

    double ox = p1.x(), oy = p1.y();
    const double c  = std::cos(qDegreesToRadians(angleDeg));
    const double s2 = std::sin(qDegreesToRadians(angleDeg));

    // 벽두께 모드: 내/외벽 10mm 돌출 원점 보정 (실내 방향)
    if (s_useWallThickness) {
        if (!flipY) { ox += 10.0 * s2; oy -= 10.0 * c; }
        else        { ox -= 10.0 * s2; oy += 10.0 * c; }
    } else if (s_wallPositionMode != 0) {
        // 입력값 모드: 벽 위치 선택에 따른 원점 보정 (ghost도 실제 배치와 일치)
        double nearLy, farLy, delta;
        if (!s_wallGroupId.isEmpty() && s_mlAtP1)
            delta = computeWallPositionDelta(c, s2, p1, flipY, nearLy, farLy);
        else
            delta = computeWallPositionDeltaFromSpan(nearLy, farLy);
        if (!flipY) { ox -= delta * s2; oy += delta * c; }
        else        { ox += delta * s2; oy -= delta * c; }
    }

    auto pt = [&](double dx, double dy) -> QPointF {
        if (!flipY)
            return QPointF(ox + dx * c - dy * s2, oy + dx * s2 + dy * c);
        else
            return QPointF(ox + dx * c + dy * s2, oy + dx * s2 - dy * c);
    };

    QPainterPath path;

    auto addPoly = [&](std::initializer_list<QPointF> pts) {
        auto it = pts.begin();
        path.moveTo(*it++);
        for (; it != pts.end(); ++it) path.lineTo(*it);
        path.closeSubpath();
    };
    auto addOpen = [&](std::initializer_list<QPointF> pts) {
        auto it = pts.begin();
        path.moveTo(*it++);
        for (; it != pts.end(); ++it) path.lineTo(*it);
    };
    auto addSeg = [&](double x1, double y1, double x2, double y2) {
        path.moveTo(pt(x1, y1));
        path.lineTo(pt(x2, y2));
    };

    if (m_style == 1) {
        // door-113: 6점 잼브(Type1 단면) 좌우 한 쌍만, 연결선 없음
        addPoly({ pt(0,0), pt(0,V), pt(S,V), pt(S,V-K), pt(J,V-K), pt(J,0) });
        addPoly({ pt(H,0), pt(H,V), pt(H-S,V), pt(H-S,V-K), pt(H-J,V-K), pt(H-J,0) });
    } else switch (s_frameType) {
    case 1:
        addPoly({ pt(0,0), pt(0,V), pt(S,V), pt(S,V-K), pt(J,V-K), pt(J,0) });
        addPoly({ pt(H,0), pt(H,V), pt(H-S,V), pt(H-S,V-K), pt(H-J,V-K), pt(H-J,0) });
        addSeg(S, V, H-S, V);   addSeg(J, V-K, H-J, V-K);
        addSeg(J, 0, H-J, 0);
        break;
    case 2:
        addPoly({ pt(0,0), pt(0,V), pt(S,V), pt(S,V-K), pt(J,V-K), pt(J,K), pt(S,0) });
        addPoly({ pt(H,0), pt(H,V), pt(H-S,V), pt(H-S,V-K), pt(H-J,V-K), pt(H-J,K), pt(H-S,0) });
        addSeg(S, V, H-S, V);   addSeg(J, V-K, H-J, V-K);
        addSeg(J, K, H-J, K);   addSeg(S, 0, H-S, 0);
        break;
    case 3:
        addPoly({ pt(0,0), pt(0,V), pt(J,V), pt(J,0) });
        addPoly({ pt(H,0), pt(H,V), pt(H-J,V), pt(H-J,0) });
        addSeg(J, V, H-J, V);   addSeg(J, 0, H-J, 0);
        break;
    default:
        addPoly({ pt(0,0), pt(0,V), pt(S,V), pt(S,V-K),
                  pt(J,V-K), pt(J,K), pt(S,K), pt(S,0) });
        addPoly({ pt(H,0), pt(H,V), pt(H-S,V), pt(H-S,V-K),
                  pt(H-J,V-K), pt(H-J,K), pt(H-S,K), pt(H-S,0) });
        addSeg(S, V, H-S, V);   addSeg(J, V-K, H-J, V-K);
        addSeg(J, K, H-J, K);   addSeg(S, 0, H-S, 0);
        break;
    }

    if (m_style == 1) {
        double lf[4][2];
        door113LeafLocal(R, T, lf);
        addPoly({ pt(HX+lf[0][0], V+lf[0][1]), pt(HX+lf[1][0], V+lf[1][1]),
                  pt(HX+lf[2][0], V+lf[2][1]), pt(HX+lf[3][0], V+lf[3][1]) });
    } else {
        addOpen({ pt(HX, V), pt(HX, V+R), pt(HX+T, V+R), pt(HX+T, V) });
    }

    {
        QPointF cen = pt(HX, V);
        QRectF  rect(cen.x() - R, cen.y() - R, R * 2.0, R * 2.0);
        const double drawStart = -angleDeg;
        const double swingDeg  = (m_style == 1) ? door113ArcSpanDeg(R, T) : 90.0;
        const double drawSpan  = flipY ? swingDeg : -swingDeg;
        path.arcMoveTo(rect, drawStart);
        path.arcTo(rect, drawStart, drawSpan);
    }

    updateGhostPreview(path, currentLayerName());
}

// ─────────────────────────────────────────────────────────────────────────────
// ── 좌표 변환 헬퍼 (pt 람다 대체) ────────────────────────────────────────────
struct DoorPtCalc {
    double ox, oy, c, s2;
    bool   flipY;
    MyVector operator()(double dx, double dy) const {
        if (!flipY)
            return MyVector(ox + dx*c - dy*s2, oy + dx*s2 + dy*c);
        else
            return MyVector(ox + dx*c + dy*s2, oy + dx*s2 - dy*c);
    }
};

static LineItem *makeDoorConn(const DoorPtCalc &pt,
                               double x1, double y1, double x2, double y2,
                               const QString &layer, const QPen &pen)
{
    LineItem *li = new LineItem(pt(x1,y1), pt(x2,y2));
    li->setLayerName(layer);
    li->setPen(pen);
    return li;
}

// ── buildGeometry ─────────────────────────────────────────────────────────────
QList<QGraphicsItem*> PlanDoorTool::buildGeometry(const DoorBuildParams &pIn, IAppContext *ctx)
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
    const double T  = DoorLayerSettings::stepHeight();
    const double R  = H - 2.0 * DoorLayerSettings::stopWidth();
    const double HX = S;

    const double c  = std::cos(qDegreesToRadians(p.angleDeg));
    const double s2 = std::sin(qDegreesToRadians(p.angleDeg));
    DoorPtCalc   pt { p.ox, p.oy, c, s2, p.flipY };

    // 레이어 색·두께·선종류 적용, 레이어가 없으면 현재 레이어 → 흰색 순으로 대체
    const QPen framePen = AbstractTool::layerPenFor(ctx, p.frameLayer);
    const QPen otherPen = AbstractTool::layerPenFor(ctx, p.otherLayer);

    std::vector<MyVector> leftPts, rightPts;
    switch (p.doorStyle == 1 ? 1 : p.frameType) {
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

    QList<QGraphicsItem*> connLines;
    if (p.doorStyle != 1) switch (p.frameType) {
    case 1:
        connLines << makeDoorConn(pt, S,V,   H-S,V,   p.otherLayer, otherPen)
                  << makeDoorConn(pt, J,V-K, H-J,V-K, p.otherLayer, otherPen)
                  << makeDoorConn(pt, J,0,   H-J,0,   p.otherLayer, otherPen);
        break;
    case 2:
        connLines << makeDoorConn(pt, S,V,   H-S,V,   p.otherLayer, otherPen)
                  << makeDoorConn(pt, J,V-K, H-J,V-K, p.otherLayer, otherPen)
                  << makeDoorConn(pt, J,K,   H-J,K,   p.otherLayer, otherPen)
                  << makeDoorConn(pt, S,0,   H-S,0,   p.otherLayer, otherPen);
        break;
    case 3:
        connLines << makeDoorConn(pt, J,V,   H-J,V,   p.otherLayer, otherPen)
                  << makeDoorConn(pt, J,0,   H-J,0,   p.otherLayer, otherPen);
        break;
    default:
        connLines << makeDoorConn(pt, S,V,   H-S,V,   p.otherLayer, otherPen)
                  << makeDoorConn(pt, J,V-K, H-J,V-K, p.otherLayer, otherPen)
                  << makeDoorConn(pt, J,K,   H-J,K,   p.otherLayer, otherPen)
                  << makeDoorConn(pt, S,0,   H-S,0,   p.otherLayer, otherPen);
        break;
    }

    std::vector<MyVector> leafPts;
    bool leafClosed = false;
    double swingDeg = 90.0;
    if (p.doorStyle == 1) {
        double lf[4][2];
        door113LeafLocal(R, T, lf);
        for (int i = 0; i < 4; ++i)
            leafPts.push_back(pt(HX + lf[i][0], V + lf[i][1]));
        leafClosed = true;
        swingDeg   = door113ArcSpanDeg(R, T);
    } else {
        leafPts = { pt(HX,V), pt(HX,V+R), pt(HX+T,V+R), pt(HX+T,V) };
    }
    PolylineItem *leafPoly = new PolylineItem(leafPts, leafClosed);
    leafPoly->setLayerName(p.frameLayer); leafPoly->setPen(framePen);

    const double   arcSpan   = p.flipY ? -swingDeg : swingDeg;
    const MyVector arcCenter = pt(HX, V);
    ArcItem *swingArc = new ArcItem(MyArc(arcCenter, R, p.angleDeg, arcSpan));
    swingArc->setLayerName(p.otherLayer); swingArc->setPen(otherPen);

    QList<QGraphicsItem*> result;
    result << leftJamb << rightJamb << connLines << leafPoly << swingArc;
    return result;
}

// placeDoor
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorTool::placeDoor(
    const QPointF &p1, double angleDeg, double V, bool flipY)
{
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->undoStack())
        return;

    const double H  = s_h;
//    const double J  = DoorLayerSettings::jambOuter();
//    const double S  = DoorLayerSettings::stopWidth();
//    const double K  = DoorLayerSettings::stepHeight();
 //   const double T  = DoorLayerSettings::stepHeight();
 //   const double R  = H - 2.0 * DoorLayerSettings::stopWidth();
 //   const double HX = S;

    double ox = p1.x(), oy = p1.y();
    const double c  = std::cos(qDegreesToRadians(angleDeg));
    const double s2 = std::sin(qDegreesToRadians(angleDeg));

    // 원점 보정 (두 모드 상호 배타)
    double wallNearLy = 0.0, wallFarLy = V;
    if (!s_useWallThickness && s_wallPositionMode != 0) {
        // 입력값 모드: 내벽/중앙/외벽 위치에 따른 원점 보정
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
        // 벽두께 모드: 내/외벽 10mm 돌출 (실내 방향으로 원점 이동)
        if (!flipY) { ox += 10.0 * s2; oy -= 10.0 * c; }
        else        { ox -= 10.0 * s2; oy += 10.0 * c; }
    }

    const QString frameLyr = resolveLayerName(getContext(), DoorLayerSettings::frameLayer());
    const QString otherLyr = resolveLayerName(getContext(), DoorLayerSettings::otherLayer());

    DoorBuildParams buildP;
    buildP.ox = ox; buildP.oy = oy; buildP.angleDeg = angleDeg;
    buildP.H = H; buildP.V = V; buildP.flipY = flipY;
    buildP.frameType = s_frameType;
    buildP.doorStyle = m_style;
    buildP.frameLayer = frameLyr; buildP.otherLayer = otherLyr;

    QList<QGraphicsItem*> newItems = buildGeometry(buildP, ctx);

    // 트림 후보를 매크로 시작 전에 수집 (프레임 선이 후보에 포함되지 않도록)
    QList<LineItem *> trimCandidates;
    {
        const QList<QGraphicsItem *> allItems = m_scene->items();
        for (QGraphicsItem *gi : allItems) {
            LineItem *li = dynamic_cast<LineItem *>(gi);
            if (!li) continue;
            if (!li->data(ML_DATA_GROUP_ID).toString().isEmpty()) continue; // ML: 별도 처리
            if (!DoorLayerSettings::trimExcludeLayer().isEmpty() && li->layerName() == DoorLayerSettings::trimExcludeLayer()) continue;
            trimCandidates << li;
        }
    }

    // ── 그룹 UUID 태깅 + 레지스트리 등록 ─────────────────────────────────────
    const QString doorUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString doorName = tr("문-%1").arg(++s_doorSeq);

    for (int i = 0; i < newItems.size(); ++i)
        tagItemWithGroupId(newItems.at(i), doorUuid);

    // 트림 Y 범위 결정 (props 저장 전에 계산)
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

    // 공간벽 마감형태: 문 양쪽(P1-P2 가상선 / 문 너비 H 만큼 떨어진 가상선)의 space 선 수집 → 끊기/연결선 계획
    SpaceFinish::Plan finishL, finishR;
    SpaceFinish::planBoth(m_scene, s_wallGroupId, p1, m_p2, ox, oy, c, s2, H, finishL, finishR);

    SymbolInfo doorInfo;
    doorInfo.type                = "PlanDoor";
    doorInfo.name                = doorName;
    doorInfo.props["H"]          = H;
    doorInfo.props["V"]          = V;
    doorInfo.props["FrameType"]  = s_frameType;
    doorInfo.props["DoorStyle"]  = m_style;
    doorInfo.props["ox"]         = ox;
    doorInfo.props["oy"]         = oy;
    doorInfo.props["angleDeg"]   = angleDeg;
    doorInfo.props["flipY"]      = flipY ? 1 : 0;
    doorInfo.props["FrameLayer"] = frameLyr;
    doorInfo.props["OtherLayer"] = otherLyr;
    // 벽 트림 복원에 필요한 파라미터
    doorInfo.props["wallGroupId"] = s_wallGroupId;
    doorInfo.props["trimYLow"]    = trimYLow;
    doorInfo.props["trimYHigh"]   = trimYHigh;
    doorInfo.props["wallNearLy"]  = wallNearLy;
    doorInfo.props["wallFarLy"]   = wallFarLy;
    // 공간벽 마감형태 (삭제 시 벽 복원 / 속성창 편집 시 연결선 재생성용)
    SpaceFinish::storeProps(doorInfo.props, finishL, finishR);
    // ─────────────────────────────────────────────────────────────────────────

    ctx->undoStack()->beginMacro(m_style == 1 ? tr("평면도어 113")
                                               : tr("평면도어 Type%1").arg(s_frameType + 1));
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
            tr("평면도어 배치 — P1(%1,%2)  H=%3  V=%4  각도=%5°  flipY=%6")
                .arg(ox,0,'f',1).arg(oy,0,'f',1)
                .arg(H,0,'f',1).arg(V,0,'f',1)
                .arg(angleDeg,0,'f',1)
                .arg(flipY ? "Y" : "N"),
            CommandDock::Info);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// findNearestMultilineAt
// ─────────────────────────────────────────────────────────────────────────────
LineItem *PlanDoorTool::findNearestMultilineAt(const QPointF &pos)
{
    if (!m_scene)
        return nullptr;

    // pos 가 실제로 다중선 선분 위에 있을 때만 감지.
    // 수직 거리 15mm 이내 + 선분 범위 50mm 연장 허용
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

        // pos 를 선분에 투영 — 선분 범위 밖이면 건너뜀
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
// P1→P2 방향(nx,ny)과 수직인 다중선 세그먼트만 탐색.
// 방향 불일치 세그먼트(|dot|≥0.5)는 건너뜀 → 다른 벽 ML 오탐 방지.
// ─────────────────────────────────────────────────────────────────────────────
LineItem *PlanDoorTool::findNearestMLWithDir(
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

        // 방향 필터: |dot(세그먼트방향, P1→P2)| < 0.5 이어야 올바른 벽 ML
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
void PlanDoorTool::trimWallInDoorOpening(
    double ox, double oy, double c, double s2,
    double yLow, double yHigh, QUndoStack *stack,
    const QList<LineItem *> &extLeftItems, const QList<LineItem *> &extRightItems,
    double extLen)
{
    if (!m_scene || s_wallGroupId.isEmpty())
        return;

    const double EPS  = 1e-6;

    auto toLocal = [&](const MyVector &p) -> QPointF {
        const double dx = p.x - ox, dy = p.y - oy;
        return QPointF(dx * c + dy * s2, dy * c - dx * s2);
    };
    auto toWorld = [&](double lx, double ly) -> MyVector {
        return MyVector(ox + lx * c - ly * s2, oy + lx * s2 + ly * c);
    };

    QList<LineItem *> wallLines;
    const QList<QGraphicsItem *> sceneItems = m_scene->items();
    for (int i = 0; i < sceneItems.size(); ++i) {
        QGraphicsItem *gi = sceneItems[i];
        LineItem *li = dynamic_cast<LineItem *>(gi);
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString() != s_wallGroupId) continue;
        if (li->data(ML_DATA_ROLE).toString() == "center") continue;
        if (!DoorLayerSettings::trimExcludeLayer().isEmpty() && li->layerName() == DoorLayerSettings::trimExcludeLayer()) continue; // 트림 제외 레이어
        // 방향 필터: 도어 방향(c,s2)에 평행한 세그먼트만 수집
        // 공유 그룹의 수직 세그먼트(다른 벽)는 제외
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
        // 공간벽 마감형태 대상 space 선은 문 양쪽 바깥(-X / +X)으로 extLen 만큼 더 끊는다
        const double cutL = extLeftItems.contains(li)  ? -extLen      : 0.0;
        const double cutR = extRightItems.contains(li) ? s_h + extLen : s_h;
        const MyLine ln   = li->getLine();
        QPointF      loc1 = toLocal(ln.start);
        QPointF      loc2 = toLocal(ln.end);

        bool swapped = false;
        if (loc1.x() > loc2.x()) { qSwap(loc1, loc2); swapped = true; }

        const double lx1 = loc1.x(), ly1 = loc1.y();
        const double lx2 = loc2.x(), ly2 = loc2.y();

        if (lx2 <= cutL + EPS || lx1 >= cutR - EPS)
            continue;

        // P1→P2 범위 밖의 다른 벽 ML 세그먼트 제외 (중간점 로컬 Y 기준)
        const double midLy = (ly1 + ly2) / 2.0;
        const double yTol  = 15.0;
        if (midLy < yLow - yTol || midLy > yHigh + yTol)
            continue;

        auto interp = [&](double cutX) -> MyVector {
            const double span = lx2 - lx1;
            const double t    = span > 1e-9 ? (cutX - lx1) / span : 0.0;
            return toWorld(cutX, ly1 + t * (ly2 - ly1));
        };

        const MyVector wSmall = swapped ? ln.end   : ln.start;
        const MyVector wLarge = swapped ? ln.start : ln.end;

        auto makeTag = [&](LineItem *nli) {
            nli->setLayerName(li->layerName());
            nli->setPen(li->pen());
            nli->setData(ML_DATA_GROUP_ID, li->data(ML_DATA_GROUP_ID));
            nli->setData(ML_DATA_ROLE,     li->data(ML_DATA_ROLE));
            const double sp = li->data(ML_DATA_SPACING).toDouble();
            if (sp > 0.0) nli->setData(ML_DATA_SPACING, sp);
        };

        if (lx1 >= cutL - EPS && lx2 <= cutR + EPS) {
            stack->push(new RemoveEntityCommand(m_scene, li));
        }
        else if (lx1 < cutL && lx2 > cutR) {
            LineItem *lp = new LineItem(wSmall, interp(cutL));
            LineItem *rp = new LineItem(interp(cutR), wLarge);
            makeTag(lp); makeTag(rp);
            stack->push(new RemoveEntityCommand(m_scene, li));
            stack->push(new AddEntityCommand(m_scene, lp));
            stack->push(new AddEntityCommand(m_scene, rp));
        }
        else if (lx1 < cutL) {
            const bool   isP1 = swapped;
            const MyVector mv  = interp(cutL);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        }
        else {
            const bool   isP1 = !swapped;
            const MyVector mv  = interp(cutR);
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
//
// 사전 수집된 후보 LineItem 목록을 로컬 X=[0, H] 범위로 트림.
// ML 그룹 선·제외 레이어 선은 호출 전 필터링된 상태로 전달됨.
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorTool::trimGeneralLines(
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

    auto toLocal = [&](const MyVector &p) -> QPointF {
        const double dx = p.x - ox, dy = p.y - oy;
        return QPointF(dx * c + dy * s2, dy * c - dx * s2);
    };
    auto toWorld = [&](double lx, double ly) -> MyVector {
        return MyVector(ox + lx * c - ly * s2, oy + lx * s2 + ly * c);
    };

    for (LineItem *li : candidates) {
        if (!li->scene())
            continue;   // 이미 씬에서 제거된 경우

        const MyLine ln   = li->getLine();
        QPointF      loc1 = toLocal(ln.start);
        QPointF      loc2 = toLocal(ln.end);

        bool swapped = false;
        if (loc1.x() > loc2.x()) { qSwap(loc1, loc2); swapped = true; }

        const double lx1 = loc1.x(), ly1 = loc1.y();
        const double lx2 = loc2.x(), ly2 = loc2.y();

        if (lx2 <= cutL + EPS || lx1 >= cutR - EPS)
            continue;

        // P1·P2 사이 Y 범위 밖의 선은 트림 제외
        const double lyMin = std::min(ly1, ly2);
        const double lyMax = std::max(ly1, ly2);
        if (lyMax < yLow - yTol || lyMin > yHigh + yTol)
            continue;

        auto interp = [&](double cutX) -> MyVector {
            const double span = lx2 - lx1;
            const double t    = span > 1e-9 ? (cutX - lx1) / span : 0.0;
            return toWorld(cutX, ly1 + t * (ly2 - ly1));
        };

        const MyVector wSmall = swapped ? ln.end   : ln.start;
        const MyVector wLarge = swapped ? ln.start : ln.end;

        auto copyAttr = [&](LineItem *nli) {
            nli->setLayerName(li->layerName());
            nli->setPen(li->pen());
        };

        if (lx1 >= cutL - EPS && lx2 <= cutR + EPS) {
            stack->push(new RemoveEntityCommand(m_scene, li));
        } else if (lx1 < cutL && lx2 > cutR) {
            LineItem *lp = new LineItem(wSmall, interp(cutL));
            LineItem *rp = new LineItem(interp(cutR), wLarge);
            copyAttr(lp); copyAttr(rp);
            stack->push(new RemoveEntityCommand(m_scene, li));
            stack->push(new AddEntityCommand(m_scene, lp));
            stack->push(new AddEntityCommand(m_scene, rp));
        } else if (lx1 < cutL) {
            const bool     isP1 = swapped;
            const MyVector mv   = interp(cutL);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        } else {
            const bool     isP1 = !swapped;
            const MyVector mv   = interp(cutR);
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
//
// 입력값 모드에서 P2 확정 직후 호출.
// |P2-P1| > s_v 이면 WallPositionDialog를 표시하여 내벽/중앙/외벽을 선택.
// 다중선 유무와 무관하게 거리 조건만으로 판단.
// P1에서 다중선을 못 찾은 경우 P2 위치에서도 추가 탐색.
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorTool::checkWallPositionIfNeeded()
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

    // P1→P2 단위벡터 (벽 두께 방향)
    const double nx = dx / dist;
    const double ny = dy / dist;

    // P1에서 감지된 ML 방향 검증 — 불일치(다른 벽 ML)이면 제거
    if (!s_wallGroupId.isEmpty() && !isMLCompatibleDirection(s_wallGroupId, nx, ny)) {
        s_wallGroupId.clear();
        s_mlAtP1 = false;
    }

    // ML 없으면 P1에서 방향 필터 재탐색 (올바른 벽의 ML만 탐지)
    if (s_wallGroupId.isEmpty()) {
        LineItem *ml = findNearestMLWithDir(m_p1, nx, ny);
        if (ml) {
            s_wallGroupId = ml->data(ML_DATA_GROUP_ID).toString();
            s_mlAtP1 = true;
        }
    }

    // P1에서도 없으면 P2에서 방향 필터 탐색
    if (s_wallGroupId.isEmpty()) {
        LineItem *ml = findNearestMLWithDir(m_p2, nx, ny);
        if (ml)
            s_wallGroupId = ml->data(ML_DATA_GROUP_ID).toString();
        // s_mlAtP1 stays false
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
//
// 주어진 다중선 그룹(groupId)의 세그먼트 방향이 P1→P2 방향(nx, ny)과
// 수직에 가까운지 확인. 수직(|dot|<0.5)이면 올바른 벽의 ML, 평행이면 다른 벽 ML.
// ─────────────────────────────────────────────────────────────────────────────
bool PlanDoorTool::isMLCompatibleDirection(
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
        // |dot(세그먼트방향, P1→P2)| < 0.5: 벽이 P1→P2에 수직으로 통과 → 올바른 ML
        // 포인트→포인트 연속 생성 그룹은 수평+수직 혼재 → 하나라도 호환이면 TRUE
        const double dp = std::abs((sdx / slen) * nx + (sdy / slen) * ny);
        if (dp < 0.5) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// computeWallPositionDelta
//
// 다중선 그룹의 선들로부터 flipY 보정된 로컬 Y 범위를 구하고
// 선택된 벽 위치 모드(내벽/중앙/외벽)에 따른 원점 보정값(delta)을 반환.
// outNearLy / outFarLy : 보정 후 원점 기준의 벽 로컬 Y 위치
// ─────────────────────────────────────────────────────────────────────────────
double PlanDoorTool::computeWallPositionDelta(
    double c, double s2, const QPointF &rawOrigin, bool flipY,
    double &outNearLy, double &outFarLy)
{
    outNearLy = 0.0;
    outFarLy  = s_v;

    if (!m_scene || s_wallGroupId.isEmpty() || s_wallPositionMode == 0)
        return 0.0;

    double y_near =  1e18;
    double y_far  = -1e18;

    // P1→P2 법선 거리 + 100mm 를 탐색 한계로 사용 (공유 그룹 반대편 벽 제외)
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
        // 방향 필터: 도어 방향(c,s2)에 평행한 세그먼트만 → 벽 면선(같은 벽 방향)
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

        // 거리 필터: |P1→P2| 법선거리 + 100mm 초과 세그먼트 제외 (공유 그룹 반대편 벽)
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
//
// 다중선 없이 일반선만 사용할 때의 위치 보정.
// s_wallDetectedSpan = |P2-P1| 거리를 벽 폭으로 사용하여 delta 계산.
// ─────────────────────────────────────────────────────────────────────────────
double PlanDoorTool::computeWallPositionDeltaFromSpan(double &outNearLy, double &outFarLy)
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
// addWallClosingLines
//
// 개구부 양 끝(lx=0, lx=H)에 벽-문틀 사이 틈을 메우는 수직 선분 추가.
// wallNearLy < 0  → 안쪽 여백 마감
// wallFarLy > V   → 바깥쪽 여백 마감
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorTool::addWallClosingLines(
    double ox, double oy, double c, double s2,
    double wallNearLy, double wallFarLy, bool flipY,
    const QString &doorUuid, QUndoStack *stack)
{
    if (!m_scene || !stack) return;

    IAppContext *ctx       = getContext();
    const QString otherLyr = resolveLayerName(getContext(), DoorLayerSettings::otherLayer());

    const QPen pen = layerPenFor(ctx, otherLyr);

    addClosingLinesStatic(m_scene, ctx,
                          ox, oy, c, s2, flipY,
                          s_h, s_v,
                          wallNearLy, wallFarLy,
                          otherLyr, doorUuid, stack);
}

// ── restoreWallOpening ────────────────────────────────────────────────────────
// 도어 삭제 시 트림된 다중선 세그먼트(좌·우 조각)를 찾아 하나로 병합 복원.
//   좌 조각 : ...──→ pt(0, ly)      (끝점이 x ≈ 0)
//   우 조각 : pt(H, ly) ──→ ...     (시작점이 x ≈ H)
// 같은 ly 에서 매칭된 쌍을 Remove×2 + Add(병합) 로 처리.
void PlanDoorTool::restoreWallOpening(
    MyGraphicsScene *scene,
    const QString   &wallGroupId,
    double ox, double oy, double c, double s2,
    double H,
    double trimYLow, double trimYHigh,
    QUndoStack *stack,
    double leftExt, double rightExt)
{
    if (!scene || wallGroupId.isEmpty() || !stack)
        return;

    const double EPS  = 5.0;
    const double yTol = 15.0;

    auto toLocal = [&](const MyVector &p) -> QPointF {
        const double dx = p.x - ox, dy = p.y - oy;
        return QPointF(dx * c + dy * s2, dy * c - dx * s2);
    };

    struct Frag {
        LineItem *item;
        double    ly;      // 도어 경계 끝점의 로컬 Y
        MyVector  farEnd;  // 도어 경계 반대쪽 끝점(세계 좌표)
        bool      used = false;
    };

    QList<Frag> leftFrags, rightFrags;
    const QList<QGraphicsItem *> allItems = scene->items();

    for (int i = 0; i < allItems.size(); ++i) {
        LineItem *li = dynamic_cast<LineItem *>(allItems.at(i));
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString() != wallGroupId) continue;
        if (li->data(ML_DATA_ROLE).toString() == "center") continue;

        const MyLine lnD = li->getLine();
        const double sdx = lnD.end.x - lnD.start.x;
        const double sdy = lnD.end.y - lnD.start.y;
        const double slen = std::sqrt(sdx * sdx + sdy * sdy);
        if (slen < 1e-9) continue;
        if (std::abs((sdx / slen) * c + (sdy / slen) * s2) < 0.5) continue;

        QPointF loc1 = toLocal(lnD.start);
        QPointF loc2 = toLocal(lnD.end);
        bool swapped = false;
        if (loc1.x() > loc2.x()) { qSwap(loc1, loc2); swapped = true; }

        const double lx1 = loc1.x(), ly1 = loc1.y();
        const double lx2 = loc2.x(), ly2 = loc2.y();
        const double midLy = (ly1 + ly2) / 2.0;
        if (midLy < trimYLow - yTol || midLy > trimYHigh + yTol) continue;

        // 공간벽 마감형태로 -X 쪽을 더 끊어낸 space 선은 끝점이 x ≈ -leftExt
        if (std::abs(lx2) < EPS || (leftExt > 0.0 && std::abs(lx2 + leftExt) < EPS)) {
            // 좌 조각: 끝점이 x ≈ 0 (또는 x ≈ -leftExt)
            Frag f;
            f.item   = li;
            f.ly     = ly2;
            f.farEnd = swapped ? lnD.end : lnD.start;
            leftFrags << f;
        } else if (std::abs(lx1 - H) < EPS ||
                   (rightExt > 0.0 && std::abs(lx1 - (H + rightExt)) < EPS)) {
            // 우 조각: 시작점이 x ≈ H (또는 x ≈ H + rightExt)
            Frag f;
            f.item   = li;
            f.ly     = ly1;
            f.farEnd = swapped ? lnD.start : lnD.end;
            rightFrags << f;
        }
    }

    // 매칭 쌍 → 병합
    for (int i = 0; i < leftFrags.size(); ++i) {
        Frag &lf = leftFrags[i];
        for (int j = 0; j < rightFrags.size(); ++j) {
            Frag &rf = rightFrags[j];
            if (rf.used) continue;
            if (std::abs(lf.ly - rf.ly) > EPS) continue;

            LineItem *merged = new LineItem(lf.farEnd, rf.farEnd);
            merged->setLayerName(lf.item->layerName());
            merged->setPen(lf.item->pen());
            merged->setData(ML_DATA_GROUP_ID, lf.item->data(ML_DATA_GROUP_ID));
            merged->setData(ML_DATA_ROLE,     lf.item->data(ML_DATA_ROLE));
            const double sp = lf.item->data(ML_DATA_SPACING).toDouble();
            if (sp > 0.0) merged->setData(ML_DATA_SPACING, sp);

            stack->push(new RemoveEntityCommand(scene, lf.item));
            stack->push(new RemoveEntityCommand(scene, rf.item));
            stack->push(new AddEntityCommand(scene, merged));
            rf.used = true;
            break;
        }
    }
}

// ── adjustLatchWallEndpoints ──────────────────────────────────────────────────
// 도어 너비가 H_old → H_new 로 바뀔 때, 래치쪽(x=H_old) 벽선 끝점을 H_new 로 이동.
// H_new > H_old : 개구부 확장 (오른쪽 조각 시작점을 더 오른쪽으로)
// H_new < H_old : 개구부 축소 (오른쪽 조각 시작점을 왼쪽으로 당겨 벽 복원)
void PlanDoorTool::adjustLatchWallEndpoints(
    MyGraphicsScene *scene,
    const QString &wallGroupId,
    double ox, double oy, double c, double s2,
    double H_old, double H_new,
    double trimYLow, double trimYHigh,
    QUndoStack *stack,
    double rightExt)
{
    if (!scene || wallGroupId.isEmpty() || qFuzzyCompare(H_old, H_new) || !stack)
        return;

    const double EPS  = 5.0;   // 끝점 판별 허용 오차 (mm)
    const double yTol = 15.0;  // Y 범위 허용 오차

    auto toLocal = [&](const MyVector &p) -> QPointF {
        const double dx = p.x - ox, dy = p.y - oy;
        return QPointF(dx * c + dy * s2, dy * c - dx * s2);
    };
    auto toWorld = [&](double lx, double ly) -> MyVector {
        return MyVector(ox + lx * c - ly * s2, oy + lx * s2 + ly * c);
    };

    const QList<QGraphicsItem *> sceneItems = scene->items();
    for (int i = 0; i < sceneItems.size(); ++i) {
        LineItem *li = dynamic_cast<LineItem *>(sceneItems.at(i));
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString() != wallGroupId) continue;
        if (li->data(ML_DATA_ROLE).toString() == "center") continue;

        const MyLine lnD = li->getLine();
        const double sdx = lnD.end.x - lnD.start.x;
        const double sdy = lnD.end.y - lnD.start.y;
        const double slen = std::sqrt(sdx * sdx + sdy * sdy);
        if (slen < 1e-9) continue;
        if (std::abs((sdx / slen) * c + (sdy / slen) * s2) < 0.5) continue; // 방향 필터

        QPointF loc1 = toLocal(lnD.start);
        QPointF loc2 = toLocal(lnD.end);

        bool swapped = false;
        if (loc1.x() > loc2.x()) { qSwap(loc1, loc2); swapped = true; }

        const double lx1 = loc1.x(), ly1 = loc1.y();
 //       const double lx2 = loc2.x(), ly2 = loc2.y();
        const double ly2 = loc2.y();

        // 래치쪽 오른쪽 조각: 시작점이 x ≈ H_old
        // (공간벽 마감형태로 더 끊어낸 space 선은 x ≈ H_old + rightExt → H_new + rightExt 로 이동)
        double newStartX;
        if (std::abs(lx1 - H_old) <= EPS)
            newStartX = H_new;
        else if (rightExt > 0.0 && std::abs(lx1 - (H_old + rightExt)) <= EPS)
            newStartX = H_new + rightExt;
        else
            continue;

        const double midLy = (ly1 + ly2) / 2.0;
        if (midLy < trimYLow - yTol || midLy > trimYHigh + yTol) continue;

        // 고정 끝점 (오른쪽 먼 끝)
        const MyVector wFar = swapped ? lnD.start : lnD.end;
        // 새 시작점: x=newStartX, y=ly1 유지
        const MyVector newStart = toWorld(newStartX, ly1);

        LineItem *newLi = new LineItem(newStart, wFar);
        newLi->setLayerName(li->layerName());
        newLi->setPen(li->pen());
        newLi->setData(ML_DATA_GROUP_ID, li->data(ML_DATA_GROUP_ID));
        newLi->setData(ML_DATA_ROLE,     li->data(ML_DATA_ROLE));
        const double sp = li->data(ML_DATA_SPACING).toDouble();
        if (sp > 0.0) newLi->setData(ML_DATA_SPACING, sp);

        stack->push(new RemoveEntityCommand(scene, li));
        stack->push(new AddEntityCommand(scene, newLi));
    }
}

// ── addClosingLinesStatic ──────────────────────────────────────────────────────
void PlanDoorTool::addClosingLinesStatic(
    MyGraphicsScene *scene, IAppContext *ctx,
    double ox, double oy, double c, double s2,
    bool flipY, double H, double V,
    double wallNearLy, double wallFarLy,
    const QString &otherLayerIn,
    const QString &doorUuid,
    QUndoStack *stack)
{
    if (!scene || !stack) return;

    const QString otherLayer = AbstractTool::resolveLayerName(ctx, otherLayerIn);

    const double EPS = 1e-4;

    auto toWorld = [&](double lx, double ly) -> MyVector {
        if (!flipY)
            return MyVector(ox + lx * c - ly * s2, oy + lx * s2 + ly * c);
        else
            return MyVector(ox + lx * c + ly * s2, oy + lx * s2 - ly * c);
    };

    const QPen pen = AbstractTool::layerPenFor(ctx, otherLayer);

    auto addSeg = [&](double lx, double ly1, double ly2) {
        if (qAbs(ly2 - ly1) < EPS) return;
        LineItem *li = new LineItem(toWorld(lx, ly1), toWorld(lx, ly2));
        li->setLayerName(otherLayer);
        li->setPen(pen);
        if (!doorUuid.isEmpty()) {
            li->setData(PD_DATA_GROUP_ID,     doorUuid);
            li->setData(PD_DATA_CLOSING_LINE, doorUuid);
        }
        stack->push(new AddEntityCommand(scene, li));
    };

    if (qAbs(wallNearLy) > EPS) {
        addSeg(0.0, 0.0, wallNearLy);
        addSeg(H,   0.0, wallNearLy);
    }
    if (qAbs(wallFarLy - V) > EPS) {
        addSeg(0.0, wallFarLy, V);
        addSeg(H,   wallFarLy, V);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PlanDoorCommand
// ─────────────────────────────────────────────────────────────────────────────
PlanDoorCommand::PlanDoorCommand(IAppContext *ctx, int doorStyle, QObject *parent)
    : CommandBase(parent)
    , m_ctx(ctx)
    , m_style(doorStyle)
{}

void PlanDoorCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new PlanDoorTool(nullptr, m_style));
}
