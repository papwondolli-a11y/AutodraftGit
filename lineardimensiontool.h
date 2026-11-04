#ifndef LINEARDIMENSIONTOOL_H
#define LINEARDIMENSIONTOOL_H

#include "abstracttool.h"
#include "mydimension.h"

class LinearDimensionTool : public AbstractTool {
    Q_OBJECT
public:
    explicit LinearDimensionTool(QObject *parent = nullptr);
    ~LinearDimensionTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;

    bool processCommand(const QString &command) override;

protected slots:
    void onFinish() override;
    void onCancel() override;

private:
    void cancelDrawing();
    void finalizeDimension();
    void updateGhostPreview();

    enum State {
        WaitStartPoint,
        WaitEndPoint,
        WaitDimLinePoint
    };

    State m_state;
    bool m_isDrawing;
    MyDimension m_data;
    QPointF m_currentMousePos;
};

#endif // LINEARDIMENSIONTOOL_H