#include "plandoorsimpletool.h"
#include "plandoorsimplesetupdialog.h"
#include "planarchdata.h"
#include "spacefinish.h"
#include "scenesymbolregistry.h"

#include "arcitem.h"
#include "caditem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "doorlayersettings.h"
#include "lineitem.h"
#include "myarc.h"
#include "multilinedata.h"
#include "mygraphicsscene.h"
#include "myvector.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QApplication>
#include <QUndoStack>
#include <QUuid>
#include <QWidget>
#include <QtMath>
#include <cmath>

double  PlanDoorSimpleTool::s_h = 900.0;
QString PlanDoorSimpleTool::s_wallGroupId;
static int s_simpleDoorSeq = 0;

// ── 도구 기본값 저장/불러오기 ────────────────────────────────────────────────
static void savePlanDoorSimpleSettings(MyGraphicsScene *scene)
{
    ToolDefaults d("PlanDoorSimpleTool", scene);
    d.put("H",                PlanDoorSimpleTool::globalH());
}

static void loadPlanDoorSimpleSettings(MyGraphicsScene *scene)
{
    DoorLayerSettings::load(scene);
    SpaceFinish::load(scene);
    ToolDefaults d("PlanDoorSimpleTool", scene);
    PlanDoorSimpleTool::setGlobalH(d.getDouble("H", 900.0));
}

double  PlanDoorSimpleTool::globalH()                         { return s_h; }
void    PlanDoorSimpleTool::setGlobalH(double v)              { if (v > 0.0) s_h = v; }

CommandBase *PlanDoorSimpleTool::createCommand(IAppContext *c)
{
    return new PlanDoorSimpleCommand(c, SimpleSingle);
}
CommandBase *PlanDoorSimpleTool::createDoubleCommand(IAppContext *c)
{
    return new PlanDoorSimpleCommand(c, SimpleDouble);
}
CommandBase *PlanDoorSimpleTool::createBiSwingCommand(IAppContext *c)
{
    return new PlanDoorSimpleCommand(c, SimpleBiSwing);
}
CommandBase *PlanDoorSimpleTool::createDoubleBiSwingCommand(IAppContext *c)
{
    return new PlanDoorSimpleCommand(c, SimpleDoubleBiSwing);
}

QString PlanDoorSimpleTool::symbolTypeForKind(SimpleDoorKind kind)
{
    switch (kind) {
    case SimpleDouble:         return QStringLiteral("PlanDoorSimpleDouble");
    case SimpleBiSwing:        return QStringLiteral("PlanDoorSimpleBiSwing");
    case SimpleDoubleBiSwing:  return QStringLiteral("PlanDoorSimpleDoubleBiSwing");
    default:                   return QStringLiteral("PlanDoorSimple");
    }
}

bool PlanDoorSimpleTool::isSimpleSymbolType(const QString &type)
{
    return type == QLatin1String("PlanDoorSimple") ||
           type == QLatin1String("PlanDoorSimpleDouble") ||
           type == QLatin1String("PlanDoorSimpleBiSwing") ||
           type == QLatin1String("PlanDoorSimpleDoubleBiSwing");
}

SimpleDoorKind PlanDoorSimpleTool::kindForSymbolType(const QString &type)
{
    if (type == QLatin1String("PlanDoorSimpleDouble"))        return SimpleDouble;
    if (type == QLatin1String("PlanDoorSimpleBiSwing"))       return SimpleBiSwing;
    if (type == QLatin1String("PlanDoorSimpleDoubleBiSwing")) return SimpleDoubleBiSwing;
    return SimpleSingle;
}

static QString simpleKindLabel(SimpleDoorKind kind)
{
    switch (kind) {
    case SimpleDouble:         return QObject::tr("단순쌍여닫이");
    case SimpleBiSwing:        return QObject::tr("단순양방여닫이");
    case SimpleDoubleBiSwing:  return QObject::tr("단순양방쌍여닫이");
    default:                   return QObject::tr("단순문");
    }
}

