#include "twolinetool.h"
#include "twolinesetupdialog.h"
#include "drawingsettingscommand.h"
#include "multilinedata.h"

#include "cadcommands.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "lineitem.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QDebug>
#include <QLineF>
#include <QUuid>
#include <QPainterPath>
#include <QPen>
#include <QUndoStack>
#include <QtMath>

// ── static storage ──────────────────────────────────────────────────────────
double  TwoLineTool::s_spacing             = 200.0;
QString TwoLineTool::s_layerName           = QString();
bool    TwoLineTool::s_centerLineEnabled   = false;
QString TwoLineTool::s_centerLineLayerName = QString();
double  TwoLineTool::s_mortarSpacing       = 24.0;
QString TwoLineTool::s_mortarLayerName     = QString();
double  TwoLineTool::s_spaceSpacing        = 50.0;
QString TwoLineTool::s_spaceLayerName      = QString();
double  TwoLineTool::s_eccentricOffset[TwoLineTool::kModeCount] = {175.0, 175.0, 175.0, 175.0, 175.0, 175.0};  // 모드별 중심선 위치
double  TwoLineTool::s_innerWallThickness    = 200.0;  // 편심6-Line 안쪩 기본값
double  TwoLineTool::s_spaceThickness        = 50.0;   // 편심6-Line 공간 기본값
double  TwoLineTool::s_outerWallThickness    = 100.0;  // 편심6-Line 바깥쪩 기본값
bool    TwoLineTool::s_innerMortarEnabled[TwoLineTool::kModeCount] = {true, true, true, true, true, true};  // 모드별 내벽 몰타르 활성
bool    TwoLineTool::s_outerMortarEnabled[TwoLineTool::kModeCount] = {true, true, true, true, true, true};  // 모드별 외벽 몰타르 활성

double  TwoLineTool::globalSpacing()             { return s_spacing; }
void    TwoLineTool::setGlobalSpacing(double v)  { if (v > 0) s_spacing = v; }
QString TwoLineTool::globalLayerName()           { return s_layerName; }
void    TwoLineTool::setGlobalLayerName(const QString &n) { s_layerName = n; }
bool    TwoLineTool::globalCenterLineEnabled()   { return s_centerLineEnabled; }
void    TwoLineTool::setGlobalCenterLineEnabled(bool v) { s_centerLineEnabled = v; }
QString TwoLineTool::globalCenterLineLayerName() { return s_centerLineLayerName; }
void    TwoLineTool::setGlobalCenterLineLayerName(const QString &n) { s_centerLineLayerName = n; }
double  TwoLineTool::globalMortarSpacing()       { return s_mortarSpacing; }
void    TwoLineTool::setGlobalMortarSpacing(double v) { if (v > 0) s_mortarSpacing = v; }
QString TwoLineTool::globalMortarLayerName()     { return s_mortarLayerName; }
void    TwoLineTool::setGlobalMortarLayerName(const QString &n) { s_mortarLayerName = n; }
double  TwoLineTool::globalSpaceSpacing()        { return s_spaceSpacing; }
void    TwoLineTool::setGlobalSpaceSpacing(double v)  { if (v > 0) s_spaceSpacing = v; }
QString TwoLineTool::globalSpaceLayerName()      { return s_spaceLayerName; }
void    TwoLineTool::setGlobalSpaceLayerName(const QString &n)  { s_spaceLayerName = n; }
int TwoLineTool::modeKey(bool isFourLine, bool isSixLine, bool isEccentric)
{
    const int base = isSixLine ? 2 : (isFourLine ? 1 : 0);
    return (isEccentric ? 3 : 0) + base;
}
double  TwoLineTool::globalEccentricOffset(int mode)              { return s_eccentricOffset[mode]; }
void    TwoLineTool::setGlobalEccentricOffset(int mode, double v) { if (v >= 0) s_eccentricOffset[mode] = v; }
double  TwoLineTool::globalInnerWallThickness()           { return s_innerWallThickness; }
void    TwoLineTool::setGlobalInnerWallThickness(double v){ if (v > 0) s_innerWallThickness = v; }
double  TwoLineTool::globalSpaceThickness()               { return s_spaceThickness; }
void    TwoLineTool::setGlobalSpaceThickness(double v)    { if (v > 0) s_spaceThickness = v; }
double  TwoLineTool::globalOuterWallThickness()           { return s_outerWallThickness; }
void    TwoLineTool::setGlobalOuterWallThickness(double v){ if (v > 0) s_outerWallThickness = v; }
bool    TwoLineTool::globalInnerMortarEnabled(int mode)              { return s_innerMortarEnabled[mode]; }
void    TwoLineTool::setGlobalInnerMortarEnabled(int mode, bool v)   { s_innerMortarEnabled[mode] = v; }
bool    TwoLineTool::globalOuterMortarEnabled(int mode)              { return s_outerMortarEnabled[mode]; }
void    TwoLineTool::setGlobalOuterMortarEnabled(int mode, bool v)   { s_outerMortarEnabled[mode] = v; }

// ── [3-3] 도면 스코프 Setup 값 로드/저장 ─────────────────────────────────────
//   네임스페이스 "TwoLineTool". 하위 키 이름은 옛 file_metadata['multiline_setup']
//   JSON 필드명과 동일하게 맞춰 레거시 파일 이관이 단순 프리픽스 부착으로 끝난다.
void TwoLineTool::loadDocDefaults(MyGraphicsScene *scene)
{
    ToolDefaults d("TwoLineTool", scene);
    setGlobalSpacing(d.getDouble("spacing", 200.0));
    setGlobalLayerName(d.getString("layerName", QString()));
    setGlobalCenterLineEnabled(d.getBool("centerLineEnabled", false));
    setGlobalCenterLineLayerName(d.getString("centerLineLayerName", QString()));
    setGlobalMortarSpacing(d.getDouble("mortarSpacing", 24.0));
    setGlobalMortarLayerName(d.getString("mortarLayerName", QString()));
    setGlobalSpaceSpacing(d.getDouble("spaceSpacing", 50.0));
    setGlobalSpaceLayerName(d.getString("spaceLayerName", QString()));
    // 모드별(4/6/편심2/4/6) 중심선 위치·몰타르 활성은 따로 저장 (구버전 단일 키는 기본값으로 이관)
    for (int m = 0; m < kModeCount; ++m) {
        const QString k = QString::number(m);
        setGlobalEccentricOffset(m, d.getDouble("eccentricOffset_" + k,
                                                d.getDouble("eccentricOffset", 175.0)));
        setGlobalInnerMortarEnabled(m, d.getBool("innerMortarEnabled_" + k,
                                                 d.getBool("innerMortarEnabled", true)));
        setGlobalOuterMortarEnabled(m, d.getBool("outerMortarEnabled_" + k,
                                                 d.getBool("outerMortarEnabled", true)));
    }
    setGlobalInnerWallThickness(d.getDouble("innerWallThickness", 200.0));
    setGlobalSpaceThickness(d.getDouble("spaceThickness", 50.0));
    setGlobalOuterWallThickness(d.getDouble("outerWallThickness", 100.0));
}

void TwoLineTool::saveDocDefaults(MyGraphicsScene *scene)
{
    ToolDefaults d("TwoLineTool", scene);
    d.put("spacing",             globalSpacing());
    d.put("layerName",           globalLayerName());
    d.put("centerLineEnabled",   globalCenterLineEnabled());
    d.put("centerLineLayerName", globalCenterLineLayerName());
    d.put("mortarSpacing",       globalMortarSpacing());
    d.put("mortarLayerName",     globalMortarLayerName());
    d.put("spaceSpacing",        globalSpaceSpacing());
    d.put("spaceLayerName",      globalSpaceLayerName());
    for (int m = 0; m < kModeCount; ++m) {
        const QString k = QString::number(m);
        d.put("eccentricOffset_"    + k, globalEccentricOffset(m));
        d.put("innerMortarEnabled_" + k, globalInnerMortarEnabled(m));
        d.put("outerMortarEnabled_" + k, globalOuterMortarEnabled(m));
    }
    d.put("innerWallThickness",  globalInnerWallThickness());
    d.put("spaceThickness",      globalSpaceThickness());
    d.put("outerWallThickness",  globalOuterWallThickness());
}

// ── ctor / dtor ──────────────────────────────────────────────────────────────
TwoLineTool::TwoLineTool(bool isFourLine, bool isSixLine, bool isEccentric, QObject *parent)
    : AbstractTool(parent),
      m_isFourLine(isFourLine || isSixLine),  // 6-Line은 4-Line 포함
      m_isSixLine(isSixLine),
      m_isEccentric(isEccentric),
      m_isDrawing(false), m_pickMode(false), m_pickBoxPending(false)
{}

