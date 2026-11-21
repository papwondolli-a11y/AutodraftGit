#ifndef MODEL3DMESH_H
#define MODEL3DMESH_H

#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QVector>

struct Model3DOpening;

// ─────────────────────────────────────────────────────────────────────────────
// Model3DMesh — Model3D 를 면으로 바꿀 때 3D 보기와 내보내기(OBJ)가 함께 쓰는 기하 도우미
// ─────────────────────────────────────────────────────────────────────────────
class Model3DMesh {
public:
    // 구멍 있는 다각형(rings, 짝-홀 규칙)을 삼각형으로 나눈다 → 점 3개씩
    //   수평 줄무늬 사다리꼴 분할: 꼭짓점 Y 사이 띠마다 변 교차를 짝지어 사다리꼴 2삼각형.
    //   반환 삼각형은 위에서 볼 때 반시계(CCW) 방향.
    static QVector<QPointF> triangulate(const QList<QPolygonF> &rings);

    // 창 유리판: 벽 두께 가운데 선분 a→b (평면), 높이 z0~z1
    static bool glassPane(const Model3DOpening &o, QPointF &a, QPointF &b, double &z0, double &z1);
};

#endif // MODEL3DMESH_H
