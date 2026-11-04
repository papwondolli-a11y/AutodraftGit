#ifndef RADIALDIMENSIONTOOL_H
#define RADIALDIMENSIONTOOL_H

#include "abstracttool.h"
#include "mydimension.h"

class RadialDimensionTool : public AbstractTool {
    Q_OBJECT
public:
    explicit RadialDimensionTool(QObject *parent = nullptr);
    ~RadialDimensionTool() override;

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
        WaitSelectArc,
        WaitDimLinePoint
    };

    State m_state;
    bool m_isDrawing;
    MyDimension m_data;
    QPointF m_currentMousePos;
};

#endif // RADIALDIMENSIONTOOL_H