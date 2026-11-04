#ifndef HOLEPTOOL_H
#define HOLEPTOOL_H

#include "abstracttool.h"
#include "myhole.h"

// 구멍(P): 클릭한 점을 기준점으로 지정한 거리만큼 떨어진 위치에 원형(각도) 배열로
// 구멍을 생성하는 도구. [선택(S)] 옵션으로 원/호 2개를 고르면, 두 중심점의 정중앙을
// 기준점으로, "작은 원(호) 반지름 + (큰 원(호) 반지름 - 작은 원(호) 반지름)/2" (두
// 반지름의 평균)를 거리값으로 삼아 동일하게 범위/갯수 배열을 적용한다.
class HolePTool : public AbstractTool {
    Q_OBJECT
public:
    enum State { WaitPlacePoint, WaitFirstRef, WaitSecondRef };

    explicit HolePTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

private:
    void cancelTool();
    void placeArrayAndReset(const MyVector &anchor, double distance);
    void createHoleAt(const MyVector &pos, const MyVector &anchor);
    void showStartPrompt();
    void addCenterLines(const MyVector &center, double diameter, const MyVector &anchor, const QString &groupId);
    bool pickCircleOrArc(QPointF pos, MyVector &outCenter, double &outRadius);
    void resetToStart();

    MyHole m_pendingData;
    State m_state = WaitPlacePoint;
    double m_centerDistance = 0.0;
    double m_arrayRange = 360.0;
    int m_arrayCount = 1;
    MyVector m_firstRefCenter;
    double m_firstRefRadius = 0.0;
};

#endif // HOLEPTOOL_H