int    TwoLineTool::mode() const { return modeKey(m_isFourLine, m_isSixLine, m_isEccentric); }
double TwoLineTool::eccOffset() const { return s_eccentricOffset[mode()]; }
bool   TwoLineTool::innerMortarOn() const { return s_innerMortarEnabled[mode()]; }
bool   TwoLineTool::outerMortarOn() const { return s_outerMortarEnabled[mode()]; }

TwoLineTool::~TwoLineTool()
{
    cancelDrawing();
}

// ── activate / deactivate ────────────────────────────────────────────────────
void TwoLineTool::activate(MyGraphicsScene *scene)
{
    // [3-3] 매 활성화마다 현재 도면의 Setup 값으로 다시 채운다(도면 전환 반영).
    loadDocDefaults(scene);

    if (m_scene == scene)
        return;

    AbstractTool::activate(scene);
    m_isDrawing      = false;
    m_pickMode       = false;
    m_pickBoxPending = false;
    m_vertices.clear();
    m_segments.clear();
    m_pickPairs.clear();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(
            (m_isSixLine  && m_isEccentric) ? tr("편심6-LINE") :
            m_isSixLine                     ? "6-LINE" :
            (m_isFourLine && m_isEccentric) ? tr("편심4-LINE") :
            m_isFourLine                    ? "4-LINE" :
            m_isEccentric                   ? tr("편심2-LINE") : "2-LINE",
            CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(
            tr(" 시작 점을 지정하세요 또는 "), {"Setup", "Pick", "Auto"});
    }
}

void TwoLineTool::deactivate()
{
    cancelDrawing();
    AbstractTool::deactivate();
}

// ── mouse events ─────────────────────────────────────────────────────────────
void TwoLineTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();

    // ── Pick 모드 처리 ────────────────────────────────────────────────────────
    if (m_pickMode) {
        if (button == Qt::RightButton) {
            // 박스 선택 중이더라도 우클릭은 즉시 커밋 종료
            m_pickBoxPending = false;
            exitPickMode(true);
        } else if (button == Qt::LeftButton) {
            if (m_pickBoxPending) {
                // ── 2번째 클릭: 박스 선택 확정 ─────────────────────────────
                finishPickBoxSelection(pos);
            } else {
                LineItem *li = findLineItemNear(pos);
                if (li) {
                    // ── 단일 객체 픽 ────────────────────────────────────────
                    processPickItem(li);
                    if (ctx && ctx->commandDock())
                        ctx->commandDock()->logMessage(
                            tr("Pick: 누적 %1쌍.").arg(m_pickPairs.size()),
                            CommandDock::Info);
                } else {
                    // ── 빈 공간 클릭 → 박스 선택 시작 ──────────────────────
                    m_pickBoxPending = true;
                    m_pickBoxCorner  = pos;
                    if (ctx && ctx->commandDock())
                        ctx->commandDock()->logMessage(
                            tr("반대 모서리 지정 (좌→우: Window / 우→좌: Crossing)"),
                            CommandDock::Info);
                }
            }
        }
        return;
    }

    // ── 일반 그리기 모드 ──────────────────────────────────────────────────────
    if (button == Qt::RightButton) {
        if (m_isDrawing) {
            commitToUndoStack();
            if (ctx && ctx->commandDock()) {
                ctx->commandDock()->logMessage("Function finished.", CommandDock::Info);
                ctx->commandDock()->setPrompt("  Command : ");
            }
            emit toolFinished();
        }
        return;
    }

    if (button != Qt::LeftButton)
        return;

    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = m_isDrawing ? m_vertices.last() : QPointF();
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (!m_isDrawing) {
        m_isDrawing = true;
        m_vertices.clear();
        m_segments.clear();
        m_vertices.append(snapped);
        m_currentMousePos = snapped;

        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(
                tr(" 다음 점을 지정 하세요 또는 "), {"Undo", "Setup"});
        }
    } else {
        m_vertices.append(snapped);
        rebuildSegments(m_vertices);

        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->logMessage("2-Line segment added.", CommandDock::Info);
            showPrompt();
        }
    }
}

void TwoLineTool::mouseMove(QPointF pos)
{
    if (!m_scene)
        return;

    m_currentMousePos = pos;

    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;

    IAppContext *ctx = getContext();
    if (ctx && ctx->snapManager()) {
        QPointF base = m_isDrawing && !m_vertices.isEmpty()
                           ? m_vertices.last() : QPointF();
        auto result = ctx->snapManager()->snapPoint(pos, m_scene, base);
        finalPos  = result.snappedPos;
        snapType  = result.type;
    }

    // ── Pick 박스 선택 미리보기 ───────────────────────────────────────────────
    if (m_pickMode && m_pickBoxPending) {
        MyGraphicsScene::SelectionMode mode =
            (pos.x() > m_pickBoxCorner.x())
                ? MyGraphicsScene::Mode_Window
                : MyGraphicsScene::Mode_Crossing;
        m_scene->setSelectionRect(m_pickBoxCorner, pos, mode);
        m_scene->setSnapMarker(pos, snapType);
        return;
    }

    if (m_isDrawing && !m_vertices.isEmpty()) {
        updateGhostDouble(m_vertices, finalPos);
    }

    if (m_scene)
        m_scene->setSnapMarker(finalPos, snapType);
}

void TwoLineTool::mouseRelease(QPointF pos, Qt::MouseButton button)
{
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

// ── keyboard / command ────────────────────────────────────────────────────────
bool TwoLineTool::processCommand(const QString &command)
{
    QString cmd = command.toLower().trimmed();
    bool success = false;

    if (cmd == "s" || cmd == "setup") {
        IAppContext *ctx = getContext();
        QWidget *parentWin = ctx ? dynamic_cast<QWidget *>(ctx->commandDock()) : nullptr;
        TwoLineSetupDialog dlg(s_spacing, s_layerName,
                               s_centerLineEnabled,
                               m_isFourLine, s_mortarSpacing, s_mortarLayerName,
                               m_isSixLine,  s_spaceSpacing,  s_spaceLayerName,
                               m_isEccentric, eccOffset(),
                               s_innerWallThickness, s_spaceThickness, s_outerWallThickness,
                               innerMortarOn(), outerMortarOn(),
                               ctx ? ctx->layerManager() : nullptr, parentWin);
        if (dlg.exec() == QDialog::Accepted) {
            setGlobalLayerName(dlg.selectedLayerName());
            setGlobalCenterLineEnabled(dlg.centerLineEnabled());
            if (m_isFourLine) {
                setGlobalMortarSpacing(dlg.mortarSpacing());
                setGlobalMortarLayerName(dlg.mortarLayerName());
                setGlobalInnerMortarEnabled(mode(), dlg.innerMortarEnabled());
                setGlobalOuterMortarEnabled(mode(), dlg.outerMortarEnabled());
            }
            if (m_isEccentric && m_isSixLine) {
                // 편심6-Line: 안쪩/공간/바깥쪩 저장
                setGlobalInnerWallThickness(dlg.innerWallThickness());
                setGlobalSpaceThickness(dlg.spaceThickness());
                setGlobalOuterWallThickness(dlg.outerWallThickness());
            } else {
                // 일반 모드: 두 선 간격 사용
                setGlobalSpacing(dlg.spacing());
                if (m_isSixLine) {
                    setGlobalSpaceSpacing(dlg.spaceSpacing());
                    setGlobalSpaceLayerName(dlg.spaceLayerName());
                }
            }
            if (m_isEccentric)
                setGlobalEccentricOffset(mode(), dlg.eccentricOffset());
            saveDocDefaults(m_scene); // [3-3] 도면 스코프 + 전역 동시 기록
            if (m_isDrawing && m_vertices.size() >= 2)
                rebuildSegments(m_vertices);
        }
        success = true;
    } else if (cmd == "p" || cmd == "pick") {
        enterPickMode();
        success = true;
    } else if (cmd == "a" || cmd == "auto") {
        executeAuto();
        success = true;
    } else if (cmd == "u" || cmd == "undo") {
        success = undoLastVertex();
    } else if (cmd == "c" || cmd == "close") {
        closeLoop();
        success = true;
    } else {
        IAppContext *ctx = getContext();
        if (ctx && ctx->parser()) {
            QPointF base = m_isDrawing && !m_vertices.isEmpty()
                               ? m_vertices.last() : QPointF(0, 0);
            QPointF resultPos;
            bool isOrtho = ctx->snapManager() && ctx->snapManager()->isOrtho();
            if (ctx->parser()->parseCoordinate(cmd, base, m_currentMousePos,
                                               isOrtho, resultPos)) {
                mousePress(resultPos, Qt::LeftButton);
                success = true;
            }
        }
    }

    if (success)
        blockEnterKeyTemporarily(200);
    return success;
}

// ── onFinish / onCancel ───────────────────────────────────────────────────────
void TwoLineTool::onFinish()
{
    if (m_pickMode) {
        exitPickMode(true);
        return;
    }
    if (m_isDrawing) {
        commitToUndoStack();
        if (getContext() && getContext()->commandDock()) {
            getContext()->commandDock()->logMessage("2-Line tool finished.", CommandDock::Info);
            getContext()->commandDock()->setPrompt(" Command : ");
        }
    }
}

void TwoLineTool::onCancel()
{
    if (m_pickMode) {
        if (m_pickBoxPending) {
            // ESC: 박스 선택만 취소, Pick 모드는 유지
            m_pickBoxPending = false;
            if (m_scene) m_scene->clearOverlay();
            IAppContext *ctx = getContext();
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(tr("박스 선택 취소."), CommandDock::Info);
            return;
        }
        exitPickMode(false);
        return;
    }
    cancelDrawing();
    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->logMessage("*Cancel*", CommandDock::Info);
        getContext()->commandDock()->setPrompt(" Command : ");
    }
}

