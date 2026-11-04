#include "advancedboundaryfinder.h"
#include "caditem.h"
#include "mycircle.h"
#include "myarc.h"
#include <QPainterPath>
#include <QPolygonF>
#include <QLineF>
#include <QtMath>
#include <map>
#include <vector>
#include <algorithm>
#include <QDebug>
#include "mymath.h"
#include "myline.h"
#include "myspline.h"
#include "mypolyline.h"
// 거리 비교를 위한 Epsilon
const double EPSILON = 1e-4;

// 부동 소수점 오차로 인한 그래프 단절을 막기 위한 좌표 스내핑 (소수점 4자리)
QPointF snap(const QPointF& p) {
    return QPointF(std::round(p.x() * 10000.0) / 10000.0,
                   std::round(p.y() * 10000.0) / 10000.0);
}

bool AdvancedBoundaryFinder::intersectLineSegments(const QPointF& a1, const QPointF& a2,
                                                   const QPointF& b1, const QPointF& b2,
                                                   QPointF& outIntersect) {
    QLineF line1(a1, a2);
    QLineF line2(b1, b2);
    QPointF intersectPoint;
    QLineF::IntersectType type = line1.intersects(line2, &intersectPoint);
    if (type == QLineF::BoundedIntersection) {
        outIntersect = intersectPoint;
        return true;
    }
    return false;
}

double AdvancedBoundaryFinder::normalizeAngle(double angle) {
    while (angle <= -M_PI) angle += 2.0 * M_PI;
    while (angle > M_PI) angle -= 2.0 * M_PI;
    return angle;
}

std::vector<EdgeSegment> AdvancedBoundaryFinder::extractSegments(const QList<CadItem*> &items) {
    std::vector<EdgeSegment> segments;

    for (int i = 0; i < items.size(); ++i) { // C++11 index-based loop
        CadItem* item = items[i];
        if (!item) continue;

        const MyEntity* entity = item->getEntityData();

        // 원(Circle) 객체의 경우, 베지어 곡선 근사 오류를 막기 위해 수학적으로 직접 분할
        if (entity && entity->type() == EntityType::Circle) {
            const MyCircle* circle = static_cast<const MyCircle*>(entity);
            int segs = 72; // 분할 개수
            std::vector<QPointF> pts;
            for (int k = 0; k < segs; ++k) {
                double angle = 2.0 * M_PI * k / segs;
                double px = circle->center.x + circle->radius * std::cos(angle);
                double py = circle->center.y + circle->radius * std::sin(angle);
                pts.push_back(snap(item->sceneTransform().map(QPointF(px, py))));
            }
            for (int k = 0; k < segs; ++k) {
                QPointF p1 = pts[k];
                QPointF p2 = pts[(k+1) % segs];
                if (QLineF(p1, p2).length() > EPSILON) segments.push_back({p1, p2, 0, 0});
            }
            continue;
        }
        // 호(Arc) 객체의 경우, 끝점 연속성 보장을 위해 수학적으로 분할하고 양 끝점을 정확히 강제 맞춤
        else if (entity && entity->type() == EntityType::Arc) {
            const MyArc* arc = static_cast<const MyArc*>(entity);
            double startRad = qDegreesToRadians(arc->startAngle);
            double spanRad = qDegreesToRadians(arc->spanAngle);
            int segs = std::max(8, int(std::ceil(72.0 * std::abs(spanRad) / (2.0 * M_PI))));

            std::vector<QPointF> pts;
            for (int k = 0; k <= segs; ++k) {
                double angle = startRad + spanRad * k / segs;
                double px = arc->center.x + arc->radius * std::cos(angle);
                double py = arc->center.y + arc->radius * std::sin(angle);
                pts.push_back(snap(item->sceneTransform().map(QPointF(px, py))));
            }

            // 수학적으로 정확한 끝점 보정
            MyVector startPt = arc->startPoint();
            MyVector endPt = arc->endPoint();
            pts.front() = snap(item->sceneTransform().map(startPt.toPointF()));
            pts.back() = snap(item->sceneTransform().map(endPt.toPointF()));

            for (int k = 0; k < segs; ++k) {
                QPointF p1 = pts[k];
                QPointF p2 = pts[k+1];
                if (QLineF(p1, p2).length() > EPSILON) segments.push_back({p1, p2, 0, 0});
            }
            continue;
        }

        // --- 기본 처리 (Line, Polyline 등 직선형 기반 객체) ---
        QPainterPath path = item->geometryPath();
        path = item->sceneTransform().map(path);
        QList<QPolygonF> polygons = path.toSubpathPolygons();

        for (int pIdx = 0; pIdx < polygons.size(); ++pIdx) {
            const QPolygonF& poly = polygons[pIdx];
            for (int k = 0; k < poly.size() - 1; ++k) {
                QPointF p1 = snap(poly[k]);
                QPointF p2 = snap(poly[k+1]);
                if (QLineF(p1, p2).length() > EPSILON) {
                    segments.push_back({p1, p2, 0, 0});
                }
            }
            // 닫힌 폴리곤 처리
            if (poly.isClosed() && poly.size() > 1) {
                QPointF p1 = snap(poly.last());
                QPointF p2 = snap(poly.first());
                if (QLineF(p1, p2).length() > EPSILON) {
                    segments.push_back({p1, p2, 0, 0});
                }
            }
        }
    }
    return segments;
}

