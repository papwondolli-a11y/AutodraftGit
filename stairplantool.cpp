#include "stairplantool.h"
#include "stairplandialog.h"
#include "stairsymboldata.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mydimension.h"
#include "mygraphicsscene.h"
#include "myvector.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QFont>
#include <QKeyEvent>
#include <QPainterPath>
#include <QUndoStack>
#include <QtMath>

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// 형상 계산 헬퍼 (stair.lsp 의 POLAR / ANGLE 대응)
// ─────────────────────────────────────────────────────────────────────────────
static QPointF polar(const QPointF &p, double ang, double d)
{
    return QPointF(p.x() + d * std::cos(ang), p.y() + d * std::sin(ang));
}

static double angleOf(const QPointF &a, const QPointF &b)
{
    return std::atan2(b.y() - a.y(), b.x() - a.x());
}

static void addLine(StairPrims &o, const QPointF &a, const QPointF &b)
{
    o.lines.append(QLineF(a, b));
}

static void addPoly(StairPrims &o, const QVector<QPointF> &pts, bool closed)
{
    StairPrims::Poly poly;
    poly.pts = pts;
    poly.closed = closed;
    o.polys.append(poly);
}

static void addCircle(StairPrims &o, const QPointF &c, double r)
{
    StairPrims::Circle ci;
    ci.c = c;
    ci.r = r;
    o.circles.append(ci);
}

static void addText(StairPrims &o, const QString &s, const QPointF &center, double h)
{
    StairPrims::Text t;
    t.s = s;
    t.pos = center;
    t.h = h;
    o.texts.append(t);
}

// 내장 심볼(stairA1/stairA3 블록) 선분을 삽입 (배율 → 회전 → 이동)
static void addSymbol(StairPrims &o, const double (*data)[4], int count,
                      const QPointF &ins, double scale, double rotRad)
{
    const double c = std::cos(rotRad);
    const double s = std::sin(rotRad);
    for (int i = 0; i < count; ++i) {
        const double x1 = data[i][0] * scale, y1 = data[i][1] * scale;
        const double x2 = data[i][2] * scale, y2 = data[i][3] * scale;
        const QPointF a(ins.x() + x1 * c - y1 * s, ins.y() + x1 * s + y1 * c);
        const QPointF b(ins.x() + x2 * c - y2 * s, ins.y() + x2 * s + y2 * c);
        addLine(o, a, b);
    }
}

// 디딤판 1줄(선 + 미끄럼방지 폴리라인)을 진행 방향으로 offset 만큼 이동시켜 추가
static void addTread(StairPrims &o, const QPointF &a, const QPointF &b,
                     const QVector<QPointF> &nosing, double lag, double offset)
{
    addLine(o, polar(a, lag, offset), polar(b, lag, offset));
    if (nosing.size() >= 2) {
        QVector<QPointF> moved;
        for (int i = 0; i < nosing.size(); ++i)
            moved.append(polar(nosing.at(i), lag, offset));
        StairPrims::Poly poly;
        poly.pts = moved;
        poly.closed = false;
        o.nosings.append(poly);
    }
}

