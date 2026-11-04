#include "planwindowtool.h"
#include "planarchdata.h"
#include "spacefinish.h"
#include "planwindowsetupdialog.h"
#include "scenesymbolregistry.h"
#include "wallpositiondialog.h"

#include "cadcommands.h"
#include "drawingsettingscommand.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "lineitem.h"
#include "mygraphicsscene.h"
#include "myvector.h"
#include "polylineitem.h"
#include "snapmanager.h"
#include "tooldefaults.h"
#include "multilinedata.h"

#include <QApplication>
#include <QSettings>
#include <QUndoStack>
#include <QUuid>
#include <QWidget>
#include <QtMath>
#include <cmath>

double  PlanWindowTool::s_w               = 1200.0;
double  PlanWindowTool::s_w3              = 2400.0;
double  PlanWindowTool::s_w4              = 2400.0;
double  PlanWindowTool::s_sashW4          = 0.0;
double  PlanWindowTool::s_sashW3          = 0.0;
double  PlanWindowTool::s_d1              = 100.0;
double  PlanWindowTool::s_d2              = 150.0;
bool    PlanWindowTool::s_useWallThickness = false;
bool    PlanWindowTool::s_double           = true;   // 이중창(true) / 단창(false)
bool    PlanWindowTool::s_glass            = false;
double  PlanWindowTool::s_sashW            = 0.0;
QString PlanWindowTool::s_frameLayer;
QString PlanWindowTool::s_otherLayer;
QString PlanWindowTool::s_trimExcludeLayer;
double  PlanWindowTool::s_win[7]           = { 40, 50, 150, 30, 100, 35, 50 };   // Win7: 이중창 50 / 단창 30
double  PlanWindowTool::s_winTop[7]        = { 20, 50, 150, 30, 100, 35, 30 };   // 상단부 전용: Win1 20, Win2/Win3 미사용
QString PlanWindowTool::s_wallGroupId;
QString PlanWindowTool::s_closingLayer;
int     PlanWindowTool::s_wallPositionMode  = 0;
int     PlanWindowTool::s_lastWallPosMode   = WallPositionDialog::Inner;
double  PlanWindowTool::s_wallDetectedSpan  = 0.0;
bool    PlanWindowTool::s_mlAtP1            = false;
static int s_windowSeq = 0;

// ── 도구 기본값 저장/불러오기 (3-2: 도면 스코프 우선, 전역은 씨앗) ─────────────
//   네임스페이스 "PlanWindowTool" = 기존 QSettings 그룹명 그대로.
static void savePlanWindowSettings(MyGraphicsScene *scene)
{
    ToolDefaults d("PlanWindowTool", scene);
    d.put("W",                PlanWindowTool::globalW());
    d.put("D1",               PlanWindowTool::globalD1());
    d.put("D2",               PlanWindowTool::globalD2());
    d.put("Glass", PlanWindowTool::glass());
    d.put("Double", PlanWindowTool::sashDouble());
    d.put("SashW",  PlanWindowTool::sashWidth());
    d.put("W4",     PlanWindowTool::globalW4());
    d.put("SashW4", PlanWindowTool::sashWidth4());
    d.put("W3",     PlanWindowTool::globalW3());
    d.put("SashW3", PlanWindowTool::sashWidth3());
    for (int i = 0; i < 7; ++i)
        d.put(QString("Win%1").arg(i + 1), PlanWindowTool::winParam(i));
    for (int i = 0; i < 7; ++i)
        d.put(QString("TopWin%1").arg(i + 1), PlanWindowTool::winParamTop(i));
    d.put("UseWallThickness", PlanWindowTool::useWallThickness());
    d.put("FrameLayer",       PlanWindowTool::frameLayer());
    d.put("OtherLayer",       PlanWindowTool::otherLayer());
    d.put("TrimExcludeLayer", PlanWindowTool::trimExcludeLayer());
}

static void loadPlanWindowSettings(MyGraphicsScene *scene)
{
    SpaceFinish::load(scene);
    ToolDefaults d("PlanWindowTool", scene);
    PlanWindowTool::setGlobalW(d.getDouble("W",                1200.0));
    PlanWindowTool::setGlobalD1(d.getDouble("D1",              100.0));
    PlanWindowTool::setGlobalD2(d.getDouble("D2",              150.0));
    PlanWindowTool::setGlass(d.getBool("Glass", false));
    PlanWindowTool::setSashDouble(d.getBool("Double", true));
    PlanWindowTool::setSashWidth(d.getDouble("SashW", 0.0));
    PlanWindowTool::setGlobalW4(d.getDouble("W4", 2400.0));
    PlanWindowTool::setSashWidth4(d.getDouble("SashW4", 0.0));
    PlanWindowTool::setGlobalW3(d.getDouble("W3", 2400.0));
    PlanWindowTool::setSashWidth3(d.getDouble("SashW3", 0.0));
    static const double kWinDef[7] = { 40, 50, 150, 30, 100, 35, 50 };
    for (int i = 0; i < 7; ++i)
        PlanWindowTool::setWinParam(i, d.getDouble(QString("Win%1").arg(i + 1), kWinDef[i]));
    static const double kTopDef[7] = { 20, 50, 150, 30, 100, 35, 30 };
    for (int i = 0; i < 7; ++i)
        PlanWindowTool::setWinParamTop(i, d.getDouble(QString("TopWin%1").arg(i + 1), kTopDef[i]));
    PlanWindowTool::setUseWallThickness(false);  // 입력값 모드 고정 (벽 두께 값 모드 제거)
    PlanWindowTool::setFrameLayer(d.getString("FrameLayer",         QString()));
    PlanWindowTool::setOtherLayer(d.getString("OtherLayer",         QString()));
    PlanWindowTool::setTrimExcludeLayer(d.getString("TrimExcludeLayer", QString()));
}

double  PlanWindowTool::globalW4()                       { return s_w4; }
void    PlanWindowTool::setGlobalW4(double v)            { if (v > 0.0) s_w4 = v; }
double  PlanWindowTool::sashWidth4()                     { return s_sashW4; }
void    PlanWindowTool::setSashWidth4(double v)          { if (v >= 0.0) s_sashW4 = v; }
double  PlanWindowTool::globalW3()                       { return s_w3; }
void    PlanWindowTool::setGlobalW3(double v)            { if (v > 0.0) s_w3 = v; }
double  PlanWindowTool::sashWidth3()                     { return s_sashW3; }
void    PlanWindowTool::setSashWidth3(double v)          { if (v >= 0.0) s_sashW3 = v; }
double  PlanWindowTool::globalW()                        { return s_w; }
void    PlanWindowTool::setGlobalW(double v)             { if (v > 0.0) s_w = v; }
double  PlanWindowTool::globalD1()                       { return s_d1; }
void    PlanWindowTool::setGlobalD1(double v)            { if (v > 0.0) s_d1 = v; }
bool    PlanWindowTool::sashDouble()                     { return s_double; }
void    PlanWindowTool::setSashDouble(bool dbl)          { s_double = dbl; }
double  PlanWindowTool::sashWidth()                      { return s_sashW; }
void    PlanWindowTool::setSashWidth(double v)           { if (v >= 0.0) s_sashW = v; }
bool    PlanWindowTool::glass()                          { return s_glass; }
void    PlanWindowTool::setGlass(bool on)                { s_glass = on; }
double  PlanWindowTool::winParam(int i)                  { return (i >= 0 && i < 7) ? s_win[i] : 0.0; }
void    PlanWindowTool::setWinParam(int i, double v)     { if (i >= 0 && i < 7 && v > 0.0) s_win[i] = v; }
double  PlanWindowTool::winParamTop(int i)               { return (i >= 0 && i < 7) ? s_winTop[i] : 0.0; }
void    PlanWindowTool::setWinParamTop(int i, double v)  { if (i >= 0 && i < 7 && v > 0.0) s_winTop[i] = v; }
double  PlanWindowTool::globalD2()                    { return s_d2; }
void    PlanWindowTool::setGlobalD2(double v)            { if (v > 0.0) s_d2 = v; }
bool    PlanWindowTool::useWallThickness()               { return s_useWallThickness; }
void    PlanWindowTool::setUseWallThickness(bool use)    { s_useWallThickness = use; }
QString PlanWindowTool::frameLayer()                     { return s_frameLayer; }
void    PlanWindowTool::setFrameLayer(const QString &n)  { s_frameLayer = n; }
QString PlanWindowTool::otherLayer()                     { return s_otherLayer; }
void    PlanWindowTool::setOtherLayer(const QString &n)  { s_otherLayer = n; }
QString PlanWindowTool::trimExcludeLayer()                     { return s_trimExcludeLayer; }
void    PlanWindowTool::setTrimExcludeLayer(const QString &n)  { s_trimExcludeLayer = n; }

