#ifndef DIMBASEREFTOOL_H
#define DIMBASEREFTOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QList>

class LinearDimensionItem;
class MyDimension;

// 치수기준: 연속(체인) 치수 중 일부(예: 20, 10, 15)를 선택하면 선택한 치수를
// 기준점 치수(누진 치수: 20, 30, 45)로 바꾼다.
//   - 선택한 치수들의 가장 안쪽(작은 쪽) 점이 공통 기준점이 된다.
//   - 가장 작은 치수가 원래 치수선 위치에 남고, 큰 치수일수록 치수선 간격만큼 바깥으로 쌓인다.
//   - 선택하지 않은 같은 줄의 치수(25, 12)와 그 바깥의 치수(전체 82)는
//     쌓인 단 수만큼 바깥으로 자동 이동한다.
// 사용: 치수선들을 차례로 클릭(다시 클릭하면 선택 해제) → 엔터/우클릭으로 실행.
class DimBaseRefTool : public AbstractTool {
    Q_OBJECT

public:
    explicit DimBaseRefTool(QObject *parent = nullptr);
    ~DimBaseRefTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    bool processCommand(const QString &command) override;

protected:
    void onCancel() override;

private:
    QList<LinearDimensionItem *> m_selected;

    void updateSelectionGhost();
    void execute();
    void resetTool();
    void finishTool();
};

#endif // DIMBASEREFTOOL_H
