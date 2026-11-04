#ifndef EXTENDTOOL_H
#define EXTENDTOOL_H

#include "abstracttool.h"
#include <QPointF>
#include <vector>

class CadItem;
class QGraphicsPathItem;

class ExtendTool : public AbstractTool {
    Q_OBJECT
public:
    explicit ExtendTool(QObject *parent = nullptr);
    virtual ~ExtendTool();

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;

    bool processCommand(const QString &command) override;

private:
    enum State {
        State_SelectingBoundaryEdges,
        State_SelectingExtendTarget,
    };

    void updatePrompt();
    void clearOverlay();

    void handleSelectionPress(QPointF pos, Qt::MouseButton button);

    void tryExtend(CadItem *targetItem, const QPointF &clickPos);
    void executeExtend(CadItem *targetItem, const QPointF &oldEnd,
                       const QPointF &newEnd, bool isFirstEnd);

    void updateHoverPreview(const QPointF &pos);
    void clearHoverPreview();

    // 가상 라인(Fence) 교차 객체 처리
    void processFenceSegment(const QPointF &p1, const QPointF &p2);

    std::vector<class MyVector>
    findAllExtendedIntersections(CadItem *targetItem, const QPointF &clickPos,
                                 bool &isFirstEnd);

    State m_currentState;
    std::vector<CadItem *> m_boundaryEdges;
    bool m_selectAllMode;

    // 가상 라인(Fence) 다중 선택을 위한 멤버 변수
    bool m_isFenceSelecting;
    std::vector<QPointF> m_fencePoints;
};

#endif // EXTENDTOOL_H