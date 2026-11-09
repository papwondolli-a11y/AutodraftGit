#ifndef DIMFLIPSIDETOOL_H
#define DIMFLIPSIDETOOL_H

#include "abstracttool.h"
#include "myvector.h"

class LinearDimensionItem;

// 치수 반대면: 선형치수 1개를 선택하면 치수를 대상물의 반대편 면으로 옮긴다.
//   - 치수 정의점에서 대상물 안쪽 방향으로 도형을 따라가 가장 먼 면을 찾아 정의점을 옮긴다.
//   - 옮긴 뒤 치수선이 놓일 위치를 다시 지정할 수 있다 (마우스 클릭, 엔터/우클릭=기본 위치).
class DimFlipSideTool : public AbstractTool {
    Q_OBJECT

public:
    explicit DimFlipSideTool(QObject *parent = nullptr);
    ~DimFlipSideTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;

protected:
    void onCancel() override;

private:
    enum State {
        WaitSelect,  // 치수 선택 대기
        WaitPosition // 치수선 위치 지정 대기
    };

    State m_state;
    LinearDimensionItem *m_target;

    MyVector m_dir;       // 치수 방향
    MyVector m_normal;    // 치수선 이동 방향축
    MyVector m_newStart;  // 반대면으로 옮긴 정의점
    MyVector m_newEnd;
    double m_defaultAbs;  // 기본 치수선 위치(법선축 절대값)

    QPointF snapped(const QPointF &pos) const;
    // 엔티티(선/폴리라인)의 꼭짓점을 읽어 p 의 반대면 기점을 찾는다. 못 찾으면 false.
    bool oppositeByVertex(const MyVector &p, const MyVector &inward, MyVector &out) const;
    // 반대면 기점 찾기: 꼭짓점 방식 우선, 실패하면 광선 방식으로 대체
    bool oppositePoint(const MyVector &p, const MyVector &inward, MyVector &out) const;
    // (대체 방식) 정의점에서 direction 으로 광선을 쏘아 도형과 만나는 거리. 못 찾으면 음수.
    double farthestExtent(const MyVector &p, const MyVector &direction) const;
    bool prepare(LinearDimensionItem *dim);
    void updatePreview(double lineAbs);
    void apply(double lineAbs);
    void resetTool();
    void finishTool();
};

#endif // DIMFLIPSIDETOOL_H
