#ifndef TRIMTOOL_H
#define TRIMTOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QPointer>
#include <vector>

class CadItem;
class MyEntity;

class TrimTool : public AbstractTool {
    Q_OBJECT

public:
    enum State { State_SelectingCuttingEdges, State_SelectingTrimSegment };

    explicit TrimTool(QObject *parent = nullptr);
    virtual ~TrimTool();

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    bool processCommand(const QString &command) override;
    void keyPress(QKeyEvent *event) override;

private:
    void updatePrompt();

    // 반환형을 bool로 변경하여 트림 성공 여부를 파악 (Fence 트림 시 씬 변화 감지용)
    bool tryTrim(CadItem *targetItem, const QPointF &clickPos);
    bool executeTrim(CadItem *targetItem,
                     const std::vector<MyVector> &intersections,
                     const QPointF &clickPos);

    // 기하 정밀 연산 도우미
    std::vector<MyVector> findAllIntersections(CadItem *targetItem);
    double getParam(const MyEntity *entity, const MyVector &pt);

    void updateHoverPreview(const QPointF &pos);
    void clearHoverPreview();

    void clearOverlay();
    void handleSelectionPress(QPointF pos, Qt::MouseButton button);
    void handleSelectionMove(QPointF pos);
    void processBoxSelection(QPointF p2);

    // 가상 라인(Fence) 교차 객체 처리
    void processFenceSegment(const QPointF &p1, const QPointF &p2);

    State m_currentState;
    bool m_isBoxSelecting;
    QPointF m_firstCorner;
    std::vector<CadItem *> m_cuttingEdges;
    bool m_selectAllMode;

    // 가상 라인(Fence) 선택을 위한 멤버 변수
    bool m_isFenceSelecting;
    QPointF m_lastFencePoint;
};

#endif // TRIMTOOL_H