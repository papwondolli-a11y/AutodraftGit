#ifndef DIMSPLITTOOL_H
#define DIMSPLITTOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QList>

class LinearDimensionItem;
class MyDimension;

// 치수분리: 선형치수 1개를 선택하고 분리 치수보조선이 놓일 점들을 지정하면
// 기존 치수를 삭제하고 점 사이마다 개별 치수로 분리한다. (예: 2500 -> 1000 + 1500)
// 옵션 N: 기존 치수를 삭제하지 않고 토탈 치수로 남기고, 분리 치수는 기존 치수선보다
// 치수선 간격만큼 안쪽에 생성한다.
class DimSplitTool : public AbstractTool {
    Q_OBJECT

public:
    explicit DimSplitTool(QObject *parent = nullptr);
    ~DimSplitTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;

protected:
    void onCancel() override;

private:
    enum State {
        WaitSelect, // 분리할 치수 선택 대기
        WaitPoints  // 분리 점 지정 대기 (엔터/우클릭으로 종료)
    };

    State m_state;
    LinearDimensionItem *m_target;
    QList<MyVector> m_points;
    bool m_keepOriginal;

    MyVector m_dir;    // 치수 방향 (start -> end 쪽이 +)
    MyVector m_normal;
    double m_tStart;
    double m_tEnd;

    QPointF snapped(const QPointF &pos) const;
    bool addPoint(const MyVector &p);
    // 분리 치수 데이터 생성. extraPoint 가 있으면 미리보기용으로 점 목록에 임시 포함.
    QList<MyDimension> buildSegments(const MyVector *extraPoint) const;
    void updatePreview(const QPointF &mouse);
    void execute();
    void resetTool();
    void finishTool();
};

#endif // DIMSPLITTOOL_H