// ── 직진 단일 계단 (singl_stair) ──────────────────────────────────────────────
static void buildSingle(const StairPlanParams &p, const QPointF &upPt, const QPointF &dnPt,
                        double lag, double ds, StairPrims &o)
{
    const double  tag = angleOf(upPt, dnPt);
    const double  twd = QLineF(upPt, dnPt).length();
    const double  dir1 = lag;

    const QPointF pt1 = polar(upPt, dir1, p.land);
    const QPointF pt2 = polar(dnPt, dir1, p.land);

    double dir2;
    int    n;
    if (p.up == 0) { dir2 = dir1;       n = p.down; }
    else           { dir2 = lag + M_PI; n = p.up; }

    QVector<QPointF> nosing;
    if (p.nonslip) {
        const QPointF pt3 = polar(pt1, tag, 100.0);
        const QPointF pt4 = polar(pt3, dir2, 40.0);
        const QPointF pt5 = polar(pt2, tag + M_PI, 100.0);
        const QPointF pt6 = polar(pt5, dir2, 40.0);
        nosing << pt3 << pt4 << pt6 << pt5;
    }
    for (int k = 0; k < n; ++k)
        addTread(o, pt1, pt2, nosing, lag, k * p.tread);

    // 보행선
    const QPointF pt7 = polar(pt1, angleOf(pt1, pt2), QLineF(pt1, pt2).length() / 2.0);
    const QPointF pt8 = polar(pt7, lag, p.tread * (n - 1));
    addLine(o, pt7, pt8);

    const double tsz = 2.5 * ds;
    const double symScale = twd / 20.0;
    QPointF numPt0;
    double  numAng;
    if (p.up != 0) {
        addCircle(o, pt8, 0.5 * ds);
        numPt0 = pt8;
        addText(o, QStringLiteral("UP"), polar(pt8, angleOf(pt7, pt8), 100.0 + tsz / 2.0), tsz);
        numAng = angleOf(pt8, pt7);
        addSymbol(o, kStairChevronLines, kStairChevronLinesCount, pt7, symScale,
                  dir1 + M_PI / 2.0);
    } else {
        addCircle(o, pt7, 0.5 * ds);
        numPt0 = pt7;
        addText(o, QStringLiteral("DN"), polar(pt7, angleOf(pt8, pt7), 100.0 + tsz / 2.0), tsz);
        numAng = angleOf(pt7, pt8);
        addSymbol(o, kStairChevronLines, kStairChevronLinesCount, pt8, symScale,
                  dir1 - M_PI / 2.0);
    }

    if (p.numbering) {
        const QPointF numPt1 = polar(numPt0, tag, twd / 2.0 - 200.0);
        QPointF numPt2 = polar(numPt1, numAng, p.tread / 2.0);
        for (int no = 1; no <= n; ++no) {
            addCircle(o, numPt2, 65.0);
            addText(o, QString::number(no), numPt2, 60.0);
            numPt2 = polar(numPt2, numAng, p.tread);
        }
    }
}

// 로컬 좌표 (a = 진행 방향 lag, t = 벽선 방향 tag) → 도면 좌표 변환기
struct StairFrame {
    QPointF o;
    double  cl, sl, ct, st;

    StairFrame(const QPointF &origin, double lag, double tag)
        : o(origin), cl(std::cos(lag)), sl(std::sin(lag)), ct(std::cos(tag)), st(std::sin(tag)) {}

    QPointF at(double a, double t) const
    {
        return QPointF(o.x() + a * cl + t * ct, o.y() + a * sl + t * st);
    }
};

