#ifndef PLANDOORDOUBLEBISWINGTOOL_H
#define PLANDOORDOUBLEBISWINGTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"
#include "plandoortool.h"   // DoorBuildParams 구조체(h1 필드 포함)를 그대로 재사용
#include <QList>
#include <QVector>
#include <QString>

class LineItem;
class QGraphicsItem;
class QUndoStack;

// ─────────────────────────────────────────────────────────────────────────────
// PlanDoorDoubleBiSwingTool — 평면 양방쌍여닫이 그리기 툴
//
// PlanDoorDoubleTool(쌍여닫이: 문1/문2 개별 너비 입력) 과
// PlanDoorBiSwingTool(양방향여닫이: 경첩이 벽 중심선에 위치, 180° 단일호)을
// 결합한 툴. 문짝 2개(문1=P1측, 문2)가 각자 자신의 경첩을 벽 두께 정중앙에 두고
// 180°(양쪽 방향)로 스윙하며, 두 문짝은 분할점(local X = H1)에서 맞닿는다.
//
//   P1 (nea 스냅) : 문틀이 들어갈 벽선 위 점 — local X = 0, local Y = 0(근접면)
//   P2 (per 스냅) : 반대편 벽선 점 — local Y = V(원접면), 두께 측정용
//   P3 (nea 스냅) : 방향점 — 문틀 진행 방향(각도) 결정
//
// 문1(좌측, P1측) : 경첩 local X = J(근접 잼브에서 잼브두께만큼 안쪽), 반경 R1 = H1 - J
// 문2(우측)      : 경첩 local X = H-J(원접 잼브에서 안쪽), 반경 R2 = H2 - J
// 두 문짝 모두 경첩 local Y = V/2(벽 두께 정중앙)에서 180° 단일호로 스윙 —
// PlanDoorBiSwingTool 과 동일한 원리를 좌우 대칭으로 확장한 것.
// ─────────────────────────────────────────────────────────────────────────────
class PlanDoorDoubleBiSwingTool : public AbstractTool {
    Q_OBJECT

public:
    explicit PlanDoorDoubleBiSwingTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    bool processCommand(const QString &command) override;

    // ── Setup 다이얼로그 값 접근자 (QSettings 그룹: "PlanDoorDoubleBiSwingTool") ──
    static double  globalH1();
    static void    setGlobalH1(double val);
    static double  globalH2();
    static void    setGlobalH2(double val);
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

    // 문틀 + 문짝 2개(각 180° 스윙) 생성 (DoorBuildParams 는 PlanDoorTool 과 공용)
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

    static double  s_h1;   // 문1(시작점 P1측) 너비
    static double  s_h2;   // 문2 너비
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
class PlanDoorDoubleBiSwingCommand : public CommandBase {
    Q_OBJECT

public:
    explicit PlanDoorDoubleBiSwingCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

private:
    IAppContext *m_ctx;
};

#endif // PLANDOORDOUBLEBISWINGTOOL_H
