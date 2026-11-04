#ifndef ADVANCEDBOUNDARYFINDER_H
#define ADVANCEDBOUNDARYFINDER_H

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <vector>

class CadItem;
class MyVector;

// 분할된 선분을 나타내는 내부 구조체
struct EdgeSegment {
    QPointF p1;
    QPointF p2;
    double angle;    // p1에서 p2로 향하는 각도 (-PI ~ PI)
    double length;
};

class AdvancedBoundaryFinder {
public:
    static QPainterPath findBoundaryPath(const QList<CadItem*> &items, const QPointF &point);
    static QPainterPath findBoundaryPathInRect(const QList<CadItem*> &items, const QRectF &rect);

private:
    static std::vector<QPointF> getExactIntersections(const QList<CadItem*> &items);
    static std::vector<EdgeSegment> extractSegments(const QList<CadItem*> &items);
    static std::vector<EdgeSegment> splitIntersectingSegments(const std::vector<EdgeSegment>& segments, const std::vector<QPointF>& exactIntersections = {});

    static std::vector<QPolygonF> findAllFaces(const std::vector<EdgeSegment>& edges);
    static double getSignedArea(const QPolygonF& poly);

    // 유틸리티
    static bool intersectLineSegments(const QPointF& a1, const QPointF& a2,
                                      const QPointF& b1, const QPointF& b2,
                                      QPointF& outIntersect);
    static double normalizeAngle(double angle);
};

#endif // ADVANCEDBOUNDARYFINDER_H