#ifndef DIMMERGETOOL_H
#define DIMMERGETOOL_H

#include "abstracttool.h"
#include "myvector.h"

class LinearDimensionItem;
class MyDimension;

// 치수병합: 같은 방향의 선형치수 2개를 선택하면 두 치수의 바깥쪽 끝점을 잇는
// 하나의 치수로 합치고 원본 2개는 삭제한다. (예: 1000 + 1500 -> 2500)
// 새 치수의 속성(레이어, 치수선 높이, 접두/접미, 공차 등)은 먼저 선택한 치수를 따른다.
class DimMergeTool : public AbstractTool {
    Q_OBJECT

public:
    explicit DimMergeTool(QObject *parent = nullptr);
    ~DimMergeTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;

    bool processCommand(const QString &command) override;

    // 치수 측정 방향 단위벡터 (수평/수직 근처는 축으로 보정). 방향을 구할 수 없으면 false.
    static bool dimDirection(const MyDimension *d, MyVector &dir);

    // 토탈 치수의 양 끝 기점(p1, p2)과 겹치는 기존 치수의 치수보조선을 숨긴다(Undo 지원).
    // 호출 측이 필요하면 beginMacro/endMacro 로 묶는다.
    static void hideCoincidentExtLines(IAppContext *ctx, const QList<LinearDimensionItem *> &items,
                                       const MyVector &p1, const MyVector &p2);

protected:
    void onCancel() override;

private:
    enum State {
        WaitFirst,  // 첫번째 치수 선택 대기
        WaitSecond, // 두번째 치수 선택 대기
        WaitOption  // 옵션 대기: 엔터=병합, N=토탈치수 (치수 추가 선택 가능)
    };

    State m_state;
    LinearDimensionItem *m_first;
    QList<LinearDimensionItem *> m_totalSel;

    LinearDimensionItem *pickLinearDim(const QPointF &pos);
    void createTotalDimension();
    void mergeSelected();
    void mergeDimensions(LinearDimensionItem *a, LinearDimensionItem *b);
    void resetTool();
    void finishTool();
};

#endif // DIMMERGETOOL_H