// ── geometry helpers ──────────────────────────────────────────────────────────

// Line-line intersection: P + t*d1 = Q + s*d2 → returns point, sets *ok.
// Cramer's rule: det = d2.x*d1.y - d1.x*d2.y
//                t   = (d2.x*dy  - dx*d2.y)  / det
QPointF TwoLineTool::lineLineIntersect(QPointF P, QPointF d1,
                                       QPointF Q, QPointF d2, bool *ok)
{
    double det = d2.x() * d1.y() - d1.x() * d2.y();
    if (qAbs(det) < 1e-10) {
        if (ok) *ok = false;
        return (P + Q) * 0.5;
    }
    double dx = Q.x() - P.x();
    double dy = Q.y() - P.y();
    double t  = (d2.x() * dy - dx * d2.y()) / det;   // 부호 수정
    if (ok) *ok = true;
    return P + t * d1;
}

// Build offset chain for a polygon-open path of n vertices.
// outLeft / outRight each have n points (start + mitered interior + end).
void TwoLineTool::computeOffsetChain(const QList<QPointF> &verts, double halfOff,
                                     QList<QPointF> &outLeft, QList<QPointF> &outRight)
{
    int n = verts.size();
    outLeft.clear();
    outRight.clear();

    if (n < 2) return;

    // Compute per-segment perpendicular unit vectors
    QList<QPointF> perps;
    perps.reserve(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        QPointF dir = verts[i + 1] - verts[i];
        double len = QLineF(verts[i], verts[i + 1]).length();
        if (len < 1e-10) {
            perps.append(i > 0 ? perps.last() : QPointF(0, 1));
        } else {
            dir /= len;
            // Perpendicular: rotate 90° CCW
            perps.append(QPointF(-dir.y(), dir.x()));
        }
    }

    // First vertex: square start
    outLeft.append(verts.first() + perps.first() * halfOff);
    outRight.append(verts.first() - perps.first() * halfOff);

    // Interior vertices: miter joint
    for (int i = 1; i < n - 1; ++i) {
        QPointF d1 = verts[i] - verts[i - 1];
        QPointF d2 = verts[i + 1] - verts[i];
        double len1 = QLineF(verts[i - 1], verts[i]).length();
        double len2 = QLineF(verts[i], verts[i + 1]).length();
        if (len1 < 1e-10 || len2 < 1e-10) {
            outLeft.append(verts[i] + perps[i] * halfOff);
            outRight.append(verts[i] - perps[i] * halfOff);
            continue;
        }
        d1 /= len1;
        d2 /= len2;

        // Left miter: intersection of left-offset lines of seg(i-1) and seg(i)
        bool ok;
        QPointF mL = lineLineIntersect(
            verts[i - 1] + perps[i - 1] * halfOff, d1,
            verts[i]     + perps[i]     * halfOff, d2, &ok);
        if (!ok) mL = verts[i] + perps[i] * halfOff;

        QPointF mR = lineLineIntersect(
            verts[i - 1] - perps[i - 1] * halfOff, d1,
            verts[i]     - perps[i]     * halfOff, d2, &ok);
        if (!ok) mR = verts[i] - perps[i] * halfOff;

        outLeft.append(mL);
        outRight.append(mR);
    }

    // Last vertex: square end
    outLeft.append(verts.last() + perps.last() * halfOff);
    outRight.append(verts.last() - perps.last() * halfOff);
}

// 비대칭 오프셋 체인: left쪽 +leftOff, right쪽 -rightOff (편심2-Line 전용).
// 미터 조인트 계산을 각 방향에 대해 독립적으로 적용한다.
void TwoLineTool::computeAsymmetricOffsetChain(const QList<QPointF> &verts,
                                               double leftOff, double rightOff,
                                               QList<QPointF> &outLeft,
                                               QList<QPointF> &outRight)
{
    int n = verts.size();
    outLeft.clear();
    outRight.clear();

    if (n < 2) return;

    // 세그먼트별 수직 단위벡터 계산 (90° CCW)
    QList<QPointF> perps;
    perps.reserve(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        QPointF dir = verts[i + 1] - verts[i];
        double len = QLineF(verts[i], verts[i + 1]).length();
        if (len < 1e-10) {
            perps.append(i > 0 ? perps.last() : QPointF(0, 1));
        } else {
            dir /= len;
            perps.append(QPointF(-dir.y(), dir.x()));
        }
    }

    // 첫 꼭짓점: square start
    outLeft.append(verts.first() + perps.first() * leftOff);
    outRight.append(verts.first() - perps.first() * rightOff);

    // 내부 꼭짓점: 좌/우 미터 조인트를 각 오프셋으로 독립 계산
    for (int i = 1; i < n - 1; ++i) {
        QPointF d1 = verts[i] - verts[i - 1];
        QPointF d2 = verts[i + 1] - verts[i];
        double len1 = QLineF(verts[i - 1], verts[i]).length();
        double len2 = QLineF(verts[i], verts[i + 1]).length();
        if (len1 < 1e-10 || len2 < 1e-10) {
            outLeft.append(verts[i] + perps[i] * leftOff);
            outRight.append(verts[i] - perps[i] * rightOff);
            continue;
        }
        d1 /= len1;
        d2 /= len2;

        bool ok;
        QPointF mL = lineLineIntersect(
            verts[i - 1] + perps[i - 1] * leftOff, d1,
            verts[i]     + perps[i]     * leftOff, d2, &ok);
        if (!ok) mL = verts[i] + perps[i] * leftOff;

        QPointF mR = lineLineIntersect(
            verts[i - 1] - perps[i - 1] * rightOff, d1,
            verts[i]     - perps[i]     * rightOff, d2, &ok);
        if (!ok) mR = verts[i] - perps[i] * rightOff;

        outLeft.append(mL);
        outRight.append(mR);
    }

    // 마지막 꼭짓점: square end
    outLeft.append(verts.last() + perps.last() * leftOff);
    outRight.append(verts.last() - perps.last() * rightOff);
}

// ── segment management ────────────────────────────────────────────────────────

QPen TwoLineTool::buildLayerPen(const QString &layerNameOverride) const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->layerManager()) return QPen(Qt::white);

    LayerManager *lm = ctx->layerManager();
    const QString &pick = layerNameOverride.isEmpty() ? s_layerName : layerNameOverride;
    Layer *curLayer = (!pick.isEmpty()) ? lm->getLayer(pick) : nullptr;
    if (!curLayer) curLayer = lm->getCurrentLayer();
    QPen pen(curLayer->color());
    pen.setWidthF(curLayer->weight());

    QString lt = curLayer->linetype();
    if (lt == "Continuous" || lt == "Continous") {
        pen.setStyle(Qt::SolidLine);
    } else {
        pen.setStyle(Qt::CustomDashLine);
        if (ctx->lineTypeManager()) {
            QVector<qreal> pat = ctx->lineTypeManager()->getQtDashPattern(lt);
            if (!pat.isEmpty())
                pen.setDashPattern(pat);
            else
                pen.setStyle(Qt::SolidLine);
        }
    }
    return pen;
}

void TwoLineTool::removeAllSegmentsFromScene()
{
    if (!m_scene) return;
    for (SegmentPair &sp : m_segments) {
        if (sp.left)        { m_scene->removeItem(sp.left);        delete sp.left;        sp.left        = nullptr; }
        if (sp.right)       { m_scene->removeItem(sp.right);       delete sp.right;       sp.right       = nullptr; }
        if (sp.center)      { m_scene->removeItem(sp.center);      delete sp.center;      sp.center      = nullptr; }
        if (sp.mortarLeft)  { m_scene->removeItem(sp.mortarLeft);  delete sp.mortarLeft;  sp.mortarLeft  = nullptr; }
        if (sp.mortarRight) { m_scene->removeItem(sp.mortarRight); delete sp.mortarRight; sp.mortarRight = nullptr; }
        if (sp.spaceLeft)   { m_scene->removeItem(sp.spaceLeft);   delete sp.spaceLeft;   sp.spaceLeft   = nullptr; }
        if (sp.spaceRight)  { m_scene->removeItem(sp.spaceRight);  delete sp.spaceRight;  sp.spaceRight  = nullptr; }
    }
    m_segments.clear();
}