// ── 꺾임(U자) 계단 (double_stair) ─────────────────────────────────────────────
static void buildDouble(const StairPlanParams &p, const QPointF &upPt, const QPointF &dnPt,
                        double lag, double ds, StairPrims &o)
{
    const int     tn  = qMax(p.up, p.down);
    const double  twd = QLineF(upPt, dnPt).length();
    const double  tag = angleOf(upPt, dnPt);
    const QPointF mid((upPt.x() + dnPt.x()) / 2.0, (upPt.y() + dnPt.y()) / 2.0);
    const StairFrame F(mid, lag, tag);

    // 타입2: 벽선(P1-P2)과 중앙 박스 상단선 사이(길이 = land-200)를 중간계단으로 채운다.
    //        계단참 길이 = 중간계단수 x 디딤판폭 + 200
    const bool   type2 = (p.type == 2 && p.midSteps > 0);
    const double L  = type2 ? p.midSteps * p.tread + 200.0 : p.land;
    const double a0 = L - 200.0;
    const double a1 = a0 + (tn - 1) * p.tread + 400.0;
    const double  wallT = -twd / 2.0;
    // 디딤판 길이(가로) Lt 와 중앙 사각형 폭 W = twd - 2*Lt (W >= 200)
    const double  maxLt = qMax(10.0, (twd - 200.0) / 2.0);
    const double  Lt = (p.treadLen > 0.0) ? qMin(p.treadLen, maxLt) : maxLt;
    const double  h  = twd / 2.0 - Lt;
    const double  d  = qMin(100.0, Lt / 4.0);   // 미끄럼방지선 양끝 안쪽 간격

    // 계단 외곽 (폭 200) + 70 안쪽으로 옵셋한 내곽
    QVector<QPointF> outer;
    outer << F.at(a0, h) << F.at(a1, h) << F.at(a1, -h) << F.at(a0, -h);
    addPoly(o, outer, true);
    QVector<QPointF> inner;
    const double hi = qMax(h - 70.0, 1.0);
    inner << F.at(a0 + 70.0, hi) << F.at(a1 - 70.0, hi)
          << F.at(a1 - 70.0, -hi) << F.at(a0 + 70.0, -hi);
    addPoly(o, inner, true);

    // UP 쪽 디딤판 1줄 (tp1-tp2) + 미끄럼방지 (tp3,tp5,tp6,tp4)
    const QPointF tp1 = F.at(L, -h);
    const QPointF tp2 = F.at(L, wallT);
    QVector<QPointF> nosUp, nosDn;
    if (p.nonslip) {
        nosUp << F.at(L, -h - d) << F.at(L - 40.0, -h - d)
              << F.at(L - 40.0, wallT + d) << F.at(L, wallT + d);
        // DN 쪽: 중심축 대칭(t 부호 반전) 후 디딤판선 기준 재대칭(a → 2L-a)
        nosDn << F.at(L, h + d) << F.at(L + 40.0, h + d)
              << F.at(L + 40.0, -wallT - d) << F.at(L, -wallT - d);
    }
    const QPointF dp1 = F.at(L, h);
    const QPointF dp2 = F.at(L, -wallT);
    if (type2) {
        // 중간계단: 벽선(a=0)과 박스 상단선(a=L-200) 사이를 진행 방향으로 잇는 디딤판선.
        //   폭 방향(t)으로 디딤판폭 간격, 박스 중앙선(t=0) 기준 좌우 대칭 (3개 = 중앙 1 + 좌우 1)
        const double aEnd = L - 200.0;
        const double dm   = qMin(100.0, aEnd / 4.0);
        for (int k = 0; k < p.midSteps; ++k) {
            const double t = (k - (p.midSteps - 1) / 2.0) * p.tread;
            addLine(o, F.at(0.0, t), F.at(aEnd, t));
            if (p.nonslip) {
                StairPrims::Poly poly;
                poly.pts << F.at(dm, t) << F.at(dm, t + 40.0)
                         << F.at(aEnd - dm, t + 40.0) << F.at(aEnd - dm, t);
                poly.closed = false;
                o.nosings.append(poly);
            }
            if (p.numbering) {
                // 중간계단 번호: UP 번호에 이어서(tn+1..), 각 선 다음 칸 중앙(+폭 방향), 박스 쪽(아랫쪽)
                const QPointF np = F.at(aEnd - 150.0, t + p.tread / 2.0);
                addCircle(o, np, 65.0);
                addText(o, QString::number(tn + 1 + k), np, 60.0);
            }
        }
    }
    for (int k = 0; k < p.up; ++k)
        addTread(o, tp1, tp2, nosUp, lag, k * p.tread);
    for (int k = 0; k < p.down; ++k)
        addTread(o, dp1, dp2, nosDn, lag, k * p.tread);

    // 계단 번호
    if (p.numbering) {
        const double aFar = L + tn * p.tread;
        QPointF insUp = F.at(aFar - (p.tread + 140.0), -(h + 150.0));
        QPointF insDn = F.at(aFar - (p.tread - 140.0), h + 150.0);
        int no = 1;
        int noDn = tn * 2 + (type2 ? p.midSteps : 0);   // 타입2: 중간계단 번호 뒤로 이어짐
        for (int i = 0; i < tn; ++i) {
            addCircle(o, insUp, 65.0);
            addText(o, QString::number(no), insUp, 70.0);
            addCircle(o, insDn, 65.0);
            addText(o, QString::number(noDn), insDn, 70.0);
            ++no;
            --noDn;
            insUp = polar(insUp, lag + M_PI, p.tread);
            insDn = polar(insDn, lag + M_PI, p.tread);
        }
    }

    // 층간 이동 표시선 (UP → 계단참 → DN)
    const double  tMid  = (-h + wallT) / 2.0;
    const double  aLp1  = L + (tn - 1) * p.tread;
    const double  aLp2  = aLp1 - (L / 2.0 + (tn - 1) * p.tread);
    const double  tSpan = twd / 2.0 + h;
    const QPointF lp1 = F.at(aLp1, tMid);
    const QPointF lp2 = F.at(aLp2, tMid);
    const QPointF lp3 = F.at(aLp2, tMid + tSpan);
    const QPointF lp4 = F.at(aLp1, tMid + tSpan);
    QVector<QPointF> lpPts;
    lpPts << lp1 << lp2 << lp3 << lp4;
    addPoly(o, lpPts, false);
    addCircle(o, lp1, 0.5 * ds);
    addCircle(o, lp4, 0.5 * ds);

    const double  tsz = 2.5 * ds;
    const QPointF tpu = polar(lp1, lag, p.tread);
    const QPointF tpd = polar(lp4, lag, p.tread);
    addText(o, QStringLiteral("UP"), tpu, tsz);
    addText(o, QStringLiteral("DN"), tpd, tsz);

    // 절단 심볼(stairA1): UP 계단(디딤판 1번선~마지막선) 중앙, 보행선 위
    const QPointF insPt = F.at(L + (p.up - 1) * p.tread / 2.0, tMid);
    addSymbol(o, kStairBreakLines, kStairBreakLinesCount, insPt, twd / 20.0,
              lag + M_PI / 2.0);
}

