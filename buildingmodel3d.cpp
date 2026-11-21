#include "buildingmodel3d.h"

#include "arcitem.h"
#include "caditem.h"
#include "layermanager.h"
#include "lineitem.h"
#include "multilinedata.h"
#include "mygraphicsscene.h"
#include "openingheights.h"
#include "planarchdata.h"
#include "polylineitem.h"
#include "scenesymbolregistry.h"

#include <QHash>
#include <QLineF>
#include <QMap>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 내부 구현
// ─────────────────────────────────────────────────────────────────────────────
namespace {

const double kTol       = 0.5;   // 점 병합·접촉 판정 허용오차 (mm)
const double kArcStepDeg = 10.0; // 호 분할 각도
const double kAreaEps   = 1.0;   // 면적 0 판정 (mm²)

struct LayerInfo {
    QString name;
    double z0 = 0.0;
    double z1 = 0.0;
};

// 입력 선분
struct Seg {
    QPointF a;
    QPointF b;
    int layer = -1;    // LayerInfo 인덱스 (가상선 = -1)
    bool real = true;  // false = 문·창 개구부 가상 경계
    double minX = 0.0;
    double maxX = 0.0;
    std::vector<double> cuts; // 이 선분 위 분할 위치 t (0~1)
};

struct OpeningDef {
    QString id;
    SymbolInfo info;
    QPolygonF rect;     // 0:(0,yL) 1:(W,yL) 2:(W,yH) 3:(0,yH)
    double width = 0.0;
    double depth = 0.0;
    bool window = false;
};

struct Edge {
    int u = 0;
    int v = 0;
    bool real = false;
    int layer = -1;
};

struct SegMinXLess {
    const std::vector<Seg> *segs;
    bool operator()(int i, int j) const { return (*segs)[i].minX < (*segs)[j].minX; }
};

struct AngleLess {
    const std::vector<double> *angles;
    bool operator()(int h1, int h2) const { return (*angles)[h1] < (*angles)[h2]; }
};

double cross(const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); }
double dot(const QPointF &a, const QPointF &b) { return a.x() * b.x() + a.y() * b.y(); }
double len(const QPointF &a) { return std::sqrt(dot(a, a)); }

// 점 p 의 선분 ab 위 투영 t 와 거리
double projectT(const QPointF &p, const QPointF &a, const QPointF &b, double *dist)
{
    const QPointF d = b - a;
    const double l2 = dot(d, d);
    double t = l2 > 1e-12 ? dot(p - a, d) / l2 : 0.0;
    const double tc = std::max(0.0, std::min(1.0, t));
    if (dist)
        *dist = len(p - (a + d * tc));
    return t;
}

void addSeg(std::vector<Seg> &out, const QPointF &a, const QPointF &b, int layer, bool real)
{
    if (len(b - a) < 1e-6)
        return;
    Seg s;
    s.a = a;
    s.b = b;
    s.layer = layer;
    s.real = real;
    s.minX = std::min(a.x(), b.x());
    s.maxX = std::max(a.x(), b.x());
    out.push_back(s);
}