void PlanWindowTool::loadSettings(MyGraphicsScene *scene) { loadPlanWindowSettings(scene); }
void PlanWindowTool::saveSettings(MyGraphicsScene *scene) { savePlanWindowSettings(scene); }

CommandBase *PlanWindowTool::createCommand(IAppContext *c)
{
    // 단창/이중창은 설정창에서 선택한다 (variant 0 = 예전 하드코딩 형상은
    // 이미 배치된 심볼의 속성 편집 재생성용으로만 남김).
    return new PlanWindowCommand(c, 2, 2);
}

CommandBase *PlanWindowTool::createCommandThreeSash(IAppContext *c)
{
    return new PlanWindowCommand(c, 2, 3);
}

CommandBase *PlanWindowTool::createCommandFourSash(IAppContext *c)
{
    return new PlanWindowCommand(c, 2, 4);
}

CommandBase *PlanWindowTool::createCommandTopOnly(IAppContext *c)
{
    return new PlanWindowCommand(c, 3, 2);
}

CommandBase *PlanWindowTool::createCommandTopOnlyThreeSash(IAppContext *c)
{
    return new PlanWindowCommand(c, 3, 3);
}

CommandBase *PlanWindowTool::createCommandTopOnlyFourSash(IAppContext *c)
{
    return new PlanWindowCommand(c, 3, 4);
}

CommandBase *PlanWindowTool::createCommandTopOnlyLinked(IAppContext *c)
{
    return new PlanWindowCommand(c, 4, 2);
}

// ── 2짝 미서기창 형상 (win-1.lsp) ─────────────────────────────────────────────
//   로컬 좌표: x = 창 진행방향(0..W), y = 벽 내측면 0 → 외측면 V.
//   kind 0 = 창틀(폴리라인), 1 = 선(LineItem, 점 목록은 연속 선분), 2 = 중심선
struct TwoSashShape {
    QVector<QPointF> pts;
    bool             closed;
    int              kind;
};

// 창짝 세트 — xs: 틀 안쪽 면 x(창틀 두께), yR: 레일 기준선 y (win-1.lsp w_po8~w_po19, 유리선 w_po20~23)
static void addSashSet(QList<TwoSashShape> &out, double W, const double *w,
                       double xs, double yR, double xJ, bool glass)
{
    const double w4 = w[3], w6 = w[5];
    const double xK = xJ - w6;              // 짝2 끝 x

    out << TwoSashShape{ { QPointF(xs,yR), QPointF(W-xs,yR) }, false, 1 };   // 레일 기준선
    out << TwoSashShape{ { QPointF(xs,yR+w4), QPointF(xJ,yR+w4), QPointF(xJ,yR-w4) }, false, 1 };
    out << TwoSashShape{ { QPointF(W-xs,yR-w4), QPointF(xK,yR-w4), QPointF(xK,yR+w4) }, false, 1 };
    out << TwoSashShape{ { QPointF(xs+w6,yR),   QPointF(xs+w6,yR+w4)   }, false, 1 };
    out << TwoSashShape{ { QPointF(W-xs-w6,yR-w4), QPointF(W-xs-w6,yR) }, false, 1 };

    // 유리 표현선 (LISP @winYS = "YES")
    if (glass) {
        out << TwoSashShape{ { QPointF(xs+w6,yR+w4/2.0), QPointF(xK,yR+w4/2.0) }, false, 1 };
        out << TwoSashShape{ { QPointF(W-xs-w6,yR-w4/2.0), QPointF(xJ,yR-w4/2.0) }, false, 1 };
    }
}

// 3짝 창짝 세트 — 왼쪽 바깥창을 오른쪽으로 미러한 바깥창 2개 + 중앙창 1개.
//   바깥창은 위쪽 레일(yR+w4), 중앙창은 아래쪽 레일(yR-w4). 중앙창은 바깥창과 w6 만큼 겹친다.
static void addSashSet3(QList<TwoSashShape> &out, double W, const double *w,
                        double xs, double yR, double xJ, bool glass, bool splitCenter)
{
    const double w4 = w[3], w6 = w[5];
    const double xL  = xJ - w6;           // 중앙창 왼쪽 끝
    const double xJr = W - xJ;            // 오른쪽 바깥창 끝 (미러)
    const double xR  = W - xL;            // 중앙창 오른쪽 끝

    out << TwoSashShape{ { QPointF(xs,yR), QPointF(W-xs,yR) }, false, 1 };   // 레일 기준선
    out << TwoSashShape{ { QPointF(xs,yR+w4), QPointF(xJ,yR+w4), QPointF(xJ,yR-w4) }, false, 1 };         // 왼쪽 창
    out << TwoSashShape{ { QPointF(W-xs,yR+w4), QPointF(xJr,yR+w4), QPointF(xJr,yR-w4) }, false, 1 };     // 오른쪽 창 (미러)
    // 중앙창 (4짝도 중앙에 나뉨선 없이 하나의 선으로 그린다)
    out << TwoSashShape{ { QPointF(xL,yR+w4), QPointF(xL,yR-w4), QPointF(xR,yR-w4), QPointF(xR,yR+w4) }, false, 1 };
    if (splitCenter) {
        // 4짝: 중앙창을 반으로 나눈 위치(x=W/2)를 중심으로 좌/우에 폭 w6, 높이 w4 프레임 배치.
        // 중앙창 아래쪽 레일(yR-w4) ~ 레일 기준선(yR) 구간. 가로변은 기존 창문 선과 겹치므로
        // 그리지 않고 세로변 3개(좌·중심·우)만 그린다. 창문 나머지 레이어(kind 1).
        const double xM = W / 2.0;
        const double yB = yR - w4;
        const double yT = yR;
        out << TwoSashShape{ { QPointF(xM-w6,yB), QPointF(xM-w6,yT) }, false, 1 };
        out << TwoSashShape{ { QPointF(xM,yB),    QPointF(xM,yT)    }, false, 1 };
        out << TwoSashShape{ { QPointF(xM+w6,yB), QPointF(xM+w6,yT) }, false, 1 };
    }
    out << TwoSashShape{ { QPointF(xs+w6,yR),   QPointF(xs+w6,yR+w4)   }, false, 1 };
    out << TwoSashShape{ { QPointF(W-xs-w6,yR), QPointF(W-xs-w6,yR+w4) }, false, 1 };

    if (glass) {
        out << TwoSashShape{ { QPointF(xs+w6,yR+w4/2.0), QPointF(xL,yR+w4/2.0) }, false, 1 };
        out << TwoSashShape{ { QPointF(xR,yR+w4/2.0), QPointF(W-xs-w6,yR+w4/2.0) }, false, 1 };
        if (splitCenter) {
            // 4짝: 중앙 프레임(x=W/2±w6) 구간은 유리선이 지나가지 않도록 둘로 나눈다.
            const double xM = W / 2.0;
            out << TwoSashShape{ { QPointF(xJ,yR-w4/2.0),    QPointF(xM-w6,yR-w4/2.0) }, false, 1 };
            out << TwoSashShape{ { QPointF(xM+w6,yR-w4/2.0), QPointF(xJr,yR-w4/2.0)   }, false, 1 };
        } else {
            out << TwoSashShape{ { QPointF(xJ,yR-w4/2.0), QPointF(xJr,yR-w4/2.0) }, false, 1 };
        }
    }
}

// 3짝: 바깥창(왼쪽) 끝 x. sashW = 중앙창 넓이, 0 이면 세 짝 같은 크기.
static double sashEndX3(double W, double xs, double w6, double sashW)
{
    const double wc = (sashW > 0.0) ? sashW : (W - 2.0 * xs + 2.0 * w6) / 3.0;
    double xJ = (W - wc) / 2.0 + w6;
    const double lo = xs + w6 + 1.0;
    const double hi = W / 2.0 - 1.0;
    if (xJ < lo) xJ = lo;
    if (xJ > hi) xJ = hi;
    return xJ;
}