StairPrims StairPlanTool::buildPrims(const StairPlanParams &p, const QPointF &up,
                                     const QPointF &dn, double lag, double dimScale)
{
    StairPrims out;
    if (QLineF(up, dn).length() < 1e-6) return out;
    if (p.up == 0 && p.down == 0) return out;
    if (p.up == 0 || p.down == 0)
        buildSingle(p, up, dn, lag, dimScale, out);
    else
        buildDouble(p, up, dn, lag, dimScale, out);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// StairPlanTool
// ─────────────────────────────────────────────────────────────────────────────
StairPlanTool::StairPlanTool(const StairPlanParams &params, QObject *parent)
    : AbstractTool(parent), m_state(WaitUp), m_params(params)
{}

void StairPlanTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitUp;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("계단평면(STAIR)"), CommandDock::Command);
    }
    showPrompt();
}

void StairPlanTool::deactivate()
{
    cancelDrawing();
    AbstractTool::deactivate();
}

void StairPlanTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    QString msg;
    switch (m_state) {
    case WaitUp:        msg = tr(" 계단참 벽선의 첫번째 모서리(UP)를 지정하세요 : "); break;
    case WaitDown:      msg = tr(" 계단참 벽선의 두번째 모서리(DN)를 지정하세요 : "); break;
    case WaitDirection: msg = tr(" 계단 진행 방향을 지정하세요 : "); break;
    }
    ctx->commandDock()->showInteractivePrompt(msg, {});
}

// P1-P2 지정 후 설정 다이얼로그. 디딤판 길이 초기값 = (P1-P2 길이 - 200) / 2
bool StairPlanTool::showSetupDialog()
{
    if (m_headless) return true;
    IAppContext *ctx = getContext();
    if (!ctx) return false;

    StairPlanParams p = m_params;
    const double twd = QLineF(m_up, m_dn).length();
    p.treadLen = qMax(10.0, (twd - 200.0) / 2.0);

    QWidget *parentWin = dynamic_cast<QWidget *>(ctx->commandDock());
    StairPlanDialog dlg(p, ctx->layerManager(), ctx->lineTypeManager(), twd, parentWin);
    if (dlg.exec() != QDialog::Accepted) return false;

    m_params = dlg.params();
    saveParams(m_scene, m_params);
    return true;
}