// 원호(중심·반지름·시작각·회전각, 도) → 선분
void addArcSegs(std::vector<Seg> &out, const QPointF &c, double r, double startDeg,
                double spanDeg, int layer)
{
    const int n = std::max(1, int(std::ceil(std::fabs(spanDeg) / kArcStepDeg)));
    QPointF prev(c.x() + r * std::cos(qDegreesToRadians(startDeg)),
                 c.y() + r * std::sin(qDegreesToRadians(startDeg)));
    for (int i = 1; i <= n; ++i) {
        const double a = qDegreesToRadians(startDeg + spanDeg * double(i) / n);
        const QPointF p(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
        addSeg(out, prev, p, layer, true);
        prev = p;
    }
}

// 폴리라인 한 구간 (bulge 포함)
void addBulgeSegs(std::vector<Seg> &out, const QPointF &p, const QPointF &q, double bulge, int layer)
{
    if (std::fabs(bulge) < 1e-9) {
        addSeg(out, p, q, layer, true);
        return;
    }
    const QPointF d = q - p;
    const double L = len(d);
    if (L < 1e-9)
        return;
    const double theta = 4.0 * std::atan(bulge);                 // 회전각 (CCW 양수)
    const QPointF mid = (p + q) * 0.5;
    const QPointF leftN(-d.y() / L, d.x() / L);
    const QPointF c = mid + leftN * ((L / 2.0) * (1.0 - bulge * bulge) / (2.0 * bulge));
    const double r = len(p - c);
    const double a0 = qRadiansToDegrees(std::atan2(p.y() - c.y(), p.x() - c.x()));
    addArcSegs(out, c, r, a0, qRadiansToDegrees(theta), layer);
}

// ── 점 병합 (격자 해시) ─────────────────────────────────────────────────────
class VertexPool {
public:
    int idOf(const QPointF &p)
    {
        const qint64 cx = qint64(std::floor(p.x() / kCell));
        const qint64 cy = qint64(std::floor(p.y() / kCell));
        for (qint64 dx = -1; dx <= 1; ++dx) {
            for (qint64 dy = -1; dy <= 1; ++dy) {
                const QList<int> ids = m_cells.value(key(cx + dx, cy + dy));
                for (int i = 0; i < ids.size(); ++i)
                    if (len(m_pts[ids.at(i)] - p) <= kTol)
                        return ids.at(i);
            }
        }
        const int id = int(m_pts.size());
        m_pts.push_back(p);
        m_cells[key(cx, cy)].append(id);
        return id;
    }
    const std::vector<QPointF> &points() const { return m_pts; }

private:
    static qint64 key(qint64 x, qint64 y) { return (x << 32) ^ (y & 0xffffffffLL); }
    static constexpr double kCell = 1.0;
    std::vector<QPointF> m_pts;
    QHash<qint64, QList<int>> m_cells;
};

// ── 합집합-찾기 (연결 성분) ─────────────────────────────────────────────────
int ufFind(std::vector<int> &p, int x)
{
    while (p[x] != x) {
        p[x] = p[p[x]];
        x = p[x];
    }
    return x;
}

void ufUnion(std::vector<int> &p, int a, int b)
{
    a = ufFind(p, a);
    b = ufFind(p, b);
    if (a != b)
        p[a] = b;
}

// 심볼 → 벽 속 개구부 사각형
bool openingRect(const QString &id, const SymbolInfo &info, OpeningDef &out)
{
    const QMap<QString, QVariant> &pr = info.props;
    if (!pr.contains(QStringLiteral("ox")) || !pr.contains(QStringLiteral("trimYLow")))
        return false;
    out.id = id;
    out.info = info;
    out.window = OpeningHeights::isWindowType(info.type);
    double w = out.window ? pr.value(QStringLiteral("W")).toDouble()
                          : pr.value(QStringLiteral("H")).toDouble();
    if (w <= 0.0)
        w = pr.value(QStringLiteral("H1")).toDouble() + pr.value(QStringLiteral("H2")).toDouble();
    const double yL = pr.value(QStringLiteral("trimYLow")).toDouble();
    const double yH = pr.value(QStringLiteral("trimYHigh")).toDouble();
    if (w <= 0.0 || yH - yL <= 0.0)
        return false;
    const double ox = pr.value(QStringLiteral("ox")).toDouble();
    const double oy = pr.value(QStringLiteral("oy")).toDouble();
    const double ang = qDegreesToRadians(pr.value(QStringLiteral("angleDeg")).toDouble());
    const double c = std::cos(ang);
    const double s = std::sin(ang);
    const double lx[4] = {0.0, w, w, 0.0};
    const double ly[4] = {yL, yL, yH, yH};
    out.rect.clear();
    for (int i = 0; i < 4; ++i)
        out.rect << QPointF(ox + lx[i] * c - ly[i] * s, oy + lx[i] * s + ly[i] * c);
    out.width = w;
    out.depth = yH - yL;
    return true;
}

bool insideExpanded(const QPolygonF &rect, const QPointF &p)
{
    if (rect.containsPoint(p, Qt::OddEvenFill))
        return true;
    for (int i = 0; i < rect.size(); ++i) {
        double d = 0.0;
        projectT(p, rect.at(i), rect.at((i + 1) % rect.size()), &d);
        if (d <= kTol)
            return true;
    }
    return false;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
double BuildingModel3D::signedArea(const QPolygonF &ring)
{
    double a = 0.0;
    for (int i = 0; i < ring.size(); ++i)
        a += cross(ring.at(i), ring.at((i + 1) % ring.size()));
    return a * 0.5;
}

double Model3DSolid::planArea() const
{
    if (rings.isEmpty())
        return 0.0;
    double a = std::fabs(BuildingModel3D::signedArea(rings.first()));
    for (int i = 1; i < rings.size(); ++i)
        a -= std::fabs(BuildingModel3D::signedArea(rings.at(i)));
    return a;
}

double Model3DSolid::volume() const
{
    return planArea() * std::max(0.0, z1 - z0);
}

// ─────────────────────────────────────────────────────────────────────────────
Model3D BuildingModel3D::build(MyGraphicsScene *scene, LayerManager *layerMgr)
{
    Model3D model;
    if (!scene || !layerMgr)
        return model;

    // ── 1) 높이 레이어 ──────────────────────────────────────────────────────
    std::vector<LayerInfo> layers;
    QHash<QString, int> layerIndex;
    const QList<Layer> all = layerMgr->getAllLayers();
    for (int i = 0; i < all.size(); ++i) {
        const Layer &l = all.at(i);
        if (l.height() <= 0.0 || !l.isLayerOn())
            continue;
        LayerInfo li;
        li.name = l.name();
        li.z0 = l.elevation();
        li.z1 = l.elevation() + l.height();
        layerIndex.insert(li.name, int(layers.size()));
        layers.push_back(li);
        model.layers << li.name;
    }

    // ── 2) 선 수집 ──────────────────────────────────────────────────────────
    std::vector<Seg> segs;
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        QGraphicsItem *gi = items.at(i);
        if (dynamic_cast<CadItem *>(gi->parentItem()) != nullptr)
            continue; // 블록 내부
        CadItem *ci = dynamic_cast<CadItem *>(gi);
        if (!ci)
            continue;
        const int layer = layerIndex.value(ci->layerName(), -1);
        if (layer < 0)
            continue;
        if (!gi->data(PD_DATA_GROUP_ID).toString().isEmpty())
            continue; // 문·창 심볼 구성선
        const QString role = gi->data(ML_DATA_ROLE).toString();
        if (role == QLatin1String("center") || role == QLatin1String("space") ||
            role == QLatin1String("mortar"))
            continue;

        if (LineItem *li = dynamic_cast<LineItem *>(gi)) {
            const MyLine ln = li->getLine();
            addSeg(segs, QPointF(ln.start.x, ln.start.y), QPointF(ln.end.x, ln.end.y), layer, true);
        } else if (PolylineItem *pi = dynamic_cast<PolylineItem *>(gi)) {
            const MyPolyline pl = pi->getPolyline();
            const int n = int(pl.vertices.size());
            const int cnt = pl.isClosed ? n : n - 1;
            for (int k = 0; k < cnt; ++k) {
                const MyVector &p = pl.vertices[k];
                const MyVector &q = pl.vertices[(k + 1) % n];
                const double b = (k < int(pl.bulges.size())) ? pl.bulges[k] : 0.0;
                addBulgeSegs(segs, QPointF(p.x, p.y), QPointF(q.x, q.y), b, layer);
            }
        } else if (ArcItem *ai = dynamic_cast<ArcItem *>(gi)) {
            const MyArc arc = ai->getArc();
            addArcSegs(segs, QPointF(arc.center.x, arc.center.y), arc.radius, arc.startAngle,
                       arc.spanAngle, layer);
        }
    }
    model.segmentCount = int(segs.size());

    // ── 3) 문·창 개구부 가상 경계 ──────────────────────────────────────────
    std::vector<OpeningDef> openings;
    const QMap<QString, SymbolInfo> &symbols = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = symbols.constBegin();
         it != symbols.constEnd(); ++it) {
        if (!OpeningHeights::isOpeningType(it.value().type))
            continue;
        OpeningDef od;
        if (!openingRect(it.key(), it.value(), od)) {
            Model3DWarning w;
            w.message = QStringLiteral("%1: 개구부 위치 정보가 없어 건너뜀").arg(it.value().name);
            model.warnings << w;
            continue;
        }
        for (int k = 0; k < 4; ++k)
            addSeg(segs, od.rect.at(k), od.rect.at((k + 1) % 4), -1, false);
        openings.push_back(od);
    }

    if (segs.empty())
        return model;

    // ── 4) 교차·접촉 지점에서 분할 ──────────────────────────────────────────
    for (size_t i = 0; i < segs.size(); ++i) {
        segs[i].cuts.push_back(0.0);
        segs[i].cuts.push_back(1.0);
    }
    std::vector<int> order(segs.size());
    for (size_t i = 0; i < segs.size(); ++i)
        order[i] = int(i);
    SegMinXLess byMinX;
    byMinX.segs = &segs;
    std::sort(order.begin(), order.end(), byMinX);

    for (size_t oi = 0; oi < order.size(); ++oi) {
        Seg &s1 = segs[order[oi]];
        const double l1 = len(s1.b - s1.a);
        for (size_t oj = oi + 1; oj < order.size(); ++oj) {
            Seg &s2 = segs[order[oj]];
            if (s2.minX > s1.maxX + kTol)
                break;
            const double minY1 = std::min(s1.a.y(), s1.b.y()), maxY1 = std::max(s1.a.y(), s1.b.y());
            const double minY2 = std::min(s2.a.y(), s2.b.y()), maxY2 = std::max(s2.a.y(), s2.b.y());
            if (minY2 > maxY1 + kTol || minY1 > maxY2 + kTol)
                continue;
            const double l2 = len(s2.b - s2.a);

            // 끝점이 상대 선분 위(허용오차 안)에 있으면 그 자리에서 분할 (T 접합·겹침)
            const QPointF ends2[2] = {s2.a, s2.b};
            for (int e = 0; e < 2; ++e) {
                double d = 0.0;
                const double t = projectT(ends2[e], s1.a, s1.b, &d);
                if (d <= kTol && t > kTol / l1 && t < 1.0 - kTol / l1)
                    s1.cuts.push_back(t);
            }
            const QPointF ends1[2] = {s1.a, s1.b};
            for (int e = 0; e < 2; ++e) {
                double d = 0.0;
                const double t = projectT(ends1[e], s2.a, s2.b, &d);
                if (d <= kTol && t > kTol / l2 && t < 1.0 - kTol / l2)
                    s2.cuts.push_back(t);
            }

            // 진짜 교차 (X 자)
            const QPointF r = s1.b - s1.a;
            const QPointF q = s2.b - s2.a;
            const double den = cross(r, q);
            if (std::fabs(den) < 1e-9 * l1 * l2)
                continue; // 평행
            const double t = cross(s2.a - s1.a, q) / den;
            const double u = cross(s2.a - s1.a, r) / den;
            if (t > kTol / l1 && t < 1.0 - kTol / l1 && u > kTol / l2 && u < 1.0 - kTol / l2) {
                s1.cuts.push_back(t);
                s2.cuts.push_back(u);
            }
        }
    }

    // ── 5) 원자 간선 (중복 제거) ────────────────────────────────────────────
    VertexPool pool;
    std::vector<Edge> edges;
    QHash<qint64, int> edgeIndex;
    for (size_t i = 0; i < segs.size(); ++i) {
        Seg &s = segs[i];
        std::sort(s.cuts.begin(), s.cuts.end());
        int prev = pool.idOf(s.a + (s.b - s.a) * s.cuts.front());
        for (size_t k = 1; k < s.cuts.size(); ++k) {
            const int cur = pool.idOf(s.a + (s.b - s.a) * s.cuts[k]);
            if (cur == prev)
                continue;
            const int lo = std::min(prev, cur), hi = std::max(prev, cur);
            const qint64 key = (qint64(lo) << 32) | qint64(hi);
            QHash<qint64, int>::iterator it = edgeIndex.find(key);
            if (it == edgeIndex.end()) {
                Edge e;
                e.u = prev;
                e.v = cur;
                e.real = s.real;
                e.layer = s.real ? s.layer : -1;
                edgeIndex.insert(key, int(edges.size()));
                edges.push_back(e);
            } else {
                Edge &e = edges[it.value()];
                if (s.real && !e.real) {
                    e.real = true;
                    e.layer = s.layer;
                }
            }
            prev = cur;
        }
    }
    const std::vector<QPointF> &pts = pool.points();
    const int nV = int(pts.size());

    // ── 5-1) 열린 벽 끝 자동 마감 ───────────────────────────────────────────
    //   손으로 그린(가져온) 도면은 문·창 자리에서 벽 두 선이 마감선 없이 끝난다.
    //   나란한 두 실선의 끝점이 벽 두께 간격(30~600)으로 마주 보면 가상 마감선으로 잇는다.
    {
        std::vector<int> degree(nV, 0);
        std::vector<int> lastEdge(nV, -1);
        for (int e = 0; e < int(edges.size()); ++e) {
            if (!edges[e].real)
                continue;
            ++degree[edges[e].u];
            ++degree[edges[e].v];
            lastEdge[edges[e].u] = e;
            lastEdge[edges[e].v] = e;
        }
        std::vector<int> ends;
        std::vector<QPointF> outDir(nV);
        for (int v = 0; v < nV; ++v) {
            if (degree[v] != 1)
                continue;
            const Edge &e = edges[lastEdge[v]];
            const int other = (e.u == v) ? e.v : e.u;
            const QPointF d = pts[v] - pts[other];
            const double l = len(d);
            if (l < 1e-9)
                continue;
            outDir[v] = d / l;
            ends.push_back(v);
        }
        std::vector<int> partner(nV, -1);
        // 가까운 쌍부터 짝짓기
        struct Cand { double dist; int a; int b; };
        std::vector<Cand> cands;
        for (size_t i = 0; i < ends.size(); ++i) {
            for (size_t j = i + 1; j < ends.size(); ++j) {
                const int a = ends[i], b = ends[j];
                if (dot(outDir[a], outDir[b]) < 0.97)
                    continue; // 같은 쪽을 향해 끝나는 나란한 두 선
                const QPointF ab = pts[b] - pts[a];
                const double across = std::fabs(cross(outDir[a], ab));
                const double along = std::fabs(dot(outDir[a], ab));
                if (across < 30.0 || across > 600.0 || along > std::max(5.0, 0.3 * across))
                    continue;
                Cand c = {len(ab), a, b};
                cands.push_back(c);
            }
        }
        for (size_t i = 0; i < cands.size(); ++i) {   // 거리순 정렬 (삽입 정렬, 후보 수 적음)
            for (size_t j = i; j > 0 && cands[j].dist < cands[j - 1].dist; --j)
                std::swap(cands[j], cands[j - 1]);
        }
        for (size_t i = 0; i < cands.size(); ++i) {
            const int a = cands[i].a, b = cands[i].b;
            if (partner[a] >= 0 || partner[b] >= 0)
                continue;
            partner[a] = b;
            partner[b] = a;
            const int lo = std::min(a, b), hi = std::max(a, b);
            const qint64 key = (qint64(lo) << 32) | qint64(hi);
            if (edgeIndex.contains(key))
                continue;
            Edge e;
            e.u = a;
            e.v = b;
            e.real = true;  // 벽/빈 공간 경계로 취급
            e.layer = edges[lastEdge[a]].layer;
            edgeIndex.insert(key, int(edges.size()));
            edges.push_back(e);
            ++model.autoCapCount;
        }
    }
    const int nH = int(edges.size()) * 2;

    // ── 6) 반-간선 구조: 정점별 각도순 정렬 → next ──────────────────────────
    std::vector<int> origin(nH);
    std::vector<double> angle(nH);
    std::vector<std::vector<int>> outgoing(nV);
    for (int e = 0; e < int(edges.size()); ++e) {
        const int h0 = 2 * e, h1 = 2 * e + 1;
        origin[h0] = edges[e].u;
        origin[h1] = edges[e].v;
        const QPointF d = pts[edges[e].v] - pts[edges[e].u];
        angle[h0] = std::atan2(d.y(), d.x());
        angle[h1] = std::atan2(-d.y(), -d.x());
        outgoing[edges[e].u].push_back(h0);
        outgoing[edges[e].v].push_back(h1);
    }
    std::vector<int> posAt(nH);
    AngleLess byAngle;
    byAngle.angles = &angle;
    for (int v = 0; v < nV; ++v) {
        std::sort(outgoing[v].begin(), outgoing[v].end(), byAngle);
        for (size_t k = 0; k < outgoing[v].size(); ++k)
            posAt[outgoing[v][k]] = int(k);
    }
    std::vector<int> next(nH);
    for (int h = 0; h < nH; ++h) {
        const int twin = h ^ 1;
        const int v = origin[twin];
        const std::vector<int> &out = outgoing[v];
        const int n = int(out.size());
        next[h] = out[(posAt[twin] - 1 + n) % n]; // 면을 왼쪽에 두고 도는 다음 간선
    }

    // ── 7) 순환(cycle) 추출 ─────────────────────────────────────────────────
    std::vector<int> cycleOf(nH, -1);
    std::vector<std::vector<int>> cycles;
    std::vector<double> cycleArea;
    for (int h = 0; h < nH; ++h) {
        if (cycleOf[h] >= 0)
            continue;
        const int cid = int(cycles.size());
        cycles.push_back(std::vector<int>());
        int cur = h;
        int guard = 0;
        while (cycleOf[cur] < 0 && guard++ <= nH) {
            cycleOf[cur] = cid;
            cycles[cid].push_back(cur);
            cur = next[cur];
        }
        QPolygonF poly;
        for (size_t k = 0; k < cycles[cid].size(); ++k)
            poly << pts[origin[cycles[cid][k]]];
        cycleArea.push_back(signedArea(poly));
    }

    // 연결 성분
    std::vector<int> uf(nV);
    for (int v = 0; v < nV; ++v)
        uf[v] = v;
    for (size_t e = 0; e < edges.size(); ++e)
        ufUnion(uf, edges[e].u, edges[e].v);

    // ── 8) 면 = 양(+) 순환 + 그 안의 구멍 순환. 면 0 = 도면 바깥 ────────────
    std::vector<int> faceOfCycle(cycles.size(), 0);
    std::vector<int> faceOuterCycle(1, -1);
    std::vector<QPolygonF> facePoly(1);
    for (size_t c = 0; c < cycles.size(); ++c) {
        if (cycleArea[c] > kAreaEps) {
            faceOfCycle[c] = int(faceOuterCycle.size());
            faceOuterCycle.push_back(int(c));
            QPolygonF poly;
            for (size_t k = 0; k < cycles[c].size(); ++k)
                poly << pts[origin[cycles[c][k]]];
            facePoly.push_back(poly);
        }
    }
    const int nF = int(faceOuterCycle.size());
    std::vector<std::vector<int>> faceHoles(nF);
    for (size_t c = 0; c < cycles.size(); ++c) {
        if (cycleArea[c] > kAreaEps)
            continue;
        const QPointF probe = pts[origin[cycles[c][0]]];
        const int comp = ufFind(uf, origin[cycles[c][0]]);
        int best = 0;
        double bestArea = 0.0;
        for (int f = 1; f < nF; ++f) {
            const int oc = faceOuterCycle[f];
            if (ufFind(uf, origin[cycles[oc][0]]) == comp)
                continue;
            if (!facePoly[f].containsPoint(probe, Qt::OddEvenFill))
                continue;
            if (best == 0 || cycleArea[oc] < bestArea) {
                best = f;
                bestArea = cycleArea[oc];
            }
        }
        faceOfCycle[c] = best;
        if (best > 0)
            faceHoles[best].push_back(int(c));
    }
    model.faceCount = nF - 1;

    // ── 9) 개구부 면 표시 ───────────────────────────────────────────────────
    std::vector<int> faceOpening(nF, -1);
    for (int f = 1; f < nF; ++f) {
        for (size_t o = 0; o < openings.size(); ++o) {
            bool allIn = true;
            for (int k = 0; k < facePoly[f].size() && allIn; ++k)
                allIn = insideExpanded(openings[o].rect, facePoly[f].at(k));
            if (allIn) {
                faceOpening[f] = int(o);
                break;
            }
        }
    }

    // ── 10) 빈 공간 ↔ 벽 번갈아 판정 (실제 선만 넘는다) ────────────────────
    struct Adj { int face; int edge; };
    std::vector<std::vector<Adj>> adj(nF);
    for (int e = 0; e < int(edges.size()); ++e) {
        const int fa = faceOfCycle[cycleOf[2 * e]];
        const int fb = faceOfCycle[cycleOf[2 * e + 1]];
        if (fa == fb) {
            if (edges[e].real)
                ++model.openEndCount; // 양쪽이 같은 면 = 끝이 열린 선
            continue;
        }
        if (!edges[e].real)
            continue;
        Adj a1 = {fb, e};
        Adj a2 = {fa, e};
        adj[fa].push_back(a1);
        adj[fb].push_back(a2);
    }
    std::vector<int> state(nF, -1); // 0 = 빈 공간, 1 = 벽
    state[0] = 0;
    std::vector<int> queue(1, 0);
    int conflicts = 0;
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        const int f = queue[qi];
        for (size_t k = 0; k < adj[f].size(); ++k) {
            const int g = adj[f][k].face;
            if (faceOpening[g] >= 0)
                continue;
            const int want = 1 - state[f];
            if (state[g] < 0) {
                state[g] = want;
                queue.push_back(g);
            } else if (state[g] != want) {
                ++conflicts;
                if (conflicts <= 20) {
                    const Edge &e = edges[adj[f][k].edge];
                    Model3DWarning w;
                    w.message = QStringLiteral("벽/빈 공간 판정이 엇갈림 (벽 끝이 닫히지 않았거나 선이 겹침)");
                    w.at = (pts[e.u] + pts[e.v]) * 0.5;
                    model.warnings << w;
                }
            }
        }
    }
    if (model.openEndCount > 0) {
        Model3DWarning w;
        w.message = QStringLiteral("끝이 열린 선 %1개 (벽 끝 마감선이 없거나 선이 떨어져 있음)")
                        .arg(model.openEndCount);
        model.warnings << w;
    }

    // 면의 경계 링과 레이어별 경계 길이
    std::vector<QList<QPolygonF>> faceRings(nF);
    std::vector<QHash<int, double>> faceLayerLen(nF);
    for (int f = 1; f < nF; ++f) {
        std::vector<int> cyc(1, faceOuterCycle[f]);
        cyc.insert(cyc.end(), faceHoles[f].begin(), faceHoles[f].end());
        for (size_t ci = 0; ci < cyc.size(); ++ci) {
            const std::vector<int> &hs = cycles[cyc[ci]];
            QPolygonF ring;
            for (size_t k = 0; k < hs.size(); ++k) {
                const int h = hs[k];
                ring << pts[origin[h]];
                const Edge &e = edges[h / 2];
                if (e.real && e.layer >= 0)
                    faceLayerLen[f][e.layer] += len(pts[e.v] - pts[e.u]);
            }
            faceRings[f] << ring;
        }
    }

    // ── 11) 벽 덩어리 ───────────────────────────────────────────────────────
    std::vector<int> faceLayer(nF, -1);
    for (int f = 1; f < nF; ++f) {
        if (state[f] != 1 || faceOpening[f] >= 0)
            continue;
        int bestLayer = -1;
        double bestLen = -1.0;
        for (QHash<int, double>::const_iterator it = faceLayerLen[f].constBegin();
             it != faceLayerLen[f].constEnd(); ++it) {
            if (it.value() > bestLen) {
                bestLen = it.value();
                bestLayer = it.key();
            }
        }
        if (bestLayer < 0)
            continue;
        faceLayer[f] = bestLayer;
        Model3DSolid s;
        s.kind = Model3DSolid::Wall;
        s.layer = layers[bestLayer].name;
        s.rings = faceRings[f];
        s.z0 = layers[bestLayer].z0;
        s.z1 = layers[bestLayer].z1;
        model.solids << s;
    }

    // ── 12) 개구부: 붙어 있는 벽에서 높이를 받고 위·아래 벽을 남긴다 ─────────
    std::vector<QHash<int, double>> openHostLen(openings.size());
    for (int e = 0; e < int(edges.size()); ++e) {
        const int fa = faceOfCycle[cycleOf[2 * e]];
        const int fb = faceOfCycle[cycleOf[2 * e + 1]];
        const double l = len(pts[edges[e].v] - pts[edges[e].u]);
        if (fa > 0 && faceOpening[fa] >= 0 && fb > 0 && faceLayer[fb] >= 0)
            openHostLen[faceOpening[fa]][faceLayer[fb]] += l;
        if (fb > 0 && faceOpening[fb] >= 0 && fa > 0 && faceLayer[fa] >= 0)
            openHostLen[faceOpening[fb]][faceLayer[fa]] += l;
    }
    LayerManager *lm = layerMgr;
    for (size_t o = 0; o < openings.size(); ++o) {
        const OpeningDef &od = openings[o];
        Model3DOpening mo;
        mo.id = od.id;
        mo.type = od.info.type;
        mo.name = od.info.name;
        mo.isWindow = od.window;
        mo.footprint = od.rect;
        mo.width = od.width;
        mo.depth = od.depth;
        mo.sill = OpeningHeights::sillOf(od.info, lm);
        mo.height = OpeningHeights::heightOf(od.info, lm);

        int host = -1;
        double hostLen = 0.0;
        for (QHash<int, double>::const_iterator it = openHostLen[o].constBegin();
             it != openHostLen[o].constEnd(); ++it) {
            if (it.value() > hostLen) {
                hostLen = it.value();
                host = it.key();
            }
        }
        if (host >= 0) {
            mo.hosted = true;
            mo.hostLayer = layers[host].name;
            mo.hostZ0 = layers[host].z0;
            mo.hostZ1 = layers[host].z1;
            const double bottomTop = std::min(mo.hostZ1, mo.hostZ0 + mo.sill);
            const double lintelBottom = mo.hostZ0 + mo.sill + mo.height;
            for (int f = 1; f < nF; ++f) {
                if (faceOpening[f] != int(o))
                    continue;
                if (bottomTop > mo.hostZ0 + 1e-6) {
                    Model3DSolid s;
                    s.kind = Model3DSolid::OpeningFill;
                    s.layer = mo.hostLayer;
                    s.rings = faceRings[f];
                    s.z0 = mo.hostZ0;
                    s.z1 = bottomTop;
                    s.openingId = od.id;
                    model.solids << s;
                }
                if (lintelBottom < mo.hostZ1 - 1e-6) {
                    Model3DSolid s;
                    s.kind = Model3DSolid::OpeningFill;
                    s.layer = mo.hostLayer;
                    s.rings = faceRings[f];
                    s.z0 = lintelBottom;
                    s.z1 = mo.hostZ1;
                    s.openingId = od.id;
                    model.solids << s;
                }
            }
            if (lintelBottom > mo.hostZ1 + 1e-6) {
                Model3DWarning w;
                w.message = QStringLiteral("%1: 개구부 윗면(%2)이 벽 높이(%3)보다 높음")
                                .arg(mo.name)
                                .arg(lintelBottom, 0, 'f', 0)
                                .arg(mo.hostZ1, 0, 'f', 0);
                w.at = od.rect.at(0);
                model.warnings << w;
            }
        } else {
            Model3DWarning w;
            w.message = QStringLiteral("%1: 높이가 있는 벽에 붙어 있지 않음").arg(mo.name);
            w.at = od.rect.at(0);
            model.warnings << w;
        }
        model.openings << mo;
    }
    return model;
}