std::vector<QPointF> AdvancedBoundaryFinder::getExactIntersections(const QList<CadItem*> &items) {
    std::vector<QPointF> exactPoints;

    auto safeAdd = [&](const std::vector<MyVector>& pts) {
        for (size_t k = 0; k < pts.size(); ++k) {
            exactPoints.push_back(QPointF(pts[k].x, pts[k].y));
        }
    };

    for (int i = 0; i < items.size(); ++i) {
        for (int j = i + 1; j < items.size(); ++j) {
            const MyEntity* e1 = items[i]->getEntityData();
            const MyEntity* e2 = items[j]->getEntityData();
            if (!e1 || !e2) continue;

            // Type 1 vs Type 2 logic
            if (e1->type() == EntityType::Line && e2->type() == EntityType::Line) {
                safeAdd(MyMath::intersectLineLine(*static_cast<const MyLine*>(e1), *static_cast<const MyLine*>(e2)));
            }
            else if (e1->type() == EntityType::Line && e2->type() == EntityType::Circle) {
                safeAdd(MyMath::intersectLineCircle(*static_cast<const MyLine*>(e1), *static_cast<const MyCircle*>(e2)));
            }
            else if (e1->type() == EntityType::Circle && e2->type() == EntityType::Line) {
                safeAdd(MyMath::intersectLineCircle(*static_cast<const MyLine*>(e2), *static_cast<const MyCircle*>(e1)));
            }
            else if (e1->type() == EntityType::Circle && e2->type() == EntityType::Circle) {
                safeAdd(MyMath::intersectCircleCircle(*static_cast<const MyCircle*>(e1), *static_cast<const MyCircle*>(e2)));
            }
            else if (e1->type() == EntityType::Line && e2->type() == EntityType::Arc) {
                safeAdd(MyMath::intersectLineArc(*static_cast<const MyLine*>(e1), *static_cast<const MyArc*>(e2)));
            }
            else if (e1->type() == EntityType::Arc && e2->type() == EntityType::Line) {
                safeAdd(MyMath::intersectLineArc(*static_cast<const MyLine*>(e2), *static_cast<const MyArc*>(e1)));
            }
            else if (e1->type() == EntityType::Circle && e2->type() == EntityType::Arc) {
                safeAdd(MyMath::intersectCircleArc(*static_cast<const MyCircle*>(e1), *static_cast<const MyArc*>(e2)));
            }
            else if (e1->type() == EntityType::Arc && e2->type() == EntityType::Circle) {
                safeAdd(MyMath::intersectCircleArc(*static_cast<const MyCircle*>(e2), *static_cast<const MyArc*>(e1)));
            }
            else if (e1->type() == EntityType::Arc && e2->type() == EntityType::Arc) {
                safeAdd(MyMath::intersectArcArc(*static_cast<const MyArc*>(e1), *static_cast<const MyArc*>(e2)));
            }
            else if (e1->type() == EntityType::Line && e2->type() == EntityType::Spline) {
                safeAdd(MyMath::intersectLineSpline(*static_cast<const MyLine*>(e1), *static_cast<const MySpline*>(e2)));
            }
            else if (e1->type() == EntityType::Spline && e2->type() == EntityType::Line) {
                safeAdd(MyMath::intersectLineSpline(*static_cast<const MyLine*>(e2), *static_cast<const MySpline*>(e1)));
            }
            else if (e1->type() == EntityType::Arc && e2->type() == EntityType::Spline) {
                safeAdd(MyMath::intersectArcSpline(*static_cast<const MyArc*>(e1), *static_cast<const MySpline*>(e2)));
            }
            else if (e1->type() == EntityType::Spline && e2->type() == EntityType::Arc) {
                safeAdd(MyMath::intersectArcSpline(*static_cast<const MyArc*>(e2), *static_cast<const MySpline*>(e1)));
            }
            else if (e1->type() == EntityType::Spline && e2->type() == EntityType::Spline) {
                safeAdd(MyMath::intersectSplineSpline(*static_cast<const MySpline*>(e1), *static_cast<const MySpline*>(e2)));
            }
            // Polyline cases can be added if required
        }
    }

    return exactPoints;
}