// 꺾임 계단: 디딤판길이 x2 + 중앙 사각형 폭(200) 이 벽선 길이보다 크면 조정
void StairPlanTool::adjustTreadLength()
{
    IAppContext *ctx = getContext();
    if (m_params.up == 0 || m_params.down == 0) return;
    const double twd = QLineF(m_up, m_dn).length();
    const double maxLt = (twd - 200.0) / 2.0;
    QString msg;
    if (maxLt < 10.0) {
        msg = tr("벽선 길이(%1)가 너무 짧아 중앙 사각형 폭 200 을 확보할 수 없습니다.").arg(twd, 0, 'f', 1);
    } else if (m_params.treadLen <= 0.0) {
        msg = tr("디딤판 길이: 자동 = %1 (벽선 %2, 중앙 사각형 폭 200)")
                  .arg(maxLt, 0, 'f', 1).arg(twd, 0, 'f', 1);
    } else if (m_params.treadLen * 2.0 + 200.0 > twd) {
        msg = tr("디딤판 길이 %1 x2 + 중앙 사각형 폭 200 이 벽선 길이 %2 보다 커서 디딤판 길이를 %3 으로 조정합니다.")
                  .arg(m_params.treadLen, 0, 'f', 1).arg(twd, 0, 'f', 1).arg(maxLt, 0, 'f', 1);
        m_params.treadLen = maxLt;
    }
    if (!msg.isEmpty() && ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(msg, CommandDock::Info);
}

void StairPlanTool::cancelDrawing()
{
    m_state = WaitUp;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
}

// UP/DN 글자 높이 = 현재 글꼴 크기 x 도면 축척 (= 2.5 x 반환값), 원 반지름 = 0.5 x 반환값
double StairPlanTool::dimScale() const
{
    IAppContext *ctx = getContext();
    double size = ctx ? ctx->currentFontSize() : 0.0;
    if (size <= 1e-6) size = 2.5;
    double drawScale = m_scene ? m_scene->drawingScale() : 1.0;
    if (drawScale <= 1e-6) drawScale = 1.0;
    return size * drawScale / 2.5;
}

// 계단 진행 방향: 벽선(UP→DN)에 수직인 두 방향 중 커서가 있는 쪽
double StairPlanTool::directionAngle(const QPointF &cursor) const
{
    const double tag = angleOf(m_up, m_dn);
    const QPointF mid((m_up.x() + m_dn.x()) / 2.0, (m_up.y() + m_dn.y()) / 2.0);
    const double nx = std::cos(tag + M_PI / 2.0);
    const double ny = std::sin(tag + M_PI / 2.0);
    const double dot = (cursor.x() - mid.x()) * nx + (cursor.y() - mid.y()) * ny;
    return dot >= 0.0 ? tag + M_PI / 2.0 : tag - M_PI / 2.0;
}

bool StairPlanTool::processCommand(const QString &command)
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->parser()) return false;

    QPointF base(0, 0);
    if (m_state == WaitDown)
        base = m_up;
    else if (m_state == WaitDirection)
        base = QPointF((m_up.x() + m_dn.x()) / 2.0, (m_up.y() + m_dn.y()) / 2.0);

    QPointF resultPos;
    if (ctx->parser()->parseCoordinate(command.trimmed(), base, base, false, resultPos)) {
        mousePress(resultPos, Qt::LeftButton);
        return true;
    }
    return false;
}

void StairPlanTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        m_scene->setGhostPath(QPainterPath(), false);
        if (m_state == WaitDirection)      { m_state = WaitDown; showPrompt(); }
        else if (m_state == WaitDown)      { m_state = WaitUp;   showPrompt(); }
        else                               { cancelDrawing(); emit toolFinished(); }
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitDown) ? m_up : QPointF();
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitUp) {
        m_up = snapped;
        m_cur = snapped;
        m_state = WaitDown;
        showPrompt();
    } else if (m_state == WaitDown) {
        if (QLineF(m_up, snapped).length() < 1e-6) return;
        m_dn = snapped;
        m_cur = snapped;
        m_scene->setGhostPath(QPainterPath(), false);
        // P1-P2 지정 후 설정창 (디딤판 길이 = 벽선 기준 자동 계산값으로 표시)
        if (!showSetupDialog()) {
            cancelDrawing();
            emit toolFinished();
            return;
        }
        adjustTreadLength();
        m_state = WaitDirection;
        showPrompt();
    } else {
        finish(snapped);
    }
}