// ── 편심6-Line 전용 세그먼트 재생성 ──────────────────────────────────────────
// 기하 공식:
//   내벽선   = 기준선 - ecc               (- 방향)
//   공간내벽 = 기준선 + (안쪩 - ecc) = +R1 (+ 방향)
//   공간외벽 = 기준선 + R1 + 공간 = +R2    (+ 방향)
//   외벽선   = 기준선 + R2 + 바깥쪩 = +R3  (+ 방향)
//   몰타르내벽 = 기준선 - (ecc+mortar)      (- 방향, 활성 시)
//   몰타르외벽 = 기준선 + (R3+mortar)       (+ 방향, 활성 시)
void TwoLineTool::rebuildSegmentsEccentric6Line(const QList<QPointF> &vertices)
{
    if (!m_scene || vertices.size() < 2) {
        removeAllSegmentsFromScene();
        return;
    }
    removeAllSegmentsFromScene();

    const double ecc  = eccOffset();
    const double R1   = s_innerWallThickness - ecc;   // 공간내벽 오프셋
    const double R2   = R1 + s_spaceThickness;         // 공간외벽 오프셋
    const double R3   = R2 + s_outerWallThickness;     // 외벽 오프셋
    const double mOff = s_mortarSpacing;

    // ── 각 오프셋별 체인 계산 ───────────────────────────────────────────────────
    // computeOffsetChain(verts, off, outLeft=+방향, outRight=-방향)
    QList<QPointF> dummy;   // 사용하지 않는 방향의 임시 버퍼

    // 내벽선: -ecc 방향 → outRight 취득
    QList<QPointF> innPts;
    computeOffsetChain(vertices, ecc, dummy, innPts);

    // 공간내벽: +R1 방향 → outLeft 취득
    QList<QPointF> sInPts;
    computeOffsetChain(vertices, R1, sInPts, dummy);

    // 공간외벽: +R2 방향 → outLeft 취득
    QList<QPointF> sOutPts;
    computeOffsetChain(vertices, R2, sOutPts, dummy);

    // 외벽선: +R3 방향 → outLeft 취득
    QList<QPointF> outerPts;
    computeOffsetChain(vertices, R3, outerPts, dummy);

    // 몰타르내벽: -(ecc+mOff) 방향 → outRight 취득 (활성 시)
    QList<QPointF> mInPts;
    if (innerMortarOn())
        computeOffsetChain(vertices, ecc + mOff, dummy, mInPts);

    // 몰타르외벽: +(R3+mOff) 방향 → outLeft 취득 (활성 시)
    QList<QPointF> mOutPts;
    if (outerMortarOn())
        computeOffsetChain(vertices, R3 + mOff, mOutPts, dummy);

    // ── 닫힌 루프: P0 미터 조인트 패치 ─────────────────────────────────────────
    // closeLoop()가 vertices = [P0,..,Pn-1,P0] 형태로 호출함.
    // computeOffsetChain은 오픈 패스로 계산하므로 P0에서의 미터 조인트를 수동 패치.
    const int n = vertices.size();
    if (n >= 4 && QLineF(vertices.first(), vertices.last()).length() < 0.001) {
        QPointF dIn  = vertices[n - 1] - vertices[n - 2];
        QPointF dOut = vertices[1]     - vertices[0];
        const double lIn  = QLineF(vertices[n - 2], vertices[n - 1]).length();
        const double lOut = QLineF(vertices[0],     vertices[1]).length();

        if (lIn > 1e-10 && lOut > 1e-10) {
            dIn  /= lIn;
            dOut /= lOut;
            QPointF perpIn (-dIn.y(),  dIn.x());
            QPointF perpOut(-dOut.y(), dOut.x());
            bool ok;

            // 내벽선 (- 방향, ecc)
            {
                QPointF m = lineLineIntersect(
                    vertices[n-2] - perpIn  * ecc, dIn,
                    vertices[0]   - perpOut * ecc, dOut, &ok);
                if (!ok) m = vertices[0] - perpOut * ecc;
                innPts.first() = m; innPts.last() = m;
            }
            // 공간내벽 (+ 방향, R1)
            {
                QPointF m = lineLineIntersect(
                    vertices[n-2] + perpIn  * R1, dIn,
                    vertices[0]   + perpOut * R1, dOut, &ok);
                if (!ok) m = vertices[0] + perpOut * R1;
                sInPts.first() = m; sInPts.last() = m;
            }
            // 공간외벽 (+ 방향, R2)
            {
                QPointF m = lineLineIntersect(
                    vertices[n-2] + perpIn  * R2, dIn,
                    vertices[0]   + perpOut * R2, dOut, &ok);
                if (!ok) m = vertices[0] + perpOut * R2;
                sOutPts.first() = m; sOutPts.last() = m;
            }
            // 외벽선 (+ 방향, R3)
            {
                QPointF m = lineLineIntersect(
                    vertices[n-2] + perpIn  * R3, dIn,
                    vertices[0]   + perpOut * R3, dOut, &ok);
                if (!ok) m = vertices[0] + perpOut * R3;
                outerPts.first() = m; outerPts.last() = m;
            }
            // 몰타르내벽 (- 방향, ecc+mOff)
            if (innerMortarOn() && !mInPts.isEmpty()) {
                QPointF m = lineLineIntersect(
                    vertices[n-2] - perpIn  * (ecc + mOff), dIn,
                    vertices[0]   - perpOut * (ecc + mOff), dOut, &ok);
                if (!ok) m = vertices[0] - perpOut * (ecc + mOff);
                mInPts.first() = m; mInPts.last() = m;
            }
            // 몰타르외벽 (+ 방향, R3+mOff)
            if (outerMortarOn() && !mOutPts.isEmpty()) {
                QPointF m = lineLineIntersect(
                    vertices[n-2] + perpIn  * (R3 + mOff), dIn,
                    vertices[0]   + perpOut * (R3 + mOff), dOut, &ok);
                if (!ok) m = vertices[0] + perpOut * (R3 + mOff);
                mOutPts.first() = m; mOutPts.last() = m;
            }
        }
    }

    // ── 펜·레이어 결정 ──────────────────────────────────────────────────────────
    IAppContext *ctx = getContext();
    QPen pen       = buildLayerPen();
    const QString layerName  = s_layerName.isEmpty() ? currentLayerName() : s_layerName;
    const QString clLayerName = DrawingSettingsCommand::s_centerLineLayer.isEmpty()
                                    ? currentLayerName() : DrawingSettingsCommand::s_centerLineLayer;
    const QString mLayerName  = s_mortarLayerName.isEmpty()
                                    ? currentLayerName() : s_mortarLayerName;
    QPen clPen = s_centerLineEnabled ? buildLayerPen(DrawingSettingsCommand::s_centerLineLayer) : QPen();
    QPen mPen  = buildLayerPen(s_mortarLayerName);

    int segCount = vertices.size() - 1;
    m_segments.reserve(segCount);

    for (int i = 0; i < segCount; ++i) {
        SegmentPair sp;
        if (ctx && ctx->entityFactory()) {
            auto addLine = [&](const QLineF &line, const QPen &p,
                               const QString &lname) -> LineItem * {
                QGraphicsItem *item = ctx->entityFactory()->createLine(line, p);
                LineItem *li = dynamic_cast<LineItem *>(item);
                if (li) {
                    li->setLayerName(lname);
                    li->setParentItem(m_scene->getLayerRoot(lname));
                    if (!li->scene()) m_scene->addItem(li);
                }
                return li;
            };

            // 내벽선 → sp.right  (- 방향 관례)
            sp.right = addLine(QLineF(innPts[i],   innPts[i+1]),   pen, layerName);
            // 외벽선 → sp.left   (+ 방향 관례)
            sp.left  = addLine(QLineF(outerPts[i], outerPts[i+1]), pen, layerName);
            // 공간내벽 → sp.spaceLeft
            sp.spaceLeft  = addLine(QLineF(sInPts[i],  sInPts[i+1]),  pen, layerName);
            // 공간외벽 → sp.spaceRight
            sp.spaceRight = addLine(QLineF(sOutPts[i], sOutPts[i+1]), pen, layerName);

            // 중심선: 편심 모드에서는 기준 경로(vertices) 자체
            if (s_centerLineEnabled)
                sp.center = addLine(QLineF(vertices[i], vertices[i+1]), clPen, clLayerName);

            // 몰타르내벽 (innerMortarOn()=true 시)
            if (innerMortarOn() && !mInPts.isEmpty())
                sp.mortarLeft  = addLine(QLineF(mInPts[i],  mInPts[i+1]),  mPen, mLayerName);
            // 몰타르외벽 (outerMortarOn()=true 시)
            if (outerMortarOn() && !mOutPts.isEmpty())
                sp.mortarRight = addLine(QLineF(mOutPts[i], mOutPts[i+1]), mPen, mLayerName);
        }
        m_segments.append(sp);
    }
}

