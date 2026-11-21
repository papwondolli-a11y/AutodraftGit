#ifndef THRUSTBEARINGTOOL_H
#define THRUSTBEARINGTOOL_H

#include "abstracttool.h"
#include "mydimension.h"
#include "myvector.h"
#include "thrustbearingdata.h"

#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <vector>

// 와셔 한 조각 (모서리 필렛·구면·볼 홈 호가 들어간 닫힌 폴리라인 + 해칭 경로)
struct ThrustSection {
    std::vector<MyVector> outline;
    std::vector<double>   bulges;
    QPainterPath          hatchPath;
};

struct ThrustBall {
    QPointF center;
    double  radius = 0.0;
};

struct ThrustBearingGeometry {
    QList<ThrustSection> sections;     // 하우징 와셔·축 와셔(+조심자리 와셔) 좌우 단면
    QList<ThrustBall>    balls;        // 좌우 볼
    QList<QLineF>        lines;        // 전체 그리기 연결선 (축 구멍 건너 면선)
    QList<QLineF>        centerLines;  // 축선 + 볼 중심선
    QList<MyDimension>   dims;
};

// ─────────────────────────────────────────────────────────────────────────
// ThrustBearingTool — 스러스트 볼 베어링(단식) 단면도 ("기계 Tools" 탭), BE3.LSP 이식
//   종류: 평면자리형(51xxx) / 조심자리형(53xxx) / 조심자리와셔형(53xxxU)
//
//   1) 실행하면 종류·계열·호칭번호(BEARING2.DAT 규격표) 또는 직접 입력한 값으로 설정
//   2) 클릭/좌표 입력 : 베어링 중심 지정 → 좌우 와셔 단면(해칭), 볼, 중심선, 치수 생성
//   3) ESC/Enter/Space/우클릭 : 종료
// 축선은 수직 (KS B 2022 그림과 동일). 레이어: BEARING / BEARING-CEN(또는 도면 중심선 레이어) / BEARING-DIM.
// ─────────────────────────────────────────────────────────────────────────
class ThrustBearingTool : public AbstractTool {
    Q_OBJECT
public:
    explicit ThrustBearingTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    bool processCommand(const QString &command) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

    // 씬과 무관한 순수 도형 계산 (origin = 베어링 전체 높이 중앙, 축선 위)
    static ThrustBearingGeometry build(const ThrustBearingParams &params, const QPointF &origin);
    static QPainterPath buildPath(const ThrustBearingParams &params, const QPointF &origin, double angleDeg = 0.0);

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

    ThrustBearingParams m_params;
    bool m_awaitAngle = false;   // 기준점 클릭 후 회전 각도 대기 중
    QPointF m_baseOrigin;
    double m_angleDeg = 0.0;
    bool m_cancelled = false;   // 설정창에서 취소됨 → 아무것도 그리지 않고 종료 대기
};

#endif // THRUSTBEARINGTOOL_H
