#include "elevatortool.h"
#include "stairplandialog.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QPainterPath>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

const QString ElevatorTool::kWallLayer = QStringLiteral("W1");
const QString ElevatorTool::kElevLayer = QStringLiteral("ELEV");

// ─────────────────────────────────────────────────────────────────────────────
// 형상 상수 — data/elev.cdb (카폭 1790 / 카깊이 1647 / 문크기 1100 / 승강로 2440x2600) 에서 추출
//   좌표계 : u = 승강로 가로 중심 기준 X, v = P1 쪽 벽 안쪽선(문 벽)에서 안쪽(뒷벽 방향)으로의 거리
// ─────────────────────────────────────────────────────────────────────────────
namespace {

const double kCarFrontGap   = 360.0;   // 문 벽 안쪽선 ~ 카 앞면 (승강로가 넉넉할 때)
const double kMinFrontGap   = 260.0;   // 최소 앞 여유 = 승강장문 60 + 카문 두께 150 + 여유 50
const double kWallJambExtra = 138.0;   // 문 개구부 반폭 = 문크기/2 + 138 (벽 개구부)
const double kWallStubLen   = 330.0;   // 개구부 옆 벽 돌출(승강로 밖) 길이
const double kFrameMargin   = 50.0;    // 카 프레임: 카 좌우/뒤 여유
const double kFrameFront    = 200.0;   // 카 프레임: 카 앞면 아래로 내려오는 길이
const double kCarDoorMid    = 100.0;   // 카 문 중간선 (카 앞면에서 밖으로)
const double kCarDoorThick  = 150.0;   // 카 문 두께 (카 앞면에서 밖으로)
const double kSillV         = 110.0;   // 문턱 가로선 위치
const double kHallDoorV1    = 10.0;    // 승강장 문 바깥선
const double kHallDoorV2    = 60.0;    // 승강장 문 안쪽선

// 승강장 문틀 (문 가장자리 기준 오프셋, 좌우 대칭)
const double kJambA1  = 18.0;       // 벽선 위 짧은 가로선 시작
const double kJambA2  = 120.0;      // 〃 끝 (= 안쪽 세로선 위치)
const double kJambIn  = 80.0;       // 안쪽 세로선 길이 (벽선에서 안쪽)
const double kJambArcCx1 = 17.973190755691;  // 상단 호 중심 X 오프셋
const double kJambArcCv1 = -15.0;            // 상단 호 중심 v
const double kJambDiagX1 = 3.215142104187;   // 사선 시작 X 오프셋
const double kJambDiagV1 = -17.68328157301;  // 사선 시작 v
const double kJambDiagX2 = 57.760596649636;  // 사선 끝 X 오프셋
const double kJambDiagV2 = -317.68328157301; // 사선 끝 v
const double kJambArc2Cx = 72.518645301136;  // 하단 호 중심 X 오프셋
const double kJambArc2Cv = -315.0;           // 하단 호 중심 v
const double kJambArcR   = 15.0;             // 호 반지름
const double kJambBotV   = -330.0;           // 하단 가로선 v
const double kJambBotX1  = 72.5;             // 하단 가로선 시작 X 오프셋
const double kJambBotX2  = 125.0;            // 하단 가로선 끝 X 오프셋
const double kJambBotUp  = 50.0;             // 하단 끝 세로선 길이

// 카운터웨이트 (뒷벽 기준)
const double kCwOuterV  = 163.0;    // 뒷벽 ~ 카운터웨이트 뒷면
const double kCwDepth   = 200.0;    // 카운터웨이트 깊이
const double kCwWidthRatio = 2.0 / 3.0;   // 균형추 폭 = 카폭 x 2/3
const double kRailInOff  = 100.0;   // 균형추 옆면 ~ 가이드레일 브래킷 안쪽 (elev.cdb 450 → 550)
const double kRailOutOff = 150.0;   // 균형추 옆면 ~ 가이드레일 브래킷 바깥 (450 → 600)
const double kCwCenterOff = 185.0;  // 균형추 옆면 ~ 중심선 끝 (450 → 635)

// 치수선 위치 (승강로 바깥쪽 오프셋)
const double kDimOffsetBack  = 533.4;   // 카폭 치수 : 뒷벽에서
const double kDimOffsetSide  = 531.8;   // 카깊이 치수 : 오른쪽 벽에서
const double kDimOffsetFront = 607.9;   // 문크기 치수 : 문 벽에서

// 승강로 깊이 방향 배치 : 문 벽 → [앞 여유] → 카 → [프레임 50 + 균형추 공간] → 뒷벽
//   뒤쪽 공간(카 뒷면 ~ 뒷벽)에는 균형추가 반드시 들어간다.
//   카 프레임(뒤 50) ~ 균형추 틈 = 균형추 ~ 뒷벽 틈 (같은 틈으로 균형추를 가운데 배치)
//     기본        : 프레임 50 + 틈 163 + 균형추 200 + 틈 163 = 576
//     8인승 이하  : 균형추 크기 2/3, 틈 1/3 → 50 + 54.3 + 133.3 + 54.3 = 292
const double kCompactCwScale  = 2.0 / 3.0;   // 8인승 이하 균형추 크기 비율
const double kCompactWallGap  = 1.0 / 3.0;   // 8인승 이하 벽 ~ 균형추 틈 비율

double cwScaleFor(bool compact)    { return compact ? kCompactCwScale : 1.0; }
double cwDepthFor(bool compact)    { return kCwDepth * cwScaleFor(compact); }
double cwWallGapFor(bool compact)  { return compact ? kCwOuterV * kCompactWallGap : kCwOuterV; }
double backIdealFor(bool compact)  { return kFrameMargin + cwDepthFor(compact) + 2.0 * cwWallGapFor(compact); }
double backMinFor(bool compact)    { return backIdealFor(compact); }

// 문 벽 안쪽선 ~ 카 앞면 거리.
//   승강로 깊이가 넉넉하면 360 고정(남는 깊이는 뒤 공간이 되어 균형추 양쪽 틈이 같이 커진다).
//   부족하면 뒤 공간을 먼저 지키고 앞 여유를 360 → 최소 kMinFrontGap 까지 줄인다.
double frontGapFor(double shaftDepth, double carDepth, bool compact)
{
    const double slack = shaftDepth - carDepth;
    return qMin(kCarFrontGap, qMax(kMinFrontGap, slack - backIdealFor(compact)));
}

// (u,v) → 도면 좌표 변환기. sg = +1 : P2 가 P1 보다 위, -1 : 아래 (문 방향이 뒤집힘)
struct ElevFrame {
    double cx;   // 승강로 가로 중심 X
    double y0;   // 문 벽 안쪽선 Y (= P1.y)
    double sg;   // v 증가 방향 부호