void TwoLineTool::rebuildSegments(const QList<QPointF> &vertices)
{
    if (!m_scene || vertices.size() < 2) {
        removeAllSegmentsFromScene();
        return;
    }

    // 편심6-Line은 독립된 기하를 사용하므로 전용 함수로 위임
    if (m_isEccentric && m_isSixLine) {
        rebuildSegmentsEccentric6Line(vertices);
        return;
    }

    removeAllSegmentsFromScene();

    double halfOff = s_spacing / 2.0;

    // 편심 모드에서는 left/right 오프셋을 비대칭으로 적용
    // 편심6-Line 기준: 기준 경로에서 내벽선까지 ecc가 -방향(right)이다.
    const double eccLeftOff  = m_isEccentric ? (s_spacing - eccOffset()) : halfOff;
    const double eccRightOff = m_isEccentric ? eccOffset()               : halfOff;

    QList<QPointF> leftPts, rightPts;
    if (m_isEccentric)
        computeAsymmetricOffsetChain(vertices, eccLeftOff, eccRightOff, leftPts, rightPts);
    else
        computeOffsetChain(vertices, halfOff, leftPts, rightPts);

    // 닫힌 루프: 시작점(P0)에서도 미터 조인트 계산
    // closeLoop()가 vertices = [P0, P1, ..., Pn-1, P0] 형태로 전달하므로
    // 첫 번째 점과 마지막 점이 같을 때 P0의 미터를 수정한다.
    const int n = vertices.size();
    if (n >= 4 && QLineF(vertices.first(), vertices.last()).length() < 0.001) {
        // 들어오는 방향: 마지막 세그먼트 Pn-2 → P0
        QPointF dIn  = vertices[n - 1] - vertices[n - 2];
        double  lIn  = QLineF(vertices[n - 2], vertices[n - 1]).length();
        // 나가는 방향: 첫 번째 세그먼트 P0 → P1
        QPointF dOut = vertices[1] - vertices[0];
        double  lOut = QLineF(vertices[0], vertices[1]).length();

        if (lIn > 1e-10 && lOut > 1e-10) {
            dIn  /= lIn;
            dOut /= lOut;
            QPointF perpIn (-dIn.y(),  dIn.x());
            QPointF perpOut(-dOut.y(), dOut.x());

            bool ok;
            // 편심 모드와 대칭 모드 모두 eccLeftOff / eccRightOff 사용
            QPointF mL = lineLineIntersect(
                vertices[n - 2] + perpIn  * eccLeftOff, dIn,
                vertices[0]     + perpOut * eccLeftOff, dOut, &ok);
            if (!ok) mL = vertices[0] + perpOut * eccLeftOff;

            QPointF mR = lineLineIntersect(
                vertices[n - 2] - perpIn  * eccRightOff, dIn,
                vertices[0]     - perpOut * eccRightOff, dOut, &ok);
            if (!ok) mR = vertices[0] - perpOut * eccRightOff;

            // 첫 번째 세그먼트 시작점과 마지막 세그먼트 끝점을 미터 점으로 교체
            leftPts.first()  = mL;   rightPts.first()  = mR;
            leftPts.last()   = mL;   rightPts.last()   = mR;
        }
    }

    // ── 몰타르선 오프셋 체인 (4-Line 전용) ──────────────────────────────────────
    // 편심 모드에서는 내벽/외벽 오프셋에 mortarSpacing을 각각 더해 비대칭 배치
    const double   mortarLeftOff  = eccLeftOff  + s_mortarSpacing;
    const double   mortarRightOff = eccRightOff + s_mortarSpacing;
    const QString  mLayerName     = s_mortarLayerName.isEmpty()
                                        ? currentLayerName() : s_mortarLayerName;
    QList<QPointF> mLPts, mRPts;
    if (m_isFourLine) {
        if (m_isEccentric)
            computeAsymmetricOffsetChain(vertices, mortarLeftOff, mortarRightOff, mLPts, mRPts);
        else
            computeOffsetChain(vertices, mortarLeftOff, mLPts, mRPts);  // 대칭 모드: 둘이 동일

        // 닫힌 루프: 몰타르선도 동일 패치 적용
        if (n >= 4 && QLineF(vertices.first(), vertices.last()).length() < 0.001) {
            QPointF dIn  = vertices[n - 1] - vertices[n - 2];
            QPointF dOut = vertices[1]     - vertices[0];
            double  lIn  = QLineF(vertices[n - 2], vertices[n - 1]).length();
            double  lOut = QLineF(vertices[0],     vertices[1]).length();
            if (lIn > 1e-10 && lOut > 1e-10) {
                dIn  /= lIn;  dOut /= lOut;
                QPointF perpIn (-dIn.y(),  dIn.x());
                QPointF perpOut(-dOut.y(), dOut.x());
                bool ok;
                QPointF mmL = lineLineIntersect(
                    vertices[n - 2] + perpIn  * mortarLeftOff, dIn,
                    vertices[0]     + perpOut * mortarLeftOff, dOut, &ok);
                if (!ok) mmL = vertices[0] + perpOut * mortarLeftOff;
                QPointF mmR = lineLineIntersect(
                    vertices[n - 2] - perpIn  * mortarRightOff, dIn,
                    vertices[0]     - perpOut * mortarRightOff, dOut, &ok);
                if (!ok) mmR = vertices[0] - perpOut * mortarRightOff;
                mLPts.first() = mmL;  mRPts.first() = mmR;
                mLPts.last()  = mmL;  mRPts.last()  = mmR;
            }
        }
    }

    // ── 공간선 오프셋 체인 (6-Line 전용) ─────────────────────────────────────────
    // 편심 모드: 공간선을 편심 비율로 비례 배분하여 spaceSpacing 총합을 보존.
    //   spaceLeftOff  = spaceSpacing × eccLeftOff  / spacing
    //   spaceRightOff = spaceSpacing × eccRightOff / spacing
    // 대칭 모드(eccLeftOff == eccRightOff == halfOff):
    //   spaceLeftOff = spaceRightOff = spaceSpacing/2 (= spaceOff) → 기존과 동일
    const double   spaceOff      = s_spaceSpacing / 2.0;
    const double   spaceLeftOff  = (s_spacing > 1e-10)
                                       ? s_spaceSpacing * eccLeftOff  / s_spacing : spaceOff;
    const double   spaceRightOff = (s_spacing > 1e-10)
                                       ? s_spaceSpacing * eccRightOff / s_spacing : spaceOff;
    const QString  sLayerName    = s_spaceLayerName.isEmpty()
                                       ? (s_layerName.isEmpty() ? currentLayerName() : s_layerName)
                                       : s_spaceLayerName;
    QList<QPointF> sLPts, sRPts;
    if (m_isSixLine) {
        if (m_isEccentric)
            computeAsymmetricOffsetChain(vertices, spaceLeftOff, spaceRightOff, sLPts, sRPts);
        else
            computeOffsetChain(vertices, spaceOff, sLPts, sRPts);

        // 닫힌 루프: 공간선도 동일 패치 적용
        if (n >= 4 && QLineF(vertices.first(), vertices.last()).length() < 0.001) {
            QPointF dIn  = vertices[n - 1] - vertices[n - 2];
            QPointF dOut = vertices[1]     - vertices[0];
            double  lIn  = QLineF(vertices[n - 2], vertices[n - 1]).length();
            double  lOut = QLineF(vertices[0],     vertices[1]).length();
            if (lIn > 1e-10 && lOut > 1e-10) {
                dIn  /= lIn;  dOut /= lOut;
                QPointF perpIn (-dIn.y(),  dIn.x());
                QPointF perpOut(-dOut.y(), dOut.x());
                bool ok;
                QPointF msL = lineLineIntersect(
                    vertices[n - 2] + perpIn  * spaceLeftOff, dIn,
                    vertices[0]     + perpOut * spaceLeftOff, dOut, &ok);
                if (!ok) msL = vertices[0] + perpOut * spaceLeftOff;
                QPointF msR = lineLineIntersect(
                    vertices[n - 2] - perpIn  * spaceRightOff, dIn,
                    vertices[0]     - perpOut * spaceRightOff, dOut, &ok);
                if (!ok) msR = vertices[0] - perpOut * spaceRightOff;
                sLPts.first() = msL;  sRPts.first() = msR;
                sLPts.last()  = msL;  sRPts.last()  = msR;
            }
        }
    }

    // ── 펜·레이어 결정 ────────────────────────────────────────────────────────
    IAppContext *ctx = getContext();
    QPen pen = buildLayerPen();
    QString layerName = s_layerName.isEmpty() ? currentLayerName() : s_layerName;

    const bool hasCenterLine = s_centerLineEnabled;
    const QString clLayerName = DrawingSettingsCommand::s_centerLineLayer.isEmpty()
                                    ? currentLayerName() : DrawingSettingsCommand::s_centerLineLayer;
    QPen clPen = hasCenterLine ? buildLayerPen(DrawingSettingsCommand::s_centerLineLayer) : QPen();
    QPen mPen  = m_isFourLine  ? buildLayerPen(s_mortarLayerName)     : QPen();
    QPen sPen  = m_isSixLine   ? buildLayerPen(s_spaceLayerName)      : QPen();

    int segCount = vertices.size() - 1;
    m_segments.reserve(segCount);

    for (int i = 0; i < segCount; ++i) {
        SegmentPair sp;

        if (ctx && ctx->entityFactory()) {
            auto addLine = [&](const QLineF &line, const QPen &p,
                               const QString &lname) -> LineItem * {
                QGraphicsItem *item = ctx->entityFactory()->createLine(line, p);
                LineItem *li = dynamic_cast<LineItem *>(item);
                if (li) {
                    li->setLayerName(lname);
                    li->setParentItem(m_scene->getLayerRoot(lname));
                    if (!li->scene()) m_scene->addItem(li);
                }
                return li;
            };

            sp.left  = addLine(QLineF(leftPts[i],  leftPts[i + 1]),  pen, layerName);
            sp.right = addLine(QLineF(rightPts[i], rightPts[i + 1]), pen, layerName);

            if (hasCenterLine) {
                QPointF cA, cB;
                if (m_isEccentric) {
                    // 편심 모드: 중심선은 사용자가 지정한 기준 경로(vertices) 자체
                    cA = vertices[i];
                    cB = vertices[i + 1];
                } else {
                    // 대칭 모드: left/right 중간점
                    cA = (leftPts[i]     + rightPts[i])     / 2.0;
                    cB = (leftPts[i + 1] + rightPts[i + 1]) / 2.0;
                }
                sp.center = addLine(QLineF(cA, cB), clPen, clLayerName);
            }

            if (m_isFourLine) {
                // 편심6-Line 기준(전 모드 공통): 내벽 = right(-방향), 외벽 = left(+방향)
                const bool leftOn  = outerMortarOn();
                const bool rightOn = innerMortarOn();
                if (leftOn)
                    sp.mortarLeft  = addLine(QLineF(mLPts[i],  mLPts[i + 1]),  mPen, mLayerName);
                if (rightOn)
                    sp.mortarRight = addLine(QLineF(mRPts[i],  mRPts[i + 1]),  mPen, mLayerName);
            }

            if (m_isSixLine) {
                sp.spaceLeft  = addLine(QLineF(sLPts[i], sLPts[i + 1]), sPen, sLayerName);
                sp.spaceRight = addLine(QLineF(sRPts[i], sRPts[i + 1]), sPen, sLayerName);
            }
        }
        m_segments.append(sp);
    }
}

