#ifndef ANGULARDIMENSIONTOOL_H
#define ANGULARDIMENSIONTOOL_H

#include "abstracttool.h"
#include "mydimension.h"

class AngularDimensionTool : public AbstractTool {
    Q_OBJECT
public:
    explicit AngularDimensionTool(QObject *parent = nullptr);
    ~AngularDimensionTool() override;

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
        WaitVertex,
        WaitFirstPoint,
        WaitSecondPoint,
        WaitDimLinePoint
    };

    State m_state;
    bool m_isDrawing;
    MyDimension m_data;
    QPointF m_currentMousePos;
};

#endif // ANGULARDIMENSIONTOOL_H