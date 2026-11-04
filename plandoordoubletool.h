#ifndef PLANDOORDOUBLETOOL_H
#define PLANDOORDOUBLETOOL_H

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
// PlanDoorDoubleTool — 평면 쌍여닫이(양개문) 그리기 툴
//
// PlanDoorTool 과 완전히 동일한 3점 입력(P1/P2/P3) 상태머신을 사용하되,
// 문짝을 좌우 대칭 2개로 나누어 배치한다는 점만 다르다.
//
//   P1 (nea 스냅) : 문틀이 들어갈 벽선 위 점 — local X = 0
//   P2 (per 스냅) : 반대쪽 벽선 점 — 두께 + 스윙 방향 결정
//   P3 (nea 스냅) : 방향점 — 문틀 진행 방향(각도) 결정
//
// 문틀(잼브)·연결선·벽 트림 로직은 PlanDoorTool 과 동일하게 H(전체 폭 = H1+H2), V(문틀 두께)를 사용한다.
// 문짝 2개는 문1(H1, 시작점 P1측)·문2(H2)로 각각 너비를 입력받으며, 같은 방향(flipY)으로 열려
// 중앙 분할점(local X = H1)에서 맞닿는다.
//   - 문1(좌측, P1측) 경첩 : local X = STOP_WIDTH,       반경 R1 = H1 - STOP_WIDTH
//   - 문2(우측)      경첩 : local X = H - STOP_WIDTH,   반경 R2 = H2 - STOP_WIDTH
//
// 벽 트림에 필요한 restoreWallOpening / adjustLatchWallEndpoints / addClosingLinesStatic 는
// 문짝 개수와 무관한 순수 기하 로직이므로 PlanDoorTool 의 정적 함수를 그대로 재사용한다
// (propertydock.cpp 참고).
// ─────────────────────────────────────────────────────────────────────────────
class PlanDoorDoubleTool : public AbstractTool {
    Q_OBJECT

public:
    explicit PlanDoorDoubleTool(QObject *parent = nullptr, int doorStyle = 0);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    bool processCommand(const QString &command) override;

    // ── Setup 다이얼로그 값 접근자 (QSettings 그룹: "PlanDoorDoubleTool") ──────
    // 문1(시작점 P1측) / 문2 너비를 각각 별도로 저장한다 (전체 폭 H = H1 + H2).
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

    // 문틀 + 좌우 문짝 2개 + 스윙호 2개 생성 (DoorBuildParams 는 PlanDoorTool 과 공용)
    static QList<QGraphicsItem*> buildGeometry(const DoorBuildParams &p, IAppContext *ctx);

    static CommandBase *createCommand(IAppContext *c);
    static CommandBase *createCommand114(IAppContext *c);   // door-113 형상 쌍여닫이

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

    int     m_style = 0;   // DoorBuildParams::doorStyle (1 = door-113 형상)
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
class PlanDoorDoubleCommand : public CommandBase {
    Q_OBJECT

public:
    explicit PlanDoorDoubleCommand(IAppContext *ctx, int doorStyle = 0, QObject *parent = nullptr);
    void execute() override;

private:
    IAppContext *m_ctx;
    int          m_style;
};

#endif // PLANDOORDOUBLETOOL_H