void TwoLineTool::updateGhostDouble(const QList<QPointF> &vertices, const QPointF &mousePos)
{
    if (!m_scene || vertices.isEmpty()) {
        if (m_scene) m_scene->setGhostPath(QPainterPath(), false);
        return;
    }

    QPointF lastVtx = vertices.last();
    double len = QLineF(lastVtx, mousePos).length();
    if (len < 1e-6) {
        m_scene->setGhostPath(QPainterPath(), false);
        return;
    }

    QPointF dir  = (mousePos - lastVtx) / len;
    QPointF perp = QPointF(-dir.y(), dir.x());   // 90° CCW
    double halfOff = s_spacing / 2.0;

    // 편심 모드에서는 좌/우 오프셋을 비대칭으로 적용
    const double ghostLeftOff  = m_isEccentric ? (s_spacing - eccOffset()) : halfOff;
    const double ghostRightOff = m_isEccentric ? eccOffset()               : halfOff;

    QPainterPath path;

    if (m_isEccentric && m_isSixLine) {
        // 편심6-Line 고스트: 내벽선/공간내벽/공간외벽/외벽선 4개 표시
        const double ecc6  = eccOffset();
        const double R1    = s_innerWallThickness - ecc6;
        const double R2    = R1 + s_spaceThickness;
        const double R3    = R2 + s_outerWallThickness;
        path.moveTo(lastVtx  - perp * ecc6); path.lineTo(mousePos - perp * ecc6);
        path.moveTo(lastVtx  + perp * R1);   path.lineTo(mousePos + perp * R1);
        path.moveTo(lastVtx  + perp * R2);   path.lineTo(mousePos + perp * R2);
        path.moveTo(lastVtx  + perp * R3);   path.lineTo(mousePos + perp * R3);
    } else {
        // 일반/편심2/4-Line 고스트: 평행 두 선
        path.moveTo(lastVtx  + perp * ghostLeftOff);
        path.lineTo(mousePos + perp * ghostLeftOff);
        path.moveTo(lastVtx  - perp * ghostRightOff);
        path.lineTo(mousePos - perp * ghostRightOff);
    }

    updateGhostPreview(path, currentLayerName());
}

// ── tool actions ─────────────────────────────────────────────────────────────

void TwoLineTool::commitToUndoStack()
{
    if (!m_scene || m_segments.isEmpty()) {
        removeAllSegmentsFromScene();
        m_vertices.clear();
        m_segments.clear();
        m_isDrawing = false;
        if (m_scene) m_scene->clearOverlay();
        return;
    }

    IAppContext *ctx = getContext();
    if (ctx && ctx->undoStack()) {
        // 한 번의 그리기 = 하나의 그룹 ID (L-코너 정리용 태깅)
        const QString gid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto tag = [&](LineItem *li, const char *role) {
            if (!li) return;
            li->setData(ML_DATA_GROUP_ID, gid);
            li->setData(ML_DATA_ROLE,     QString(role));
        };

        ctx->undoStack()->beginMacro("Draw 2-Line");
        for (SegmentPair &sp : m_segments) {
            tag(sp.left,        "outer");
            tag(sp.right,       "outer");
            tag(sp.center,      "center");
            tag(sp.mortarLeft,  "mortar");
            tag(sp.mortarRight, "mortar");
            tag(sp.spaceLeft,   "space");
            tag(sp.spaceRight,  "space");

            // 전체 폭: 몰타르선이 outer 바깥에 위치하므로 +2*mortarSpacing
            const double totalWidth = (sp.mortarLeft || sp.mortarRight)
                ? (s_spacing + 2.0 * s_mortarSpacing)
                : s_spacing;

            for (LineItem *li : {sp.left, sp.right, sp.center,
                                 sp.mortarLeft, sp.mortarRight,
                                 sp.spaceLeft,  sp.spaceRight}) {
                if (!li) continue;
                li->setData(ML_DATA_SPACING, totalWidth);
                if (li->scene() == m_scene)
                    m_scene->removeItem(li);
                ctx->undoStack()->push(new AddEntityCommand(m_scene, li));
            }
            sp = SegmentPair{};
        }
        ctx->undoStack()->endMacro();
    }

    m_segments.clear();
    m_vertices.clear();
    m_isDrawing = false;

    if (m_scene)
        m_scene->clearOverlay();
}

void TwoLineTool::cancelDrawing()
{
    removeAllSegmentsFromScene();
    m_vertices.clear();
    m_isDrawing = false;

    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
}

bool TwoLineTool::undoLastVertex()
{
    if (m_vertices.size() <= 1) {
        cancelDrawing();
        return false;
    }

    m_vertices.removeLast();

    if (m_vertices.size() >= 2) {
        rebuildSegments(m_vertices);
        updateGhostDouble(m_vertices, m_currentMousePos);
    } else {
        removeAllSegmentsFromScene();
        if (m_scene) m_scene->setGhostPath(QPainterPath(), false);
    }

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage("Undo.", CommandDock::Info);

    return true;
}

void TwoLineTool::closeLoop()
{
    if (m_vertices.size() < 3 || !m_isDrawing)
        return;

    // Add first point to close the loop
    if (QLineF(m_vertices.last(), m_vertices.first()).length() > 0.001) {
        m_vertices.append(m_vertices.first());
        rebuildSegments(m_vertices);
    }

    commitToUndoStack();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage("Loop closed.", CommandDock::Info);
        ctx->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void TwoLineTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;

    QStringList opts;
    opts << "Undo" << "Setup" << "Pick" << "Auto";
    if (m_vertices.size() >= 3)
        opts << "Close";
    ctx->commandDock()->showInteractivePrompt(tr(" 다음 점을 지정 하세요 또는 "), opts);
}