void StairPlanTool::mouseMove(QPointF pos)
{
    if (!m_scene || m_scene->views().isEmpty()) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitDown) ? m_up : QPointF();
        auto r = ctx->snapManager()->snapPoint(pos, m_scene, base);
        snapped = r.snappedPos;
        snapType = r.type;
    }
    m_cur = snapped;

    if (m_state == WaitDown) {
        updateGhostLinePreview(QLineF(m_up, snapped), m_params.layer);
    } else if (m_state == WaitDirection) {
        updateGhost(snapped);
    }
    m_scene->setSnapMarker(m_cur, snapType);
}

void StairPlanTool::updateGhost(const QPointF &cursor)
{
    const StairPrims prims = buildPrims(m_params, m_up, m_dn, directionAngle(cursor), dimScale());
    QPainterPath path;
    for (int i = 0; i < prims.lines.size(); ++i) {
        path.moveTo(prims.lines.at(i).p1());
        path.lineTo(prims.lines.at(i).p2());
    }
    QVector<StairPrims::Poly> allPolys = prims.polys;
    allPolys += prims.nosings;
    for (int i = 0; i < allPolys.size(); ++i) {
        const StairPrims::Poly &pl = allPolys.at(i);
        if (pl.pts.isEmpty()) continue;
        path.moveTo(pl.pts.first());
        for (int j = 1; j < pl.pts.size(); ++j) path.lineTo(pl.pts.at(j));
        if (pl.closed) path.closeSubpath();
    }
    for (int i = 0; i < prims.circles.size(); ++i)
        path.addEllipse(prims.circles.at(i).c, prims.circles.at(i).r, prims.circles.at(i).r);
    updateGhostPreview(path, m_params.layer);
}

void StairPlanTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelDrawing();
        emit toolFinished();
    } else {
        AbstractTool::keyPress(event);
    }
}

void StairPlanTool::ensureLayer(IAppContext *ctx, const QString &name)
{
    if (!ctx || !ctx->layerManager() || name.isEmpty()) return;
    if (ctx->layerManager()->getLayer(name)) return;
    Layer layer(name, QColor(0, 255, 0), 0.25, QStringLiteral("Continuous"),
                false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

QGraphicsItem *StairPlanTool::finalizeItem(QGraphicsItem *item, IAppContext *ctx, const QString &layer)
{
    if (!item) return nullptr;
    if (CadItem *ci = dynamic_cast<CadItem *>(item))
        ci->setLayerName(layer);
    item->setParentItem(m_scene->getLayerRoot(layer));
    if (item->scene() == m_scene) m_scene->removeItem(item);
    if (ctx->undoStack()) ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
    else                  m_scene->addItem(item);
    return item;
}

void StairPlanTool::finish(const QPointF &cursor)
{
    IAppContext *ctx = getContext();
    if (!m_scene || !ctx || !ctx->entityFactory()) { cancelDrawing(); emit toolFinished(); return; }

    const StairPrims prims = buildPrims(m_params, m_up, m_dn, directionAngle(cursor), dimScale());
    if (prims.lines.isEmpty() && prims.polys.isEmpty()) { cancelDrawing(); emit toolFinished(); return; }

    // 레이어: 계단 / 미끄럼방지 / 글자(UP·DN·번호의 원과 글자)
    const QString textLayer = AbstractTool::resolveLayerName(ctx, ctx->textLayerName());
    ensureLayer(ctx, m_params.layer);
    ensureLayer(ctx, m_params.nonslipLayer);
    const QPen pen     = AbstractTool::layerPenFor(ctx, m_params.layer);
    const QPen slipPen = AbstractTool::layerPenFor(ctx, m_params.nonslipLayer);
    const QPen textPen = AbstractTool::layerPenFor(ctx, textLayer);
    EntityFactory *ef = ctx->entityFactory();

    QFont font;
    font.setFamily(ctx->currentFontFamily());

    if (ctx->undoStack()) ctx->undoStack()->beginMacro(tr("계단평면"));

    for (int i = 0; i < prims.lines.size(); ++i)
        finalizeItem(ef->createLine(prims.lines.at(i), pen), ctx, m_params.layer);

    for (int i = 0; i < prims.polys.size(); ++i) {
        const StairPrims::Poly &pl = prims.polys.at(i);
        std::vector<MyVector> verts;
        for (int j = 0; j < pl.pts.size(); ++j) verts.push_back(MyVector(pl.pts.at(j)));
        finalizeItem(ef->createPolyline(verts, pl.closed, pen), ctx, m_params.layer);
    }

    for (int i = 0; i < prims.nosings.size(); ++i) {
        const StairPrims::Poly &pl = prims.nosings.at(i);
        std::vector<MyVector> verts;
        for (int j = 0; j < pl.pts.size(); ++j) verts.push_back(MyVector(pl.pts.at(j)));
        finalizeItem(ef->createPolyline(verts, pl.closed, slipPen), ctx, m_params.nonslipLayer);
    }

    for (int i = 0; i < prims.circles.size(); ++i)
        finalizeItem(ef->createCircle(prims.circles.at(i).c, prims.circles.at(i).r, textPen),
                     ctx, textLayer);

    for (int i = 0; i < prims.texts.size(); ++i) {
        const StairPrims::Text &t = prims.texts.at(i);
        font.setPointSizeF(t.h);
        // lisp "j mc" (가운데·중간) → 가로 가운데 정렬, 세로는 대문자 높이의 절반만큼 내려 기준선 보정
        const QPointF base(t.pos.x(), t.pos.y() - t.h * 0.36);
        finalizeItem(ef->createText(t.s, base, font, textPen.color(), 0.0, 1), ctx, textLayer);
    }

    if (ctx->undoStack()) ctx->undoStack()->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("계단평면 생성 완료."), CommandDock::Info);
        ctx->commandDock()->setPrompt(QString::fromUtf8(" Command : "));
    }

    cancelDrawing();
    emit toolFinished();
}