// 4짝: 바깥창(왼쪽) 끝 x. sashW = 안쪽창 1개 넓이(안쪽창 2개는 같은 크기), 0 이면 네 짝 같은 크기.
static double sashEndX4(double W, double xs, double w6, double sashW)
{
    const double wi = (sashW > 0.0) ? sashW : (W - 2.0 * xs + 2.0 * w6) / 4.0;
    double xJ = (W / 2.0 - wi) + w6;
    const double lo = xs + w6 + 1.0;
    const double hi = W / 2.0 - 1.0;
    if (xJ < lo) xJ = lo;
    if (xJ > hi) xJ = hi;
    return xJ;
}

// 짝1(왼쪽) 끝 x. sashW > 0 이면 짝1 넓이 = sashW, 0 이면 두 짝 같은 크기 (W/2 + xs/2).
static double sashEndX(double W, double xs, double w6, double sashW)
{
    double xJ = (sashW > 0.0) ? (xs + sashW) : (W / 2.0 + xs / 2.0);
    const double lo = xs + w6 + 1.0;
    const double hi = W - xs - 1.0;
    if (xJ < lo) xJ = lo;
    if (xJ > hi) xJ = hi;
    return xJ;
}

static QList<TwoSashShape> twoSashShapes(double W, const double *w, bool glass, bool dbl, double sashW, int sashes, bool topOnly = false, bool linked = false)
{
    QList<TwoSashShape> out;
 //   const double h  = W / 2.0;
    // 상단부만: 하단 직사각형(Win2·Win3)을 쓰지 않으므로 0 으로 취급하고 이중창도 해제
    const double w2 = topOnly ? 0.0 : w[1], w3 = topOnly ? 0.0 : w[2];
    const double w5 = w[4], w6 = w[5], w7 = w[6];
    if (topOnly) dbl = false;

    // LISP 의 y=-Win1 (창틀 안쪽 끝)을 로컬 y=0 으로 옮겨, 벽 내측면은 y=Win1 에 놓인다.
    const double yIn   = 0.0;            // 창틀 안쪽 끝
    const double yMid  = w3;             // 창틀 직사각형 상단 (LISP w_po4)
    const double yTop  = yMid + w5;      // 창틀 바깥 끝 (LISP w_po5)
    const double yRail = yMid + w5 / 2.0; // 레일 기준선 (LISP w_po8)
    const double xJ    = (sashes == 4) ? sashEndX4(W, w7, w6, sashW)
                       : (sashes == 3) ? sashEndX3(W, w7, w6, sashW)
                                       : sashEndX(W, w7, w6, sashW);   // 짝1 끝 x (LISP w_po11)

    // 창틀 좌/우 (미러)
    if (!topOnly) {
        out << TwoSashShape{ { QPointF(0,yIn), QPointF(w2,yIn), QPointF(w2,yMid), QPointF(0,yMid) }, true, 0 };
        out << TwoSashShape{ { QPointF(W,yIn), QPointF(W-w2,yIn), QPointF(W-w2,yMid), QPointF(W,yMid) }, true, 0 };
    }
    // 상단부만이면 하단 직사각형이 없으므로 이 프레임을 닫힌 사각형으로 만든다
    out << TwoSashShape{ { QPointF(0,yMid), QPointF(0,yTop), QPointF(w7,yTop), QPointF(w7,yMid) }, topOnly, 0 };
    out << TwoSashShape{ { QPointF(W,yMid), QPointF(W,yTop), QPointF(W-w7,yTop), QPointF(W-w7,yMid) }, topOnly, 0 };

    // 창틀 수평선 (상단부만이면 yIn == yMid 이므로 한 번만)
    if (!topOnly)
        out << TwoSashShape{ { QPointF(w2,yIn),   QPointF(W-w2,yIn)   }, false, 1 };
    out << TwoSashShape{ { QPointF(topOnly ? w7 : w2,yMid),  QPointF(topOnly ? W-w7 : W-w2,yMid)  }, false, 1 };
    out << TwoSashShape{ { QPointF(w7,yTop),  QPointF(W-w7,yTop)  }, false, 1 };

    // 상단 창짝 세트 (단창·이중창 공통)
    if (linked) {
        // 연결형: 양쪽 창틀 프레임 중앙(yRail)에서 Win4/2 위(p1,p3)·아래(p2,p4)를 잡아
        // p1-p3, p2-p4 를 한 선으로 연결하고, 양 끝에서 Win6 안쪽 지점에 Win4 길이의 세로선을 긋는다(대칭).
        const double w4 = w[3], hy = w4 / 2.0;
        const double yU = yRail + hy, yL = yRail - hy;
        out << TwoSashShape{ { QPointF(w7,yU), QPointF(W-w7,yU) }, false, 1 };   // p1-p3
        out << TwoSashShape{ { QPointF(w7,yL), QPointF(W-w7,yL) }, false, 1 };   // p2-p4
        out << TwoSashShape{ { QPointF(w7+w6,yU),   QPointF(w7+w6,yL)   }, false, 1 };
        out << TwoSashShape{ { QPointF(W-w7-w6,yU), QPointF(W-w7-w6,yL) }, false, 1 };
    } else if (sashes >= 3)
        addSashSet3(out, W, w, w7, yRail, xJ, glass, sashes == 4);
    else
        addSashSet(out, W, w, w7, yRail, xJ, glass);
    // 이중창: 하단(직사각형 구간)에도 같은 창짝 세트를 만든다
    if (dbl)
    {
        if (sashes >= 3)
            addSashSet3(out, W, w, w2, w3 / 2.0,
                        (sashes == 4) ? sashEndX4(W, w2, w6, sashW) : sashEndX3(W, w2, w6, sashW),
                        glass, sashes == 4);
        else
            addSashSet(out, W, w, w2, w3 / 2.0, sashEndX(W, w2, w6, sashW), glass);
    }

    // 중심선
    const double xc = linked ? W / 2.0 : xJ - w6 / 2.0;
    // 중심선: 창 두께(Win3+Win5) 양쪽으로 Win5/2 씩 더 길게 연장
    const double ext = w5 / 2.0;
    out << TwoSashShape{ { QPointF(xc, -ext), QPointF(xc, yTop + ext) }, false, 2 };
    if (sashes >= 3)   // 미러된 반대쪽 겹침 중심선
        out << TwoSashShape{ { QPointF(W - xc, -ext), QPointF(W - xc, yTop + ext) }, false, 2 };
    return out;
}

// ── 생성자 ───────────────────────────────────────────────────────────────────
PlanWindowTool::PlanWindowTool(QObject *parent, int variant, int sashes)
    : AbstractTool(parent)
    , m_variant(variant)
    , m_sashes((sashes >= 3) ? ((sashes == 4) ? 4 : 3) : 2)
    , m_state(WaitP1)
    , m_currentMousePos(0.0, 0.0)
{}

// ─────────────────────────────────────────────────────────────────────────────
// applySnapForState — 상태별 osnap 자동 전환
//   WaitP1 / WaitP3 : Nearest ON,  Perpendicular OFF
//   WaitP2          : Nearest OFF, Perpendicular ON
// ─────────────────────────────────────────────────────────────────────────────
void PlanWindowTool::applySnapForState(State s)
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
void PlanWindowTool::restoreSavedSnap()
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
void PlanWindowTool::activate(MyGraphicsScene *scene)
{
    // [3-2] 매 활성화마다 현재 도면의 값으로 다시 채운다(도면 전환 반영).
    loadPlanWindowSettings(scene);
    syncVariant();

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
        ctx->commandDock()->logMessage("PLAN-WINDOW", CommandDock::Command);
    }

    showP1Prompt();
}

// ── deactivate ───────────────────────────────────────────────────────────────
void PlanWindowTool::deactivate()
{
    restoreSavedSnap();
    m_state = WaitP1;
    if (m_scene)
        m_scene->clearOverlay();
    AbstractTool::deactivate();
}

// ── showP1Prompt ─────────────────────────────────────────────────────────────
void PlanWindowTool::showP1Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(tr(" P1: 창문 시작점 지정 (nea) "), {"Setup"});
}