std::vector<EdgeSegment> AdvancedBoundaryFinder::splitIntersectingSegments(const std::vector<EdgeSegment>& segments, const std::vector<QPointF>& exactIntersections) {
    std::vector<EdgeSegment> result;

    // [#5 수정] O(N²) 교차 검사 상한값 방어 코드
    // 선분이 너무 많을 경우 UI 프리즈가 발생합니다.
    // 상한값을 초과하면 경고를 출력하고 교차 처리 없이 원본 선분을 그대로 반환합니다.
    const size_t MAX_SEGMENTS = 500;
    if (segments.size() > MAX_SEGMENTS) {
        qWarning() << "[AdvancedBoundaryFinder] Too many segments (" << segments.size()
                   << "). Skipping intersection split to avoid O(N²) freeze."
                   << "Consider simplifying the geometry.";
        // angle, length 미리 계산 후 원본 선분 반환
        std::vector<EdgeSegment> raw = segments;
        for (size_t i = 0; i < raw.size(); ++i) {
            QLineF line(raw[i].p1, raw[i].p2);
            raw[i].length = line.length();
            raw[i].angle = std::atan2(line.dy(), line.dx());
        }
        return raw;
    }

    for (size_t i = 0; i < segments.size(); ++i) {
        std::vector<QPointF> splitPoints;
        splitPoints.push_back(segments[i].p1);
        splitPoints.push_back(segments[i].p2);
        
        // MyMath의 정확한 교점들을 해당 선분에 스냅
        QLineF currentLine(segments[i].p1, segments[i].p2);
        for (size_t k = 0; k < exactIntersections.size(); ++k) {
            QPointF p = exactIntersections[k];
            // 선분의 바운딩 박스 체크를 완화하여 약간 떨어진 교점도 포섭할 수 있게 함
            double t = 0;
            if (currentLine.length() > EPSILON) {
                QLineF pLine(currentLine.p1(), p);
                t = (pLine.dx() * currentLine.dx() + pLine.dy() * currentLine.dy()) / (currentLine.length() * currentLine.length());
            }
            if (t >= -0.01 && t <= 1.01) {
                QPointF projected(currentLine.p1().x() + t * currentLine.dx(), currentLine.p1().y() + t * currentLine.dy());
                if (QLineF(p, projected).length() < EPSILON * 50) { // 오차 허용범위 확대
                    QPointF snapped = snap(projected);
                    bool isDup = false;
                    for (size_t d = 0; d < splitPoints.size(); ++d) {
                        if (QLineF(splitPoints[d], snapped).length() < EPSILON) {
                            isDup = true; break;
                        }
                    }
                    if (!isDup) splitPoints.push_back(snapped);
                }
            }
        }

        for (size_t j = 0; j < segments.size(); ++j) {
            if (i == j) continue;
            QPointF intersect;
            if (intersectLineSegments(segments[i].p1, segments[i].p2, segments[j].p1, segments[j].p2, intersect)) {
                QPointF snapped = snap(intersect);
                // [추가] 중복 포인트 방지
                bool isDuplicate = false;
                for (size_t d = 0; d < splitPoints.size(); ++d) {
                    if (QLineF(splitPoints[d], snapped).length() < EPSILON) {
                        isDuplicate = true;
                        break;
                    }
                }
                if (!isDuplicate) {
                    splitPoints.push_back(snapped);
                }
            }
        }

        // 정렬
        QPointF origin = segments[i].p1;
        std::sort(splitPoints.begin(), splitPoints.end(), [&origin](const QPointF& a, const QPointF& b) {
            return QLineF(origin, a).length() < QLineF(origin, b).length();
        });

        // 중복 제거 후 분할된 선분 추가
        for (size_t k = 0; k < splitPoints.size() - 1; ++k) {
            QPointF p1 = splitPoints[k];
            QPointF p2 = splitPoints[k+1];
            if (QLineF(p1, p2).length() > EPSILON) {
                result.push_back({p1, p2, 0, 0});
            }
        }
    }

    // 계산 편의를 위해 각 edge마다 angle과 length 미리 계산
    for (size_t i = 0; i < result.size(); ++i) {
        QLineF line(result[i].p1, result[i].p2);
        result[i].length = line.length();
        result[i].angle = std::atan2(line.dy(), line.dx());
    }

    return result;
}