    QPointF pt(double u, double v) const { return QPointF(cx + u, y0 + sg * v); }
    QLineF  line(double u1, double v1, double u2, double v2) const
    {
        return QLineF(pt(u1, v1), pt(u2, v2));
    }
    // 수직 반전 시 호 각도는 y → -y 로 거울 반사된다.
    ElevatorArc arc(double cu, double cv, double r, double start, double span) const
    {
        ElevatorArc a;
        a.center = pt(cu, cv);
        a.radius = r;
        if (sg > 0) {
            a.startAngle = start;
            a.spanAngle  = span;
        } else {
            double s = 360.0 - (start + span);
            while (s < 0.0)    s += 360.0;
            while (s >= 360.0) s -= 360.0;
            a.startAngle = s;
            a.spanAngle  = span;
        }
        return a;
    }
};

MyDimension makeLinearDim(const QPointF &start, const QPointF &end,
                          const QPointF &dimLine, double fixedAngle)
{
    const MyVector vs(start.x(), start.y());
    const MyVector ve(end.x(), end.y());
    const MyVector vd(dimLine.x(), dimLine.y());
    MyDimension d(vs, ve, vd);
    d.dimensionType = DimensionType::Linear;
    d.useFixedAngle = true;
    d.fixedAngle    = fixedAngle;
    return d;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 형상 계산
// ─────────────────────────────────────────────────────────────────────────────
ElevatorGeom ElevatorTool::buildGeometry(const QPointF &p1, const QPointF &p2,
                                         const ElevatorParams &p)
{
    ElevatorGeom g;

    const double sx = std::fabs(p2.x() - p1.x());   // 승강로 폭
    const double sy = std::fabs(p2.y() - p1.y());   // 승강로 깊이
    if (sx < 1e-6 || sy < 1e-6) return g;

    ElevFrame f;
    f.cx = (p1.x() + p2.x()) * 0.5;
    f.y0 = p1.y();
    f.sg = (p2.y() >= p1.y()) ? 1.0 : -1.0;

    const double hs = sx * 0.5;                       // 승강로 반폭
    const double hw = p.carWidth * 0.5;               // 카 반폭
    const double hd = p.doorSize * 0.5;               // 문 반폭
    const double ho = hd + kWallJambExtra;            // 벽 개구부 반폭
    const double vF = frontGapFor(sy, p.carDepth, p.compactCw);    // 카 앞면
    const double vB = vF + p.carDepth;                // 카 뒷면

    // ── W1 : 승강로 벽선 ────────────────────────────────────────────────
    g.wallLines.append(f.line(-hs, sy, hs, sy));      // 뒷벽
    g.wallLines.append(f.line(-hs, 0.0, -hs, sy));    // 좌측벽
    g.wallLines.append(f.line(hs, 0.0, hs, sy));      // 우측벽
    if (ho < hs) {
        g.wallLines.append(f.line(-hs, 0.0, -ho, 0.0));   // 문 벽 (좌)
        g.wallLines.append(f.line(ho, 0.0, hs, 0.0));     // 문 벽 (우)
    }
    g.wallLines.append(f.line(-ho, 0.0, -ho, -kWallStubLen));  // 개구부 옆 돌출 (좌)
    g.wallLines.append(f.line(ho, 0.0, ho, -kWallStubLen));    // 개구부 옆 돌출 (우)

    // ── ELEV : 카 (사각형 + X자 대각선, 앞면은 문 개구부만큼 열림) ──────────
    g.elevLines.append(f.line(-hw, vB, hw, vB));
    g.elevLines.append(f.line(-hw, vF, -hw, vB));
    g.elevLines.append(f.line(hw, vF, hw, vB));
    if (hd < hw) {
        g.elevLines.append(f.line(-hw, vF, -hd, vF));
        g.elevLines.append(f.line(hd, vF, hw, vF));
    }
    g.elevLines.append(f.line(-hw, vB, hw, vF));
    g.elevLines.append(f.line(hw, vB, -hw, vF));

    // ── ELEV : 카 문 ───────────────────────────────────────────────────
    g.elevLines.append(f.line(-hd, vF, -hd, vF - kCarDoorThick));
    g.elevLines.append(f.line(hd, vF, hd, vF - kCarDoorThick));
    g.elevLines.append(f.line(-hd, vF - kCarDoorMid, hd, vF - kCarDoorMid));
    g.elevLines.append(f.line(-hd, vF - kCarDoorThick, hd, vF - kCarDoorThick));

    // ── ELEV : 카 프레임 ───────────────────────────────────────────────
    {
        const double fu = hw + kFrameMargin;
        const double fv1 = vF - kFrameFront;
        const double fv2 = vB + kFrameMargin;
        g.elevLines.append(f.line(-fu, fv1, fu, fv1));
        g.elevLines.append(f.line(-fu, fv2, fu, fv2));
        g.elevLines.append(f.line(-fu, fv1, -fu, fv2));
        g.elevLines.append(f.line(fu, fv1, fu, fv2));
    }

    // ── ELEV : 승강장 문 + 문턱선 ────────────────────────────────────────
    g.elevLines.append(f.line(-hd, kHallDoorV1, hd, kHallDoorV1));
    g.elevLines.append(f.line(-hd, kHallDoorV2, hd, kHallDoorV2));
    g.elevLines.append(f.line(-hd, kHallDoorV1, -hd, kHallDoorV2));
    g.elevLines.append(f.line(hd, kHallDoorV1, hd, kHallDoorV2));
    g.elevLines.append(f.line(-hs, kSillV, hs, kSillV));

    // ── ELEV : 승강장 문틀 (좌우 대칭) ───────────────────────────────────
    for (int k = 0; k < 2; ++k) {
        const double s = (k == 0) ? -1.0 : 1.0;
        g.elevLines.append(f.line(s * (hd + kJambA1), 0.0, s * (hd + kJambA2), 0.0));
        g.elevLines.append(f.line(s * (hd + kJambA2), 0.0, s * (hd + kJambA2), -kJambIn));
        g.elevLines.append(f.line(s * (hd + kJambDiagX1), kJambDiagV1,
                                  s * (hd + kJambDiagX2), kJambDiagV2));
        g.elevLines.append(f.line(s * (hd + kJambBotX1), kJambBotV,
                                  s * (hd + kJambBotX2), kJambBotV));
        g.elevLines.append(f.line(s * (hd + kJambBotX2), kJambBotV,
                                  s * (hd + kJambBotX2), kJambBotV + kJambBotUp));
        if (s < 0.0) {
            g.elevArcs.append(f.arc(-(hd + kJambArcCx1), kJambArcCv1, kJambArcR, 349.69515353123, 100.30484646877));
            g.elevArcs.append(f.arc(-(hd + kJambArc2Cx), kJambArc2Cv, kJambArcR, 270.0, 79.69515353123));
        } else {
            g.elevArcs.append(f.arc(hd + kJambArcCx1, kJambArcCv1, kJambArcR, 90.0, 100.30484646877));
            g.elevArcs.append(f.arc(hd + kJambArc2Cx, kJambArc2Cv, kJambArcR, 190.30484646877, 79.69515353123));
        }
    }

    // ── ELEV : 카운터웨이트 + 가이드레일 브래킷 (뒷벽 기준) ─────────────────
    //   뒷벽 기준으로 항상 그린다. 자리가 모자라면 checkFit 이 경고한다.
    const double cwSc = cwScaleFor(p.compactCw);   // 8인승 이하 : 2/3 크기
    //   카 프레임 뒷면 ~ 균형추 틈 = 균형추 ~ 뒷벽 틈 (뒤 공간 가운데에 배치)
    const double cwSpaceStart = vB + kFrameMargin;
    const double cwGapEq = (sy - cwSpaceStart - cwDepthFor(p.compactCw)) * 0.5;
    const double cwV1 = cwSpaceStart + cwGapEq;
    const double cwV2 = cwV1 + cwDepthFor(p.compactCw);
    {
        const double cv2 = cwV2;                      // 바깥(뒷벽 쪽)
        const double cv1 = cwV1;                      // 안쪽
        const double hwCw = p.carWidth * kCwWidthRatio * 0.5;   // 균형추 반폭 (카폭의 2/3 의 절반)
        g.elevLines.append(f.line(-hwCw, cv1, hwCw, cv1));
        g.elevLines.append(f.line(-hwCw, cv2, hwCw, cv2));
        g.elevLines.append(f.line(-hwCw, cv1, -hwCw, cv2));
        g.elevLines.append(f.line(hwCw, cv1, hwCw, cv2));
        for (int k = 0; k < 2; ++k) {
            const double s = (k == 0) ? -1.0 : 1.0;
            const double rIn  = s * (hwCw + kRailInOff * cwSc);
            const double rOut = s * (hwCw + kRailOutOff * cwSc);
            g.elevLines.append(f.line(rIn, cv1, rIn, cv2));
            g.elevLines.append(f.line(rOut, cv1, rOut, cv2));
            g.elevLines.append(f.line(rIn, cv1, rOut, cv1));
            g.elevLines.append(f.line(rIn, cv2, rOut, cv2));
        }
        const double cvMid = (cv1 + cv2) * 0.5;
        g.elevLines.append(f.line(-(hwCw + kCwCenterOff * cwSc), cvMid, hwCw + kCwCenterOff * cwSc, cvMid));
    }

    // ── 치수 : 카폭 / 문크기 / 카깊이 ─────────────────────────────────────
    g.dims.append(makeLinearDim(f.pt(-hw, vB), f.pt(hw, vB),
                                f.pt(-hw, sy + kDimOffsetBack), 0.0));
    g.dims.append(makeLinearDim(f.pt(-hd, kHallDoorV1), f.pt(hd, kHallDoorV1),
                                f.pt(-hd, -kDimOffsetFront), 0.0));
    g.dims.append(makeLinearDim(f.pt(hw, vB), f.pt(hw, vF),
                                f.pt(hs + kDimOffsetSide, vB), 90.0));
    return g;
}

double ElevatorTool::maxCarDepth(double shaftD, bool compactCw)
{
    return shaftD - kMinFrontGap - backMinFor(compactCw);
}

QString ElevatorTool::checkFit(double shaftW, double shaftD, const ElevatorParams &p)
{
    if (shaftW < p.carWidth + 2.0 * kFrameMargin)
        return tr("승강로 폭(%1)이 카 프레임 폭(%2)보다 작습니다.")
            .arg(shaftW, 0, 'f', 1).arg(p.carWidth + 2.0 * kFrameMargin, 0, 'f', 1);
    const double needD = kMinFrontGap + p.carDepth + backMinFor(p.compactCw);
    if (shaftD < needD)
        return tr("승강로 깊이(%1)가 앞 여유 %2 + 카깊이 + 균형추 공간(틈 포함) %3 = %4 보다 작습니다.")
            .arg(shaftD, 0, 'f', 1).arg(kMinFrontGap, 0, 'f', 0)
            .arg(backMinFor(p.compactCw), 0, 'f', 0).arg(needD, 0, 'f', 1);
    if (p.doorSize >= p.carWidth)
        return tr("문크기(%1)가 카폭(%2) 이상입니다.")
            .arg(p.doorSize, 0, 'f', 1).arg(p.carWidth, 0, 'f', 1);
    return QString();
}

// ─────────────────────────────────────────────────────────────────────────────
// ElevatorTool
// ─────────────────────────────────────────────────────────────────────────────
ElevatorTool::ElevatorTool(const ElevatorParams &params, QObject *parent)
    : AbstractTool(parent), m_state(WaitP1), m_params(params)
{}

void ElevatorTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitP1;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("엘리베이터"), CommandDock::Command);
    }
    showPrompt();
}

