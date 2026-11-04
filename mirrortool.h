#ifndef MIRRORTOOL_H
#define MIRRORTOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QPointF>
#include <vector>

class CadItem;

class MirrorTool : public AbstractTool {
    Q_OBJECT
public:
    enum State {
        State_SelectingObjects,
        State_SelectingObjects_SecondCorner,
        State_WaitingForFirstPoint,
        State_WaitingForSecondPoint,
        State_AskingEraseSource
    };

    explicit MirrorTool(IAppContext *ctx, QObject *parent = nullptr);
    virtual ~MirrorTool();

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

private:
    void updatePrompt();
    void clearOverlay();

    void handleSelectionPress(QPointF pos, Qt::MouseButton button);
    void handleSelectionMove(QPointF pos);
    void processBoxSelection(QPointF p2);

    void finishMirror(bool eraseSource);

    State m_currentState;
    QPointF m_firstCorner;

    std::vector<CadItem *> m_selectedItems;
    MyVector m_mirrorP1;
    MyVector m_mirrorP2;

    void updateMirrorGhost(const QPointF &currentPos);
};

#endif // MIRRORTOOL_H