// ── commitPairs ──────────────────────────────────────────────────────────────

void TwoLineTool::commitPairs(QList<SegmentPair> &pairs, const QString &macroName)
{
    if (!m_scene || pairs.isEmpty()) {
        pairs.clear();
        return;
    }
    IAppContext *ctx = getContext();
    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->beginMacro(macroName);
        for (SegmentPair &sp : pairs) {
            // Pick/Auto: 선택된 중심선 하나당 독립 그룹 ID
            const QString gid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            auto tag = [&](LineItem *li, const char *role) {
                if (!li) return;
                li->setData(ML_DATA_GROUP_ID, gid);
                li->setData(ML_DATA_ROLE,     QString(role));
            };
            tag(sp.left,        "outer");
            tag(sp.right,       "outer");
            tag(sp.center,      "center");
            tag(sp.mortarLeft,  "mortar");
            tag(sp.mortarRight, "mortar");
            tag(sp.spaceLeft,   "space");
            tag(sp.spaceRight,  "space");

            const double totalWidth = (sp.mortarLeft || sp.mortarRight)
                ? (s_spacing + 2.0 * s_mortarSpacing)
                : s_spacing;

            for (LineItem *li : {sp.left, sp.right, sp.center,
                                 sp.mortarLeft, sp.mortarRight,
                                 sp.spaceLeft,  sp.spaceRight}) {
                if (!li) continue;
                li->setData(ML_DATA_SPACING, totalWidth);
                if (li->scene() == m_scene)
                    m_scene->removeItem(li);
                ctx->undoStack()->push(new AddEntityCommand(m_scene, li));
            }
            sp = SegmentPair{};
        }
        ctx->undoStack()->endMacro();
    }
    pairs.clear();
}

// ── generateOffsetPairFromLine ────────────────────────────────────────────────
// 단일 중심선 QLineF → 좌우 두 평행선 생성 (중심선은 그대로 유지)

void TwoLineTool::generateOffsetPairFromLine(const QLineF &center,
                                              const QString &layerName,
                                              const QPen   &pen,
                                              SegmentPair  &out)
{
    out = SegmentPair{};
    if (!m_scene) return;

    double len = center.length();
    if (len < 1e-6) return;

    QPointF dir  = (center.p2() - center.p1()) / len;
    QPointF perp(-dir.y(), dir.x());              // 90° CCW
    double halfOff = s_spacing / 2.0;

    // 편심 모드에서는 좌/우 오프셋을 비대칭으로 적용
    const double pairLeftOff  = m_isEccentric ? (s_spacing - eccOffset()) : halfOff;
    const double pairRightOff = m_isEccentric ? eccOffset()               : halfOff;

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->entityFactory()) return;

    auto addLine = [&](const QLineF &ln) -> LineItem * {
        QGraphicsItem *item = ctx->entityFactory()->createLine(ln, pen);
        LineItem *li = dynamic_cast<LineItem *>(item);
        if (li) {
            li->setLayerName(layerName);
            li->setParentItem(m_scene->getLayerRoot(layerName));
            if (!li->scene()) m_scene->addItem(li);
        }
        return li;
    };

    // ── 편심6-Line Pick/Auto 전용 분기 ─────────────────────────────────────────
    if (m_isEccentric && m_isSixLine) {
        const double ecc6  = eccOffset();
        const double R1    = s_innerWallThickness - ecc6;
        const double R2    = R1 + s_spaceThickness;
        const double R3    = R2 + s_outerWallThickness;
        const double mOff6 = s_mortarSpacing;

        // 내벽선(-ecc)→right, 외벽선(+R3)→left, 공간내벽(+R1)→spaceLeft, 공간외벽(+R2)→spaceRight
        out.right      = addLine(QLineF(center.p1() - perp * ecc6,  center.p2() - perp * ecc6));
        out.left       = addLine(QLineF(center.p1() + perp * R3,    center.p2() + perp * R3));
        out.spaceLeft  = addLine(QLineF(center.p1() + perp * R1,    center.p2() + perp * R1));
        out.spaceRight = addLine(QLineF(center.p1() + perp * R2,    center.p2() + perp * R2));

        if (innerMortarOn() || outerMortarOn()) {
            const QString mLayer6 = s_mortarLayerName.isEmpty() ? layerName : s_mortarLayerName;
            QPen mPen6 = buildLayerPen(s_mortarLayerName);
            auto addMortar6 = [&](const QLineF &ln) -> LineItem * {
                QGraphicsItem *item = ctx->entityFactory()->createLine(ln, mPen6);
                LineItem *li = dynamic_cast<LineItem *>(item);
                if (li) {
                    li->setLayerName(mLayer6);
                    li->setParentItem(m_scene->getLayerRoot(mLayer6));
                    if (!li->scene()) m_scene->addItem(li);
                }
                return li;
            };
            if (innerMortarOn())
                out.mortarLeft  = addMortar6(QLineF(center.p1() - perp * (ecc6 + mOff6),
                                                    center.p2() - perp * (ecc6 + mOff6)));
            if (outerMortarOn())
                out.mortarRight = addMortar6(QLineF(center.p1() + perp * (R3   + mOff6),
                                                    center.p2() + perp * (R3   + mOff6)));
        }
        return;
    }

    out.left  = addLine(QLineF(center.p1() + perp * pairLeftOff,
                               center.p2() + perp * pairLeftOff));
    out.right = addLine(QLineF(center.p1() - perp * pairRightOff,
                               center.p2() - perp * pairRightOff));

    if (m_isFourLine) {
        // 편심 모드: 내벽/외벽 오프셋에 mortarSpacing을 각각 더해 비대칭 배치
        const double mLeftOff   = pairLeftOff  + s_mortarSpacing;
        const double mRightOff  = pairRightOff + s_mortarSpacing;
        const QString mLayer    = s_mortarLayerName.isEmpty()
                                      ? layerName : s_mortarLayerName;
        QPen mPen = buildLayerPen(s_mortarLayerName);
        auto addMortar = [&](const QLineF &ln) -> LineItem * {
            QGraphicsItem *item = ctx->entityFactory()->createLine(ln, mPen);
            LineItem *li = dynamic_cast<LineItem *>(item);
            if (li) {
                li->setLayerName(mLayer);
                li->setParentItem(m_scene->getLayerRoot(mLayer));
                if (!li->scene()) m_scene->addItem(li);
            }
            return li;
        };
        const bool leftOn  = outerMortarOn();
        const bool rightOn = innerMortarOn();
        if (leftOn)
            out.mortarLeft  = addMortar(QLineF(center.p1() + perp * mLeftOff,
                                               center.p2() + perp * mLeftOff));
        if (rightOn)
            out.mortarRight = addMortar(QLineF(center.p1() - perp * mRightOff,
                                               center.p2() - perp * mRightOff));

        if (m_isSixLine) {
            // 편심 모드: spaceSpacing을 편심 비율로 비례 배분 (rebuildSegments와 동일 공식)
            const double  spaceOff   = s_spaceSpacing / 2.0;
            const double  sLeftOff   = (s_spacing > 1e-10)
                                           ? s_spaceSpacing * pairLeftOff  / s_spacing : spaceOff;
            const double  sRightOff  = (s_spacing > 1e-10)
                                           ? s_spaceSpacing * pairRightOff / s_spacing : spaceOff;
            const QString sLayer     = s_spaceLayerName.isEmpty() ? layerName : s_spaceLayerName;
            QPen sPen = buildLayerPen(s_spaceLayerName);
            auto addSpace = [&](const QLineF &ln) -> LineItem * {
                QGraphicsItem *item = ctx->entityFactory()->createLine(ln, sPen);
                LineItem *li = dynamic_cast<LineItem *>(item);
                if (li) {
                    li->setLayerName(sLayer);
                    li->setParentItem(m_scene->getLayerRoot(sLayer));
                    if (!li->scene()) m_scene->addItem(li);
                }
                return li;
            };
            out.spaceLeft  = addSpace(QLineF(center.p1() + perp * sLeftOff,
                                             center.p2() + perp * sLeftOff));
            out.spaceRight = addSpace(QLineF(center.p1() - perp * sRightOff,
                                             center.p2() - perp * sRightOff));
        }
    }
}

// ── Pick mode ─────────────────────────────────────────────────────────────────

