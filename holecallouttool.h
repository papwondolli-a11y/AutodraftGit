#ifndef HOLECALLOUTTOOL_H
#define HOLECALLOUTTOOL_H

#include "abstracttool.h"
#include "myhole.h"
#include "myleader.h"
#include <QList>

class HoleItem;

// 선택한 구멍(들)의 스펙으로 콜아웃 텍스트를 자동 조립해 지시선(리더)을 생성하는 도구.
// AutoCAD Mechanical의 Hole Callout과 동일한 흐름: 구멍(원)을 클릭 선택하면 지름/탭/
// 카운터보어/카운터싱크 정보를 읽어 텍스트를 자동으로 만들고, 리더를 붙여 배치한다.
class HoleCalloutTool : public AbstractTool {
    Q_OBJECT
public:
    enum State { WaitSelectHole, WaitFirstPoint, WaitSecondPoint };

    explicit HoleCalloutTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

private:
    void cancelTool();
    void resetToSelect();
    void showPrompt();
    bool pickHole(QPointF pos, HoleItem *&outItem);
    void finishSelection();
    void finalizeLeader();
    void updateLeaderGhost();

    State m_state = WaitSelectHole;
    QList<HoleItem *> m_pickedItems;
    QList<MyHole> m_selectedHoles;
    MyLeader m_data;
    QPointF m_currentMousePos;
};

#endif // HOLECALLOUTTOOL_H
