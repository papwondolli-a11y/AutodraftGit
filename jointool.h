#ifndef JOINTOOL_H
#define JOINTOOL_H

#include "abstracttool.h"
#include "iappcontext.h"
#include <QPair>
#include <QPointF>

class ArcItem;
class CircleItem;
class LineItem;

// ─────────────────────────────────────────────────────────────────────────────
// JoinTool — 선분/다중선/호 연결하기 (JOIN)
//
// 선분: 같은 기울기·같은 무한선 위에 있는 것을 하나로 합침.
//        다중선(ML_DATA_GROUP_ID)은 그룹 단위로 역할별 동시 처리.
// 호  : 같은 중심·반지름이고 끝점이 맞닿거나 겹치는 호들을 합침.
//        합친 결과가 360° 이상이면 원(CircleItem)으로 변환.
// ─────────────────────────────────────────────────────────────────────────────
class JoinTool : public AbstractTool {
    Q_OBJECT

public:
    explicit JoinTool(QObject *parent = nullptr);
    ~JoinTool();

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

private:
    void showPrompt() const;
    void handleSelectionPress(QPointF pos, Qt::MouseButton button);
    void handleSelectionMove(QPointF pos);
    void processBoxSelection(QPointF p2);
    void executeJoin();

    // ── 선분 헬퍼 ──────────────────────────────────────────────────────────
    static bool      linesAreCollinear(LineItem *a, LineItem *b);
    static LineItem *mergeLineGroup(const QList<LineItem *> &group);

    // ── 호 헬퍼 ────────────────────────────────────────────────────────────
    // 두 호가 같은 원 위에 있는지 판단
    static bool arcsOnSameCircle(ArcItem *a, ArcItem *b);
    // 같은 원 위의 호 그룹을 각도 병합. 결과 = {startAngle, spanAngle} 쌍 목록.
    // 빈 목록 반환 = 합산이 360° 이상(원)
    static QList<QPair<double,double>> mergeArcAngles(const QList<ArcItem *> &group);

    bool    m_isBoxSelecting;
    QPointF m_firstCorner;
};

#endif // JOINTOOL_H
