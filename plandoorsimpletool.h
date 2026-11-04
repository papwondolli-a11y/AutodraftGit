#ifndef PLANDOORSIMPLETOOL_H
#define PLANDOORSIMPLETOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"
#include <QList>
#include <QPointF>
#include <QVector>
#include <QString>

class LineItem;
class MyGraphicsScene;
class QUndoStack;

// 단순문 종류 (문틀·프레임 없는 4가지 문 형태)
enum SimpleDoorKind {
    SimpleSingle = 0,        // 외여닫이 (90° 호 1개)
    SimpleDouble,            // 쌍여닫이 (문1·문2 각 90° 호)
    SimpleBiSwing,           // 양방향여닫이 (벽 중심 경첩, 180° 호)
    SimpleDoubleBiSwing      // 양방쌍여닫이 (문1·문2 각 180° 호)
};

// 단순문 형상 생성 파라미터 (문틀·문틀 프레임 없이 문짝선 + 스윙호만)
struct SimpleDoorBuildParams {
    double  ox = 0, oy = 0, angleDeg = 0;
    double  H = 900, V = 200;      // H: 문 너비, V: 벽 두께
    bool    flipY = false;
    QString layer;                 // 문짝선 레이어 (도어 레이어설정의 "문틀")
    QString arcLayer;              // 스윙 호 레이어 (도어 레이어설정의 "나머지")
    SimpleDoorKind kind = SimpleSingle;   // 쌍여닫이류는 H 를 문1=문2=H/2 로 균등 분할
};

// ─────────────────────────────────────────────────────────────────────────────
// PlanDoorSimpleTool — 단순문 그리기 툴 (3점 입력 방식, PlanDoorTool 과 동일 흐름)
//
//   P1 : 벽선 위 점 — 경첩 끝 (local X=0)
//   P2 : 반대편 벽선 점 — 벽 두께 + 스윙 방향 (항상 벽 두께 값 사용)
//   P3 : 방향점 — P1→P3 = 문 진행 방향(각도)
//
// 형상: 문짝 선 1개 + 스윙 호 1개. 문틀/프레임/연결선 없음, 문짝선=문틀 레이어, 호=나머지 레이어 (도어 레이어설정).
// 벽 트림: 다중선 벽 + 일반선 트림, 트림 제외 레이어 지원.
// ─────────────────────────────────────────────────────────────────────────────
class PlanDoorSimpleTool : public AbstractTool {
    Q_OBJECT

public:
    explicit PlanDoorSimpleTool(SimpleDoorKind kind = SimpleSingle, QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    bool processCommand(const QString &command) override;

    static double  globalH();
    static void    setGlobalH(double val);

    static QList<QGraphicsItem*> buildGeometry(const SimpleDoorBuildParams &p, IAppContext *ctx);

    // 트림된 벽 개구부 양끝(local X=0, X=H)을 벽 두께 방향으로 잇는 마감선 2개 추가
    // (propertydock 재빌드에서도 호출). layer 가 비어 있으면 현재 레이어로 대체.
    static void addClosingLinesStatic(MyGraphicsScene *scene, IAppContext *ctx,
                                      double ox, double oy, double c, double s2,
                                      bool flipY, double H, double V,
                                      const QString &layer, const QString &doorUuid,
                                      QUndoStack *stack);

    // 심볼 타입 <-> 종류 변환 (속성창에서 사용)
    static QString symbolTypeForKind(SimpleDoorKind kind);
    static bool    isSimpleSymbolType(const QString &type);
    static SimpleDoorKind kindForSymbolType(const QString &type);

    static CommandBase *createCommand(IAppContext *c);               // 단순 외여닫이
    static CommandBase *createDoubleCommand(IAppContext *c);         // 단순 쌍여닫이
    static CommandBase *createBiSwingCommand(IAppContext *c);        // 단순 양방향여닫이
    static CommandBase *createDoubleBiSwingCommand(IAppContext *c);  // 단순 양방쌍여닫이

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
    void      detectWallAtP1();
    QString   layerNameAt(const QPointF &pos) const;
    LineItem *findNearestMultilineAt(const QPointF &pos);
    void      trimWallInDoorOpening(double ox, double oy, double c, double s2,
                                    double yLow, double yHigh, QUndoStack *stack,
                                    const QList<LineItem *> &extLeftItems = QList<LineItem *>(),
                                    const QList<LineItem *> &extRightItems = QList<LineItem *>(),
                                    double extLen = 0.0);
    void      trimGeneralLines(const QList<LineItem *> &candidates,
                               double ox, double oy, double c, double s2,
                               double yLow, double yHigh,
                               QUndoStack *stack);

    static double  s_h;
    static QString s_wallGroupId;

    SimpleDoorKind m_kind = SimpleSingle;
    State   m_state = WaitP1;
    QPointF m_p1;
    QPointF m_p2;
    QPointF m_p3;         // 배치 직전 세 번째 점 (마감선 레이어 결정용)
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
class PlanDoorSimpleCommand : public CommandBase {
    Q_OBJECT

public:
    explicit PlanDoorSimpleCommand(IAppContext *ctx, SimpleDoorKind kind = SimpleSingle,
                                   QObject *parent = nullptr);
    void execute() override;

private:
    IAppContext *m_ctx;
    SimpleDoorKind m_kind;
};

#endif // PLANDOORSIMPLETOOL_H
