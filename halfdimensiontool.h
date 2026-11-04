#ifndef HALFDIMENSIONTOOL_H
#define HALFDIMENSIONTOOL_H

#include "abstracttool.h"
#include "mydimension.h"

class HalfDimensionTool : public AbstractTool {
    Q_OBJECT
public:
    explicit HalfDimensionTool(QObject *parent = nullptr);
    ~HalfDimensionTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;

    bool processCommand(const QString &command) override;

protected slots:
    void onFinish() override;
    void onCancel() override;

private:
    void cancelDrawing();
    void finalizeDimension();
    void updateGhostPreview();

    enum State { WaitStartPoint, WaitCenterPoint, WaitDimLinePoint };
    State m_state;
    bool m_isDrawing;
    MyDimension m_data;
    QPointF m_currentMousePos;
    bool m_isHvMode; // [추가] 수평/수직 모드 여부
};

#endif // HALFDIMENSIONTOOL_H