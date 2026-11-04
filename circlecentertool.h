#ifndef CIRCLECENTERTOOL_H
#define CIRCLECENTERTOOL_H

#include "abstracttool.h"

class CircleCenterTool : public AbstractTool {
    Q_OBJECT

public:
    enum State {
        WaitingSelect,
        DrawingRadius
    };

    explicit CircleCenterTool(QObject *parent = nullptr);
    ~CircleCenterTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

protected:
    void onFinish() override;
    void onCancel() override;

private:
    void updatePrompt();
    void addCenterLines(const QPointF &center, double radius);
    void finishCircleAndCenter(double radius);
    void cancelRadiusDrawing();

    State    m_state;
    QPointF  m_centerPoint;
    QPointF  m_currentMousePos;
    bool     m_isDiameterMode;
};

#endif // CIRCLECENTERTOOL_H
