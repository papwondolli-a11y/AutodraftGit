#ifndef TAPEREDROLLERBEARINGTOOL_H
#define TAPEREDROLLERBEARINGTOOL_H

#include "abstracttool.h"
#include "mydimension.h"
#include "myvector.h"
#include "taperedrollerbearingdata.h"

#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <vector>

// 컵 또는 콘 단면 한 조각 (모서리 필렛이 들어간 닫힌 폴리라인 + 해칭 경로)
struct TrbSection {
    std::vector<MyVector> outline;
    std::vector<double>   bulges;
    QPainterPath          hatchPath;
};

struct TaperedRollerBearingGeometry {
    TrbSection         cupU, coneU, cupL, coneL;  // 위/아래 단면의 컵·콘
    QList<QLineF>      lines;        // 롤러 끝선, 컵 앞면 틈선, (전체 그리기) 면 연결선
    QList<QLineF>      axisLines;    // 롤러 축선 (위·아래) — 중심선 레이어
    bool               hasCenterLine = false;
    QLineF             centerLine;   // 베어링 중심선
    QList<MyDimension> dims;
};

// ─────────────────────────────────────────────────────────────────────────
// TaperedRollerBearingTool — 테이퍼롤러베어링 단면도 ("기계 Tools" 탭), BE2.LSP 이식
//
//   1) 실행하면 계열·호칭번호(BEARING1.DAT 규격표) 또는 직접 입력한 d/D/B/T/C/r/R 로 설정
//   2) 클릭/좌표 입력 : 베어링 중심 지정 → 위·아래 컵·콘 단면(해칭), 롤러 끝선, 중심선, 치수 생성
//   3) ESC/Enter/Space/우클릭 : 종료
// 롤러 크기는 BE2.LSP 비율: 지름 ≈ (D-d)/4·cosα, 길이 = 0.7·C (큰 쪽 0.25C, 작은 쪽 0.45C).
// 레이어: BEARING(외형) / 도면 설정 중심선 레이어 또는 BEARING-CEN / BEARING-DIM(치수).
// ─────────────────────────────────────────────────────────────────────────
class TaperedRollerBearingTool : public AbstractTool {
    Q_OBJECT
public:
    explicit TaperedRollerBearingTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    bool processCommand(const QString &command) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

    // 씬과 무관한 순수 도형 계산 (origin = 베어링 전체 폭 중앙, 축선 위)
    static TaperedRollerBearingGeometry build(const TaperedRollerBearingParams &params,
                                              const QPointF &origin);
    static QPainterPath buildPath(const TaperedRollerBearingParams &params, const QPointF &origin, double angleDeg = 0.0);

private:
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

    TaperedRollerBearingParams m_params;
    bool m_awaitAngle = false;   // 기준점 클릭 후 회전 각도 대기 중
    QPointF m_baseOrigin;
    double m_angleDeg = 0.0;
    bool m_cancelled = false;   // 설정창에서 취소됨 → 아무것도 그리지 않고 종료 대기
};

#endif // TAPEREDROLLERBEARINGTOOL_H