// ─────────────────────────────────────────────────────────────────────────────
// 설정 저장/불러오기
// ─────────────────────────────────────────────────────────────────────────────
StairPlanParams StairPlanTool::loadParams(MyGraphicsScene *scene)
{
    StairPlanParams p;
    ToolDefaults d("StairPlanTool", scene);
    p.up        = d.getInt("Up", p.up);
    p.down      = d.getInt("Down", p.down);
    p.tread     = d.getDouble("Tread", p.tread);
    p.land      = d.getDouble("Land", p.land);
    p.type      = d.getInt("Type", p.type);
    p.midSteps  = d.getInt("MidSteps", p.midSteps);
    p.treadLen  = d.getDouble("TreadLen", p.treadLen);
    p.numbering = d.getBool("Numbering", p.numbering);
    p.nonslip   = d.getBool("Nonslip", p.nonslip);
    p.layer     = d.getString("Layer", p.layer);
    p.nonslipLayer = d.getString("NonslipLayer", p.layer);
    return p;
}

void StairPlanTool::saveParams(MyGraphicsScene *scene, const StairPlanParams &p)
{
    ToolDefaults d("StairPlanTool", scene);
    d.put("Up", p.up);
    d.put("Down", p.down);
    d.put("Tread", p.tread);
    if (p.type != 2) d.put("Land", p.land);   // 타입2 의 계단참 길이는 자동 계산값이라 저장하지 않음
    d.put("Type", p.type);
    d.put("MidSteps", p.midSteps);
    d.put("TreadLen", p.treadLen);
    d.put("Numbering", p.numbering);
    d.put("Nonslip", p.nonslip);
    d.put("Layer", p.layer);
    d.put("NonslipLayer", p.nonslipLayer);
}

// ─────────────────────────────────────────────────────────────────────────────
// StairPlanCommand
// ─────────────────────────────────────────────────────────────────────────────
CommandBase *StairPlanCommand::createCommand(IAppContext *c)
{
    return new StairPlanCommand(c);
}

StairPlanCommand::StairPlanCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void StairPlanCommand::execute()
{
    if (!m_ctx) return;

    MyGraphicsScene *scene = m_ctx->activeScene();
    StairPlanParams p = StairPlanTool::loadParams(scene);

    m_ctx->startTool(new StairPlanTool(p));
}