void ElevatorTool::deactivate()
{
    cancelDrawing();
    AbstractTool::deactivate();
}

void ElevatorTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(
        m_state == WaitP1 ? tr(" 승강로 문쪽 모서리(P1)를 지정하세요 : ")
                          : tr(" 승강로 대각 모서리(P2)를 지정하세요 : "), {});
}

void ElevatorTool::cancelDrawing()
{
    m_state = WaitP1;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
}

bool ElevatorTool::processCommand(const QString &command)
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->parser()) return false;
    const QPointF base = (m_state == WaitP2) ? m_p1 : QPointF(0, 0);
    QPointF result;
    if (ctx->parser()->parseCoordinate(command.trimmed(), base, base, false, result)) {
        mousePress(result, Qt::LeftButton);
        return true;
    }
    return false;
}

void ElevatorTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        m_scene->setGhostPath(QPainterPath(), false);
        if (m_state == WaitP2) { m_state = WaitP1; showPrompt(); }
        else                   { cancelDrawing(); emit toolFinished(); }
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        const QPointF base = (m_state == WaitP2) ? m_p1 : QPointF();
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitP1) {
        m_p1 = snapped;
        m_state = WaitP2;
        showPrompt();
        return;
    }

    // P2 는 P1 과 X, Y 가 모두 달라야 한다 (직사각형 승강로)
    if (std::fabs(snapped.x() - m_p1.x()) < 1e-6 || std::fabs(snapped.y() - m_p1.y()) < 1e-6)
        return;
    m_p2 = snapped;
    m_scene->setGhostPath(QPainterPath(), false);
    if (!showSetupDialog()) {
        cancelDrawing();
        emit toolFinished();
        return;
    }
    finish();
}

