#ifndef PLANDOORBISWINGTOOL_H
#define PLANDOORBISWINGTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"
#include "plandoortool.h"   // DoorBuildParams 구조체를 그대로 재사용
#include <QList>
#include <QVector>
#include <QString>

class LineItem;
class QGraphicsItem;
class QUndoStack;

// ─────────────────────────────────────────────────────────────────────────────
// PlanDoorBiSwingTool — 평면 양방향 여닫이(자재문) 그리기 툴
//
// PlanDoorTool 과 완전히 동일한 3점 입력(P1/P2/P3) 상태머신·Setup 다이얼로그
// (PlanDoorSetupDialog 재사용)·벽 트림 로직을 사용하되, 문짝이 한쪽으로만
// 열리는 대신 경첩을 기준으로 양쪽 방향(안쪽·바깥쪽) 모두로 90°씩,
// 총 180° 를 하나의 연속된 호로 스윙하는 자재문(자유정첩문) 심볼을 생성한다.
//
//   P1 (nea 스냅) : 문틀이 들어갈 벽선 위 점 — local X = 0, local Y = 0(근접면)
//   P2 (per 스냅) : 반대편 벽선 점 — local Y = V(원접면), 두께 측정용
//   P3 (nea 스냅) : 방향점 — 문틀 진행 방향(각도) 결정
//
// 경첩은 벽 두께의 정중앙(local Y = V/2)에 위치하며, 문짝(직선 1개)은
// 경첩을 지나 근접면 바깥(-R)과 원접면 바깥(+R) 양쪽으로 뻗은 하나의 직선으로,
// 스윙호는 그 두 극단(local Y = V/2±R)을 문틀 진행 방향(local +X, 닫힘 기준선)을
// 지나며 잇는 180° 호 하나로 표현한다. flipY 는 문틀(잼브) 위치·벽 두께 계산에는
// 그대로 사용되지만, 호는 좌우 대칭이라 flipY 부호와 무관하게 동일한 반원을 그린다.
// ─────────────────────────────────────────────────────────────────────────────
class PlanDoorBiSwingTool : public AbstractTool {
    Q_OBJECT

public:
    explicit PlanDoorBiSwingTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    bool processCommand(const QString &command) override;

    // ── Setup 다이얼로그 값 접근자 (QSettings 그룹: "PlanDoorBiSwingTool") ────
    static double  globalH();
    static void    setGlobalH(double val);
    static double  globalV();
    static void    setGlobalV(double val);
    static int     frameType();
    static void    setFrameType(int t);
    static QString frameLayer();
    static void    setFrameLayer(const QString &name);
    static QString otherLayer();
    static void    setOtherLayer(const QString &name);
    static QString trimExcludeLayer();
    static void    setTrimExcludeLayer(const QString &name);
    static bool    useWallThickness();
    static void    setUseWallThickness(bool use);

    // 문틀 + 문짝(직선 1개) + 180° 스윙호 생성 (DoorBuildParams 는 PlanDoorTool 과 공용)
    static QList<QGraphicsItem*> buildGeometry(const DoorBuildParams &p, IAppContext *ctx);

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
    void      captureP1WallDirs(const QPointF &pos);
    QPointF   resolveP1WallDir() const;
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
    static int     s_frameType;
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
    QVector<QPointF> m_p1WallDirs; // wall direction candidates at anchor (2+ at a corner)

    // 툴 진입 시 저장한 스냅 설정 (비활성화 시 복원)
    bool m_savedNearest = false;
    bool m_savedPerp    = false;
    bool m_snapSaved    = false;
};

// ─────────────────────────────────────────────────────────────────────────────
class PlanDoorBiSwingCommand : public CommandBase {
    Q_OBJECT

public:
    explicit PlanDoorBiSwingCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

private:
    IAppContext *m_ctx;
};

#endif // PLANDOORBISWINGTOOL_H