// 스냅된 점을 키로 사용하기 위한 비교자
struct PointComparator {
    bool operator()(const QPointF& a, const QPointF& b) const {
        if (std::abs(a.x() - b.x()) > 1e-5) return a.x() < b.x();
        return a.y() < b.y() - 1e-5;
    }
};

double AdvancedBoundaryFinder::getSignedArea(const QPolygonF& poly) {
    double area = 0.0;
    for (int i = 0; i < poly.size() - 1; ++i) {
        area += (poly[i].x() * poly[i+1].y() - poly[i+1].x() * poly[i].y());
    }
    return area / 2.0;
}

std::vector<QPolygonF> AdvancedBoundaryFinder::findAllFaces(const std::vector<EdgeSegment>& edges) {
    std::map<QPointF, std::vector<EdgeSegment>, PointComparator> adjList;

    for (size_t i = 0; i < edges.size(); ++i) {
        EdgeSegment fwd = edges[i];
        EdgeSegment bwd = {edges[i].p2, edges[i].p1, normalizeAngle(edges[i].angle + M_PI), edges[i].length};
        adjList[snap(fwd.p1)].push_back(fwd);
        adjList[snap(bwd.p1)].push_back(bwd);
    }

    for (auto it = adjList.begin(); it != adjList.end(); ++it) {
        std::sort(it->second.begin(), it->second.end(), [](const EdgeSegment& a, const EdgeSegment& b) {
            return a.angle < b.angle;
        });
    }

    struct EdgeId {
        QPointF p1, p2;
        bool operator<(const EdgeId& o) const {
            if (std::abs(p1.x() - o.p1.x()) > 1e-5) return p1.x() < o.p1.x();
            if (std::abs(p1.y() - o.p1.y()) > 1e-5) return p1.y() < o.p1.y();
            if (std::abs(p2.x() - o.p2.x()) > 1e-5) return p2.x() < o.p2.x();
            return p2.y() < o.p2.y() - 1e-5;
        }
    };
    std::map<EdgeId, bool> visited;
    std::vector<QPolygonF> internalFaces;

    // [수정] C++11 range-loop 제거 및 Iterator 기반 처리
    for (auto it = adjList.begin(); it != adjList.end(); ++it) {
        for (size_t k = 0; k < it->second.size(); ++k) {
            const EdgeSegment& startEdge = it->second[k];
            EdgeId sid = {snap(startEdge.p1), snap(startEdge.p2)};
            if (visited[sid]) continue;

            QPolygonF poly;
            EdgeSegment curr = startEdge;
            int steps = 0;
            int maxSteps = edges.size() * 2;
            bool closed = false;

            while (steps < maxSteps) {
                poly << curr.p1;
                visited[{snap(curr.p1), snap(curr.p2)}] = true;

                if (QLineF(snap(curr.p2), snap(startEdge.p1)).length() < EPSILON) {
                    poly << curr.p2; // 닫기
                    closed = true;
                    break;
                }

                QPointF currVert = snap(curr.p2);
                if (adjList.find(currVert) == adjList.end()) break;

                const auto& outgoing = adjList[currVert];
                if (outgoing.empty()) break;

                double incomingRev = normalizeAngle(curr.angle + M_PI);
                int nextIdx = -1;
                for (size_t i = 0; i < outgoing.size(); ++i) {
                    if (outgoing[i].angle > incomingRev + EPSILON) {
                        nextIdx = i; break;
                    }
                }
                if (nextIdx == -1) nextIdx = 0;

                curr = outgoing[nextIdx];
                steps++;
            }

            // 닫힌 폴리곤이며, 시계 반대방향(내부 영역)인 경우 등록
            if (closed && getSignedArea(poly) < -EPSILON) {
                internalFaces.push_back(poly);
            }
        }
    }

    return internalFaces;
}