void TwoLineTool::enterPickMode()
{
    // 진행 중인 그리기가 있으면 먼저 커밋
    if (m_isDrawing)
        commitToUndoStack();

    m_pickMode       = true;
    m_pickBoxPending = false;
    m_pickPairs.clear();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("PICK — 중심선 객체를 선택하세요."), CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(
            tr(" 객체를 선택하거나 "), {tr("종료(우클릭/Enter)")});
    }
}

void TwoLineTool::exitPickMode(bool commit)
{
    m_pickMode       = false;
    m_pickBoxPending = false;

    if (commit) {
        int cnt = m_pickPairs.size();
        commitPairs(m_pickPairs, "2-Line Pick");
        IAppContext *ctx = getContext();
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->logMessage(
                tr("Pick 완료: %1개 쌍 생성됨.").arg(cnt), CommandDock::Info);
            ctx->commandDock()->setPrompt(" Command : ");
        }
    } else {
        // 취소: 씬에서 임시 아이템 제거
        if (m_scene) {
            for (SegmentPair &sp : m_pickPairs) {
                if (sp.left)   { m_scene->removeItem(sp.left);   delete sp.left; }
                if (sp.right)  { m_scene->removeItem(sp.right);  delete sp.right; }
                if (sp.center) { m_scene->removeItem(sp.center); delete sp.center; }
            }
        }
        m_pickPairs.clear();
    }

    if (m_scene) m_scene->clearOverlay();
    emit toolFinished();
}

// 단일 LineItem 처리 (단일 픽·박스 공통)
void TwoLineTool::processPickItem(LineItem *li)
{
    if (!li) return;
    QString layerName = s_layerName.isEmpty() ? currentLayerName() : s_layerName;
    QPen pen = buildLayerPen();
    MyLine data = li->getLine();
    SegmentPair sp;
    generateOffsetPairFromLine(QLineF(data.start.toPointF(), data.end.toPointF()),
                               layerName, pen, sp);
    m_pickPairs.append(sp);
}

// 박스 선택 확정: Window(L→R) 또는 Crossing(R→L)
void TwoLineTool::finishPickBoxSelection(const QPointF &secondCorner)
{
    m_pickBoxPending = false;
    if (m_scene) m_scene->clearOverlay();

    bool isWindow = (secondCorner.x() > m_pickBoxCorner.x());
    QList<CadItem *> items = isWindow
        ? collectItemsInWindowRect(m_pickBoxCorner, secondCorner)
        : collectItemsInCrossingRect(m_pickBoxCorner, secondCorner);

    int count = 0;
    for (CadItem *ci : std::as_const(items)) {
        LineItem *li = dynamic_cast<LineItem *>(ci);
        if (!li) continue;
        processPickItem(li);
        ++count;
    }

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        if (count > 0)
            ctx->commandDock()->logMessage(
                tr("Pick [%1]: 선 %2개 선택 → 누적 %3쌍.")
                    .arg(isWindow ? "Window" : "Crossing")
                    .arg(count)
                    .arg(m_pickPairs.size()),
                CommandDock::Info);
        else
            ctx->commandDock()->logMessage(
                tr("선택 영역에 선 객체가 없습니다."), CommandDock::Info);
    }
}

LineItem *TwoLineTool::findLineItemNear(const QPointF &scenePos)
{
    // AbstractTool::itemAtPos() 사용 — SelectTool과 동일한 CadPicker 기반 히트 판정.
    // 뷰 스케일에 따라 tolerance를 5px(뷰 좌표)로 보정하고,
    // 타입 우선순위(HatchItem 최하위)와 distanceTo() 최솟값으로 최적 객체 반환.
    QGraphicsItem *hit = itemAtPos(scenePos);
    return dynamic_cast<LineItem *>(hit);
}

// ── Auto mode ─────────────────────────────────────────────────────────────────

void TwoLineTool::executeAuto()
{
    if (!m_scene) return;

    // 진행 중인 그리기가 있으면 먼저 커밋
    if (m_isDrawing)
        commitToUndoStack();

    // 중심선 레이어 결정 (전역 중심선 레이어 사용)
    QString clLayer = DrawingSettingsCommand::s_centerLineLayer.isEmpty()
                          ? currentLayerName()
                          : DrawingSettingsCommand::s_centerLineLayer;

    IAppContext *ctx = getContext();
    if (clLayer.isEmpty()) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("Auto 오류: 중심선 레이어가 설정되지 않았습니다. 도면설정(DrawingSettings)에서 설정하세요."),
                CommandDock::Info);
        return;
    }

    // 생성 대상 레이어·펜 결정
    QString targetLayer = s_layerName.isEmpty() ? currentLayerName() : s_layerName;
    QPen pen = buildLayerPen();

    // 씬 전체에서 중심선 레이어의 LineItem 탐색
    QList<SegmentPair> results;
    for (QGraphicsItem *item : m_scene->items()) {
        LineItem *li = dynamic_cast<LineItem *>(item);
        if (!li) continue;
        if (li->layerName() != clLayer) continue;

        MyLine data = li->getLine();
        QLineF line(data.start.toPointF(), data.end.toPointF());
        SegmentPair sp;
        generateOffsetPairFromLine(line, targetLayer, pen, sp);
        results.append(sp);
    }

    if (results.isEmpty()) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("Auto: 레이어 \"%1\"에서 선 객체를 찾을 수 없습니다.")
                    .arg(clLayer), CommandDock::Info);
        return;
    }

    int cnt = results.size();
    commitPairs(results, "2-Line Auto");

    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            tr("Auto 완료: \"%1\" 레이어에서 %2개 선에 2-Line 생성됨.")
                .arg(clLayer).arg(cnt), CommandDock::Info);
        ctx->commandDock()->setPrompt(" Command : ");
    }

    emit toolFinished();
}

// ── TwoLineSetupCommand ───────────────────────────────────────────────────────
TwoLineSetupCommand::TwoLineSetupCommand(IAppContext *ctx, bool isFourLine,
                                         bool isSixLine, bool isEccentric, QObject *parent)
    : CommandBase(parent), m_ctx(ctx),
      m_isFourLine(isFourLine), m_isSixLine(isSixLine), m_isEccentric(isEccentric)
{}

void TwoLineSetupCommand::execute()
{
    if (!m_ctx) return;
    QWidget *parentWin = dynamic_cast<QWidget *>(m_ctx->commandDock());
    const int mode = TwoLineTool::modeKey(m_isFourLine, m_isSixLine, m_isEccentric);
    TwoLineSetupDialog dlg(TwoLineTool::globalSpacing(),
                           TwoLineTool::globalLayerName(),
                           TwoLineTool::globalCenterLineEnabled(),
                           m_isFourLine,
                           TwoLineTool::globalMortarSpacing(),
                           TwoLineTool::globalMortarLayerName(),
                           m_isSixLine,
                           TwoLineTool::globalSpaceSpacing(),
                           TwoLineTool::globalSpaceLayerName(),
                           m_isEccentric,
                           TwoLineTool::globalEccentricOffset(mode),
                           TwoLineTool::globalInnerWallThickness(),
                           TwoLineTool::globalSpaceThickness(),
                           TwoLineTool::globalOuterWallThickness(),
                           TwoLineTool::globalInnerMortarEnabled(mode),
                           TwoLineTool::globalOuterMortarEnabled(mode),
                           m_ctx->layerManager(), parentWin);
    if (dlg.exec() == QDialog::Accepted) {
        TwoLineTool::setGlobalLayerName(dlg.selectedLayerName());
        TwoLineTool::setGlobalCenterLineEnabled(dlg.centerLineEnabled());
        if (m_isFourLine) {
            TwoLineTool::setGlobalMortarSpacing(dlg.mortarSpacing());
            TwoLineTool::setGlobalMortarLayerName(dlg.mortarLayerName());
            TwoLineTool::setGlobalInnerMortarEnabled(mode, dlg.innerMortarEnabled());
            TwoLineTool::setGlobalOuterMortarEnabled(mode, dlg.outerMortarEnabled());
        }
        if (m_isEccentric && m_isSixLine) {
            // 편심6-Line: 안쪩/공간/바깥쪩 저장
            TwoLineTool::setGlobalInnerWallThickness(dlg.innerWallThickness());
            TwoLineTool::setGlobalSpaceThickness(dlg.spaceThickness());
            TwoLineTool::setGlobalOuterWallThickness(dlg.outerWallThickness());
        } else {
            TwoLineTool::setGlobalSpacing(dlg.spacing());
            if (m_isSixLine) {
                TwoLineTool::setGlobalSpaceSpacing(dlg.spaceSpacing());
                TwoLineTool::setGlobalSpaceLayerName(dlg.spaceLayerName());
            }
        }
        if (m_isEccentric)
            TwoLineTool::setGlobalEccentricOffset(mode, dlg.eccentricOffset());
        TwoLineTool::saveDocDefaults(m_ctx->activeScene()); // [3-3]
    }
}