void ElevatorTool::mouseMove(QPointF pos)
{
    if (!m_scene || m_scene->views().isEmpty()) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        const QPointF base = (m_state == WaitP2) ? m_p1 : QPointF();
        auto r = ctx->snapManager()->snapPoint(pos, m_scene, base);
        snapped = r.snappedPos;
        snapType = r.type;
    }
    if (m_state == WaitP2) {
        // 현재 설정값으로 전체 형상 미리보기 (벽선 + 엘리베이터 선 + 호)
        const ElevatorGeom g = buildGeometry(m_p1, snapped, m_params);
        QPainterPath path;
        if (m_params.drawWalls) {
            for (int i = 0; i < g.wallLines.size(); ++i) {
                path.moveTo(g.wallLines.at(i).p1());
                path.lineTo(g.wallLines.at(i).p2());
            }
        }
        for (int i = 0; i < g.elevLines.size(); ++i) {
            path.moveTo(g.elevLines.at(i).p1());
            path.lineTo(g.elevLines.at(i).p2());
        }
        for (int i = 0; i < g.elevArcs.size(); ++i) {
            const ElevatorArc &a = g.elevArcs.at(i);
            const int seg = 8;
            for (int k = 0; k <= seg; ++k) {
                const double ang = qDegreesToRadians(a.startAngle + a.spanAngle * k / seg);
                const QPointF q(a.center.x() + a.radius * std::cos(ang),
                                a.center.y() + a.radius * std::sin(ang));
                if (k == 0) path.moveTo(q);
                else        path.lineTo(q);
            }
        }
        updateGhostPreview(path, kElevLayer);
    }
    m_scene->setSnapMarker(snapped, snapType);
}

void ElevatorTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelDrawing();
        emit toolFinished();
    } else {
        AbstractTool::keyPress(event);
    }
}

bool ElevatorTool::showSetupDialog()
{
    if (m_headless) return true;
    IAppContext *ctx = getContext();
    if (!ctx) return false;
    QWidget *parentWin = dynamic_cast<QWidget *>(ctx->commandDock());
    ElevatorDialog dlg(m_params, std::fabs(m_p2.x() - m_p1.x()),
                       std::fabs(m_p2.y() - m_p1.y()), parentWin);
    if (dlg.exec() != QDialog::Accepted) return false;
    m_params = dlg.params();
    saveParams(m_scene, m_params);
    return true;
}

void ElevatorTool::ensureLayer(IAppContext *ctx, const QString &name, const QColor &color)
{
    if (!ctx || !ctx->layerManager() || name.isEmpty()) return;
    if (ctx->layerManager()->getLayer(name)) return;
    Layer layer(name, color, 0.25, QStringLiteral("Continuous"),
                false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

void ElevatorTool::finish()
{
    IAppContext *ctx = getContext();
    if (!m_scene || !ctx || !ctx->entityFactory()) { cancelDrawing(); emit toolFinished(); return; }

    const ElevatorGeom g = buildGeometry(m_p1, m_p2, m_params);
    if (g.elevLines.isEmpty()) { cancelDrawing(); emit toolFinished(); return; }

    const QString warn = checkFit(std::fabs(m_p2.x() - m_p1.x()),
                                  std::fabs(m_p2.y() - m_p1.y()), m_params);
    if (!warn.isEmpty() && ctx->commandDock())
        ctx->commandDock()->logMessage(tr("경고: ") + warn, CommandDock::Info);

    if (m_params.drawWalls)
        ensureLayer(ctx, kWallLayer, QColor(255, 255, 0));
    ensureLayer(ctx, kElevLayer, QColor(0, 255, 255));
    const QString dimLayer = AbstractTool::resolveLayerName(ctx, QString());

    const QPen wallPen = AbstractTool::layerPenFor(ctx, kWallLayer);
    const QPen elevPen = AbstractTool::layerPenFor(ctx, kElevLayer);
    const QPen dimPen  = AbstractTool::layerPenFor(ctx, dimLayer);

    QUndoStack *stack = ctx->undoStack();
    if (stack) stack->beginMacro(tr("엘리베이터"));

    if (m_params.drawWalls) {
        for (int i = 0; i < g.wallLines.size(); ++i)
            addItem(ctx->entityFactory()->createLine(g.wallLines.at(i), wallPen), kWallLayer);
    }
    for (int i = 0; i < g.elevLines.size(); ++i)
        addItem(ctx->entityFactory()->createLine(g.elevLines.at(i), elevPen), kElevLayer);
    for (int i = 0; i < g.elevArcs.size(); ++i) {
        const ElevatorArc &a = g.elevArcs.at(i);
        addItem(ctx->entityFactory()->createArc(a.center, a.radius, a.startAngle,
                                                a.spanAngle, elevPen), kElevLayer);
    }
    for (int i = 0; m_params.drawDims && i < g.dims.size(); ++i) {
        MyDimension d = g.dims.at(i);
        applyDetailRegionScale(d);
        addItem(ctx->entityFactory()->createDimension(d, dimPen), dimLayer);
    }

    if (stack) stack->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("엘리베이터 완료"), CommandDock::Info);
        ctx->commandDock()->setPrompt(QString::fromUtf8(" Command : "));
    }
    cancelDrawing();
    emit toolFinished();
}

