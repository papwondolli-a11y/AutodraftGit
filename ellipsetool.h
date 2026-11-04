#ifndef ELLIPSETOOL_H
#define ELLIPSETOOL_H

#include "abstracttool.h"
#include "myellipse.h"
#include <QPointF>

// ─────────────────────────────────────────────────────────────────────────
// EllipseTool — 타원(ELLIPSE) 그리기 툴
//
// AutoCAD의 ELLIPSE 명령과 동일한 흐름으로 동작한다.
//   기본(축, 끝점)  : 1) 축의 첫 번째 끝점  2) 축의 다른 끝점  3) 다른 축의 반거리
//   중심점(C)       : 1) 중심점  2) 축의 끝점  3) 다른 축의 반거리
//   호(A)           : 위 방법으로 타원을 정의한 뒤 시작각/끝각(매개각, 도)을 지정
//                     (시작각 → 끝각 방향은 반시계방향)
//   완성된 타원은 EllipseItem 하나로 생성된다.
// ─────────────────────────────────────────────────────────────────────────
class EllipseTool : public AbstractTool {
    Q_OBJECT
public:
    enum State {
        WaitFirst,      // 축의 첫 번째 끝점 (또는 옵션 A / C)
        WaitSecond,     // 축의 다른 끝점
        WaitCenter,     // 중심점 (C 옵션)
        WaitCenterAxis, // 중심에서 축의 끝점 (C 옵션)
        WaitOther,      // 다른 축의 반거리
        WaitStartAngle, // 호: 시작각
        WaitEndAngle    // 호: 끝각
    };

    explicit EllipseTool(QObject *parent = nullptr);
    ~EllipseTool();

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    bool processCommand(const QString &command) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

    // 장축 벡터(중심→장축끝)와 "다른 축 반거리"로 타원 데이터를 만든다.
    // 반거리가 장축보다 크면 두 축을 서로 바꿔 ratio <= 1 을 유지한다.
    static MyEllipse buildEllipse(const QPointF &center, const QPointF &axisEnd,
                                  const QPointF &otherRef);
    static MyEllipse buildEllipseByDistance(const QPointF &center,
                                            const QPointF &axisEnd,
                                            double otherDistance);

private:
    void showFirstPrompt();
    void showSecondPrompt();
    void showCenterPrompt();
    void showCenterAxisPrompt();
    void showOtherPrompt();
    void showStartAnglePrompt();
    void showEndAnglePrompt();

    void handlePoint(const QPointF &pos);
    void defineAxisEnd(const QPointF &axisEnd, bool fromCenter);
    void finishOtherAxis(const QPointF &otherRef, double overrideDistance = -1.0);
    void acceptStartParam(double param);
    void finishEllipse(const MyEllipse &ellipse);
    void cancelDrawing();
    void updateGhost(const QPointF &pos);

    State   m_state;
    bool    m_arcMode;     // 호(A) 옵션 활성
    QPointF m_p1;          // 축 첫 번째 끝점
    QPointF m_center;      // 중심
    QPointF m_axisEnd;     // 중심 기준 장축(1차 축) 끝점
    QPointF m_currentPos;  // 현재 마우스 위치
    MyEllipse m_baseEllipse; // 호 지정 단계에서 사용하는 완성된 전체 타원
    double  m_startParam;  // 호 시작 매개각(라디안)
};

#endif // ELLIPSETOOL_H
