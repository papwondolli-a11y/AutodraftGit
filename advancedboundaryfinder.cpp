#include "advancedboundaryfinder.h"
#include "caditem.h"
#include "mycircle.h"
#include "myarc.h"
#include <QPainterPath>
#include <QPolygonF>
#include <QLineF>
#include <QTransform>
#include <QtMath>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <cmath>
#include <QDebug>
#include "mymath.h"
#include "myline.h"
#include "myspline.h"
#include "mypolyline.h"

// ─────────────────────────────────────────────────────────────────────────────
// 닫힌 공간 찾기
//   ① 모든 도형을 선분으로 (원·호는 72등분 현 + 다른 도형이 닿는 점을 꼭짓점으로 — 접선·원 위에서 끝나는 선도 이어지게)
//   ② 선분끼리 교차·끝점 닿음(T자)·겹침을 모두 나눔 (x 순으로 훑어 가까운 것끼리만 — 선분 수 상한 없이 빠름)
//   ③ 끊긴 가지를 쳐 낸 뒤 반쪽 변(half-edge)을 돌아 면을 찾음. 같은 점을 두 번 지나는 길은 고리로 나눠
//      바깥 고리와 구멍 고리로 구분
// ─────────────────────────────────────────────────────────────────────────────
namespace {
const double EPSILON = 1e-4;
const double MERGE_TOL = 5e-4;      // 이보다 가까운 꼭짓점은 같은 점
const double JOIN_TOL = 1e-3;       // 선 끝이 다른 선·원에 닿은 것으로 보는 거리
const size_t MAX_SEGMENTS = 60000;  // 터무니없이 큰 입력 방어

double dist(const QPointF& a, const QPointF& b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }
double cross(const QPointF& a, const QPointF& b) { return a.x() * b.y() - a.y() * b.x(); }
double dot(const QPointF& a, const QPointF& b) { return a.x() * b.x() + a.y() * b.y(); }

void addSegment(std::vector<EdgeSegment>& out, const QPointF& a, const QPointF& b)
{
    if (dist(a, b) > EPSILON)
        out.push_back(EdgeSegment{a, b});
}

// 원·호 (씬 좌표)
struct CurveData {
    QPointF center;
    double radius = 0.0;
    bool full = true;
    double start = 0.0;             // 라디안 (반시계로 start → start + span)
    double span = 2.0 * M_PI;
    QPointF p0, p1;                 // 호의 정확한 양 끝점
};

struct CurvePoint {
    double t;
    QPointF p;
};
bool lessCurvePoint(const CurvePoint& a, const CurvePoint& b) { return a.t < b.t; }

double wrap2pi(double a)
{
    a = std::fmod(a, 2.0 * M_PI);
    return a < 0.0 ? a + 2.0 * M_PI : a;
}

// 곡선을 현으로 — 닿는 점(touch)을 꼭짓점으로 끼움
void tessellate(const CurveData& c, const std::vector<QPointF>& touch, std::vector<EdgeSegment>& out)
{
    const int segs = c.full ? 72 : std::max(8, int(std::ceil(72.0 * c.span / (2.0 * M_PI))));
    std::vector<CurvePoint> pts;
    for (int k = 0; k <= segs; ++k) {
        const double t = c.span * k / segs;
        const double a = c.start + t;
        pts.push_back(CurvePoint{t, c.center + QPointF(std::cos(a), std::sin(a)) * c.radius});
    }
    if (c.full) {
        pts.back().p = pts.front().p;
    } else {
        pts.front().p = c.p0;
        pts.back().p = c.p1;
    }
    for (size_t i = 0; i < touch.size(); ++i) {
        const QPointF d = touch[i] - c.center;
        if (std::fabs(std::hypot(d.x(), d.y()) - c.radius) > JOIN_TOL)
            continue;
        const double t = wrap2pi(std::atan2(d.y(), d.x()) - c.start);
        if (t < c.span)
            pts.push_back(CurvePoint{t, touch[i]});
    }
    std::stable_sort(pts.begin(), pts.end(), lessCurvePoint);
    for (size_t i = 1; i < pts.size(); ++i)
        addSegment(out, pts[i - 1].p, pts[i].p);
}

// 한 선분 위의 나눌 점
struct SplitPoint {
    double t;
    QPointF p;
};
bool lessSplit(const SplitPoint& a, const SplitPoint& b) { return a.t < b.t; }

// 점이 선분에 닿으면 나눌 점으로
void addTouch(const EdgeSegment& s, const QPointF& p, std::vector<SplitPoint>& out)
{
    const QPointF d = s.p2 - s.p1;
    const double len = std::hypot(d.x(), d.y());
    const double t = dot(p - s.p1, d) / (len * len);
    if (t <= 0.0 || t >= 1.0 || std::fabs(cross(d, p - s.p1)) / len > JOIN_TOL)
        return;
    out.push_back(SplitPoint{t, p});
}

// 두 선분의 교차·닿음
void splitPair(const EdgeSegment& a, const EdgeSegment& b, std::vector<SplitPoint>& sa, std::vector<SplitPoint>& sb)
{
    addTouch(a, b.p1, sa);
    addTouch(a, b.p2, sa);
    addTouch(b, a.p1, sb);
    addTouch(b, a.p2, sb);
    const QPointF da = a.p2 - a.p1, db = b.p2 - b.p1;
    const double den = cross(da, db);
    if (std::fabs(den) < 1e-9 * std::hypot(da.x(), da.y()) * std::hypot(db.x(), db.y()))
        return;                                         // 나란함 — 겹침은 끝점 닿음으로 처리됨
    const double ta = cross(b.p1 - a.p1, db) / den;
    const double tb = cross(b.p1 - a.p1, da) / den;
    if (ta < 0.0 || ta > 1.0 || tb < 0.0 || tb > 1.0)
        return;
    const QPointF p = a.p1 + da * ta;
    sa.push_back(SplitPoint{ta, p});
    sb.push_back(SplitPoint{tb, p});
}

// x 최솟값 순서 정렬용
struct SweepItem {
    double xmin;
    size_t idx;
};
bool lessSweep(const SweepItem& a, const SweepItem& b) { return a.xmin < b.xmin; }

// 가까운 점을 하나로 묶는 꼭짓점 모음
class VertexPool {
public:
    int idOf(const QPointF& p)
    {
        const long long gx = (long long)std::floor(p.x() / CELL), gy = (long long)std::floor(p.y() / CELL);
        int best = -1;
        double bestD = MERGE_TOL;
        for (long long x = gx - 1; x <= gx + 1; ++x)
            for (long long y = gy - 1; y <= gy + 1; ++y) {
                const std::map<std::pair<long long, long long>, std::vector<int> >::const_iterator it =
                    m_grid.find(std::make_pair(x, y));
                if (it == m_grid.end())
                    continue;
                for (size_t i = 0; i < it->second.size(); ++i) {
                    const double d = dist(pts[size_t(it->second[i])], p);
                    if (d < bestD) {
                        bestD = d;
                        best = it->second[i];
                    }
                }
            }
        if (best >= 0)
            return best;
        pts.push_back(p);
        const int id = int(pts.size()) - 1;
        m_grid[std::make_pair(gx, gy)].push_back(id);
        return id;
    }
    std::vector<QPointF> pts;

private:
    static constexpr double CELL = 1e-3;
    std::map<std::pair<long long, long long>, std::vector<int> > m_grid;
};

// 한 꼭짓점에서 나가는 변 (반시계 각도 순)
struct OutEdge {
    double angle;
    int half;                       // 반쪽 변 번호
};
bool lessOut(const OutEdge& a, const OutEdge& b) { return a.angle < b.angle; }

int rootOf(std::vector<int>& parent, int v)
{
    while (parent[size_t(v)] != v) {
        parent[size_t(v)] = parent[size_t(parent[size_t(v)])];
        v = parent[size_t(v)];
    }
    return v;
}

// 면 안쪽의 점 (가장 긴 변의 가운데에서 안쪽으로 조금)
QPointF insidePoint(const QPolygonF& poly)
{
    int best = 0;
    double bestL = -1.0;
    for (int i = 0; i + 1 < poly.size(); ++i) {
        const double l = dist(poly.at(i), poly.at(i + 1));
        if (l > bestL) {
            bestL = l;
            best = i;
        }
    }
    const QPointF a = poly.at(best), b = poly.at(best + 1);
    const QPointF n(-(b.y() - a.y()) / bestL, (b.x() - a.x()) / bestL);
    const double off = std::min(0.01 * bestL, 10.0 * EPSILON);
    const QPointF m = (a + b) * 0.5;
    return poly.containsPoint(m + n * off, Qt::OddEvenFill) ? m + n * off : m - n * off;
}
} // namespace

