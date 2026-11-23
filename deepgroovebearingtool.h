#ifndef DEEPGROOVEBEARINGTOOL_H
#define DEEPGROOVEBEARINGTOOL_H

#include "abstracttool.h"
#include "deepgroovebearingdata.h"
#include "mydimension.h"
#include "myvector.h"

#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <vector>

// 베어링 단면 한 쪽(위 또는 아래)의 도형
struct BearingHalfShape {
    std::vector<MyVector> outline;   // 둥근 모서리(필렛 r) 외형 (닫힌 폴리라인)
    std::vector<double>   bulges;    // 꼭짓점별 bulge (모서리 호)
    QPointF               ballCenter;
    double                ballRadius = 0.0;
    QList<QLineF>         seatLines;  // 볼받침 수평선 4개 (볼 45° 점 → 좌우 면)
    QLineF                crossH;     // 볼 중심 십자선
    QLineF                crossV;
    QPainterPath          hatchPath;  // 외형 - 볼받침 틈 - 볼
};

struct BearingGeometry {
    BearingHalfShape  upper;
    BearingHalfShape  lower;
    bool              hasCenterLine = false;
    QLineF            centerLine;
    QList<QLineF>     connLines;   // 전체 그리기: 위·아래 단면을 잇는 좌우 면 수직선
    QList<MyDimension> dims;
};

// ─────────────────────────────────────────────────────────────────────────
// DeepGrooveBearingTool — 깊은 홈 볼베어링 단면도 ("기계 Tools" 탭)
//
//   1) 실행하면 호칭번호(693~6940) 또는 직접 입력한 d/D/B/r 로 다이얼로그 설정
//   2) 클릭/좌표 입력 : 베어링 중심 지정 → 위·아래 단면(해칭+볼), 중심선, B/ød/øD 치수 생성
//   3) ESC/Enter/Space/우클릭 : 종료
// 레이어: BEARING(외형) / BEARING-CEN(중심선) / BEARING-DIM(치수). 해칭은 "그리기" 탭 해칭 레이어.
// ─────────────────────────────────────────────────────────────────────────
class DeepGrooveBearingTool : public AbstractTool {
    Q_OBJECT
public:
    explicit DeepGrooveBearingTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    bool processCommand(const QString &command) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

    // 씬과 무관한 순수 도형 계산 (origin = 베어링 중심)
    static BearingGeometry build(const DeepGrooveBearingParams &params, const QPointF &origin);
    static QPainterPath buildPath(const DeepGrooveBearingParams &params, const QPointF &origin, double angleDeg = 0.0);

    // MCP 등 자동 배치: 설정되어 있으면 activate 에서 설정창 없이 이 값을 쓴다
    static void setHeadlessParams(const DeepGrooveBearingParams &params);
    static void clearHeadless();

    // 베어링 넣기(축에 맞춰): 설정창·클릭 없이 origin(기준점)·angleDeg(축 방향) 에 바로 그림 (한 undo 매크로)
    static void placeDirect(IAppContext *ctx, MyGraphicsScene *scene, const DeepGrooveBearingParams &params, const QPointF &origin,
                            double angleDeg);

private:
    static bool                    s_headless;
    static DeepGrooveBearingParams s_headlessParams;

    void showPositionPrompt();
    void placeAt(const QPointF &origin);
    void finishTool();
    void startAngleStep(const QPointF &origin);   // 기준점 확정 -> 회전 각도 입력 단계
    void finishAngleStep(double angleDeg);
    void cancelAngleStep();
    double angleFromPoint(const QPointF &p) const;
    void ensureLayer(IAppContext *ctx, const QString &name, const QColor &color,
                     const QString &linetype);
    QGraphicsItem *finalizeItem(QGraphicsItem *item, const QString &layer);

    DeepGrooveBearingParams m_params;
    bool m_awaitAngle = false;   // 기준점 클릭 후 회전 각도 대기 중
    QPointF m_baseOrigin;
    double m_angleDeg = 0.0;
    bool m_cancelled = false;   // 설정창에서 취소됨 → 아무것도 그리지 않고 종료 대기
};

#endif // DEEPGROOVEBEARINGTOOL_H