QPainterPath AdvancedBoundaryFinder::findBoundaryPath(const QList<CadItem*> &items, const QPointF &point) {
    if (items.isEmpty()) return QPainterPath();

    std::vector<QPointF> exactIntersections = getExactIntersections(items);
    std::vector<EdgeSegment> segments = extractSegments(items);
    std::vector<EdgeSegment> splitSegments = splitIntersectingSegments(segments, exactIntersections);

    std::vector<QPolygonF> allFaces = findAllFaces(splitSegments);

    int mainFaceIdx = -1;
    double minArea = 1e99;

    for (size_t i = 0; i < allFaces.size(); ++i) { // 인덱스 기반 루프 유지
        QPainterPath p;
        p.addPolygon(allFaces[i]);
        if (p.contains(point)) {
            double area = std::abs(getSignedArea(allFaces[i]));
            if (area < minArea) {
                minArea = area;
                mainFaceIdx = (int)i;
            }
        }
    }

    if (mainFaceIdx == -1) return QPainterPath();

    QPainterPath finalPath;
    finalPath.addPolygon(allFaces[mainFaceIdx]);

    for (size_t i = 0; i < allFaces.size(); ++i) {
        if ((int)i == mainFaceIdx) continue;

        const QPolygonF& face = allFaces[i];

        bool sharesVertex = false;
        for (int v1 = 0; v1 < face.size(); ++v1) {
            QPointF p1 = snap(face[v1]);
            for (int v2 = 0; v2 < allFaces[mainFaceIdx].size(); ++v2) {
                QPointF p2 = snap(allFaces[mainFaceIdx][v2]);
                if (QLineF(p1, p2).length() < EPSILON) {
                    sharesVertex = true;
                    break;
                }
            }
            if (sharesVertex) break;
        }

        if (!sharesVertex) {
            if (face.size() > 0 && finalPath.contains(face[0])) {
                QPainterPath holePath;
                holePath.addPolygon(face);
                finalPath.addPath(holePath);
            }
        }
    }

    finalPath.setFillRule(Qt::OddEvenFill);
    return finalPath;
}

QPainterPath AdvancedBoundaryFinder::findBoundaryPathInRect(const QList<CadItem*> &items, const QRectF &rect) {
    // [수정] 드래그 방향으로 인해 사각형의 width, height가 음수일 경우 예외가 발생하는 것을 방지
    QRectF normRect = rect.normalized();
    if (normRect.isEmpty()) return QPainterPath();

    QPainterPath finalPath;
    finalPath.addRect(normRect); // 베이스 경계는 정규화된 사각형 사용

    if (items.isEmpty()) {
        finalPath.setFillRule(Qt::OddEvenFill);
        return finalPath;
    }

    std::vector<QPointF> exactIntersections = getExactIntersections(items);
    std::vector<EdgeSegment> segments = extractSegments(items);
    std::vector<EdgeSegment> splitSegments = splitIntersectingSegments(segments, exactIntersections);
    std::vector<QPolygonF> allFaces = findAllFaces(splitSegments);

    for (size_t i = 0; i < allFaces.size(); ++i) { // 인덱스 루프
        const QPolygonF& face = allFaces[i];

        if (face.size() > 0) {
            QRectF faceRect = face.boundingRect();

            // normRect 기반으로 확장된 영역 계산
            QRectF expanded(normRect.adjusted(-EPSILON, -EPSILON, EPSILON, EPSILON));

            bool isMainBoundary = (std::abs(faceRect.width() - normRect.width()) < EPSILON * 10 &&
                                   std::abs(faceRect.height() - normRect.height()) < EPSILON * 10);

            if (expanded.contains(faceRect) && !isMainBoundary) {
                QPainterPath holePath;
                holePath.addPolygon(face);
                finalPath.addPath(holePath);
            }
        }
    }

    finalPath.setFillRule(Qt::OddEvenFill);
    return finalPath;
}