#ifndef ROTATETOOL_H
#define ROTATETOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QPointF>
#include <vector>

class CadItem;
class SnapManager;

class RotateTool : public AbstractTool {
    Q_OBJECT
public:
    enum State {
        State_SelectingObjects,
        State_SelectingObjects_SecondCorner,
        State_WaitingForBasePoint,
        State_WaitingForAngle
    };

    explicit RotateTool(IAppContext *ctx, QObject *parent = nullptr);
    virtual ~RotateTool();

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

private:
    void updatePrompt();
    void clearOverlay();
    void handleSelectionMove(QPointF pos);
    void processBoxSelection(QPointF p2);
    void finishRotate(double angle);

    // 스냅 계산 및 마커 표시 통합 함수
    QPointF getSnappedPoint(const QPointF &pos, const QPointF &base = QPointF());

    State m_currentState;
    QPointF m_firstCorner;
    std::vector<CadItem *> m_selectedItems;
    MyVector m_basePoint;
    QPointF m_lastMousePos;

    void updateRotateGhost(const QPointF &currentPos);
};

#endif // ROTATETOOL_H