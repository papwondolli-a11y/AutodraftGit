#ifndef SURFACEFINISHTOOL_H
#define SURFACEFINISHTOOL_H

#include "abstracttool.h"
#include "mysurfacefinish.h"

class SurfaceFinishTool : public AbstractTool {
    Q_OBJECT
public:
    // WaitAngle: 지시선 비활성화 시, 위치를 클릭한 다음 각도를 지정하는 단계
    enum State { WaitStartPoint, WaitNextPoint, WaitAngle };

    explicit SurfaceFinishTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

private:
    void cancelTool();
    void finishTool();
    void finishNoLeaderPlacement(double angle);
    void placeItemAndReset();

    void abortCurrentPlacement();
    void showStartPrompt();

    SurfaceFinishData m_pendingData;
    State             m_state;
    QList<MyVector>   m_pts;
};

#endif // SURFACEFINISHTOOL_H