// ── 좌표 변환 헬퍼 (로컬 → 월드, 람다 대체) ─────────────────────────────────
struct SimpleDoorPt {
    double ox, oy, c, s2;
    bool   flipY;
    MyVector operator()(double dx, double dy) const {
        if (!flipY)
            return MyVector(ox + dx*c - dy*s2, oy + dx*s2 + dy*c);
        return MyVector(ox + dx*c + dy*s2, oy + dx*s2 - dy*c);
    }
};

// 월드 → 로컬(도어 좌표계, flipY 무관: 트림 계산은 항상 !flipY 기준 축 사용)
static QPointF simpleToLocal(double ox, double oy, double c, double s2, const MyVector &p)
{
    const double dx = p.x - ox, dy = p.y - oy;
    return QPointF(dx * c + dy * s2, dy * c - dx * s2);
}

static MyVector simpleToWorld(double ox, double oy, double c, double s2, double lx, double ly)
{
    return MyVector(ox + lx * c - ly * s2, oy + lx * s2 + ly * c);
}

// 로컬 선분 (lx1,ly1)-(lx2,ly2) 위 로컬 X = cutX 인 점의 월드 좌표
static MyVector simpleInterp(double ox, double oy, double c, double s2,
                             double lx1, double ly1, double lx2, double ly2, double cutX)
{
    const double span = lx2 - lx1;
    const double t    = span > 1e-9 ? (cutX - lx1) / span : 0.0;
    return simpleToWorld(ox, oy, c, s2, cutX, ly1 + t * (ly2 - ly1));
}

// 문짝선 / 스윙호 생성 헬퍼
static void addSimpleLeaf(QList<QGraphicsItem*> &out, const MyVector &a, const MyVector &b,
                          const QString &layer, const QPen &pen)
{
    LineItem *li = new LineItem(a, b);
    li->setLayerName(layer);
    li->setPen(pen);
    out << li;
}

static void addSimpleArc(QList<QGraphicsItem*> &out, const MyVector &center, double r,
                         double startDeg, double spanDeg, const QString &layer, const QPen &pen)
{
    ArcItem *arc = new ArcItem(MyArc(center, r, startDeg, spanDeg));
    arc->setLayerName(layer);
    arc->setPen(pen);
    out << arc;
}

// ── 생성자 ───────────────────────────────────────────────────────────────────
PlanDoorSimpleTool::PlanDoorSimpleTool(SimpleDoorKind kind, QObject *parent)
    : AbstractTool(parent)
    , m_kind(kind)
    , m_state(WaitP1)
    , m_currentMousePos(0.0, 0.0)
{}

// applySnapForState — WaitP1/WaitP3: Nearest ON, WaitP2: Perpendicular ON
void PlanDoorSimpleTool::applySnapForState(State s)
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->snapManager())
        return;

    SnapManager *sm         = ctx->snapManager();
    const bool  wantNearest = (s == WaitP1 || s == WaitP3);
    const bool  wantPerp    = (s == WaitP2);

    if (sm->isSnapNearest()       != wantNearest) sm->toggleSnapNearest();
    if (sm->isSnapPerpendicular() != wantPerp)    sm->toggleSnapPerpendicular();
}

void PlanDoorSimpleTool::restoreSavedSnap()
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

void PlanDoorSimpleTool::activate(MyGraphicsScene *scene)
{
    loadPlanDoorSimpleSettings(scene);

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
        ctx->commandDock()->logMessage("PLAN-DOOR-SIMPLE", CommandDock::Command);
    }

    showP1Prompt();
}

void PlanDoorSimpleTool::deactivate()
{
    restoreSavedSnap();

    m_state = WaitP1;
    if (m_scene)
        m_scene->clearOverlay();
    AbstractTool::deactivate();
}

// ── 프롬프트 ─────────────────────────────────────────────────────────────────
void PlanDoorSimpleTool::showP1Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(tr(" P1: 도어가 들어갈 선  지정 (nea) "), {"Setup"});
}

