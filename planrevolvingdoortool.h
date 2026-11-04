#ifndef PLANREVOLVINGDOORTOOL_H
#define PLANREVOLVINGDOORTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"
#include <QList>
#include <QString>

class LineItem;
class QGraphicsItem;
class QUndoStack;

// ─────────────────────────────────────────────────────────────────────────────
// RevolvingDoorBuildParams — 회전문 생성 파라미터.
// PlanDoorTool 계열의 DoorBuildParams(frameType·h1 등 문짝 스윙 전용 필드)와는
// 형상이 근본적으로 달라(원형 인클로저 + N개 회전 날개) 별도 구조체로 정의한다.
// ─────────────────────────────────────────────────────────────────────────────
struct RevolvingDoorBuildParams {
    double  ox = 0, oy = 0, angleDeg = 0;
    double  H = 2400, V = 200;     // H: 개구부 전체 폭(Setup "문 크기"), V: 벽 두께
    bool    flipY = false;          // 벽 위치(내/외) 원점 보정에만 사용, 형상 자체는 대칭
    int     wingCount = 4;          // 날개 개수 (3 또는 4)
    QString frameLayer, otherLayer;
};

// ─────────────────────────────────────────────────────────────────────────────
// PlanRevolvingDoorTool — 평면 회전문 그리기 툴
//
// 입력 흐름은 PlanDoorBiSwingTool 과 동일한 3점 입력(P1/P2/P3)을 그대로 사용한다.
//   P1 (nea 스냅) : 문틀이 들어갈 벽선 위 점 — local X = 0, local Y = 0(근접면)
//   P2 (per 스냅) : 반대편 벽선 점 — local Y = V(원접면), 두께 측정용
//   P3 (nea 스냅) : 방향점 — 문틀 진행 방향(각도) 결정
//
// 회전축 중심 = local (H/2, V/2). 문틀(Type4 고정, 45mm)이 양쪽에 생성되고,
// 그 안쪽으로 두께 45mm 의 원형 인클로저(외벽호, 좌/우 두 개의 180° 호)가 있으며,
// 다시 그 안쪽에서 회전축까지 N개(3 또는 4, Setup 설정)의 날개(두께 30mm)가
// X자(4개일 때) 형태로 뻗어 있다.
//
//   R_outer = H/2 - J(45)                (외벽호 외경)
//   R_inner = R_outer - 45(외벽호 두께)   (외벽호 내경)
//   R_wing  = R_inner - 50(여유)          (날개 길이 = 회전축~날개 끝)
// ─────────────────────────────────────────────────────────────────────────────
class PlanRevolvingDoorTool : public AbstractTool {
    Q_OBJECT

public:
    explicit PlanRevolvingDoorTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    bool processCommand(const QString &command) override;

    // ── Setup 다이얼로그 값 접근자 (QSettings 그룹: "PlanRevolvingDoorTool") ──
    static double  globalH();
    static void    setGlobalH(double val);
    static double  globalV();
    static void    setGlobalV(double val);
    static int     wingCount();
    static void    setWingCount(int n);
    static QString frameLayer();
    static void    setFrameLayer(const QString &name);
    static QString otherLayer();
    static void    setOtherLayer(const QString &name);
    static QString trimExcludeLayer();
    static void    setTrimExcludeLayer(const QString &name);
    static bool    useWallThickness();
    static void    setUseWallThickness(bool use);

    static QList<QGraphicsItem*> buildGeometry(const RevolvingDoorBuildParams &p, IAppContext *ctx);

    static CommandBase *createCommand(IAppContext *c);

protected:
    void onFinish() override;
    void onCancel() override;

private:
    enum State { WaitP1, WaitP2, WaitP3, WaitP1Distance };

    bool      computeFrom3Pts(const QPointF &p1, const QPointF &p2, const QPointF &p3,
                               double &outAngleDeg, double &outV, bool &outFlipY) const;
    void      placeDoor(const QPointF &p1, double angleDeg, double V, bool flipY);
    void      updateGhostDoor(const QPointF &p1, double angleDeg, double V, bool flipY);
    void      showP1Prompt() const;
    void      showP1DistancePrompt() const;
    void      showP2Prompt() const;
    void      showP3Prompt() const;
    QPointF   computeP1FromAnchor(double dist) const;
    void      applySnapForState(State s);
    void      restoreSavedSnap();
    LineItem *findNearestMultilineAt(const QPointF &pos);
    LineItem *findNearestMLWithDir(const QPointF &pos, double nx, double ny);
    void      trimWallInDoorOpening(double ox, double oy, double c, double s2,
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
                                  const QString &doorUuid, QUndoStack *stack);
    void      trimGeneralLines(const QList<LineItem *> &candidates,
                               double ox, double oy, double c, double s2,
                               double yLow, double yHigh,
                               QUndoStack *stack);

    static double  s_h;
    static double  s_v;
    static bool    s_useWallThickness;
    static int     s_wingCount;
    static QString s_wallGroupId;
    static int     s_wallPositionMode;
    static int     s_lastWallPosMode;
    static double  s_wallDetectedSpan;
    static bool    s_mlAtP1;

    State   m_state = WaitP1;
    QPointF m_p1;
    QPointF m_p2;
    QPointF m_currentMousePos;
    QPointF m_p1Anchor;   // Shift+클릭 앵커점
    QPointF m_p1WallDir;  // 앵커에서의 벽 방향 (정규화, 0,0=미탐지)

    // 툴 진입 시 저장한 스냅 설정 (비활성화 시 복원)
    bool m_savedNearest = false;
    bool m_savedPerp    = false;
    bool m_snapSaved    = false;
};

// ─────────────────────────────────────────────────────────────────────────────
class PlanRevolvingDoorCommand : public CommandBase {
    Q_OBJECT

public:
    explicit PlanRevolvingDoorCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

private:
    IAppContext *m_ctx;
};

#endif // PLANREVOLVINGDOORTOOL_H
