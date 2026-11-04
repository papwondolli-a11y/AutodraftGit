#ifndef LEADERTOOL_H
#define LEADERTOOL_H

#include "abstracttool.h"
#include "myleader.h"

class LeaderTool : public AbstractTool {
    Q_OBJECT
public:
    explicit LeaderTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;

    bool processCommand(const QString &command) override;

protected:
    void onFinish() override;
    void onCancel() override;

private:
    enum State { WaitFirstPoint, WaitSecondPoint, WaitText };
    State m_state;
    MyLeader m_data;
    bool m_isDrawing;
    QPointF m_currentMousePos;

    void finalizeLeader();
    void updateGhostPreview();
};

#endif // LEADERTOOL_H