void PlanDoorSimpleTool::showP1DistancePrompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(tr(" 거리를 입력하세요 : "), {});
}

void PlanDoorSimpleTool::showP2Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(
        tr(" P2: 반대편 선  지정 (per) — 벽 두께 자동 측정 "), {});
}

void PlanDoorSimpleTool::showP3Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(
        tr(" P3: 방향점 지정 (nea) — 문 진행 방향 "), {});
}

// captureP1WallDirs: collect the (non-parallel) directions of every multiline
// segment passing through the anchor. At a corner both directions become candidates.
void PlanDoorSimpleTool::captureP1WallDirs(const QPointF &pos)
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
QPointF PlanDoorSimpleTool::resolveP1WallDir() const
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

// 앵커에서 벽 방향으로 dist 이동한 최종 P1 좌표
QPointF PlanDoorSimpleTool::computeP1FromAnchor(double dist) const
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

// P1 위치의 다중선(벽) 그룹 감지 → s_wallGroupId
void PlanDoorSimpleTool::detectWallAtP1()
{
    LineItem *ml  = findNearestMultilineAt(m_p1);
    s_wallGroupId = ml ? ml->data(ML_DATA_GROUP_ID).toString() : QString();
}

// pos 에 가장 가까운 선(LineItem)의 레이어명 (허용거리 15mm, 없으면 빈 문자열)
QString PlanDoorSimpleTool::layerNameAt(const QPointF &pos) const
{
    if (!m_scene)
        return QString();

    const double kTol = 15.0;
    double   bestDist = kTol;
    QString  best;

    const QList<QGraphicsItem *> allItems = m_scene->items();
    for (int i = 0; i < allItems.size(); ++i) {
        LineItem *li = dynamic_cast<LineItem *>(allItems.at(i));
        if (!li) continue;

        const MyLine  ln = li->getLine();
        const QPointF a(ln.start.x, ln.start.y);
        const QPointF b(ln.end.x,   ln.end.y);
        const QPointF seg = b - a;
        const double  len2 = seg.x() * seg.x() + seg.y() * seg.y();
        double t = 0.0;
        if (len2 > 1e-12) {
            t = ((pos.x() - a.x()) * seg.x() + (pos.y() - a.y()) * seg.y()) / len2;
            t = qBound(0.0, t, 1.0);
        }
        const QPointF q = a + seg * t;
        const double  d = std::hypot(pos.x() - q.x(), pos.y() - q.y());
        if (d < bestDist) {
            bestDist = d;
            best     = li->layerName();
        }
    }
    return best;
}

// ── mouseMove ────────────────────────────────────────────────────────────────
void PlanDoorSimpleTool::mouseMove(QPointF pos)
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
void PlanDoorSimpleTool::mousePress(QPointF pos, Qt::MouseButton button)
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
            // Shift+클릭: 앵커 저장 후 거리 입력 대기
            m_p1Anchor = snapped;
            captureP1WallDirs(snapped);
            m_state = WaitP1Distance;
            m_scene->clearOverlay();
            showP1DistancePrompt();
            return;
        }

        m_p1 = snapped;
        detectWallAtP1();

        m_state = WaitP2;
        applySnapForState(WaitP2);
        QPainterPath path;
        path.moveTo(m_p1);
        path.lineTo(m_currentMousePos);
        updateGhostPreview(path, currentLayerName());
        showP2Prompt();

    } else if (m_state == WaitP2) {
        m_p2 = snapped;
        m_state = WaitP3;
        applySnapForState(WaitP3);
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
        m_p3 = snapped;
        placeDoor(m_p1, angleDeg, V, flipY);
        m_state = WaitP1;
        applySnapForState(WaitP1);
        showP1Prompt();
    }
}

void PlanDoorSimpleTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/) {}

