#ifndef ADVANCEDBOUNDARYFINDER_H
#define ADVANCEDBOUNDARYFINDER_H

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <vector>

class CadItem;
class MyVector;

// 선분 (원·호는 잘게 나눈 현)
struct EdgeSegment {
    QPointF p1;
    QPointF p2;
};

// 닫힌 공간 하나: 바깥 고리 + 같은 덩어리(선이 이어진 도형) 안의 구멍 고리
struct BoundaryFace {
    QPolygonF outer;
    QList<QPolygonF> holes;
    int component = -1;      // 이어진 선 덩어리 번호 (다른 덩어리 = 떨어진 섬)
    double area = 0.0;
};

class AdvancedBoundaryFinder {
public:
    static QPainterPath findBoundaryPath(const QList<CadItem*> &items, const QPointF &point);
    static QPainterPath findBoundaryPathInRect(const QList<CadItem*> &items, const QRectF &rect);

private:
    static std::vector<QPointF> getExactIntersections(const QList<CadItem*> &items);
    static std::vector<EdgeSegment> extractSegments(const QList<CadItem*> &items, const std::vector<QPointF>& exactIntersections);
    static std::vector<EdgeSegment> splitIntersectingSegments(const std::vector<EdgeSegment>& segments);

    static std::vector<BoundaryFace> findAllFaces(const std::vector<EdgeSegment>& edges);
    static double getSignedArea(const QPolygonF& poly);
};

#endif // ADVANCEDBOUNDARYFINDER_H
