#ifndef DIMLINEARTOOL_H
#define DIMLINEARTOOL_H

#include "abstracttool.h"
#include "mydimension.h"

class DimLinearTool : public AbstractTool {
    Q_OBJECT
public:
    explicit DimLinearTool(QObject *parent = nullptr);
    ~DimLinearTool() override;

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
    double decideAngle(const QPointF &pos) const;
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

#endif // DIMLINEARTOOL_H