// ── processCommand ────────────────────────────────────────────────────────────
bool PlanDoorSimpleTool::processCommand(const QString &command)
{
    IAppContext *ctx = getContext();
    const QString cmd = command.trimmed();

    // WaitP1Distance: 거리 입력 → 최종 P1 확정
    if (m_state == WaitP1Distance) {
        bool ok;
        double dist = cmd.toDouble(&ok);
        if (ok && dist > 0.0) {
            m_p1 = computeP1FromAnchor(dist);
            detectWallAtP1();
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

    // WaitP1: Setup 또는 좌표 입력
    if (m_state == WaitP1) {
        const QString lower = cmd.toLower();
        if (lower == "s" || lower == "setup") {
            QWidget *parentWin =
                ctx ? dynamic_cast<QWidget *>(ctx->commandDock()) : nullptr;

            PlanDoorSimpleSetupDialog dlg(s_h, parentWin);
            dlg.setSpaceFinishType(SpaceFinish::type());
            if (dlg.exec() == QDialog::Accepted) {
                SpaceFinish::setType(dlg.spaceFinishType());
                SpaceFinish::save(m_scene);
                setGlobalH(dlg.h());
                savePlanDoorSimpleSettings(m_scene);
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
                detectWallAtP1();
                m_state = WaitP2;
                applySnapForState(WaitP2);
                showP2Prompt();
                return true;
            }
        }
    }

    // WaitP2: 좌표 입력
    if (m_state == WaitP2) {
        if (ctx && ctx->parser()) {
            QPointF resultPos;
            const bool isOrtho = ctx->snapManager() && ctx->snapManager()->isOrtho();
            if (ctx->parser()->parseCoordinate(
                    cmd, m_p1, m_currentMousePos, isOrtho, resultPos)) {
                m_p2 = resultPos;
                m_state = WaitP3;
                applySnapForState(WaitP3);
                showP3Prompt();
                return true;
            }
        }
    }

    // WaitP3: 좌표 입력 → 배치
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
                    m_p3 = resultPos;
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

void PlanDoorSimpleTool::onFinish()
{
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

void PlanDoorSimpleTool::onCancel()
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
// computeFrom3Pts — 벽 두께는 항상 P1→P2 측정값(V = |dot(P2-P1, n)|)을 그대로 사용
// ─────────────────────────────────────────────────────────────────────────────
bool PlanDoorSimpleTool::computeFrom3Pts(
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

    if (std::abs(vSigned) < 10.0)
        return false;

    outAngleDeg = qRadiansToDegrees(std::atan2(uy, ux));
    outV        = std::abs(vSigned);
    outFlipY    = (vSigned < 0.0);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// updateGhostDoor — 문짝선 + 스윙 호 미리보기
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorSimpleTool::updateGhostDoor(
    const QPointF &p1, double angleDeg, double V, bool flipY)
{
    if (!m_scene)
        return;

    // 실제 배치와 같은 형상을 생성해 고스트 경로로 변환 (형상 로직 중복 방지)
    SimpleDoorBuildParams bp;
    bp.ox = p1.x(); bp.oy = p1.y(); bp.angleDeg = angleDeg;
    bp.H = s_h; bp.V = V; bp.flipY = flipY;
    bp.layer    = DoorLayerSettings::frameLayer();   // 문짝선 = 문틀 레이어
    bp.arcLayer = DoorLayerSettings::otherLayer();   // 스윙 호 = 나머지 레이어
    bp.kind = m_kind;

    QList<QGraphicsItem *> items = buildGeometry(bp, getContext());
    QPainterPath path;
    for (int i = 0; i < items.size(); ++i) {
        CadItem *ci = dynamic_cast<CadItem *>(items.at(i));
        if (ci)
            path.addPath(ci->geometryPath());   // 아직 씬에 없으므로 월드 좌표 == 아이템 좌표
        delete items.at(i);
    }
    updateGhostPreview(path, currentLayerName());
}

// ─────────────────────────────────────────────────────────────────────────────
// buildGeometry — 문짝선(경첩 local X=0, Y=V → V+H) + 스윙 호(반경 H, 90°)
// ─────────────────────────────────────────────────────────────────────────────
QList<QGraphicsItem*> PlanDoorSimpleTool::buildGeometry(const SimpleDoorBuildParams &pIn, IAppContext *ctx)
{
    SimpleDoorBuildParams p = pIn;
    p.layer    = AbstractTool::resolveLayerName(ctx, p.layer);
    p.arcLayer = AbstractTool::resolveLayerName(ctx, p.arcLayer);

    const double H  = p.H;
    const double V  = p.V;
    const double c  = std::cos(qDegreesToRadians(p.angleDeg));
    const double s2 = std::sin(qDegreesToRadians(p.angleDeg));
    const SimpleDoorPt pt { p.ox, p.oy, c, s2, p.flipY };

    const QPen pen    = AbstractTool::layerPenFor(ctx, p.layer);
    const QPen arcPen = AbstractTool::layerPenFor(ctx, p.arcLayer);

    QList<QGraphicsItem*> result;

    const double H1 = H / 2.0;   // 쌍여닫이류: 문1 = 문2 = H/2
    const double H2 = H - H1;

    switch (p.kind) {
    case SimpleDouble:
        // 문1: 경첩 X=0, 문짝 (0,V)->(0,V+H1), 호 시작 +X, 90도
        // 문2: 경첩 X=H, 문짝 (H,V)->(H,V+H2), 호 시작 -X(+180도), 반대 방향 90도
        addSimpleLeaf(result, pt(0, V), pt(0, V + H1), p.layer, pen);
        addSimpleArc (result, pt(0, V), H1, p.angleDeg,         p.flipY ? -90.0 : 90.0, p.arcLayer, arcPen);
        addSimpleLeaf(result, pt(H, V), pt(H, V + H2), p.layer, pen);
        addSimpleArc (result, pt(H, V), H2, p.angleDeg + 180.0, p.flipY ? 90.0 : -90.0, p.arcLayer, arcPen);
        break;
    case SimpleBiSwing: {
        // 경첩: 벽 두께 정중앙 (0, V/2). 문짝은 양쪽으로 H, 호는 +X 경유 180도 반원
        const double hy = V / 2.0;
        addSimpleLeaf(result, pt(0, hy - H), pt(0, hy + H), p.layer, pen);
        addSimpleArc (result, pt(0, hy), H, p.angleDeg - 90.0, 180.0, p.arcLayer, arcPen);
        break;
    }
    case SimpleDoubleBiSwing: {
        const double hy = V / 2.0;
        addSimpleLeaf(result, pt(0, hy - H1), pt(0, hy + H1), p.layer, pen);
        addSimpleArc (result, pt(0, hy), H1, p.angleDeg - 90.0, 180.0, p.arcLayer, arcPen);
        addSimpleLeaf(result, pt(H, hy - H2), pt(H, hy + H2), p.layer, pen);
        addSimpleArc (result, pt(H, hy), H2, p.angleDeg + 90.0, 180.0, p.arcLayer, arcPen);
        break;
    }
    default:
        addSimpleLeaf(result, pt(0, V), pt(0, V + H), p.layer, pen);
        addSimpleArc (result, pt(0, V), H, p.angleDeg, p.flipY ? -90.0 : 90.0, p.arcLayer, arcPen);
        break;
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// placeDoor
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorSimpleTool::placeDoor(
    const QPointF &p1, double angleDeg, double V, bool flipY)
{
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->undoStack())
        return;

    const double H  = s_h;
    const double ox = p1.x(), oy = p1.y();
    const double c  = std::cos(qDegreesToRadians(angleDeg));
    const double s2 = std::sin(qDegreesToRadians(angleDeg));

    const QString doorLyr  = resolveLayerName(ctx, DoorLayerSettings::frameLayer());   // 문짝선
    const QString otherLyr = resolveLayerName(ctx, DoorLayerSettings::otherLayer());   // 스윙 호

    SimpleDoorBuildParams buildP;
    buildP.ox = ox; buildP.oy = oy; buildP.angleDeg = angleDeg;
    buildP.H = H; buildP.V = V; buildP.flipY = flipY;
    buildP.layer    = doorLyr;
    buildP.arcLayer = otherLyr;
    buildP.kind  = m_kind;

    QList<QGraphicsItem *> newItems = buildGeometry(buildP, ctx);

    // 트림 후보를 매크로 시작 전에 수집 (문 선이 후보에 포함되지 않도록)
    QList<LineItem *> trimCandidates;
    {
        const QList<QGraphicsItem *> allItems = m_scene->items();
        for (int i = 0; i < allItems.size(); ++i) {
            LineItem *li = dynamic_cast<LineItem *>(allItems.at(i));
            if (!li) continue;
            if (!li->data(ML_DATA_GROUP_ID).toString().isEmpty()) continue; // ML: 별도 처리
            if (!DoorLayerSettings::trimExcludeLayer().isEmpty() && li->layerName() == DoorLayerSettings::trimExcludeLayer()) continue;
            trimCandidates << li;
        }
    }

    // 마감선 레이어 = P3 위치에 있는 선의 레이어 (없으면 문 레이어). 트림 전에 조회해야 한다.
    QString closingLyr = layerNameAt(m_p3);
    if (closingLyr.isEmpty())
        closingLyr = doorLyr;

    const QString doorUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString doorName = QStringLiteral("%1-%2").arg(simpleKindLabel(m_kind)).arg(++s_simpleDoorSeq);

    for (int i = 0; i < newItems.size(); ++i)
        newItems.at(i)->setData(PD_DATA_GROUP_ID, doorUuid);

    // 트림 Y 범위 = P1·P2 의 로컬 Y (벽 두께 전체)
    const double p1ly = (p1.y() - oy) * c - (p1.x() - ox) * s2;
    const double p2ly = (m_p2.y() - oy) * c - (m_p2.x() - ox) * s2;
    const double trimYLow  = std::min(p1ly, p2ly);
    const double trimYHigh = std::max(p1ly, p2ly);

    // 공간벽 마감형태: 양쪽(P1-P2 가상선 / 너비 H 만큼 떨어진 가상선)의 space 선 수집 → 끊기/연결선 계획
    SpaceFinish::Plan finishL, finishR;
    SpaceFinish::planBoth(m_scene, s_wallGroupId, p1, m_p2, ox, oy, c, s2, H, finishL, finishR);

    SymbolInfo doorInfo;
    doorInfo.type                = symbolTypeForKind(m_kind);
    doorInfo.name                = doorName;
    doorInfo.props["H"]          = H;
    doorInfo.props["V"]          = V;
    doorInfo.props["ox"]         = ox;
    doorInfo.props["oy"]         = oy;
    doorInfo.props["angleDeg"]   = angleDeg;
    doorInfo.props["flipY"]      = flipY ? 1 : 0;
    doorInfo.props["FrameLayer"] = doorLyr;   // 문짝선 레이어 (속성창 재빌드 공용 키)
    doorInfo.props["OtherLayer"] = otherLyr;  // 스윙 호 레이어
    doorInfo.props["wallGroupId"] = s_wallGroupId;
    doorInfo.props["trimYLow"]    = trimYLow;
    doorInfo.props["trimYHigh"]   = trimYHigh;
    doorInfo.props["ClosingLayer"] = closingLyr;   // 마감선 레이어 (P3 선의 레이어)
    doorInfo.props["wallNearLy"]  = 0.0;
    doorInfo.props["wallFarLy"]   = V;
    SpaceFinish::storeProps(doorInfo.props, finishL, finishR);

    ctx->undoStack()->beginMacro(simpleKindLabel(m_kind));
    ctx->undoStack()->push(new RegisterSymbolCommand(m_scene->symbolRegistry(), doorUuid, doorInfo));
    for (int i = 0; i < newItems.size(); ++i)
        ctx->undoStack()->push(new AddEntityCommand(m_scene, newItems.at(i)));

    if (!s_wallGroupId.isEmpty()) {
        trimWallInDoorOpening(ox, oy, c, s2, trimYLow, trimYHigh, ctx->undoStack(),
                              finishL.extItems, finishR.extItems, SpaceFinish::extLength());
        SpaceFinish::addConnectors(m_scene, finishL, finishR, doorUuid, ctx->undoStack());
        // 트림된 다중선 양끝을 위·아래 벽선끼리 잇는 마감선 (P3 레이어)
        addClosingLinesStatic(m_scene, ctx, ox, oy, c, s2, flipY, H, V,
                              closingLyr, doorUuid, ctx->undoStack());
    }
    trimGeneralLines(trimCandidates, ox, oy, c, s2, trimYLow, trimYHigh, ctx->undoStack());

    ctx->undoStack()->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            (simpleKindLabel(m_kind) + tr(" 배치 — P1(%1,%2)  W=%3  벽두께=%4  각도=%5°  flipY=%6"))
                .arg(ox,0,'f',1).arg(oy,0,'f',1)
                .arg(H,0,'f',1).arg(V,0,'f',1)
                .arg(angleDeg,0,'f',1)
                .arg(flipY ? "Y" : "N"),
            CommandDock::Info);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// addClosingLinesStatic — 개구부 양끝(X=0, X=H)에서 벽 두께 방향(Y=0→V) 마감선 2개
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorSimpleTool::addClosingLinesStatic(
    MyGraphicsScene *scene, IAppContext *ctx,
    double ox, double oy, double c, double s2,
    bool flipY, double H, double V,
    const QString &layer, const QString &doorUuid,
    QUndoStack *stack)
{
    if (!scene || !stack)
        return;

    const QString lyr = AbstractTool::resolveLayerName(ctx, layer);
    const QPen    pen = AbstractTool::layerPenFor(ctx, lyr);
    const SimpleDoorPt pt { ox, oy, c, s2, flipY };

    const double xs[2] = { 0.0, H };
    for (int i = 0; i < 2; ++i) {
        LineItem *li = new LineItem(pt(xs[i], 0.0), pt(xs[i], V));
        li->setLayerName(lyr);
        li->setPen(pen);
        li->setData(PD_DATA_GROUP_ID,     doorUuid);
        li->setData(PD_DATA_CLOSING_LINE, doorUuid);
        stack->push(new AddEntityCommand(scene, li));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// findNearestMultilineAt — pos 가 실제 다중선 선분 위(수직 15mm, 연장 50mm)일 때만 감지
// ─────────────────────────────────────────────────────────────────────────────
LineItem *PlanDoorSimpleTool::findNearestMultilineAt(const QPointF &pos)
{
    if (!m_scene)
        return nullptr;

    const double kPerpTol = 15.0;
    const double kExtend  = 50.0;

    LineItem *best     = nullptr;
    double    bestDist = kPerpTol;

    const QList<QGraphicsItem *> allItems = m_scene->items();
    for (int i = 0; i < allItems.size(); ++i) {
        LineItem *li = dynamic_cast<LineItem *>(allItems.at(i));
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
// trimWallInDoorOpening — 다중선 벽 세그먼트를 로컬 X=[0,H] 범위로 트림
//   (트림 제외 레이어에 속한 선은 건드리지 않는다)
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorSimpleTool::trimWallInDoorOpening(
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
        LineItem *li = dynamic_cast<LineItem *>(sceneItems[i]);
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString() != s_wallGroupId) continue;
        if (li->data(ML_DATA_ROLE).toString() == "center") continue;
        if (!DoorLayerSettings::trimExcludeLayer().isEmpty() && li->layerName() == DoorLayerSettings::trimExcludeLayer()) continue;
        // 도어 방향(c,s2)에 평행한 세그먼트만 수집 (공유 그룹의 수직 세그먼트 제외)
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
        QPointF      loc1 = simpleToLocal(ox, oy, c, s2, ln.start);
        QPointF      loc2 = simpleToLocal(ox, oy, c, s2, ln.end);

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

        const MyVector wSmall = swapped ? ln.end   : ln.start;
        const MyVector wLarge = swapped ? ln.start : ln.end;

        if (lx1 >= cutL - EPS && lx2 <= cutR + EPS) {
            stack->push(new RemoveEntityCommand(m_scene, li));
        }
        else if (lx1 < cutL && lx2 > cutR) {
            LineItem *lp = new LineItem(wSmall, simpleInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL));
            LineItem *rp = new LineItem(simpleInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR), wLarge);
            LineItem *pieces[2] = { lp, rp };
            for (int k = 0; k < 2; ++k) {
                LineItem *nli = pieces[k];
                nli->setLayerName(li->layerName());
                nli->setPen(li->pen());
                nli->setData(ML_DATA_GROUP_ID, li->data(ML_DATA_GROUP_ID));
                nli->setData(ML_DATA_ROLE,     li->data(ML_DATA_ROLE));
                const double sp = li->data(ML_DATA_SPACING).toDouble();
                if (sp > 0.0) nli->setData(ML_DATA_SPACING, sp);
            }
            stack->push(new RemoveEntityCommand(m_scene, li));
            stack->push(new AddEntityCommand(m_scene, lp));
            stack->push(new AddEntityCommand(m_scene, rp));
        }
        else if (lx1 < cutL) {
            const bool     isP1 = swapped;
            const MyVector mv   = simpleInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        }
        else {
            const bool     isP1 = !swapped;
            const MyVector mv   = simpleInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// trimGeneralLines — 사전 수집된 일반선 후보를 로컬 X=[0,H] 범위로 트림
//   (ML 그룹 선·제외 레이어 선은 호출 전 필터링된 상태)
// ─────────────────────────────────────────────────────────────────────────────
void PlanDoorSimpleTool::trimGeneralLines(
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

    for (int idx = 0; idx < candidates.size(); ++idx) {
        LineItem *li = candidates.at(idx);
        if (!li->scene())
            continue;   // 이미 씬에서 제거된 경우

        const MyLine ln   = li->getLine();
        QPointF      loc1 = simpleToLocal(ox, oy, c, s2, ln.start);
        QPointF      loc2 = simpleToLocal(ox, oy, c, s2, ln.end);

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
            LineItem *lp = new LineItem(wSmall, simpleInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL));
            LineItem *rp = new LineItem(simpleInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR), wLarge);
            lp->setLayerName(li->layerName()); lp->setPen(li->pen());
            rp->setLayerName(li->layerName()); rp->setPen(li->pen());
            stack->push(new RemoveEntityCommand(m_scene, li));
            stack->push(new AddEntityCommand(m_scene, lp));
            stack->push(new AddEntityCommand(m_scene, rp));
        } else if (lx1 < cutL) {
            const bool     isP1 = swapped;
            const MyVector mv   = simpleInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutL);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        } else {
            const bool     isP1 = !swapped;
            const MyVector mv   = simpleInterp(ox, oy, c, s2, lx1, ly1, lx2, ly2, cutR);
            stack->push(new ExtendEntityCommand(
                li,
                QPointF(isP1 ? ln.start.x : ln.end.x,
                        isP1 ? ln.start.y : ln.end.y),
                QPointF(mv.x, mv.y), isP1));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PlanDoorSimpleCommand
// ─────────────────────────────────────────────────────────────────────────────
PlanDoorSimpleCommand::PlanDoorSimpleCommand(IAppContext *ctx, SimpleDoorKind kind, QObject *parent)
    : CommandBase(parent)
    , m_ctx(ctx)
    , m_kind(kind)
{}

void PlanDoorSimpleCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new PlanDoorSimpleTool(m_kind));
}
