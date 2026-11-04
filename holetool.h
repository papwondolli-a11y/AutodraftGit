#ifndef HOLETOOL_H
#define HOLETOOL_H

#include "abstracttool.h"
#include "myhole.h"
#include "myline.h"

class HoleTool : public AbstractTool {
    Q_OBJECT
public:
    enum State { WaitPlacePoint, WaitFirstLine, WaitSecondLine };

    explicit HoleTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

private:
    void cancelTool();
    void placeItemAndReset(const MyVector &pos);
    void createHoleAt(const MyVector &pos);
    void showStartPrompt();
    void addCenterLines(const MyVector &center, double diameter, const QString &groupId);
    bool pickLine(QPointF pos, MyLine &outLine);
    void resetToStart();

    MyHole m_pendingData;
    State m_state = WaitPlacePoint;
    double m_offsetX = 0.0;
    double m_offsetY = 0.0;
    MyLine m_firstLine;

    int m_arrayCountX = 1;
    double m_arraySpacingX = 0.0;
    int m_arrayCountY = 1;
    double m_arraySpacingY = 0.0;
};

#endif // HOLETOOL_H
