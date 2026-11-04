#ifndef SEVERTOOL_H
#define SEVERTOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QPointF>
#include <QPointer>

class CadItem;

class SeverTool : public AbstractTool {
    Q_OBJECT
public:
    enum State {
        State_SelectingFirstPoint,
        State_SelectingSecondPoint,
        State_WaitingForDistance  // Shift+클릭 후 거리 입력 대기
    };

    explicit SeverTool(QObject *parent = nullptr);
    virtual ~SeverTool();

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

private:
    void updatePrompt();
    void executeBreak();
    void clearOverlay();

    // 기하학적 파라미터 계산용 헬퍼
    double getParam(const class MyEntity *entity, const MyVector &pt);

    // 객체 기울기 방향으로 dist 이동한 p2 계산
    MyVector computeP2FromDistance(const class MyEntity *entity,
                                   const MyVector &p1, double dist,
                                   const QPointF &mousePos);

    State m_currentState;
    CadItem *m_targetItem;
    MyVector m_p1;
    MyVector m_p2;
    MyVector m_dirPoint;      // Shift+클릭으로 고정된 방향 기준점
    QPointF m_currentMousePos;
};

#endif // SEVERTOOL_H
