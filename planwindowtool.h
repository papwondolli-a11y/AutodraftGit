#ifndef PLANWINDOWTOOL_H
#define PLANWINDOWTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"
#include <QList>
#include <QString>

class LineItem;
class QGraphicsItem;
class QUndoStack;
class MyGraphicsScene;

struct WindowBuildParams {
    double  ox = 0, oy = 0, angleDeg = 0;
    double  W = 1200, V = 200, D1 = 100, D2 = 150;
    bool    flipY = false;
    double  win[7] = { 40, 50, 150, 30, 100, 35, 30 };   // 2짝 창 치수 Win1~Win7 (win-1.lsp)
    int     sashes = 2;           // 슬라이딩 창짝 수 (2, 3, 4)
    double  sashW = 0.0;          // 슬라이딩 창문(짝1) 넓이, 0 = 같은 크기
    bool    glass = false;        // 2짝 창 유리 표현선 (win-1.lsp @winYS)
    int     variant = 0;          // 0 = 예전 하드코딩 형상, 1 = 단창(2짝, win-1.lsp), 2 = 이중창(하단에도 창짝 세트), 3 = 상단부만
    QString frameLayer, otherLayer;
};

// ─────────────────────────────────────────────────────────────────────────────
// PlanWindowTool — 평면 창문(미서기창) 그리기 툴 (3점 입력 방식)
// ─────────────────────────────────────────────────────────────────────────────
class PlanWindowTool : public AbstractTool {
    Q_OBJECT

public:
    explicit PlanWindowTool(QObject *parent = nullptr, int variant = 0, int sashes = 2);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    bool processCommand(const QString &command) override;

    // 단창/이중창 설정을 바꾼 뒤 현재 도구의 형상 변형(m_variant)에 반영 (AI(MCP) 호출용)
    void refreshVariant() { syncVariant(); }

    static double  globalW4();
    static void    setGlobalW4(double val);
    static double  sashWidth4();
    static void    setSashWidth4(double v);
    static double  globalW3();
    static void    setGlobalW3(double val);
    static double  sashWidth3();
    static void    setSashWidth3(double v);
    static double  globalW();
    static void    setGlobalW(double val);
    static double  globalD1();
    static void    setGlobalD1(double val);
    static double  globalD2();
    static void    setGlobalD2(double val);
    static double  winParam(int idx);                // idx 0..6 = Win1..Win7
    static void    setWinParam(int idx, double val);
    static bool    sashDouble();                     // true = 이중창, false = 단창
    static void    setSashDouble(bool dbl);
    static double  sashWidth();                // 0 = 두 짝 같은 크기
    static void    setSashWidth(double v);
    static bool    glass();
    static void    setGlass(bool on);
    static QString frameLayer();
    static void    setFrameLayer(const QString &name);
    static QString otherLayer();
    static void    setOtherLayer(const QString &name);
    static QString trimExcludeLayer();
    static void    setTrimExcludeLayer(const QString &name);
    static bool    useWallThickness();
    static void    setUseWallThickness(bool use);

    static void    loadSettings(MyGraphicsScene *scene);   // 도면(ToolDefaults)에서 전 설정 로드
    static void    saveSettings(MyGraphicsScene *scene);   // 창문설정 다이얼로그에서 저장용

    static QList<QGraphicsItem*> buildGeometry(const WindowBuildParams &p, IAppContext *ctx);
    static CommandBase *createCommand(IAppContext *c);
    static CommandBase *createCommandThreeSash(IAppContext *c);
    static CommandBase *createCommandFourSash(IAppContext *c);
    static CommandBase *createCommandTopOnly(IAppContext *c);   // 2짝 상단부만 생성 (variant 3)
    static CommandBase *createCommandTopOnlyThreeSash(IAppContext *c);   // 3짝 상단부만
    static CommandBase *createCommandTopOnlyFourSash(IAppContext *c);    // 4짝 상단부만
    static CommandBase *createCommandTopOnlyLinked(IAppContext *c);      // 4짝 상단부 옆: 반대편 슬라이딩 프레임 연결형 (variant 4)
    static double  winParamTop(int idx);             // 상단부 전용 Win1..Win7
    static void    setWinParamTop(int idx, double val);

protected:
    void onFinish() override;
    void onCancel() override;

private:
    enum State { WaitP1, WaitP2, WaitP3, WaitP1Distance };

