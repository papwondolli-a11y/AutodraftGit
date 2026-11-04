#ifndef DIMLEADERTOOL_H
#define DIMLEADERTOOL_H

#include "abstracttool.h"
#include "basedimensionitem.h"

class DimLeaderTool : public AbstractTool {
    Q_OBJECT
public:
    explicit DimLeaderTool(QObject *parent = nullptr);

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
    enum State { WaitSelectDimension, WaitLeaderEndPoint };
    State m_state;
    bool m_isDrawing;
    QPointF m_currentMousePos;

    BaseDimensionItem *m_selectedDim = nullptr;

    void updateGhostPreview();
};

#endif // DIMLEADERTOOL_H