// ── showP1DistancePrompt ─────────────────────────────────────────────────────
void PlanWindowTool::showP1DistancePrompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(tr(" 거리를 입력하세요 : "), {});
}

// ── computeP1FromAnchor ──────────────────────────────────────────────────────
QPointF PlanWindowTool::computeP1FromAnchor(double dist) const
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
void PlanWindowTool::showP2Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    if (s_useWallThickness)
        ctx->commandDock()->showInteractivePrompt(tr(" P2: 외벽 두께측정 점 지정 (per) "), {});
    else
        ctx->commandDock()->showInteractivePrompt(tr(" P2: 두께 방향 지정 (per) "), {});
}

// ── showP3Prompt ─────────────────────────────────────────────────────────────
void PlanWindowTool::showP3Prompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(tr(" P3: 진행 방향점 지정 (nea) "), {});
}

// ── mouseMove ────────────────────────────────────────────────────────────────
void PlanWindowTool::mouseMove(QPointF pos)
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
            updateGhostWindow(m_p1, angleDeg, V, flipY);
        else
            m_scene->clearOverlay();
    }
}

// ── mousePress ───────────────────────────────────────────────────────────────
void PlanWindowTool::mousePress(QPointF pos, Qt::MouseButton button)
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
        double angleDeg, V;
        bool   flipY;
        if (computeFrom3Pts(m_p1, m_p2, m_currentMousePos, angleDeg, V, flipY))
            updateGhostWindow(m_p1, angleDeg, V, flipY);
        showP3Prompt();

    } else {
        double angleDeg, V;
        bool   flipY;
        if (!computeFrom3Pts(m_p1, m_p2, snapped, angleDeg, V, flipY)) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(tr("P1·P2·P3 가 너무 가깝거나 일직선입니다."), CommandDock::Info);
            return;
        }
        m_scene->clearOverlay();
        placeWindow(m_p1, angleDeg, V, flipY);
        m_state = WaitP1;
        applySnapForState(WaitP1);
        showP1Prompt();
    }
}

void PlanWindowTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/) {}