void ElevatorTool::addItem(QGraphicsItem *item, const QString &layerName)
{
    IAppContext *ctx = getContext();
    if (!item || !m_scene || !ctx) return;
    if (CadItem *ci = dynamic_cast<CadItem *>(item)) ci->setLayerName(layerName);
    item->setParentItem(m_scene->getLayerRoot(layerName));
    if (item->scene() == m_scene) m_scene->removeItem(item);
    if (ctx->undoStack()) ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
    else                  m_scene->addItem(item);
}

ElevatorParams ElevatorTool::loadParams(MyGraphicsScene *scene)
{
    ElevatorParams p;
    ToolDefaults d("ElevatorTool", scene);
    p.carWidth = d.getDouble("CarWidth", p.carWidth);
    p.carDepth = d.getDouble("CarDepth", p.carDepth);
    p.doorSize = d.getDouble("DoorSize", p.doorSize);
    p.maker    = d.getString("Maker", p.maker);
    p.drawWalls = d.getBool("DrawWalls", p.drawWalls);
    p.drawDims  = d.getBool("DrawDims", p.drawDims);
    return p;
}

void ElevatorTool::saveParams(MyGraphicsScene *scene, const ElevatorParams &p)
{
    ToolDefaults d("ElevatorTool", scene);
    d.put("CarWidth", p.carWidth);
    d.put("CarDepth", p.carDepth);
    d.put("DoorSize", p.doorSize);
    d.put("Maker", p.maker);
    d.put("DrawWalls", p.drawWalls);
    d.put("DrawDims", p.drawDims);
}

// ─────────────────────────────────────────────────────────────────────────────
// 인승별 규격 로드
// ─────────────────────────────────────────────────────────────────────────────
static QString resolveElevatorDataDir()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    QString dir = exeDir + QStringLiteral("/Data/elevator");
    if (QDir(dir).exists()) return dir;
    const QString alt = QDir::cleanPath(exeDir + QStringLiteral("/../Data/elevator"));
    if (QDir(alt).exists()) return alt;
#ifdef APP_SOURCE_PATH
    const QString devDir = QDir::cleanPath(QStringLiteral(APP_SOURCE_PATH))
                           + QStringLiteral("/Data/elevator");
    if (QDir(devDir).exists()) return devDir;
#endif
    return dir;
}

