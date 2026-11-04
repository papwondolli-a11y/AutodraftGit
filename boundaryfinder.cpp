#include "boundaryfinder.h"
#include "caditem.h"
#include "arcitem.h"
#include "circleitem.h"
#include "polylineitem.h"
#include <QPainterPath>
#include <QPolygonF>

CadItem* BoundaryFinder::findInnermostBoundary(const QList<CadItem*> &items, const QPointF &point) {
    CadItem* bestTarget = nullptr;
    double minArea = -1.0;

    for (int i = 0; i < items.size(); ++i) { // C++11 index-based loop
        CadItem* ci = items[i];
        if (!ci) continue;

        QPainterPath path = ci->geometryPath();
        
        // 닫힌 객체인지 확인
        // - Circle: 항상 닫힌 경계
        // - Arc: 항상 닫힌 경계로 간주 (호 양 끝점이 연결된 구조로 사용)
        // - Polyline: IsClosed 속성으로 판별
        bool isClosed = false;
        if (dynamic_cast<CircleItem*>(ci)) {
            isClosed = true;
        } else if (dynamic_cast<ArcItem*>(ci)) {
            isClosed = true;
        } else if (PolylineItem* pi = dynamic_cast<PolylineItem*>(ci)) {
            if (pi->getProperties().value("IsClosed").toBool()) isClosed = true;
        }

        if (isClosed && path.contains(point)) {
            double currentArea = calculatePathArea(path);
            if (bestTarget == nullptr || currentArea < minArea) {
                minArea = currentArea;
                bestTarget = ci;
            }
        }
    }

    return bestTarget;
}

double BoundaryFinder::calculatePathArea(const QPainterPath &path) {
    // Shoelace(신발끈) 공식으로 실제 다각형 면적을 계산합니다.
    // boundingRect().width()*height()는 bounding box 면적으로 부정확합니다.
    double area = 0.0;
    QList<QPolygonF> polys = path.toSubpathPolygons();
    for (int p = 0; p < polys.size(); ++p) {
        const QPolygonF &poly = polys.at(p);
        int n = poly.size();
        for (int i = 0; i < n - 1; ++i) {
            area += (poly[i].x() * poly[i + 1].y()) - (poly[i + 1].x() * poly[i].y());
        }
    }
    return std::abs(area) / 2.0;
}
