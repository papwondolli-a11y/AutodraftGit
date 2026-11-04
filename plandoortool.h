#ifndef PLANDOORTOOL_H
#define PLANDOORTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"
#include <QList>
#include <QVector>
#include <QString>
#include <QVariantMap>

class LineItem;
class QGraphicsItem;
class QUndoStack;

struct DoorBuildParams {
    double  ox = 0, oy = 0, angleDeg = 0;
    double  H = 900, V = 200;
    bool    flipY = false;
    int     frameType = 0;
    int     doorStyle = 0;   // 0 = 기본 평면도어, 1 = door-113.lsp 형상 (30° 열린 문짝 + 호)
    QString frameLayer, otherLayer;
    // 쌍여닫이(PlanDoorDoubleTool) 전용 — 문1(시작점 P1측) 너비. 문2 너비 = H - h1.
    // 0 이하이면 미설정으로 간주하여 H/2 균등분할로 대체된다. PlanDoorTool(단일문)은 사용하지 않는다.
    double  h1 = 0;
};

// door-113 문짝 형상 헬퍼 (PlanDoorTool·PlanDoorDoubleTool 공용)
//   door113ArcSpanDeg : 닫힘선(0°)에서 문짝 끝까지의 호 각도(°)
//   door113LeafLocal  : 경첩 기준 문짝 4점 local 오프셋 (X=문 진행, Y=P2 방향)
double door113ArcSpanDeg(double R, double T);
void   door113LeafLocal(double R, double T, double out[4][2]);

// ─────────────────────────────────────────────────────────────────────────────
// PlanDoorTool — 평면도어 그리기 툴 (3점 입력 방식)
//
// 입력 흐름:
//   P1 (nea 스냅 자동):  내벽 점 — 경첩 끝 (local X=0)
//   P2 (per 스냅 자동):  외벽 점 — P1→P2 = 두께 방향 + 스윙 방향
//   P3 (nea 스냅 자동):  방향점  — P1→P3 = 문틀 진행 방향(각도)
//
// V 모드:
//   입력값 모드  (s_useWallThickness=false): V = Setup 설정값,  P2 = 방향만
//   벽두께 모드  (s_useWallThickness=true) : V = |dot(P2-P1,n)|, P2 = 두께+방향
//
// 우클릭:  WaitP3→WaitP2→WaitP1 단계 되돌리기,  WaitP1 = 툴 종료
// ─────────────────────────────────────────────────────────────────────────────
class PlanDoorTool : public AbstractTool {
    Q_OBJECT

public:
    explicit PlanDoorTool(QObject *parent = nullptr, int doorStyle = 0);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    bool processCommand(const QString &command) override;

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

    static QList<QGraphicsItem*> buildGeometry(const DoorBuildParams &p, IAppContext *ctx);

    // 도어 삭제 시 트림된 벽선을 원래대로 복원 (좌·우 조각 병합)
    static void restoreWallOpening(MyGraphicsScene *scene,
                                   const QString &wallGroupId,
                                   double ox, double oy, double c, double s2,
                                   double H,
                                   double trimYLow, double trimYHigh,
                                   QUndoStack *stack,
                                   double leftExt = 0.0,    // 공간벽 마감형태로 좌측(-X)으로 더 끊어낸 길이
                                   double rightExt = 0.0);  // 우측(+X)으로 더 끊어낸 길이

    // 도어 너비 변경 시 래치쪽 벽선 끝점을 H_old → H_new 로 이동
    static void adjustLatchWallEndpoints(MyGraphicsScene *scene,
                                         const QString &wallGroupId,
                                         double ox, double oy, double c, double s2,
                                         double H_old, double H_new,
                                         double trimYLow, double trimYHigh,
                                         QUndoStack *stack,
                                         double rightExt = 0.0);   // 공간벽 마감형태 우측 확장 길이

    // addWallClosingLines 의 정적 버전 (propertydock 에서 호출)
    static void addClosingLinesStatic(MyGraphicsScene *scene, IAppContext *ctx,
                                      double ox, double oy, double c, double s2,
                                      bool flipY, double H, double V,
                                      double wallNearLy, double wallFarLy,
                                      const QString &otherLayer,
                                      const QString &doorUuid,
                                      QUndoStack *stack);

    static CommandBase *createCommand(IAppContext *c);
    static CommandBase *createCommand113(IAppContext *c);   // door-113.lsp 형상

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
    // extLeftItems 는 -X 쪽으로, extRightItems 는 +X 쪽으로 extLen 만큼 더 끊어낸다 (공간벽 마감형태 swtype2~4)
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

    int     m_style = 0;   // DoorBuildParams::doorStyle
    State   m_state = WaitP1;
    QPointF m_p1;
    QPointF m_p2;
    QPointF m_currentMousePos;
    QPointF m_p1Anchor;   // Shift+클릭 앵커점
    QPointF m_p1WallDir;  // 앵커에서의 벽 방향 (정규화, 0,0=미탐지)
    QVector<QPointF> m_p1WallDirs; // 앵커를 지나는 벽 방향 후보 (모서리면 2개 이상)

    // 툴 진입 시 저장한 스냅 설정 (비활성화 시 복원)
    bool m_savedNearest = false;
    bool m_savedPerp    = false;
    bool m_snapSaved    = false;
};

// ─────────────────────────────────────────────────────────────────────────────
class PlanDoorCommand : public CommandBase {
    Q_OBJECT

public:
    explicit PlanDoorCommand(IAppContext *ctx, int doorStyle = 0, QObject *parent = nullptr);
    void execute() override;

private:
    IAppContext *m_ctx;
    int          m_style;
};

#endif // PLANDOORTOOL_H