    QString   layerOfEntityAt(const QPointF &pos) const;   // pos 에 걸린 선의 레이어 (없으면 빈 문자열)
    void      syncVariant();             // 설정(단창/이중창)에 맞춰 m_variant 갱신
    double    curW() const;               // 현재 도구의 창문 넓이 (2짝: s_w, 3짝: s_w3)
    double    curSashW() const;           // 현재 도구의 슬라이딩 창문 넓이
    double   *curWin() const;             // 현재 도구의 Win1~Win7 배열 (상단부 전용이면 s_winTop)
    double    windowThickness() const;   // 벽 위치 판단용 창 두께 (2짝: Win3+Win5, 그 외: D1+D2)
    double    wallProtrusion()  const;   // 벽면 기준 창틀 돌출량 (2짝: Win1, 그 외: 0)
    bool      computeFrom3Pts(const QPointF &p1, const QPointF &p2, const QPointF &p3,
                               double &outAngleDeg, double &outV, bool &outFlipY) const;
    void      placeWindow(const QPointF &p1, double angleDeg, double V, bool flipY);
    void      updateGhostWindow(const QPointF &p1, double angleDeg, double V, bool flipY);
    void      showP1Prompt() const;
    void      showP1DistancePrompt() const;
    void      showP2Prompt() const;
    void      showP3Prompt() const;
    QPointF   computeP1FromAnchor(double dist) const;
    void      applySnapForState(State s);
    void      restoreSavedSnap();
    LineItem *findNearestMultilineAt(const QPointF &pos);
    LineItem *findNearestMLWithDir(const QPointF &pos, double nx, double ny);
    void      trimWallInWindowOpening(double ox, double oy, double c, double s2,
                                      double yLow, double yHigh, QUndoStack *stack,
                                    const QList<LineItem *> &extLeftItems = QList<LineItem *>(),
                                    const QList<LineItem *> &extRightItems = QList<LineItem *>(),
                                    double extLen = 0.0);
    void      checkWallPositionIfNeeded();
    bool      isMLCompatibleDirection(const QString &groupId, double nx, double ny) const;
    double    computeWallPositionDelta(double c, double s2, const QPointF &rawOrigin,
                                       bool flipY,
                                       double &outNearLy, double &outFarLy);
    double    computeWallPositionDeltaFromSpan(double &outNearLy, double &outFarLy);
    void      addWallClosingLines(double ox, double oy, double c, double s2,
                                  double wallNearLy, double wallFarLy, bool flipY,
                                  const QString &winUuid, QUndoStack *stack);
    void      trimGeneralLines(const QList<LineItem *> &candidates,
                               double ox, double oy, double c, double s2,
                               double yLow, double yHigh,
                               QUndoStack *stack);

    static double  s_w;
    static double  s_w3;         // 3짝 창문 넓이 (초기 2400)
    static double  s_w4;         // 4짝 창문 넓이 (초기 2400)
    static double  s_sashW4;     // 4짝 안쪽창 1개 넓이 (0 = 4개 같은 크기)
    static double  s_sashW3;     // 3짝 중앙창 넓이 (0 = 3개 같은 크기)
    static double  s_d1;
    static double  s_d2;
    static bool    s_useWallThickness;
    static double  s_win[7];
    static double  s_winTop[7];  // 상단부 전용 (Win1 초기 20, Win2/Win3 미사용)
    static bool    s_double;
    static QString s_closingLayer;   // 마감선 레이어 = P2 점에 찍힌 객체의 레이어
    static bool    s_glass;
    static QString s_frameLayer;
    static QString s_otherLayer;
    static QString s_trimExcludeLayer;
    static double  s_sashW;
    static QString s_wallGroupId;
    static int     s_wallPositionMode;
    static int     s_lastWallPosMode;
    static double  s_wallDetectedSpan;
    static bool    s_mlAtP1;

    int     m_variant = 0;
    int     m_sashes  = 2;
    State   m_state = WaitP1;
    QPointF m_p1;
    QPointF m_p2;
    QPointF m_currentMousePos;
    QPointF m_p1Anchor;   // Shift+클릭 앵커점
    QPointF m_p1WallDir;  // 앵커에서의 벽 방향 (정규화, 0,0=미탐지)

    bool m_savedNearest = false;
    bool m_savedPerp    = false;
    bool m_snapSaved    = false;
};

// ─────────────────────────────────────────────────────────────────────────────
class PlanWindowCommand : public CommandBase {
    Q_OBJECT

public:
    explicit PlanWindowCommand(IAppContext *ctx, int variant = 0, int sashes = 2, QObject *parent = nullptr);
    void execute() override;

private:
    IAppContext *m_ctx;
    int          m_variant;
    int          m_sashes;
};

#endif // PLANWINDOWTOOL_H
