#ifndef GDTTOOL_H
#define GDTTOOL_H

#include "abstracttool.h"
#include "mygdt.h"

class GdtTool : public AbstractTool {
    Q_OBJECT
public:
    enum State { WaitStartPoint, WaitNextPoint };

    explicit GdtTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

private:
    void cancelTool();
    void finishTool();
    void placeItemAndReset(); // [추가] 씬에 객체 추가 및 상태 초기화

    void abortCurrentPlacement();
    void showStartPrompt();

    GdtData m_pendingData;
    State   m_state;
    QList<MyVector> m_pts;
    bool    m_cancelled = false;   // 설정창에서 취소됨 → 아무것도 그리지 않고 종료 대기
};

#endif // GDTTOOL_H