// ── processCommand ────────────────────────────────────────────────────────────
bool PlanWindowTool::processCommand(const QString &command)
{
    IAppContext *ctx = getContext();
    const QString cmd = command.trimmed();

    if (m_state == WaitP1Distance) {
        bool ok;
        double dist = cmd.toDouble(&ok);
        if (ok && dist > 0.0) {
            m_p1 = computeP1FromAnchor(dist);
            {
                LineItem *ml   = findNearestMultilineAt(m_p1);
                s_wallGroupId  = ml ? ml->data(ML_DATA_GROUP_ID).toString() : QString();
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
            QWidget *parentWin = ctx ? dynamic_cast<QWidget *>(ctx->commandDock()) : nullptr;
            PlanWindowSetupDialog dlg(curW(), s_glass, s_double, curSashW(), m_sashes,
                                      m_variant >= 3, parentWin);
            dlg.setSpaceFinishType(SpaceFinish::type());
            if (dlg.exec() == QDialog::Accepted) {
                SpaceFinish::setType(dlg.spaceFinishType());
                SpaceFinish::save(m_scene);
                if (m_sashes == 4)      setGlobalW4(dlg.w());
                else if (m_sashes == 3) setGlobalW3(dlg.w());
                else                    setGlobalW(dlg.w());
                setUseWallThickness(false);
                setGlass(dlg.glass());
                if (m_variant < 3) {
                    // 단창/이중창 전환 시 Win7(바깥틀폭) 초기값 자동 변경 (이중창 50 / 단창 30)
                    if (dlg.isDouble() != s_double)
                        setWinParam(6, dlg.isDouble() ? 50.0 : 30.0);
                    setSashDouble(dlg.isDouble());
                }
                if (m_sashes == 4)      setSashWidth4(dlg.sashW());
                else if (m_sashes == 3) setSashWidth3(dlg.sashW());
                else                    setSashWidth(dlg.sashW());
                syncVariant();
                savePlanWindowSettings(m_scene);
            }
            showP1Prompt();
            return true;
        }

        if (ctx && ctx->parser()) {
            QPointF resultPos;
            const bool isOrtho = ctx->snapManager() && ctx->snapManager()->isOrtho();
            if (ctx->parser()->parseCoordinate(cmd, m_currentMousePos, m_currentMousePos, isOrtho, resultPos)) {
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
            if (ctx->parser()->parseCoordinate(cmd, m_p1, m_currentMousePos, isOrtho, resultPos)) {
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
            if (ctx->parser()->parseCoordinate(cmd, m_p2, m_currentMousePos, isOrtho, resultPos)) {
                double angleDeg, V;
                bool   flipY;
                if (computeFrom3Pts(m_p1, m_p2, resultPos, angleDeg, V, flipY)) {
                    if (m_scene) m_scene->clearOverlay();
                    placeWindow(m_p1, angleDeg, V, flipY);
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

void PlanWindowTool::onFinish()
{
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

void PlanWindowTool::onCancel()
{
    m_state = WaitP1;
    if (m_scene)
        m_scene->clearOverlay();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage("*Cancel*", CommandDock::Info);
    onFinish();
}

// ── layerOfEntityAt ──────────────────────────────────────────────────────────
QString PlanWindowTool::layerOfEntityAt(const QPointF &pos) const
{
    if (!m_scene)
        return QString();

    const double kTol = 15.0;
    QString best;
    double  bestDist = kTol;

    const QList<QGraphicsItem *> allItems = m_scene->items();
    for (int i = 0; i < allItems.size(); ++i) {
        LineItem *li = dynamic_cast<LineItem *>(allItems.at(i));
        if (!li) continue;
        if (li->data(ML_DATA_ROLE).toString() == "center") continue;

        const MyLine  ln = li->getLine();
        const QPointF a(ln.start.x, ln.start.y);
        const QPointF seg = QPointF(ln.end.x, ln.end.y) - a;
        const double  len2 = seg.x() * seg.x() + seg.y() * seg.y();
        if (len2 < 1e-12) continue;

        const QPointF d = pos - a;
        double t = (d.x() * seg.x() + d.y() * seg.y()) / len2;
        t = qBound(0.0, t, 1.0);
        const QPointF q = a + seg * t;
        const double  dist = std::hypot(pos.x() - q.x(), pos.y() - q.y());
        if (dist < bestDist) {
            bestDist = dist;
            best     = li->layerName();
        }
    }
    return best;
}

// ── curW / curSashW ──────────────────────────────────────────────────────────
double PlanWindowTool::curW() const
{
    return (m_sashes == 4) ? s_w4 : (m_sashes == 3) ? s_w3 : s_w;
}
double PlanWindowTool::curSashW() const
{
    return (m_sashes == 4) ? s_sashW4 : (m_sashes == 3) ? s_sashW3 : s_sashW;
}

// ── syncVariant ──────────────────────────────────────────────────────────────
void PlanWindowTool::syncVariant()
{
    if (m_variant >= 3)   // 상단부만(3) / 연결형(4): 단창/복창 설정과 무관
        return;
    m_variant = s_double ? 2 : 1;
}

double *PlanWindowTool::curWin() const
{
    return (m_variant >= 3) ? s_winTop : s_win;
}

// ── windowThickness / wallProtrusion ─────────────────────────────────────────
double PlanWindowTool::windowThickness() const
{
    if (m_variant >= 3)
        return s_winTop[4];   // 상단부만: Win3 미사용
    return (m_variant >= 1) ? (s_win[2] + s_win[4]) : (s_d1 + s_d2);
}

double PlanWindowTool::wallProtrusion() const
{
    return (m_variant >= 1) ? curWin()[0] : 0.0;
}

// ── computeFrom3Pts ──────────────────────────────────────────────────────────
bool PlanWindowTool::computeFrom3Pts(
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
    outV        = s_useWallThickness ? std::abs(vSigned) : windowThickness();
    outFlipY    = (vSigned < 0.0);
    return true;
}

// ── updateGhostWindow ────────────────────────────────────────────────────────
void PlanWindowTool::updateGhostWindow(
    const QPointF &p1, double angleDeg, double V, bool flipY)
{
    if (!m_scene)
        return;

    const double W  = curW();
    double d1 = s_d1;
    double d2 = s_d2;
    if (s_useWallThickness) {
        d1 = V / 2.0;
        d2 = V / 2.0;
    }

    double ox = p1.x(), oy = p1.y();
    const double c  = std::cos(qDegreesToRadians(angleDeg));
    const double s2 = std::sin(qDegreesToRadians(angleDeg));

    // 입력값 모드 벽 위치 정렬 보정 (다중선/일반선 공통)
    if (!s_useWallThickness && s_wallPositionMode != 0) {
        // 벽 위치는 항상 P1/P2 기준 (다중선 유무와 무관)
        double nearLy, farLy;
        const double delta = computeWallPositionDeltaFromSpan(nearLy, farLy);
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

    if (m_variant >= 1) {
        const QList<TwoSashShape> shapes = twoSashShapes(W, curWin(), s_glass, m_variant == 2, curSashW(), m_sashes, m_variant >= 3, m_variant == 4);
        for (int i = 0; i < shapes.size(); ++i) {
            const TwoSashShape &sh = shapes.at(i);
            path.moveTo(pt(sh.pts.at(0).x(), sh.pts.at(0).y()));
            for (int k = 1; k < sh.pts.size(); ++k)
                path.lineTo(pt(sh.pts.at(k).x(), sh.pts.at(k).y()));
            if (sh.closed)
                path.closeSubpath();
        }
        updateGhostPreview(path, currentLayerName());
        return;
    }

    // 15. p1-p2-p5-p6-close pline (왼쪽 하단 직사각형)
    addPoly({ pt(0, 0), pt(0, d1), pt(50, d1), pt(50, 0) });

    // 16. p2-p3-p4-p5 pline (왼쪽 상단 ㄷ자)
    addOpen({ pt(0, d1), pt(0, d1 + d2), pt(50, d1 + d2), pt(50, d1) });

    // 17. p7-p8-p9-p10 pline (왼쪽 상단 날개 ㄷ자)
    addOpen({ pt(50, d1 + d2/2.0 + 30.0), pt(100, d1 + d2/2.0 + 30.0), pt(100, d1 + d2/2.0), pt(50, d1 + d2/2.0) });

    // 18. c1-c2 라인 (중심 수직선)
    addSeg(W / 2.0, d1 + d2, W / 2.0, 0);

    // 19. c3-c4-c6-c5-close (중앙 상단 50x30 직사각형), c5-c7-c8-c6 (아래 디귿자)
    addPoly({ pt(W / 2.0 - 25.0, d1 + d2/2.0 + 30.0), pt(W / 2.0 + 25.0, d1 + d2/2.0 + 30.0), pt(W / 2.0 + 25.0, d1 + d2/2.0), pt(W / 2.0 - 25.0, d1 + d2/2.0) });
    addOpen({ pt(W / 2.0 - 25.0, d1 + d2/2.0), pt(W / 2.0 - 25.0, d1 + d2/2.0 - 30.0), pt(W / 2.0 + 25.0, d1 + d2/2.0 - 30.0), pt(W / 2.0 + 25.0, d1 + d2/2.0) });

    // 20. 반대편 대칭 형상 생성 (X 대칭)
    // p1'-p2'-p5'-p6'-close
    addPoly({ pt(W, 0), pt(W, d1), pt(W - 50.0, d1), pt(W - 50.0, 0) });
    // p2'-p3'-p4'-p5'
    addOpen({ pt(W, d1), pt(W, d1 + d2), pt(W - 50.0, d1 + d2), pt(W - 50.0, d1) });
    // p7'-p8'-p9'-p10' (Y레벨 아래로 30만큼 엇갈림)
    addOpen({ pt(W - 50.0, d1 + d2/2.0), pt(W - 100.0, d1 + d2/2.0), pt(W - 100.0, d1 + d2/2.0 - 30.0), pt(W - 50.0, d1 + d2/2.0 - 30.0) });

    // 21. 상단 수평 연결선
    addSeg(100.0, d1 + d2/2.0 + 30.0, W / 2.0 - 25.0, d1 + d2/2.0 + 30.0); // p8-c3
    addSeg(100.0, d1 + d2/2.0, W / 2.0 - 25.0, d1 + d2/2.0);             // p9-c5
    addSeg(W / 2.0 + 25.0, d1 + d2/2.0, W - 100.0, d1 + d2/2.0);          // c6-p8'
    addSeg(W / 2.0 + 25.0, d1 + d2/2.0 - 30.0, W - 100.0, d1 + d2/2.0 - 30.0); // c8-p9'

    // 가이드 수평선 추가 (p4-p4', p5-p5', p6-p6')
    addSeg(50.0, d1 + d2, W - 50.0, d1 + d2); // p4-p4'
    addSeg(50.0, d1, W - 50.0, d1);           // p5-p5'
    addSeg(50.0, 0, W - 50.0, 0);             // p6-p6'

    // 22. 같은 방법으로 하단 생성
    // 왼쪽 하단 날개 ㄷ자 (p11-p12-p13-p14, Y레벨 위로 30만큼 엇갈림)
    addOpen({ pt(50.0, d1/2.0 + 30.0), pt(100.0, d1/2.0 + 30.0), pt(100.0, d1/2.0), pt(50.0, d1/2.0) });
    // 오른쪽 하단 날개 ㄷ자 (p11'-p12'-p13'-p14', Y레벨 아래로 30만큼 엇갈림)
    addOpen({ pt(W - 50.0, d1/2.0 - 30.0), pt(W - 100.0, d1/2.0 - 30.0), pt(W - 100.0, d1/2.0), pt(W - 50.0, d1/2.0) });
    // 하단 중앙 직사각형 블록
    addPoly({ pt(W / 2.0 - 25.0, d1/2.0 + 30.0), pt(W / 2.0 + 25.0, d1/2.0 + 30.0), pt(W / 2.0 + 25.0, d1/2.0), pt(W / 2.0 - 25.0, d1/2.0) });
    addOpen({ pt(W / 2.0 - 25.0, d1/2.0), pt(W / 2.0 - 25.0, d1/2.0 - 30.0), pt(W / 2.0 + 25.0, d1/2.0 - 30.0), pt(W / 2.0 + 25.0, d1/2.0) });
    // 하단 수평 연결선 (엇갈림 수정 반영)
    addSeg(100.0, d1/2.0 + 30.0, W / 2.0 - 25.0, d1/2.0 + 30.0);          // p12-cc3
    addSeg(100.0, d1/2.0, W / 2.0 - 25.0, d1/2.0);                        // p13-cc5
    addSeg(W / 2.0 + 25.0, d1/2.0, W - 100.0, d1/2.0);                    // cc6-p13'
    addSeg(W / 2.0 + 25.0, d1/2.0 - 30.0, W - 100.0, d1/2.0 - 30.0);      // cc8-p12'

    if (s_glass) {
        addSeg(100.0, d1 + d2/2.0 + 15.0, W / 2.0 - 25.0, d1 + d2/2.0 + 15.0);
        addSeg(W / 2.0 + 25.0, d1 + d2/2.0 - 15.0, W - 100.0, d1 + d2/2.0 - 15.0);
        addSeg(100.0, d1/2.0 + 15.0, W / 2.0 - 25.0, d1/2.0 + 15.0);
        addSeg(W / 2.0 + 25.0, d1/2.0 - 15.0, W - 100.0, d1/2.0 - 15.0);
    }

    updateGhostPreview(path, currentLayerName());
}

// ── 좌표 변환 헬퍼 ───────────────────────────────────────────────────────────
struct WinPtCalc {
    double ox, oy, c, s2;
    bool   flipY;
    MyVector operator()(double dx, double dy) const {
        if (!flipY)
            return MyVector(ox + dx*c - dy*s2, oy + dx*s2 + dy*c);
        else
            return MyVector(ox + dx*c + dy*s2, oy + dx*s2 - dy*c);
    }
};

// ── buildGeometry ─────────────────────────────────────────────────────────────
QList<QGraphicsItem*> PlanWindowTool::buildGeometry(const WindowBuildParams &pIn, IAppContext *ctx)
{
    // 지정 레이어가 도면에 없거나 비어 있으면 현재 레이어로 대체 (속성 편집 재생성 경로 포함)
    WindowBuildParams p = pIn;
    p.frameLayer = AbstractTool::resolveLayerName(ctx, p.frameLayer);
    p.otherLayer = AbstractTool::resolveLayerName(ctx, p.otherLayer);

    const double W  = p.W;
    const double d1 = p.D1;
    const double d2 = p.D2;

    const double c  = std::cos(qDegreesToRadians(p.angleDeg));
    const double s2 = std::sin(qDegreesToRadians(p.angleDeg));
    WinPtCalc    mp { p.ox, p.oy, c, s2, p.flipY };

    // 레이어 색·두께·선종류 적용, 레이어가 없으면 현재 레이어 → 흰색 순으로 대체
    const QPen framePen = AbstractTool::layerPenFor(ctx, p.frameLayer);
    const QPen otherPen = AbstractTool::layerPenFor(ctx, p.otherLayer);

    QList<QGraphicsItem*> items;

    if (p.variant >= 1) {
        const QList<TwoSashShape> shapes = twoSashShapes(W, p.win, p.glass, p.variant == 2, p.sashW, p.sashes, p.variant >= 3, p.variant == 4);
        const QString centerLayer = AbstractTool::resolveLayerName(ctx, DrawingSettingsCommand::s_centerLineLayer);
        const QPen    centerPen   = AbstractTool::layerPenFor(ctx, centerLayer);
        for (int i = 0; i < shapes.size(); ++i) {
            const TwoSashShape &sh = shapes.at(i);
            std::vector<MyVector> wpts;
            for (int k = 0; k < sh.pts.size(); ++k)
                wpts.push_back(mp(sh.pts.at(k).x(), sh.pts.at(k).y()));

            if (sh.kind == 1 || sh.kind == 2) {
                for (size_t k = 0; k + 1 < wpts.size(); ++k) {
                    LineItem *li = new LineItem(wpts.at(k), wpts.at(k + 1));
                    if (sh.kind == 2) {
                        // 중심선 — 도면틀 설정(도면설정)의 중심선 레이어 사용 (미지정이면 현재 레이어)
                        li->setLayerName(centerLayer);
                        li->setPen(centerPen);
                    } else {
                        li->setLayerName(p.otherLayer);
                        li->setPen(otherPen);
                    }
                    items << li;
                }
            } else {
                PolylineItem *pl = new PolylineItem(wpts, sh.closed);
                pl->setLayerName(p.frameLayer);
                pl->setPen(framePen);
                items << pl;
            }
        }
        return items;
    }

    items << new PolylineItem({ mp(0,0), mp(0,d1), mp(50,d1), mp(50,0) }, true);
    items << new PolylineItem({ mp(0,d1), mp(0,d1+d2), mp(50,d1+d2), mp(50,d1) }, false);
    items << new PolylineItem({ mp(50,d1+d2/2.0+30), mp(100,d1+d2/2.0+30), mp(100,d1+d2/2.0), mp(50,d1+d2/2.0) }, false);

    LineItem *centerLine = new LineItem(mp(W/2.0,d1+d2), mp(W/2.0,0));
    centerLine->setPen(otherPen);
    items << centerLine;

    items << new PolylineItem({ mp(W/2.0-25,d1+d2/2.0+30), mp(W/2.0+25,d1+d2/2.0+30), mp(W/2.0+25,d1+d2/2.0), mp(W/2.0-25,d1+d2/2.0) }, true);
    items << new PolylineItem({ mp(W/2.0-25,d1+d2/2.0), mp(W/2.0-25,d1+d2/2.0-30), mp(W/2.0+25,d1+d2/2.0-30), mp(W/2.0+25,d1+d2/2.0) }, false);
    items << new PolylineItem({ mp(W,0), mp(W,d1), mp(W-50,d1), mp(W-50,0) }, true);
    items << new PolylineItem({ mp(W,d1), mp(W,d1+d2), mp(W-50,d1+d2), mp(W-50,d1) }, false);
    items << new PolylineItem({ mp(W-50,d1+d2/2.0), mp(W-100,d1+d2/2.0), mp(W-100,d1+d2/2.0-30), mp(W-50,d1+d2/2.0-30) }, false);
    items << new LineItem(mp(100,d1+d2/2.0+30), mp(W/2.0-25,d1+d2/2.0+30));
    items << new LineItem(mp(100,d1+d2/2.0),    mp(W/2.0-25,d1+d2/2.0));
    items << new LineItem(mp(W/2.0+25,d1+d2/2.0),    mp(W-100,d1+d2/2.0));
    items << new LineItem(mp(W/2.0+25,d1+d2/2.0-30), mp(W-100,d1+d2/2.0-30));
    items << new LineItem(mp(50,d1+d2), mp(W-50,d1+d2));
    items << new LineItem(mp(50,d1),    mp(W-50,d1));
    items << new LineItem(mp(50,0),     mp(W-50,0));
    items << new PolylineItem({ mp(50,d1/2.0+30), mp(100,d1/2.0+30), mp(100,d1/2.0), mp(50,d1/2.0) }, false);
    items << new PolylineItem({ mp(W-50,d1/2.0-30), mp(W-100,d1/2.0-30), mp(W-100,d1/2.0), mp(W-50,d1/2.0) }, false);
    items << new PolylineItem({ mp(W/2.0-25,d1/2.0+30), mp(W/2.0+25,d1/2.0+30), mp(W/2.0+25,d1/2.0), mp(W/2.0-25,d1/2.0) }, true);
    items << new PolylineItem({ mp(W/2.0-25,d1/2.0), mp(W/2.0-25,d1/2.0-30), mp(W/2.0+25,d1/2.0-30), mp(W/2.0+25,d1/2.0) }, false);
    items << new LineItem(mp(100,d1/2.0+30),    mp(W/2.0-25,d1/2.0+30));
    items << new LineItem(mp(100,d1/2.0),        mp(W/2.0-25,d1/2.0));
    items << new LineItem(mp(W/2.0+25,d1/2.0),   mp(W-100,d1/2.0));
    items << new LineItem(mp(W/2.0+25,d1/2.0-30),mp(W-100,d1/2.0-30));

    // 유리 표현선 — 각 창짝 레일 사이 중간선
    if (p.glass) {
        items << new LineItem(mp(100,d1+d2/2.0+15),      mp(W/2.0-25,d1+d2/2.0+15));
        items << new LineItem(mp(W/2.0+25,d1+d2/2.0-15), mp(W-100,d1+d2/2.0-15));
        items << new LineItem(mp(100,d1/2.0+15),         mp(W/2.0-25,d1/2.0+15));
        items << new LineItem(mp(W/2.0+25,d1/2.0-15),    mp(W-100,d1/2.0-15));
    }

    // 레이어·펜 적용
    for (int i = 0; i < items.size(); ++i) {
        CadItem *cad = dynamic_cast<CadItem*>(items.at(i));
        if (!cad) continue;
        if (cad == centerLine) {
            cad->setLayerName(p.otherLayer);
        } else if (dynamic_cast<PolylineItem*>(items.at(i))) {
            cad->setLayerName(p.frameLayer); cad->setPen(framePen);
        } else {
            cad->setLayerName(p.otherLayer); cad->setPen(otherPen);
        }
    }
    return items;
}

// ── placeWindow ──────────────────────────────────────────────────────────────
void PlanWindowTool::placeWindow(const QPointF &p1, double angleDeg, double V, bool flipY)
{
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    if (!ctx)
        return;

    const double W  = curW();
    double d1 = s_d1;
    double d2 = s_d2;
    if (s_useWallThickness) {
        d1 = V / 2.0;
        d2 = V / 2.0;
    }

    double ox = p1.x(), oy = p1.y();
    const double c  = std::cos(qDegreesToRadians(angleDeg));
    const double s2 = std::sin(qDegreesToRadians(angleDeg));

    double wallNearLy = 0.0, wallFarLy = V;
    if (!s_useWallThickness && s_wallPositionMode != 0) {
        const double delta = computeWallPositionDeltaFromSpan(wallNearLy, wallFarLy);
        if (!flipY) { ox -= delta * s2; oy += delta * c; }
        else        { ox += delta * s2; oy -= delta * c; }
    }

    const QString frameLyr = resolveLayerName(getContext(), s_frameLayer);
    const QString otherLyr = resolveLayerName(getContext(), s_otherLayer);

    WindowBuildParams buildP;
    buildP.ox = ox; buildP.oy = oy; buildP.angleDeg = angleDeg;
    buildP.W = W; buildP.V = V; buildP.D1 = d1; buildP.D2 = d2;
    buildP.flipY = flipY;
    buildP.variant = m_variant;
    if (m_variant >= 1)
        for (int i = 0; i < 7; ++i) buildP.win[i] = curWin()[i];
    buildP.glass = s_glass;
    buildP.sashW = curSashW();
    buildP.sashes = m_sashes;
    buildP.frameLayer = frameLyr; buildP.otherLayer = otherLyr;

    QList<QGraphicsItem*> newEntities = buildGeometry(buildP, ctx);

    // 트림 후보 수집 (C++11 range-loop 제거, 인덱스 루프 사용)
    QList<LineItem *> trimCandidates;
    {
        const QList<QGraphicsItem *> allItems = m_scene->items();
        for (int i = 0; i < allItems.size(); ++i) {
            LineItem *li = dynamic_cast<LineItem *>(allItems.at(i));
            if (!li) continue;
            if (!li->data(ML_DATA_GROUP_ID).toString().isEmpty()) continue;
            if (!s_trimExcludeLayer.isEmpty() && li->layerName() == s_trimExcludeLayer) continue;
            trimCandidates << li;
        }
    }

    // ── 트림 Y 범위 결정 (props 저장 전에 계산) ──────────────────────────────
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

    // ── 그룹 UUID 태깅 + 레지스트리 등록 ─────────────────────────────────────
    const QString winUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString winName = tr("창문-%1").arg(++s_windowSeq);

    for (int i = 0; i < newEntities.size(); ++i)
        newEntities.at(i)->setData(PD_DATA_GROUP_ID, winUuid);

    // 공간벽 마감형태: 양쪽(P1-P2 가상선 / 너비 W 만큼 떨어진 가상선)의 space 선 수집 → 끊기/연결선 계획
    SpaceFinish::Plan finishL, finishR;
    SpaceFinish::planBoth(m_scene, s_wallGroupId, p1, m_p2, ox, oy, c, s2, W, finishL, finishR);

    SymbolInfo winInfo;
    winInfo.type                = "PlanWindow";
    winInfo.name                = winName;
    winInfo.props["W"]          = W;
    winInfo.props["V"]          = V;
    winInfo.props["D1"]         = d1;
    winInfo.props["D2"]         = d2;
    winInfo.props["ox"]         = ox;
    winInfo.props["oy"]         = oy;
    winInfo.props["angleDeg"]   = angleDeg;
    winInfo.props["flipY"]      = flipY ? 1 : 0;
    winInfo.props["Variant"]    = m_variant;
    winInfo.props["Glass"] = s_glass ? 1 : 0;
    winInfo.props["SashW"] = curSashW();
    winInfo.props["Sashes"] = m_sashes;
    for (int i = 0; i < 7; ++i)
        winInfo.props[QString("Win%1").arg(i + 1)] = (m_variant >= 1) ? curWin()[i] : 0.0;
    winInfo.props["FrameLayer"] = frameLyr;
    winInfo.props["OtherLayer"] = otherLyr;
    // 벽 트림 복원에 필요한 파라미터
    winInfo.props["wallGroupId"] = s_wallGroupId;
    winInfo.props["trimYLow"]    = trimYLow;
    winInfo.props["trimYHigh"]   = trimYHigh;
    winInfo.props["wallNearLy"]  = wallNearLy;
    winInfo.props["wallFarLy"]   = wallFarLy;
    // 속성 도크 형태 교체 시 벽 위치 옵션 재적용/마감선 재생성용
    winInfo.props["WallPosMode"]  = s_useWallThickness ? 0 : s_wallPositionMode;
    winInfo.props["ClosingLayer"] = s_closingLayer.isEmpty() ? s_otherLayer : s_closingLayer;
    SpaceFinish::storeProps(winInfo.props, finishL, finishR);

    ctx->undoStack()->beginMacro(tr("평면 창문 배치"));
    ctx->undoStack()->push(new RegisterSymbolCommand(m_scene->symbolRegistry(), winUuid, winInfo));

    for (int i = 0; i < newEntities.size(); ++i)
        ctx->undoStack()->push(new AddEntityCommand(m_scene, newEntities.at(i)));

    if (!s_wallGroupId.isEmpty()) {
        trimWallInWindowOpening(ox, oy, c, s2, trimYLow, trimYHigh, ctx->undoStack(),
                              finishL.extItems, finishR.extItems, SpaceFinish::extLength());
        SpaceFinish::addConnectors(m_scene, finishL, finishR, winUuid, ctx->undoStack());
    }
    // 벽 위치는 항상 P1/P2 기준이므로 다중선 유무와 무관하게 끊어진 벽선을 마감한다.
    if (!s_useWallThickness && s_wallPositionMode != 0)
        addWallClosingLines(ox, oy, c, s2, wallNearLy, wallFarLy, flipY,
                            winUuid, ctx->undoStack());
    trimGeneralLines(trimCandidates, ox, oy, c, s2, trimYLow, trimYHigh, ctx->undoStack());

    ctx->undoStack()->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            tr("평면 창문 배치 — P1(%1,%2)  W=%3  V=%4  각도=%5°")
                .arg(ox, 0, 'f', 1).arg(oy, 0, 'f', 1)
                .arg(W, 0, 'f', 1).arg(V, 0, 'f', 1)
                .arg(angleDeg, 0, 'f', 1),
            CommandDock::Info);
    }
}

// ── findNearestMultilineAt ───────────────────────────────────────────────────
LineItem *PlanWindowTool::findNearestMultilineAt(const QPointF &pos)
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
    for (int i = 0; i < allItems.size(); ++i) {
        LineItem *li = dynamic_cast<LineItem *>(allItems.at(i));
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

// ── findNearestMLWithDir ─────────────────────────────────────────────────────
LineItem *PlanWindowTool::findNearestMLWithDir(
    const QPointF &pos, double nx, double ny)
{
    if (!m_scene) return nullptr;

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

// ── trimWallInWindowOpening ──────────────────────────────────────────────────
void PlanWindowTool::trimWallInWindowOpening(
    double ox, double oy, double c, double s2,
    double yLow, double yHigh, QUndoStack *stack,
    const QList<LineItem *> &extLeftItems, const QList<LineItem *> &extRightItems,
    double extLen)
{
    if (!m_scene || s_wallGroupId.isEmpty())
        return;

    const double baseCutL = 0.0;
    const double baseCutR = curW();
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
    // C++11 range-loop 대신 인덱스 기반 루프 사용
    for (int i = 0; i < sceneItems.size(); ++i) {
        LineItem *li = dynamic_cast<LineItem *>(sceneItems.at(i));
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString() != s_wallGroupId) continue;
        if (li->data(ML_DATA_ROLE).toString() == "center") continue;
        // 방향 필터: 도어 방향(c,s2)에 평행한 세그먼트만 수집 (공유 그룹의 다른 벽 제외)
        const MyLine lnD = li->getLine();
        const double sdxD = lnD.end.x - lnD.start.x;
        const double sdyD = lnD.end.y - lnD.start.y;
        const double slenD = std::sqrt(sdxD * sdxD + sdyD * sdyD);
        if (slenD < 1e-9) continue;
        if (std::abs((sdxD / slenD) * c + (sdyD / slenD) * s2) < 0.5) continue;
        wallLines << li;
    }

    // C++11 range-loop 대신 인덱스 기반 루프 사용
    for (int i = 0; i < wallLines.size(); ++i) {
        LineItem *li = wallLines.at(i);
        // 공간벽 마감형태 대상 space 선은 개구부 양쪽 바깥(-X / +X)으로 extLen 만큼 더 끊는다
        const double cutL = extLeftItems.contains(li)  ? baseCutL - extLen : baseCutL;
        const double cutR = extRightItems.contains(li) ? baseCutR + extLen : baseCutR;
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

// ── trimGeneralLines ─────────────────────────────────────────────────────────
void PlanWindowTool::trimGeneralLines(
    const QList<LineItem *> &candidates,
    double ox, double oy, double c, double s2,
    double yLow, double yHigh,
    QUndoStack *stack)
{
    if (!m_scene || !stack || candidates.isEmpty())
        return;

    const double cutL = 0.0;
    const double cutR = curW();
    const double EPS  = 1e-6;
    const double yTol = 10.0;

    auto toLocal = [&](const MyVector &p) -> QPointF {
        const double dx = p.x - ox, dy = p.y - oy;
        return QPointF(dx * c + dy * s2, dy * c - dx * s2);
    };
    auto toWorld = [&](double lx, double ly) -> MyVector {
        return MyVector(ox + lx * c - ly * s2, oy + lx * s2 + ly * c);
    };

    // C++11 range-loop 대신 인덱스 기반 루프 사용
    for (int i = 0; i < candidates.size(); ++i) {
        LineItem *li = candidates.at(i);
        if (!li->scene())
            continue;

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

// ── checkWallPositionIfNeeded ────────────────────────────────────────────────
// 입력값 모드에서 P2 확정 직후 호출.
// |P2-P1| > 창문 두께(d1+d2) 이면 WallPositionDialog 표시.
// 다중선 유무와 무관하게 거리 조건만으로 판단.
// P1에서 다중선을 못 찾은 경우 P2 위치에서도 추가 탐색.
void PlanWindowTool::checkWallPositionIfNeeded()
{
    s_closingLayer = layerOfEntityAt(m_p2);
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

    if (windowThickness() >= dist) {
        // 창 두께 >= |P1P2| 이면 선택창 없이 P1 기준 내벽(돌출량만큼 돌출)으로 고정.
        // 벽 바깥면(P2)이 창 상단보다 멀면 상단 쪽 끊어진 벽선은 마감선으로 처리된다.
        s_wallPositionMode = WallPositionDialog::Inner;
        return;
    }

    IAppContext *ctx = getContext();
    QWidget *parentWin = ctx ? dynamic_cast<QWidget *>(ctx->commandDock()) : nullptr;

    WallPositionDialog dlg(static_cast<WallPositionDialog::Mode>(s_lastWallPosMode), parentWin);
    if (dlg.exec() != QDialog::Accepted) {
        s_wallGroupId.clear();
        return;
    }

    s_wallPositionMode = static_cast<int>(dlg.mode());
    s_lastWallPosMode  = s_wallPositionMode;
}

// ── isMLCompatibleDirection ──────────────────────────────────────────────────
bool PlanWindowTool::isMLCompatibleDirection(
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

// ── computeWallPositionDelta ─────────────────────────────────────────────────
double PlanWindowTool::computeWallPositionDelta(
    double c, double s2, const QPointF &rawOrigin, bool flipY,
    double &outNearLy, double &outFarLy)
{
    outNearLy = 0.0;
    const double windowThickness = this->windowThickness();
    const double prot            = wallProtrusion();
    outFarLy  = windowThickness;

    if (!m_scene || s_wallGroupId.isEmpty() || s_wallPositionMode == 0)
        return 0.0;

    double y_near =  1e18;
    double y_far  = -1e18;

    // P1→P2 법선 거리 + 100mm 를 탐색 한계로 사용 (공유 그룹 반대편 벽 제외)
    const double dp2x0 = m_p2.x() - rawOrigin.x();
    const double dp2y0 = m_p2.y() - rawOrigin.y();
    double p2ly0 = dp2y0 * c - dp2x0 * s2;
    if (flipY) p2ly0 = -p2ly0;
    const double maxLy = std::abs(p2ly0) + 100.0;

    const QList<QGraphicsItem *> sceneItems = m_scene->items();
    // C++11 range-loop 대신 인덱스 기반 루프 사용
    for (int i = 0; i < sceneItems.size(); ++i) {
        LineItem *li = dynamic_cast<LineItem *>(sceneItems.at(i));
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString() != s_wallGroupId) continue;
        if (li->data(ML_DATA_ROLE).toString() == "center") continue;

        const MyLine ln = li->getLine();
        // 방향 필터: 창문 방향(c,s2)에 평행한 세그먼트만 처리 (공유 그룹의 다른 벽 제외)
        const double sdxF = ln.end.x - ln.start.x;
        const double sdyF = ln.end.y - ln.start.y;
        const double slenF = std::sqrt(sdxF * sdxF + sdyF * sdyF);
        if (slenF < 1e-9) continue;
        if (std::abs((sdxF / slenF) * c + (sdyF / slenF) * s2) < 0.5) continue;

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
        delta = y_near - prot;
        break;
    case WallPositionDialog::Center:
        delta = (y_near + y_far) / 2.0 - windowThickness / 2.0;
        break;
    case WallPositionDialog::Outer:
        delta = y_far - (windowThickness - prot);
        break;
    }

    outNearLy = y_near - delta;
    outFarLy  = y_far  - delta;
    return delta;
}

// ── computeWallPositionDeltaFromSpan ────────────────────────────────────────
// 다중선 없이 일반선만 사용할 때의 위치 보정.
// s_wallDetectedSpan = |P2-P1| 거리를 벽 폭으로 사용하여 delta 계산.
double PlanWindowTool::computeWallPositionDeltaFromSpan(double &outNearLy, double &outFarLy)
{
    const double span            = s_wallDetectedSpan;
    const double windowThickness = this->windowThickness();
    const double prot            = wallProtrusion();
    outNearLy = 0.0;
    outFarLy  = span;

    if (s_wallPositionMode == 0 || span <= 0.0)
        return 0.0;

    double delta = 0.0;
    switch (s_wallPositionMode) {
    case WallPositionDialog::Inner:
        delta = -prot;
        break;
    case WallPositionDialog::Center:
        delta = (span - windowThickness) / 2.0;
        break;
    case WallPositionDialog::Outer:
        delta = span - (windowThickness - prot);
        break;
    }

    outNearLy = -delta;
    outFarLy  = span - delta;
    return delta;
}

// ── addWallClosingLines ──────────────────────────────────────────────────────
void PlanWindowTool::addWallClosingLines(
    double ox, double oy, double c, double s2,
    double wallNearLy, double wallFarLy, bool flipY,
    const QString &winUuid, QUndoStack *stack)
{
    if (!m_scene || !stack) return;

    const double V   = windowThickness();
    const double EPS = 1e-4;

    auto toWorld = [&](double lx, double ly) -> MyVector {
        if (!flipY)
            return MyVector(ox + lx * c - ly * s2, oy + lx * s2 + ly * c);
        else
            return MyVector(ox + lx * c + ly * s2, oy + lx * s2 - ly * c);
    };

    IAppContext *ctx = getContext();
    // 마감선 레이어: P2 점에 찍힌 객체의 레이어 (없으면 창문 나머지 레이어)
    const QString otherLyr = resolveLayerName(
        getContext(), s_closingLayer.isEmpty() ? s_otherLayer : s_closingLayer);

    const QPen pen = layerPenFor(ctx, otherLyr);

    auto addSeg = [&](double lx, double ly1, double ly2) {
        if (qAbs(ly2 - ly1) < EPS) return;
        LineItem *li = new LineItem(toWorld(lx, ly1), toWorld(lx, ly2));
        li->setLayerName(otherLyr);
        li->setPen(pen);
        if (!winUuid.isEmpty()) {
            li->setData(PD_DATA_GROUP_ID,     winUuid);
            li->setData(PD_DATA_CLOSING_LINE, winUuid);
        }
        stack->push(new AddEntityCommand(m_scene, li));
    };

    // 창틀이 벽면보다 돌출되는 것이 정상 → 벽이 창보다 안쪽/바깥쪽으로 나온 경우만 마감
    const bool nearGap = (wallNearLy < -EPS);
    const bool farGap  = (wallFarLy > V + EPS);

    if (nearGap) {
        addSeg(0.0, 0.0, wallNearLy);
        addSeg(curW(), 0.0, wallNearLy);
    }
    if (farGap) {
        addSeg(0.0, wallFarLy, V);
        addSeg(curW(), wallFarLy, V);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PlanWindowCommand
// ─────────────────────────────────────────────────────────────────────────────
PlanWindowCommand::PlanWindowCommand(IAppContext *ctx, int variant, int sashes, QObject *parent)
    : CommandBase(parent)
    , m_ctx(ctx)
    , m_variant(variant)
    , m_sashes(sashes)
{}

void PlanWindowCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new PlanWindowTool(nullptr, m_variant, m_sashes));
}
