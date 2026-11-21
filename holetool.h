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

    // 헤드리스(MCP): 설정창 없이 값을 그대로 쓴다. 오프셋(offsetX/Y)이 0,0이 아니면
    // 기준선 2개를 차례로 클릭(mousePress)해 교차점 기준으로 배치한다.
    void setHeadlessParams(const MyHole &data, double offsetX, double offsetY,
                           int countX, double spacingX, int countY, double spacingY);

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

    bool m_headless = false; // true면 activate()에서 설정창을 건너뛴다(setHeadlessParams)
};

#endif // HOLETOOL_H
