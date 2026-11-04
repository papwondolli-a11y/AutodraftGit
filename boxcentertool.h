#ifndef BOXCENTERTOOL_H
#define BOXCENTERTOOL_H

#include "abstracttool.h"
#include <QPointF>

class LineItem;

class BoxCenterTool : public AbstractTool {
    Q_OBJECT

public:
    enum State {
        WaitingP1,
        WaitingP2
    };

    explicit BoxCenterTool(QObject *parent = nullptr);
    ~BoxCenterTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;

protected:
    void onFinish() override;
    void onCancel() override;

private:
    void createCenterLines();
    void updateGhostBox(const QPointF &p2);
    void updatePrompt();

    State   m_state;
    QPointF m_p1;
    QPointF m_currentMousePos;
};

#endif // BOXCENTERTOOL_H