QVector<ElevatorMaker> ElevatorTool::loadMakers()
{
    QVector<ElevatorMaker> out;
    QFile f(QDir(resolveElevatorDataDir()).filePath(QStringLiteral("elevator_specs.json")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return out;

    const QJsonArray makers = doc.object().value(QStringLiteral("makers")).toArray();
    for (int i = 0; i < makers.size(); ++i) {
        const QJsonObject mo = makers.at(i).toObject();
        ElevatorMaker m;
        m.name   = mo.value(QStringLiteral("name")).toString();
        m.source = mo.value(QStringLiteral("source")).toString();
        const QJsonArray specs = mo.value(QStringLiteral("specs")).toArray();
        for (int k = 0; k < specs.size(); ++k) {
            const QJsonObject so = specs.at(k).toObject();
            ElevatorSpec sp;
            sp.persons    = so.value(QStringLiteral("persons")).toInt();
            sp.loadKg     = so.value(QStringLiteral("loadKg")).toDouble();
            sp.doorWidth  = so.value(QStringLiteral("doorWidth")).toDouble();
            sp.carWidth   = so.value(QStringLiteral("carWidth")).toDouble();
            sp.carDepth   = so.value(QStringLiteral("carDepth")).toDouble();
            sp.shaftWidth = so.value(QStringLiteral("shaftWidth")).toDouble();
            sp.shaftDepth = so.value(QStringLiteral("shaftDepth")).toDouble();
            if (sp.persons <= 0 || sp.carWidth <= 0.0 || sp.carDepth <= 0.0) continue;
            m.specs.append(sp);
        }
        if (!m.name.isEmpty() && !m.specs.isEmpty()) out.append(m);
    }
    return out;
}

int ElevatorTool::nearestSpecIndex(const ElevatorMaker &maker, double shaftW, double shaftD)
{
    int best = -1;
    double bestDist = 0.0;
    for (int i = 0; i < maker.specs.size(); ++i) {
        const ElevatorSpec &sp = maker.specs.at(i);
        const double dw = sp.shaftWidth - shaftW;
        const double dd = sp.shaftDepth - shaftD;
        const double dist = dw * dw + dd * dd;
        if (best < 0 || dist < bestDist) {
            best = i;
            bestDist = dist;
        }
    }
    return best;
}

// ─────────────────────────────────────────────────────────────────────────────
// 최소 인승 이하 승강로 — 계산식
//   최소 인승 규격(예: 8인승)의 (승강로 - 카) 여유를 그대로 쓴다.
//     카폭 = 승강로 폭 - (규격 승강로폭 - 규격 카폭)       (8인승 : 600)
//     카깊이 = 승강로 깊이 - (규격 승강로깊이 - 규격 카깊이) (8인승 : 330)
//   문크기는 최소 규격 문폭을 쓰되, 카폭 대비 규격 비율(8인승 : 800/1250)을 넘지 않게 50 단위로 맞춘다.
// ─────────────────────────────────────────────────────────────────────────────
static int smallestSpecIndex(const ElevatorMaker &mk)
{
    int best = -1;
    for (int i = 0; i < mk.specs.size(); ++i) {
        if (best < 0 || mk.specs.at(i).persons < mk.specs.at(best).persons)
            best = i;
    }
    return best;
}

static bool isBelowSmallest(const ElevatorMaker &mk, double shaftW, double shaftD)
{
    const int s0 = smallestSpecIndex(mk);
    if (s0 < 0) return false;
    const ElevatorSpec &sp = mk.specs.at(s0);
    return shaftW < sp.shaftWidth || shaftD < sp.shaftDepth;
}

static bool calcSmallCar(const ElevatorMaker &mk, double shaftW, double shaftD,
                         double &carW, double &carD, double &door)
{
    const int s0 = smallestSpecIndex(mk);
    if (s0 < 0) return false;
    const ElevatorSpec &sp = mk.specs.at(s0);

    carW = qMax(300.0, shaftW - (sp.shaftWidth - sp.carWidth));
    carD = qMax(300.0, shaftD - (sp.shaftDepth - sp.carDepth));

    const double maxDoor = std::floor(carW * (sp.doorWidth / sp.carWidth) / 50.0) * 50.0;
    door = qMax(50.0, qMin(sp.doorWidth, maxDoor));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 사용자 크기 목록 저장 파일 (기하공차 프리셋과 같은 data 폴더)
//   data 폴더 탐색 : 1) exe 옆 data  2) 상위 폴더의 data  3) 소스 루트의 data(APP_SOURCE_PATH)
// ─────────────────────────────────────────────────────────────────────────────
static const int kCustomCarWidthRole = Qt::UserRole;
static const int kCustomCarDepthRole = Qt::UserRole + 1;
static const int kCustomDoorRole     = Qt::UserRole + 2;

static QString resolveElevatorUserDir()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    QString dir = exeDir + QStringLiteral("/data");
    if (QDir(dir).exists()) return dir;
    const QString alt = QDir::cleanPath(exeDir + QStringLiteral("/../data"));
    if (QDir(alt).exists()) return alt;
#ifdef APP_SOURCE_PATH
    const QString devDir = QDir::cleanPath(QStringLiteral(APP_SOURCE_PATH))
                           + QStringLiteral("/data");
    if (QDir(devDir).exists()) return devDir;
#endif
    return dir;
}

static QString elevatorCustomFilePath()
{
    return QDir(resolveElevatorUserDir()).filePath(QStringLiteral("elevator_custom_sizes.json"));
}

// ─────────────────────────────────────────────────────────────────────────────
// ElevatorDialog
// ─────────────────────────────────────────────────────────────────────────────
ElevatorDialog::ElevatorDialog(const ElevatorParams &params, double shaftW, double shaftD,
                               QWidget *parent)
    : QDialog(parent), m_shaftW(shaftW), m_shaftD(shaftD), m_updating(true)
{
    setWindowTitle(tr("엘리베이터"));
    setModal(true);

    m_makers = ElevatorTool::loadMakers();

    m_comboMaker = new QComboBox(this);
    m_comboSpec  = new QComboBox(this);
    for (int i = 0; i < m_makers.size(); ++i)
        m_comboMaker->addItem(m_makers.at(i).name);
    const int prev = m_comboMaker->findText(params.maker);
    if (prev >= 0) m_comboMaker->setCurrentIndex(prev);

    m_spinCarWidth = new QDoubleSpinBox(this);
    m_spinCarWidth->setRange(1.0, 100000.0);
    m_spinCarWidth->setDecimals(1);
    m_spinCarWidth->setSingleStep(10.0);
    m_spinCarWidth->setValue(params.carWidth);

    m_spinCarDepth = new QDoubleSpinBox(this);
    m_spinCarDepth->setRange(1.0, 100000.0);
    m_spinCarDepth->setDecimals(1);
    m_spinCarDepth->setSingleStep(10.0);
    m_spinCarDepth->setValue(params.carDepth);

    m_spinDoorSize = new QDoubleSpinBox(this);
    m_spinDoorSize->setRange(1.0, 100000.0);
    m_spinDoorSize->setDecimals(1);
    m_spinDoorSize->setSingleStep(10.0);
    m_spinDoorSize->setValue(params.doorSize);

    m_btnSaveCustom = new QPushButton(tr("사용자 크기"), this);
    m_btnSaveCustom->setToolTip(tr("현재 카폭·카깊이·문크기를 이름을 붙여 왼쪽 목록에 저장합니다."));

    // 왼쪽 : 저장된 사용자 크기 목록 (클릭하면 값이 입력칸에 채워진다)
    m_listCustom = new QListWidget(this);
    m_listCustom->setMinimumWidth(150);
    m_listCustom->setMaximumWidth(170);
    m_listCustom->setContextMenuPolicy(Qt::CustomContextMenu);

    m_checkWalls = new QCheckBox(tr("승강로 벽체선 그리기"), this);
    m_checkWalls->setChecked(params.drawWalls);
    m_checkDims = new QCheckBox(tr("치수선 그리기"), this);
    m_checkDims->setChecked(params.drawDims);

    m_labelInfo = new QLabel(this);
    m_labelInfo->setWordWrap(true);

    QFormLayout *form = new QFormLayout;
    if (!m_makers.isEmpty()) {
        form->addRow(tr("제조사/규격"), m_comboMaker);
        form->addRow(tr("인승"), m_comboSpec);
    }
    form->addRow(tr("카폭"), m_spinCarWidth);
    form->addRow(tr("카깊이"), m_spinCarDepth);
    form->addRow(tr("문크기"), m_spinDoorSize);
    form->addRow(m_btnSaveCustom);
    form->addRow(m_checkWalls);
    form->addRow(m_checkDims);
    form->addRow(m_labelInfo);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QVBoxLayout *listCol = new QVBoxLayout;
    listCol->addWidget(new QLabel(tr("사용자 크기 목록"), this));
    listCol->addWidget(m_listCustom, 1);

    QHBoxLayout *body = new QHBoxLayout;
    body->addLayout(listCol);
    body->addLayout(form, 1);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->addLayout(body);
    root->addWidget(buttons);

    loadCustomList();

    // 입력한 승강로 크기에 가장 가까운 인승을 초기값으로 채운다 (규격이 있을 때)
    if (!m_makers.isEmpty())
        populateSpecCombo(true);
    m_updating = false;

    connect(m_comboMaker, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ElevatorDialog::onMakerChanged);
    connect(m_comboSpec, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ElevatorDialog::onSpecChanged);
    connect(m_btnSaveCustom, &QPushButton::clicked, this, &ElevatorDialog::onSaveCustomClicked);
    connect(m_listCustom, &QListWidget::itemClicked, this, &ElevatorDialog::onCustomItemClicked);
    connect(m_listCustom, &QListWidget::customContextMenuRequested, this, &ElevatorDialog::onCustomContextMenu);
    connect(m_spinCarWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ElevatorDialog::updateInfo);
    connect(m_spinCarDepth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ElevatorDialog::updateInfo);
    connect(m_spinDoorSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ElevatorDialog::updateInfo);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateInfo();
}

void ElevatorDialog::populateSpecCombo(bool selectNearest)
{
    const bool prevUpdating = m_updating;
    m_updating = true;
    m_comboSpec->clear();

    const int mi = m_comboMaker->currentIndex();
    if (mi >= 0 && mi < m_makers.size()) {
        const ElevatorMaker &mk = m_makers.at(mi);
        // 0번 항목 : 최소 인승 규격보다 작은 승강로 — 승강로 크기에서 카를 계산
        const int si0 = smallestSpecIndex(mk);
        m_comboSpec->addItem(si0 >= 0
            ? tr("%1인승 이하 (승강로 크기로 계산)").arg(mk.specs.at(si0).persons)
            : tr("최소 규격 이하 (승강로 크기로 계산)"));
        for (int i = 0; i < mk.specs.size(); ++i) {
            const ElevatorSpec &sp = mk.specs.at(i);
            m_comboSpec->addItem(tr("%1인승 (%2kg) — 승강로 %3 x %4")
                                     .arg(sp.persons)
                                     .arg(sp.loadKg, 0, 'f', 0)
                                     .arg(sp.shaftWidth, 0, 'f', 0)
                                     .arg(sp.shaftDepth, 0, 'f', 0));
        }
        if (selectNearest) {
            if (isBelowSmallest(mk, m_shaftW, m_shaftD)) {
                m_comboSpec->setCurrentIndex(0);
            } else {
                const int ni = ElevatorTool::nearestSpecIndex(mk, m_shaftW, m_shaftD);
                if (ni >= 0) m_comboSpec->setCurrentIndex(ni + 1);
            }
        }
    }

    // 선택된 인승 값으로 입력칸 갱신 (onSpecChanged 는 m_updating 중이면 무시하므로 잠시 해제)
    m_updating = false;
    onSpecChanged(m_comboSpec->currentIndex());
    m_updating = prevUpdating;
}

void ElevatorDialog::onMakerChanged(int index)
{
    Q_UNUSED(index);
    if (m_updating) return;
    populateSpecCombo(true);
}

void ElevatorDialog::onSpecChanged(int index)
{
    if (m_updating) return;
    const int mi = m_comboMaker->currentIndex();
    if (mi < 0 || mi >= m_makers.size()) return;
    const ElevatorMaker &mk = m_makers.at(mi);
    if (index < 0 || index > mk.specs.size()) return;

    double cw = 0.0, cd = 0.0, door = 0.0;
    if (index == 0) {
        // 최소 인승 이하 : 승강로 크기에서 계산
        if (!calcSmallCar(mk, m_shaftW, m_shaftD, cw, cd, door)) return;
    } else {
        const ElevatorSpec &sp = mk.specs.at(index - 1);
        cw = sp.carWidth;
        cd = sp.carDepth;
        door = sp.doorWidth;
    }

    // 최소 인승(8인승) 이하는 균형추를 2/3 크기, 벽~균형추 틈을 1/3 로 줄인다.
    {
        const int s0 = smallestSpecIndex(mk);
        m_compactCw = (index == 0)
                      || (s0 >= 0 && mk.specs.at(index - 1).persons <= mk.specs.at(s0).persons);
    }

    // 카 뒤에는 균형추 공간이 반드시 필요하므로 카깊이를 승강로에서 허용되는 최대값으로 제한한다.
    m_capNote.clear();
    const double maxD = ElevatorTool::maxCarDepth(m_shaftD, m_compactCw);
    if (maxD >= 300.0 && cd > maxD) {
        m_capNote = tr("카깊이 %1 → %2 (앞 여유 %3 + 균형추 공간 %4 확보)")
                        .arg(cd, 0, 'f', 0).arg(maxD, 0, 'f', 0)
                        .arg(kMinFrontGap, 0, 'f', 0).arg(backMinFor(m_compactCw), 0, 'f', 0);
        cd = maxD;
    }

    // 입력칸 갱신 중 valueChanged → updateInfo 가 반복 호출되지 않도록 막는다.
    m_updating = true;
    m_spinCarWidth->setValue(cw);
    m_spinCarDepth->setValue(cd);
    m_spinDoorSize->setValue(door);
    m_updating = false;
    updateInfo();
}

void ElevatorDialog::loadCustomList()
{
    m_listCustom->clear();
    QFile f(elevatorCustomFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return;

    const QJsonArray arr = doc.array();
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr.at(i).toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) continue;
        const double cw = o.value(QStringLiteral("carWidth")).toDouble();
        const double cd = o.value(QStringLiteral("carDepth")).toDouble();
        const double dr = o.value(QStringLiteral("doorSize")).toDouble();
        QListWidgetItem *item = new QListWidgetItem(name, m_listCustom);
        item->setData(kCustomCarWidthRole, cw);
        item->setData(kCustomCarDepthRole, cd);
        item->setData(kCustomDoorRole, dr);
        item->setToolTip(tr("카폭 %1 x 카깊이 %2 / 문 %3")
                             .arg(cw, 0, 'f', 0).arg(cd, 0, 'f', 0).arg(dr, 0, 'f', 0));
    }
}

bool ElevatorDialog::saveCustomList()
{
    QJsonArray arr;
    for (int i = 0; i < m_listCustom->count(); ++i) {
        const QListWidgetItem *item = m_listCustom->item(i);
        QJsonObject o;
        o.insert(QStringLiteral("name"), item->text());
        o.insert(QStringLiteral("carWidth"), item->data(kCustomCarWidthRole).toDouble());
        o.insert(QStringLiteral("carDepth"), item->data(kCustomCarDepthRole).toDouble());
        o.insert(QStringLiteral("doorSize"), item->data(kCustomDoorRole).toDouble());
        arr.append(o);
    }

    QDir dir(resolveElevatorUserDir());
    if (!dir.exists()) dir.mkpath(QStringLiteral("."));
    QFile f(elevatorCustomFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

void ElevatorDialog::onSaveCustomClicked()
{
    bool ok = false;
    const QString defName = tr("%1x%2-%3")
                                .arg(m_spinCarWidth->value(), 0, 'f', 0)
                                .arg(m_spinCarDepth->value(), 0, 'f', 0)
                                .arg(m_spinDoorSize->value(), 0, 'f', 0);
    QString name = QInputDialog::getText(this, tr("사용자 크기 저장"),
                                         tr("저장할 이름:"), QLineEdit::Normal, defName, &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    QListWidgetItem *target = nullptr;
    for (int i = 0; i < m_listCustom->count(); ++i) {
        if (m_listCustom->item(i)->text() == name) {
            target = m_listCustom->item(i);
            break;
        }
    }
    if (target) {
        if (QMessageBox::question(this, tr("사용자 크기 저장"),
                                  tr("'%1' 이름이 이미 있습니다. 덮어쓸까요?").arg(name))
            != QMessageBox::Yes)
            return;
    } else {
        target = new QListWidgetItem(name, m_listCustom);
    }

    target->setData(kCustomCarWidthRole, m_spinCarWidth->value());
    target->setData(kCustomCarDepthRole, m_spinCarDepth->value());
    target->setData(kCustomDoorRole, m_spinDoorSize->value());
    target->setToolTip(tr("카폭 %1 x 카깊이 %2 / 문 %3")
                           .arg(m_spinCarWidth->value(), 0, 'f', 0)
                           .arg(m_spinCarDepth->value(), 0, 'f', 0)
                           .arg(m_spinDoorSize->value(), 0, 'f', 0));

    if (!saveCustomList())
        QMessageBox::warning(this, tr("저장 실패"), tr("파일을 저장할 수 없습니다."));
}

void ElevatorDialog::onCustomItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    // 입력칸 갱신 중 valueChanged → updateInfo 가 반복 호출되지 않도록 막는다.
    m_updating = true;
    m_spinCarWidth->setValue(item->data(kCustomCarWidthRole).toDouble());
    m_spinCarDepth->setValue(item->data(kCustomCarDepthRole).toDouble());
    m_spinDoorSize->setValue(item->data(kCustomDoorRole).toDouble());
    m_updating = false;
    m_capNote.clear();   // 사용자가 직접 지정한 값이므로 인승 규격에 의한 제한 안내는 지운다
    updateInfo();
}

void ElevatorDialog::onCustomContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_listCustom->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    QAction *deleteAction = menu.addAction(tr("삭제"));
    if (menu.exec(m_listCustom->mapToGlobal(pos)) != deleteAction) return;

    delete m_listCustom->takeItem(m_listCustom->row(item));
    saveCustomList();
}

void ElevatorDialog::updateInfo()
{
    if (m_updating) return;
    QString text = tr("입력 승강로 %1 x %2").arg(m_shaftW, 0, 'f', 1).arg(m_shaftD, 0, 'f', 1);

    const int mi = m_comboMaker->currentIndex();
    const int si = m_comboSpec->currentIndex();
    if (mi >= 0 && mi < m_makers.size() && si == 0) {
        const ElevatorMaker &mk = m_makers.at(mi);
        const int s0 = smallestSpecIndex(mk);
        if (s0 >= 0) {
            const ElevatorSpec &sp = mk.specs.at(s0);
            text += tr("\n%1인승 이하 : 승강로에서 카폭 -%2, 카깊이 -%3 여유를 뺀 값으로 계산")
                        .arg(sp.persons)
                        .arg(sp.shaftWidth - sp.carWidth, 0, 'f', 0)
                        .arg(sp.shaftDepth - sp.carDepth, 0, 'f', 0);
        }
    } else if (mi >= 0 && mi < m_makers.size() && si >= 1 && si <= m_makers.at(mi).specs.size()) {
        const ElevatorSpec &sp = m_makers.at(mi).specs.at(si - 1);
        text += tr("\n선택 인승 규격 승강로 %1 x %2 (차이 %3 x %4)")
                    .arg(sp.shaftWidth, 0, 'f', 0).arg(sp.shaftDepth, 0, 'f', 0)
                    .arg(m_shaftW - sp.shaftWidth, 0, 'f', 0)
                    .arg(m_shaftD - sp.shaftDepth, 0, 'f', 0);
    }
    if (m_compactCw)
        text += tr("\n8인승 이하 : 균형추 2/3 크기, 벽~균형추 틈 1/3 적용");
    if (!m_capNote.isEmpty())
        text += QStringLiteral("\n") + m_capNote;
    const QString warn = ElevatorTool::checkFit(m_shaftW, m_shaftD, params());
    if (!warn.isEmpty())
        text += QStringLiteral("\n") + warn;
    m_labelInfo->setText(text);
}

QString ElevatorDialog::selectedMakerName() const
{
    return m_comboMaker->currentText();
}

ElevatorParams ElevatorDialog::params() const
{
    ElevatorParams p;
    p.carWidth = m_spinCarWidth->value();
    p.carDepth = m_spinCarDepth->value();
    p.doorSize = m_spinDoorSize->value();
    p.maker    = selectedMakerName();
    p.drawWalls = m_checkWalls->isChecked();
    p.drawDims  = m_checkDims->isChecked();
    p.compactCw = m_compactCw;
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// ElevatorCommand
// ─────────────────────────────────────────────────────────────────────────────
CommandBase *ElevatorCommand::createCommand(IAppContext *c)
{
    return new ElevatorCommand(c);
}

ElevatorCommand::ElevatorCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void ElevatorCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new ElevatorTool(ElevatorTool::loadParams(m_ctx->activeScene())));
}
