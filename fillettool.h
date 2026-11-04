#ifndef FILLETTOOL_H
#define FILLETTOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QPointF>

class CadItem;
class MyLine;

// 선과 원(호) Fillet 계산 결과를 담는 구조체
struct LineCircleFilletResult {
    bool valid = false;
    MyVector center;
    MyVector tLine;
    MyVector tCirc;
    double a1 = 0;
    double span = 0;
};

struct CircleCircleFilletResult {
    bool valid = false;
    MyVector center;
    MyVector t1;
    MyVector t2;
    double a1 = 0;
    double span = 0;
};

class FilletTool : public AbstractTool {
    Q_OBJECT

public:
    enum State {
        State_WaitingForFirst,
        State_WaitingForSecond,
        State_BoxSelecting
    };

    explicit FilletTool(QObject *parent = nullptr);
    virtual ~FilletTool();

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

    // Selection Box
    void handleSelectionPress(QPointF pos, Qt::MouseButton button);
    void handleSelectionMove(QPointF pos);
    void processBoxSelection(QPointF p2);

    // Fillet Logic (trimLines 매개변수 유지)
    void tryFillet(CadItem *item1, CadItem *item2, const QPointF &p1,
                   const QPointF &p2, bool trimLines = true);
    void executeFillet(CadItem *item1, CadItem *item2, const QPointF &p1,
                       const QPointF &p2, bool trimLines);

    // 선과 원/호 사이의 Fillet 수학 연산 도우미
    LineCircleFilletResult calcLineCircleFillet(const MyLine *line, const MyVector &cCenter,
                                                double cRadius, double fRadius,
                                                const QPointF &pPickLine, const QPointF &pPickCirc);

    CircleCircleFilletResult calcCircleCircleFillet(const MyVector &c1Center, double c1Radius,
                                                    const MyVector &c2Center, double c2Radius,
                                                    double fRadius, const QPointF &p1, const QPointF &p2);

    void updateHoverPreview(const QPointF &pos);
    void clearHoverPreview();

    State m_currentState;
    CadItem *m_firstItem;
    QPointF m_firstPickPos;

    static double s_lastRadius;
    double m_radius;

    // Box Selection
    bool m_isBoxSelecting;
    QPointF m_firstCorner;
};

#endif // FILLETTOOL_H