std::vector<EdgeSegment> AdvancedBoundaryFinder::extractSegments(const QList<CadItem*> &items,
                                                                 const std::vector<QPointF>& exactIntersections) {
    std::vector<EdgeSegment> segments;
    std::vector<CurveData> curves;
    std::vector<QPointF> touch = exactIntersections;    // 원·호에 닿는지 볼 점: 교점 + 모든 선·호의 끝점

    for (int i = 0; i < items.size(); ++i) {
        CadItem* item = items[i];
        if (!item) continue;
        const QTransform tr = item->sceneTransform();
        const MyEntity* entity = item->getEntityData();
        const bool isCircle = entity && entity->type() == EntityType::Circle;
        const bool isArc = entity && entity->type() == EntityType::Arc;

        if (isCircle || isArc) {
            // 원·호: 베지어 근사 대신 중심·반지름으로 직접 나눔
            CurveData c;
            if (isCircle) {
                const MyCircle* circle = static_cast<const MyCircle*>(entity);
                c.center = tr.map(QPointF(circle->center.x, circle->center.y));
                c.radius = dist(c.center, tr.map(QPointF(circle->center.x + circle->radius, circle->center.y)));
            } else {
                const MyArc* arc = static_cast<const MyArc*>(entity);
                c.full = false;
                c.center = tr.map(QPointF(arc->center.x, arc->center.y));
                QPointF s = tr.map(arc->startPoint().toPointF()), e = tr.map(arc->endPoint().toPointF());
                double span = qDegreesToRadians(arc->spanAngle);
                if (tr.determinant() < 0.0)
                    span = -span;                       // 뒤집힌 변환
                if (span < 0.0) {
                    std::swap(s, e);
                    span = -span;
                }
                c.radius = dist(c.center, s);
                c.start = std::atan2(s.y() - c.center.y(), s.x() - c.center.x());
                c.span = span;
                c.p0 = s;
                c.p1 = e;
                touch.push_back(s);
                touch.push_back(e);
            }
            if (c.radius > EPSILON)
                curves.push_back(c);
            continue;
        }

        // 직선형 객체 (선·폴리라인·블록 등)
        const QPainterPath path = tr.map(item->geometryPath());
        const QList<QPolygonF> polygons = path.toSubpathPolygons();
        for (int pIdx = 0; pIdx < polygons.size(); ++pIdx) {
            const QPolygonF& poly = polygons.at(pIdx);
            for (int k = 0; k + 1 < poly.size(); ++k)
                addSegment(segments, poly.at(k), poly.at(k + 1));
        }
    }
    for (size_t i = 0; i < segments.size(); ++i) {
        touch.push_back(segments[i].p1);
        touch.push_back(segments[i].p2);
    }
    for (size_t i = 0; i < curves.size(); ++i)
        tessellate(curves[i], touch, segments);
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

std::vector<EdgeSegment> AdvancedBoundaryFinder::splitIntersectingSegments(const std::vector<EdgeSegment>& segments) {
    if (segments.size() > MAX_SEGMENTS) {
        qWarning() << "[AdvancedBoundaryFinder] Too many segments:" << segments.size();
        return segments;
    }
    const size_t n = segments.size();
    std::vector<std::vector<SplitPoint> > splits(n);
    // x 순으로 훑으며 경계 사각이 (거의) 닿는 것끼리만 검사
    std::vector<SweepItem> order;
    for (size_t i = 0; i < n; ++i)
        order.push_back(SweepItem{std::min(segments[i].p1.x(), segments[i].p2.x()) - JOIN_TOL, i});
    std::sort(order.begin(), order.end(), lessSweep);
    std::vector<size_t> active;
    for (size_t k = 0; k < order.size(); ++k) {
        const size_t i = order[k].idx;
        const double ylo = std::min(segments[i].p1.y(), segments[i].p2.y()) - JOIN_TOL;
        const double yhi = std::max(segments[i].p1.y(), segments[i].p2.y()) + JOIN_TOL;
        size_t w = 0;
        for (size_t m = 0; m < active.size(); ++m) {
            const size_t j = active[m];
            if (std::max(segments[j].p1.x(), segments[j].p2.x()) + JOIN_TOL < order[k].xmin)
                continue;                               // 왼쪽으로 지나감
            active[w++] = j;
            if (std::max(segments[j].p1.y(), segments[j].p2.y()) < ylo ||
                std::min(segments[j].p1.y(), segments[j].p2.y()) > yhi)
                continue;
            splitPair(segments[i], segments[j], splits[i], splits[j]);
        }
        active.resize(w);
        active.push_back(i);
    }
    std::vector<EdgeSegment> result;
    for (size_t i = 0; i < n; ++i) {
        std::vector<SplitPoint>& sp = splits[i];
        std::sort(sp.begin(), sp.end(), lessSplit);
        QPointF prev = segments[i].p1;
        for (size_t k = 0; k < sp.size(); ++k) {
            result.push_back(EdgeSegment{prev, sp[k].p});
            prev = sp[k].p;
        }
        result.push_back(EdgeSegment{prev, segments[i].p2});
    }
    return result;
}

double AdvancedBoundaryFinder::getSignedArea(const QPolygonF& poly) {
    double area = 0.0;
    for (int i = 0; i + 1 < poly.size(); ++i)
        area += (poly[i].x() * poly[i+1].y() - poly[i+1].x() * poly[i].y());
    return area / 2.0;
}

std::vector<BoundaryFace> AdvancedBoundaryFinder::findAllFaces(const std::vector<EdgeSegment>& edges) {
    std::vector<BoundaryFace> faces;
    VertexPool pool;
    std::set<std::pair<int, int> > edgeSet;             // 겹친 선은 하나로
    for (size_t i = 0; i < edges.size(); ++i) {
        const int a = pool.idOf(edges[i].p1), b = pool.idOf(edges[i].p2);
        if (a != b)
            edgeSet.insert(std::make_pair(std::min(a, b), std::max(a, b)));
    }
    const size_t nv = pool.pts.size();
    // 끊긴 가지(한쪽 끝이 빈 선) 쳐 내기
    std::vector<std::set<int> > nb(nv);
    for (std::set<std::pair<int, int> >::const_iterator it = edgeSet.begin(); it != edgeSet.end(); ++it) {
        nb[size_t(it->first)].insert(it->second);
        nb[size_t(it->second)].insert(it->first);
    }
    std::vector<int> stack;
    for (size_t v = 0; v < nv; ++v)
        if (nb[v].size() == 1)
            stack.push_back(int(v));
    while (!stack.empty()) {
        const int v = stack.back();
        stack.pop_back();
        if (nb[size_t(v)].size() != 1)
            continue;
        const int o = *nb[size_t(v)].begin();
        nb[size_t(v)].clear();
        nb[size_t(o)].erase(v);
        if (nb[size_t(o)].size() == 1)
            stack.push_back(o);
    }
    // 반쪽 변: 2k = a→b, 2k+1 = b→a. 덩어리(이어진 선) 번호
    std::vector<int> from, to, parent(nv);
    for (size_t v = 0; v < nv; ++v)
        parent[v] = int(v);
    std::vector<std::vector<OutEdge> > out(nv);
    for (size_t v = 0; v < nv; ++v)
        for (std::set<int>::const_iterator it = nb[v].begin(); it != nb[v].end(); ++it) {
            if (*it < int(v))
                continue;
            const int h = int(from.size());
            from.push_back(int(v));
            to.push_back(*it);
            from.push_back(*it);
            to.push_back(int(v));
            const QPointF d = pool.pts[size_t(*it)] - pool.pts[v];
            out[v].push_back(OutEdge{std::atan2(d.y(), d.x()), h});
            out[size_t(*it)].push_back(OutEdge{std::atan2(-d.y(), -d.x()), h + 1});
            parent[size_t(rootOf(parent, int(v)))] = rootOf(parent, *it);
        }
    // 다음 변: 도착점에서 되돌아가는 변의 시계 방향 바로 앞 변 → 면이 왼쪽에 놓인 반시계 길
    std::vector<int> next(from.size(), -1);
    for (size_t v = 0; v < nv; ++v) {
        std::sort(out[v].begin(), out[v].end(), lessOut);
        const size_t m = out[v].size();
        for (size_t k = 0; k < m; ++k)
            next[size_t(out[v][k].half ^ 1)] = out[v][(k + m - 1) % m].half;
    }
    std::vector<int> walkOf(from.size(), -1);
    for (size_t h0 = 0; h0 < from.size(); ++h0) {
        if (walkOf[h0] >= 0)
            continue;
        std::vector<int> walk;
        int h = int(h0);
        while (walkOf[size_t(h)] < 0) {
            walkOf[size_t(h)] = int(h0);
            walk.push_back(h);
            h = next[size_t(h)];
        }
        // 길을 같은 점에서 나눠 단순 고리들로 (왕복하는 다리 변은 면적 0 고리라 버려짐)
        std::vector<int> path;                          // 지나온 꼭짓점
        std::map<int, size_t> at;                       // 꼭짓점 → path 안 자리
        std::vector<QPolygonF> loops;
        const int first = from[size_t(walk[0])];
        path.push_back(first);
        at[first] = 0;
        for (size_t k = 0; k < walk.size(); ++k) {
            const int v = to[size_t(walk[k])];
            const std::map<int, size_t>::iterator f = at.find(v);
            if (f == at.end()) {
                at[v] = path.size();
                path.push_back(v);
                continue;
            }
            const size_t s = f->second;
            if (path.size() - s >= 3) {
                QPolygonF loop;
                for (size_t q = s; q < path.size(); ++q)
                    loop << pool.pts[size_t(path[q])];
                loop << loop.first();
                loops.push_back(loop);
            }
            while (path.size() > s + 1) {
                at.erase(path.back());
                path.pop_back();
            }
        }
        // 반시계 고리 = 바깥, 시계 고리 = 그 안의 구멍 (바깥 면의 길은 모두 시계 고리)
        BoundaryFace face;
        for (size_t k = 0; k < loops.size(); ++k) {
            const double area = getSignedArea(loops[k]);
            if (area > face.area) {
                face.outer = loops[k];
                face.area = area;
            }
        }
        if (face.area <= EPSILON)
            continue;
        for (size_t k = 0; k < loops.size(); ++k)
            if (getSignedArea(loops[k]) < -EPSILON)
                face.holes << loops[k];
        face.component = rootOf(parent, first);
        faces.push_back(face);
    }
    return faces;
}

QPainterPath AdvancedBoundaryFinder::findBoundaryPath(const QList<CadItem*> &items, const QPointF &point) {
    if (items.isEmpty()) return QPainterPath();

    const std::vector<EdgeSegment> segments = extractSegments(items, getExactIntersections(items));
    const std::vector<BoundaryFace> allFaces = findAllFaces(splitIntersectingSegments(segments));

    // 점을 둘러싼 가장 작은 공간
    int mainFaceIdx = -1;
    double minArea = 1e99;
    for (size_t i = 0; i < allFaces.size(); ++i) {
        if (allFaces[i].area < minArea && allFaces[i].outer.containsPoint(point, Qt::OddEvenFill)) {
            minArea = allFaces[i].area;
            mainFaceIdx = (int)i;
        }
    }
    if (mainFaceIdx == -1) return QPainterPath();

    const BoundaryFace& mainFace = allFaces[size_t(mainFaceIdx)];
    QPainterPath finalPath;
    finalPath.setFillRule(Qt::OddEvenFill);
    finalPath.addPolygon(mainFace.outer);
    for (int k = 0; k < mainFace.holes.size(); ++k)
        finalPath.addPolygon(mainFace.holes.at(k));
    const QPainterPath base = finalPath;

    // 떨어진 섬 (이어지지 않은 다른 덩어리가 이 공간 안에 있음)
    for (size_t i = 0; i < allFaces.size(); ++i) {
        if (allFaces[i].component == mainFace.component)
            continue;
        if (base.contains(insidePoint(allFaces[i].outer)))
            finalPath.addPolygon(allFaces[i].outer);
    }
    return finalPath;
}

QPainterPath AdvancedBoundaryFinder::findBoundaryPathInRect(const QList<CadItem*> &items, const QRectF &rect) {
    // 드래그 방향에 따라 폭·높이가 음수일 수 있음
    const QRectF normRect = rect.normalized();
    if (normRect.isEmpty()) return QPainterPath();

    QPainterPath finalPath;
    finalPath.setFillRule(Qt::OddEvenFill);
    finalPath.addRect(normRect);
    if (items.isEmpty())
        return finalPath;

    const std::vector<EdgeSegment> segments = extractSegments(items, getExactIntersections(items));
    const std::vector<BoundaryFace> allFaces = findAllFaces(splitIntersectingSegments(segments));
    const QRectF expanded(normRect.adjusted(-EPSILON, -EPSILON, EPSILON, EPSILON));
    for (size_t i = 0; i < allFaces.size(); ++i) {
        const QRectF faceRect = allFaces[i].outer.boundingRect();
        const bool isMainBoundary = (std::abs(faceRect.width() - normRect.width()) < EPSILON * 10 &&
                                     std::abs(faceRect.height() - normRect.height()) < EPSILON * 10);
        if (expanded.contains(faceRect) && !isMainBoundary)
            finalPath.addPolygon(allFaces[i].outer);
    }
    return finalPath;
}