// ─────────────────────────────────────────────────────────────────────────────
QString BuildingModel3D::summaryText(const Model3D &m)
{
    QString out;
    if (m.layers.isEmpty())
        return QStringLiteral("높이가 설정된(켜진) 레이어가 없습니다.\n"
                              "3D 건축용 > 레이어 높이 설정에서 벽 레이어의 높이를 먼저 입력하세요.");

    out += QStringLiteral("높이 레이어: %1\n").arg(m.layers.join(QStringLiteral(", ")));
    out += QStringLiteral("입력 선분 %1개, 닫힌 영역 %2개, 자동 마감한 벽 끝 %3곳\n\n")
               .arg(m.segmentCount)
               .arg(m.faceCount)
               .arg(m.autoCapCount);

    // 레이어별 벽
    QMap<QString, int> wallCount;
    QMap<QString, double> wallArea, wallVol, fillVol;
    for (int i = 0; i < m.solids.size(); ++i) {
        const Model3DSolid &s = m.solids.at(i);
        if (s.kind == Model3DSolid::Wall) {
            wallCount[s.layer] += 1;
            wallArea[s.layer] += s.planArea();
            wallVol[s.layer] += s.volume();
        } else {
            fillVol[s.layer] += s.volume();
        }
    }
    out += QStringLiteral("[벽]\n");
    if (wallCount.isEmpty())
        out += QStringLiteral("  벽으로 판정된 영역이 없습니다.\n");
    for (QMap<QString, int>::const_iterator it = wallCount.constBegin(); it != wallCount.constEnd(); ++it) {
        const QString l = it.key();
        out += QStringLiteral("  %1: %2개, 바닥면적 %3 m², 체적 %4 m³ (개구부 위·아래 벽 %5 m³ 별도)\n")
                   .arg(l)
                   .arg(it.value())
                   .arg(wallArea.value(l) / 1e6, 0, 'f', 2)
                   .arg(wallVol.value(l) / 1e9, 0, 'f', 3)
                   .arg(fillVol.value(l) / 1e9, 0, 'f', 3);
    }

    int doors = 0, windows = 0, unhosted = 0;
    for (int i = 0; i < m.openings.size(); ++i) {
        if (m.openings.at(i).isWindow)
            ++windows;
        else
            ++doors;
        if (!m.openings.at(i).hosted)
            ++unhosted;
    }
    out += QStringLiteral("\n[개구부] 문 %1개, 창 %2개").arg(doors).arg(windows);
    if (unhosted > 0)
        out += QStringLiteral(" (벽에 붙지 않음 %1개)").arg(unhosted);
    out += QStringLiteral("\n");
    for (int i = 0; i < m.openings.size() && i < 30; ++i) {
        const Model3DOpening &o = m.openings.at(i);
        out += QStringLiteral("  %1: 폭 %2, 하단 %3, 높이 %4%5\n")
                   .arg(o.name)
                   .arg(o.width, 0, 'f', 0)
                   .arg(o.sill, 0, 'f', 0)
                   .arg(o.height, 0, 'f', 0)
                   .arg(o.hosted ? QStringLiteral(" (벽: %1)").arg(o.hostLayer) : QString());
    }

    out += QStringLiteral("\n[경고] %1건\n").arg(m.warnings.size());
    for (int i = 0; i < m.warnings.size() && i < 20; ++i) {
        const Model3DWarning &w = m.warnings.at(i);
        out += QStringLiteral("  - %1").arg(w.message);
        if (!w.at.isNull())
            out += QStringLiteral(" @ (%1, %2)").arg(w.at.x(), 0, 'f', 0).arg(w.at.y(), 0, 'f', 0);
        out += QStringLiteral("\n");
    }
    return out;
}
