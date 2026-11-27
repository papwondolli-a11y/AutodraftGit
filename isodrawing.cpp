#include "isodrawing.h"

#include "model3dexport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

// Blender 쪽 스크립트: 가져오기 스크립트를 돌린 뒤(불리언 수정자 구성) 나사커터만 끄고, 평가된 메시를 mm 로 적는다
//   %1 = 가져오기 스크립트, %2 = 결과 파일
const char *kIsoScript = R"PY(# AutoDraft 등각도용 메시 내보내기 (자동 생성)
import bpy, runpy, re
SCRIPT = r"%1"
OUT = r"%2"
try:
    runpy.run_path(SCRIPT, run_name="__main__")
except SystemExit:
    pass
TOOL = re.compile(r"_(키홈커터|구멍커터|나사커터|합침|파내기)\d*$")
for o in bpy.data.objects:
    if o.type == 'MESH':
        for md in o.modifiers:
            if md.name.startswith("나사커터"):
                md.show_viewport = False
bpy.context.view_layer.update()
dg = bpy.context.evaluated_depsgraph_get()
with open(OUT, "w", encoding="utf-8") as f:
    for o in bpy.data.objects:
        if o.type != 'MESH' or TOOL.search(o.name) or o.name == "바닥":
            continue
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        M = o.matrix_world
        f.write("o %s\n" % o.name)
        for v in me.vertices:
            w = M @ v.co
            f.write("v %.6f %.6f %.6f\n" % (w.x * 1000.0, w.y * 1000.0, w.z * 1000.0))
        me.calc_loop_triangles()
        for t in me.loop_triangles:
            f.write("f %d %d %d\n" % (t.vertices[0], t.vertices[1], t.vertices[2]))
        ev.to_mesh_clear()
print("ISO_DONE")
)PY";

// 투상된 삼각형 (가림 판정용)
struct Tri2 {
    QPointF a, b, c;
    double za = 0.0, zb = 0.0, zc = 0.0;   // 깊이 (클수록 보는 사람 쪽)
    QRectF box;
};

// 점 q 가 삼각형 안(가장자리에서 조금 안쪽)인가 → 그 자리 깊이
bool triDepthAt(const Tri2 &t, const QPointF &q, double &z)
{
    const double x0 = t.a.x(), y0 = t.a.y();
    const double d1x = t.b.x() - x0, d1y = t.b.y() - y0, d2x = t.c.x() - x0, d2y = t.c.y() - y0;
    const double den = d1x * d2y - d1y * d2x;
    if (std::fabs(den) < 1e-18)
        return false;
    const double px = q.x() - x0, py = q.y() - y0;
    const double u = (px * d2y - py * d2x) / den;
    const double v = (d1x * py - d1y * px) / den;
    const double w = 1.0 - u - v;
    const double e = 1e-6;
    if (u <= e || v <= e || w <= e)
        return false;
    z = t.za * w + t.zb * u + t.zc * v;
    return true;
}

// 합칠 선 하나: 방향 u, 직선 거리 c, 직선 위 구간 t0~t1
struct Item {
    double t0, t1, c;
    QPointF u;
};

bool itemLess(const Item &p, const Item &q)
{
    return p.t0 < q.t0;
}

// 같은 직선 위에서 이어지거나 겹치는 선을 하나로 (방향·직선 거리로 묶어 정렬 후 합침)
void mergeLines(QList<QLineF> &lines, double tol)
{
    QMap<QPair<qint64, qint64>, QList<Item>> buckets;
    for (int i = 0; i < lines.size(); ++i) {
        const QLineF &ln = lines.at(i);
        double dx = ln.dx(), dy = ln.dy();
        const double L = std::hypot(dx, dy);
        if (L < tol)
            continue;
        dx /= L;
        dy /= L;
        if (dx < -1e-12 || (std::fabs(dx) <= 1e-12 && dy < 0.0)) {
            dx = -dx;
            dy = -dy;
        }
        const QPointF u(dx, dy);
        const double c = -dy * ln.x1() + dx * ln.y1();
        const double ta = dx * ln.x1() + dy * ln.y1(), tb = dx * ln.x2() + dy * ln.y2();
        const double ang = std::atan2(dy, dx);
        const QPair<qint64, qint64> key(qint64(std::llround(ang / 1e-4)), qint64(std::llround(c / (tol * 4.0))));
        Item it;
        it.t0 = std::min(ta, tb);
        it.t1 = std::max(ta, tb);
        it.c = c;
        it.u = u;
        buckets[key] << it;
    }
    QList<QLineF> out;
    for (QMap<QPair<qint64, qint64>, QList<Item>>::iterator b = buckets.begin(); b != buckets.end(); ++b) {
        QList<Item> &items = b.value();
        std::sort(items.begin(), items.end(), itemLess);
        Item cur = items.first();
        for (int i = 1; i <= items.size(); ++i) {
            if (i < items.size() && items.at(i).t0 <= cur.t1 + tol) {
                cur.t1 = std::max(cur.t1, items.at(i).t1);
                continue;
            }
            const QPointF n(-cur.u.y(), cur.u.x());
            const QPointF foot = n * cur.c;
            out << QLineF(foot + cur.u * cur.t0, foot + cur.u * cur.t1);
            if (i < items.size())
                cur = items.at(i);
        }
    }
    lines = out;
}

// ── 원·호 모서리 찾기 ──
//   메시의 원통 둘레는 짧은 직선(현) 여러 개 → 이어진 모서리 줄에서 꼭짓점들이 한 평면·한 원 위에 있는 구간을 3D 원으로
const double kTwoPi = 6.283185307179586;

struct Circ3 {
    QVector3D c;                      // 중심
    double R = 0.0;                   // 반지름
    QVector3D n, e1, e2;              // 법선, 평면 축 (e2 = n × e1)
    QList<QPair<double, double>> vis; // 보이는 각 구간 (라디안, 시작 ∈ [0, 2π), 끝 = 시작 + 길이)
    QList<QPair<double, double>> hid; // 숨은 각 구간
};

double wrapPi(double a)
{
    while (a > M_PI)
        a -= kTwoPi;
    while (a <= -M_PI)
        a += kTwoPi;
    return a;
}

double circTol(double R)
{
    return std::max(1e-3, 2e-3 * R);
}

// 점들이 한 원 위인가 (꼭짓점 4개 이상, 변마다 중심각 25° 이하). sweep = 변 중심각 합 (라디안)
//   길이 조건(합 20° 이상)은 부르는 쪽에서 — 앞에서부터 늘려 가며 맞출 때 짧은 처음 구간에서 멈추지 않게
bool fitCircle(const QVector<QVector3D> &pts, bool closed, Circ3 &out, double *sweep = nullptr)
{
    const int m = pts.size();
    if (m < 4)
        return false;
    // 평면 법선 (Newell) — 열린 줄은 끝을 이은 다각형으로
    double nx = 0.0, ny = 0.0, nz = 0.0, gx = 0.0, gy = 0.0, gz = 0.0;
    for (int i = 0; i < m; ++i) {
        const QVector3D &a = pts.at(i), &b = pts.at((i + 1) % m);
        nx += (double(a.y()) - b.y()) * (double(a.z()) + b.z());
        ny += (double(a.z()) - b.z()) * (double(a.x()) + b.x());
        nz += (double(a.x()) - b.x()) * (double(a.y()) + b.y());
        gx += a.x();
        gy += a.y();
        gz += a.z();
    }
    const double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (nl < 1e-9)
        return false;                                    // 한 직선 위
    const QVector3D n(float(nx / nl), float(ny / nl), float(nz / nl));
    const QVector3D g(float(gx / m), float(gy / m), float(gz / m));
    QVector3D e1 = QVector3D::crossProduct(n, std::fabs(n.x()) < 0.9f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0)).normalized();
    const QVector3D e2 = QVector3D::crossProduct(n, e1).normalized();
    // 평면 좌표에서 원 맞추기 (x² + y² + Dx + Ey + F = 0 최소제곱)
    double s[3][4] = {{0}};
    QVector<double> xs(m), ys(m);
    for (int i = 0; i < m; ++i) {
        const QVector3D q = pts.at(i) - g;
        const double x = QVector3D::dotProduct(q, e1), y = QVector3D::dotProduct(q, e2);
        xs[i] = x;
        ys[i] = y;
        const double row[3] = {x, y, 1.0}, rhs = -(x * x + y * y);
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b)
                s[a][b] += row[a] * row[b];
            s[a][3] += row[a] * rhs;
        }
    }
    for (int col = 0; col < 3; ++col) {                  // 가우스 소거
        int piv = col;
        for (int r = col + 1; r < 3; ++r)
            if (std::fabs(s[r][col]) > std::fabs(s[piv][col]))
                piv = r;
        if (std::fabs(s[piv][col]) < 1e-18)
            return false;
        for (int k = 0; k < 4; ++k)
            std::swap(s[col][k], s[piv][k]);
        for (int r = 0; r < 3; ++r) {
            if (r == col)
                continue;
            const double f = s[r][col] / s[col][col];
            for (int k = col; k < 4; ++k)
                s[r][k] -= f * s[col][k];
        }
    }
    const double D = s[0][3] / s[0][0], E = s[1][3] / s[1][1], F = s[2][3] / s[2][2];
    const double cx = -D / 2.0, cy = -E / 2.0, rr = cx * cx + cy * cy - F;
    if (rr <= 0.0)
        return false;
    const double R = std::sqrt(rr);
    double span = 0.0;
    for (int i = 0; i < m; ++i)
        span = std::max(span, double((pts.at(i) - pts.at(0)).length()));
    if (R > span * 50.0)
        return false;                                    // 거의 직선
    const double tol = circTol(R);
    double total = 0.0;
    for (int i = 0; i < m; ++i) {
        if (std::fabs(QVector3D::dotProduct(pts.at(i) - g, n)) > tol)
            return false;                                // 평면 밖
        if (std::fabs(std::hypot(xs.at(i) - cx, ys.at(i) - cy) - R) > tol)
            return false;                                // 원 밖
        if (i + 1 < m || closed) {
            const int j = (i + 1) % m;
            const double da = std::fabs(wrapPi(std::atan2(ys.at(j) - cy, xs.at(j) - cx) - std::atan2(ys.at(i) - cy, xs.at(i) - cx)));
            if (da > qDegreesToRadians(25.0))
                return false;                            // 성긴 다각형 (육각 등)
            total += da;
        }
    }
    if (sweep)
        *sweep = total;
    out.c = g + e1 * float(cx) + e2 * float(cy);
    out.R = R;
    out.n = n;
    out.e1 = e1;
    out.e2 = e2;
    return true;
}

// 이미 찾은 원과 같은 원인가 → 그 번호, 없으면 추가
int addCircle(QList<Circ3> &circles, const Circ3 &c)
{
    const double tol = circTol(c.R);
    for (int i = 0; i < circles.size(); ++i) {
        const Circ3 &o = circles.at(i);
        if (std::fabs(o.R - c.R) <= tol && (o.c - c.c).length() <= tol
            && std::fabs(QVector3D::dotProduct(o.n, c.n)) > 0.9999f)
            return i;
    }
    circles << c;
    return circles.size() - 1;
}

double circAngle(const Circ3 &c, const QVector3D &p)
{
    const QVector3D q = p - c.c;
    return std::atan2(QVector3D::dotProduct(q, c.e2), QVector3D::dotProduct(q, c.e1));
}

// 꼭짓점이 원 위인가
bool onCirc(const Circ3 &c, const QVector3D &p)
{
    const QVector3D q = p - c.c;
    const double h = QVector3D::dotProduct(q, c.n);
    const double rad = std::sqrt(std::max(0.0, double(q.lengthSquared()) - h * h));
    return std::fabs(h) <= circTol(c.R) && std::fabs(rad - c.R) <= circTol(c.R);
}

// 한 메시의 모서리(feats) 중 원 위의 것 → edgeCircle[k] = 원 번호 (아니면 -1)
void detectCircles(const QVector<QVector3D> &v, const QVector<QPair<int, int>> &feats, QList<Circ3> &circles,
                   QVector<int> &edgeCircle)
{
    edgeCircle = QVector<int>(feats.size(), -1);
    QHash<int, QVector<int>> at;                         // 꼭짓점 → 모서리
    for (int k = 0; k < feats.size(); ++k) {
        at[feats.at(k).first] << k;
        at[feats.at(k).second] << k;
    }
    QVector<bool> used(feats.size(), false);
    // 모서리 줄 (가지가 갈리는 꼭짓점에서 끊음): 끝이 있는 줄 먼저, 다음 닫힌 고리
    QList<QVector<int>> chainEdges;
    QList<QVector<int>> chainVerts;
    QList<bool> chainClosed;
    for (int pass = 0; pass < 2; ++pass) {
        for (int k0 = 0; k0 < feats.size(); ++k0) {
            if (used.at(k0))
                continue;
            int startV = -1;
            if (pass == 0) {
                if (at.value(feats.at(k0).first).size() != 2)
                    startV = feats.at(k0).first;
                else if (at.value(feats.at(k0).second).size() != 2)
                    startV = feats.at(k0).second;
                else
                    continue;
            } else {
                startV = feats.at(k0).first;
            }
            QVector<int> es, vs;
            vs << startV;
            int curV = startV, curE = k0;
            bool closed = false;
            while (true) {
                used[curE] = true;
                es << curE;
                const int nextV = feats.at(curE).first == curV ? feats.at(curE).second : feats.at(curE).first;
                vs << nextV;
                if (nextV == startV) {
                    closed = true;
                    break;
                }
                const QVector<int> &nb = at.value(nextV);
                if (nb.size() != 2)
                    break;
                const int nextE = nb.at(0) == curE ? nb.at(1) : nb.at(0);
                if (used.at(nextE))
                    break;
                curV = nextV;
                curE = nextE;
            }
            chainEdges << es;
            chainVerts << vs;
            chainClosed << closed;
        }
    }
    // 줄마다: 통째로 원이면 그대로, 아니면 앞에서부터 원이 되는 가장 긴 구간씩
    for (int ci = 0; ci < chainEdges.size(); ++ci) {
        const QVector<int> &es = chainEdges.at(ci);
        const QVector<int> &vs = chainVerts.at(ci);
        if (chainClosed.at(ci)) {
            QVector<QVector3D> pts;
            for (int i = 0; i + 1 < vs.size(); ++i)
                pts << v.at(vs.at(i));
            Circ3 c;
            if (fitCircle(pts, true, c)) {
                const int id = addCircle(circles, c);
                for (int i = 0; i < es.size(); ++i)
                    edgeCircle[es.at(i)] = id;
                continue;
            }
        }
        int i = 0;
        while (i < es.size()) {
            int best = -1;
            double bestSweep = 0.0;
            Circ3 bestC;
            for (int j = i + 3; j <= es.size(); ++j) {   // 모서리 i..j-1 (꼭짓점 i..j)
                QVector<QVector3D> pts;
                for (int t = i; t <= j; ++t)
                    pts << v.at(vs.at(t));
                Circ3 c;
                double sw = 0.0;
                if (!fitCircle(pts, false, c, &sw))
                    break;
                best = j;
                bestC = c;
                bestSweep = sw;
            }
            if (best < 0 || bestSweep < qDegreesToRadians(20.0)) {   // 원이 아니거나 너무 짧은 구간
                ++i;
                continue;
            }
            const int id = addCircle(circles, bestC);
            for (int t = i; t < best; ++t)
                edgeCircle[es.at(t)] = id;
            i = best;
        }
    }
    // 남은 짧은 조각: 양 끝이 찾은 원 위 + 짧은 현이면 그 원
    for (int k = 0; k < feats.size(); ++k) {
        if (edgeCircle.at(k) >= 0)
            continue;
        const QVector3D A = v.at(feats.at(k).first), B = v.at(feats.at(k).second);
        for (int c = 0; c < circles.size(); ++c) {
            const Circ3 &cc = circles.at(c);
            if (onCirc(cc, A) && onCirc(cc, B)
                && std::fabs(wrapPi(circAngle(cc, B) - circAngle(cc, A))) <= qDegreesToRadians(25.0)) {
                edgeCircle[k] = c;
                break;
            }
        }
    }
}

// 각 구간 합치기 → 전체 원이면 full
QList<QPair<double, double>> mergeArcs(QList<QPair<double, double>> in, bool &full)
{
    full = false;
    QList<QPair<double, double>> out;
    if (in.isEmpty())
        return out;
    std::sort(in.begin(), in.end());
    const double gap = 1e-4;
    QPair<double, double> cur = in.first();
    for (int i = 1; i < in.size(); ++i) {
        if (in.at(i).first <= cur.second + gap) {
            cur.second = std::max(cur.second, in.at(i).second);
        } else {
            out << cur;
            cur = in.at(i);
        }
    }
    out << cur;
    // 2π 를 넘어 첫 구간과 이어지는 것
    if (out.size() > 1 && out.last().second + gap >= out.first().first + kTwoPi) {
        out.first().first = out.last().first;
        out.first().second = std::max(out.first().second + kTwoPi, out.last().second);
        out.removeLast();
    }
    for (int i = 0; i < out.size(); ++i)
        if (out.at(i).second - out.at(i).first >= kTwoPi - 1e-3) {
            full = true;
            return QList<QPair<double, double>>();
        }
    return out;
}

// 3D 원 → 화면 타원 (r·u = 화면 축, d = 시선, center = 그림 가운데)
void emitEllipses(const Circ3 &c, const QList<QPair<double, double>> &arcs, bool full, const QVector3D &r,
                  const QVector3D &u, const QVector3D &d, double scale, const QPointF &center, QList<ProjEllipse> &out)
{
    // 장축 = 원 평면 안에서 시선과 수직인 방향 (n × d). 정면으로 보면 아무 방향(원)
    QVector3D a1 = QVector3D::crossProduct(c.n, d);
    if (a1.length() < 1e-6f)
        a1 = c.e1;
    a1.normalize();
    const QVector3D a2 = QVector3D::crossProduct(c.n, a1).normalized();
    const double phi = std::atan2(QVector3D::dotProduct(a1, c.e2), QVector3D::dotProduct(a1, c.e1));
    const QPointF A(QVector3D::dotProduct(r, a1) * c.R * scale, QVector3D::dotProduct(u, a1) * c.R * scale);
    QPointF B(QVector3D::dotProduct(r, a2) * c.R * scale, QVector3D::dotProduct(u, a2) * c.R * scale);
    const double la = std::hypot(A.x(), A.y()), lb = std::hypot(B.x(), B.y());
    if (la < 1e-9)
        return;
    const bool flip = A.x() * B.y() - A.y() * B.x() < 0.0;    // 단축이 장축의 시계 방향 → 매개각 뒤집기
    ProjEllipse e;
    e.c = QPointF(QVector3D::dotProduct(r, c.c) * scale, QVector3D::dotProduct(u, c.c) * scale) - center;
    e.major = A;
    e.ratio = std::max(1e-6, std::min(1.0, lb / la));
    if (full) {
        out << e;
        return;
    }
    for (int i = 0; i < arcs.size(); ++i) {
        double s0 = arcs.at(i).first - phi, s1 = arcs.at(i).second - phi;
        if (flip) {
            const double t = -s1;
            s1 = -s0;
            s0 = t;
        }
        const double len = s1 - s0;
        if (len < 1e-4)
            continue;
        s0 = std::fmod(s0, kTwoPi);
        if (s0 < 0.0)
            s0 += kTwoPi;
        ProjEllipse p = e;
        p.full = false;
        p.t0 = s0;
        p.t1 = s0 + len;
        out << p;
    }
}

// 화면에서 같은 타원인가 (중심·크기·비율, 장축은 부호 무관) — 전체 타원끼리, 또는 같은 호끼리
bool sameEllipse(const ProjEllipse &a, const ProjEllipse &b)
{
    const double la = std::hypot(a.major.x(), a.major.y()), lb = std::hypot(b.major.x(), b.major.y());
    const double tol = std::max(1e-4, la * 1e-4);
    if (std::fabs(la - lb) > tol || std::hypot(a.c.x() - b.c.x(), a.c.y() - b.c.y()) > tol
        || std::fabs(a.ratio - b.ratio) > 1e-4)
        return false;
    if (a.full && b.full) {
        if (a.ratio > 0.9999)
            return true;                                 // 원: 장축 방향 무관
        const double cr = std::fabs(a.major.x() * b.major.y() - a.major.y() * b.major.x());
        return cr <= tol * la;
    }
    return !a.full && !b.full && std::hypot(a.major.x() - b.major.x(), a.major.y() - b.major.y()) <= tol
           && std::fabs(a.t0 - b.t0) < 1e-4 && std::fabs(a.t1 - b.t1) < 1e-4;
}

// 겹친 타원 지우기 (예: 위에서 본 원기둥의 위·아래 둘레). 실선과 겹친 숨은선도 지움
void dedupEllipses(QList<ProjEllipse> &vis, QList<ProjEllipse> &hid)
{
    QList<ProjEllipse> v;
    for (int i = 0; i < vis.size(); ++i) {
        bool dup = false;
        for (int k = 0; k < v.size() && !dup; ++k)
            dup = sameEllipse(v.at(k), vis.at(i));
        if (!dup)
            v << vis.at(i);
    }
    QList<ProjEllipse> h;
    for (int i = 0; i < hid.size(); ++i) {
        bool dup = false;
        for (int k = 0; k < v.size() && !dup; ++k)
            dup = sameEllipse(v.at(k), hid.at(i));
        for (int k = 0; k < h.size() && !dup; ++k)
            dup = sameEllipse(h.at(k), hid.at(i));
        if (!dup)
            h << hid.at(i);
    }
    vis = v;
    hid = h;
}

} // namespace

void IsoDrawing::isoAxes(bool standUp, QVector3D &view, QVector3D &up, double &scale)
{
    // 3D 창 [등각] 버튼과 같은 요·피치를 창과 같은 식으로 바꿈 → 세우기 설정까지 창과 같은 모습
    //   세우기 꺼짐: view = (1, −1, 1)/√3, 위 = 도면 Z   /  켜짐: view = (1, 1, 1)/√3, 위 = 도면 Y
    orbitAxes(kIsoYawDeg, kIsoPitchDeg, standUp, view, up, scale);
    scale = std::sqrt(1.5);                              // 등각도: 축 방향 실제 길이 (투상 축소 0.816 을 되돌림)
}

void IsoDrawing::orbitAxes(double yawDeg, double pitchDeg, bool standUp, QVector3D &view, QVector3D &up, double &scale)
{
    const double yaw = qDegreesToRadians(yawDeg), pitch = qDegreesToRadians(pitchDeg);
    view = QVector3D(float(std::cos(pitch) * std::cos(yaw)), float(std::cos(pitch) * std::sin(yaw)),
                     float(std::sin(pitch)));
    up = QVector3D(0.0f, 0.0f, 1.0f);                    // 3D 창과 같은 위쪽
    if (standUp) {                                       // 창이 부품을 세워 보여 줌 → 창 방향을 모델 좌표로 되돌림
        view = QVector3D(view.x(), view.z(), -view.y());
        up = QVector3D(0.0f, 1.0f, 0.0f);
    }
    scale = 1.0;
}

bool IsoDrawing::blenderMeshes(const Model3D &model, QList<IsoMesh> &out, QString *error)
{
    out.clear();
    const QString exe = Model3DExport::findBlenderExe();
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        if (error)
            *error = QStringLiteral("Blender 를 찾지 못했습니다. 부품 3D 보기의 [Blender로 열기]로 위치를 한 번 지정하세요.");
        return false;
    }
    // 3D 보기용 표시판(Keyway)은 빼고 보냄 — 실제 구멍 모서리와 겹치는 가짜 선이 생기지 않게
    Model3D m = model;
    for (int i = 0; i < m.meshes.size(); ++i) {
        QList<Model3DMeshGroup> keep;
        for (int g = 0; g < m.meshes.at(i).groups.size(); ++g)
            if (m.meshes.at(i).groups.at(g).material != QLatin1String("Keyway"))
                keep << m.meshes.at(i).groups.at(g);
        m.meshes[i].groups = keep;
    }
    const QString objPath = Model3DExport::tempObjPath(QStringLiteral("autodraft_iso"));
    if (!Model3DExport::writeObj(m, objPath, error))
        return false;
    QString importScript;
    if (!Model3DExport::writeBlenderScript(objPath, &importScript, error))
        return false;
    const QString dir = QFileInfo(objPath).absolutePath();
    const QString outPath = dir + QStringLiteral("/autodraft_iso_mesh.txt");
    const QString pyPath = dir + QStringLiteral("/autodraft_iso_export.py");
    QFile::remove(outPath);
    {
        QFile py(pyPath);
        if (!py.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (error)
                *error = QStringLiteral("스크립트를 쓸 수 없습니다: %1").arg(pyPath);
            return false;
        }
        QTextStream ts(&py);
        ts << QString::fromUtf8(kIsoScript).arg(importScript, outPath);
    }
    QProcess proc;
    proc.start(exe, QStringList() << QStringLiteral("-b") << QStringLiteral("--factory-startup")
                                  << QStringLiteral("--python") << pyPath);
    if (!proc.waitForStarted(20000) || !proc.waitForFinished(300000)) {
        proc.kill();
        if (error)
            *error = QStringLiteral("Blender 실행이 끝나지 않았습니다.");
        return false;
    }
    QFile f(outPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Blender 결과가 없습니다.\n%1").arg(QString::fromLocal8Bit(proc.readAllStandardError().right(800)));
        return false;
    }
    QTextStream in(&f);
    IsoMesh cur;
    bool has = false;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith(QLatin1String("o "))) {
            if (has && !cur.f.isEmpty())
                out << cur;
            cur = IsoMesh();
            cur.name = line.mid(2);
            has = true;
        } else if (line.startsWith(QLatin1String("v "))) {
            const QStringList p = line.mid(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (p.size() >= 3) {
                // Blender 좌표 → 모델 좌표 (x 오른쪽, y 위, z 보는 쪽). 세워 내보낸 모델은 (X, −Z, Y) 로 돌아가 있으므로 되돌린다
                const float bx = p.at(0).toFloat(), by = p.at(1).toFloat(), bz = p.at(2).toFloat();
                cur.v << (model.standUp ? QVector3D(bx, bz, -by) : QVector3D(bx, by, bz));
            }
        } else if (line.startsWith(QLatin1String("f "))) {
            const QStringList p = line.mid(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (p.size() >= 3)
                cur.f << p.at(0).toInt() << p.at(1).toInt() << p.at(2).toInt();
        }
    }
    if (has && !cur.f.isEmpty())
        out << cur;
    if (out.isEmpty() && error)
        *error = QStringLiteral("그릴 부품 메시가 없습니다.");
    return !out.isEmpty();
}

ProjDrawing IsoDrawing::build(const QList<IsoMesh> &meshes, const QVector3D &viewIn, const QVector3D &upIn, double scale,
                              bool showHidden, double featureDeg)
{
    ProjDrawing out;
    const QVector3D d = viewIn.normalized();
    QVector3D up = upIn;
    QVector3D r = QVector3D::crossProduct(up, d);
    if (r.length() < 1e-4f) {                            // 위쪽과 시선이 나란함 (바로 위에서 봄)
        up = QVector3D(0.0f, 1.0f, 0.0f);
        r = QVector3D::crossProduct(up, d);
        if (r.length() < 1e-4f)
            r = QVector3D(1.0f, 0.0f, 0.0f);
    }
    r.normalize();
    const QVector3D u = QVector3D::crossProduct(d, r).normalized();

    // 투상 삼각형 + 경계
    QVector<Tri2> tris;
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (int m = 0; m < meshes.size(); ++m) {
        const IsoMesh &me = meshes.at(m);
        for (int k = 0; k + 2 < me.f.size(); k += 3) {
            const int ia = me.f.at(k), ib = me.f.at(k + 1), ic = me.f.at(k + 2);
            if (ia < 0 || ib < 0 || ic < 0 || ia >= me.v.size() || ib >= me.v.size() || ic >= me.v.size())
                continue;
            const QVector3D A = me.v.at(ia), B = me.v.at(ib), C = me.v.at(ic);
            Tri2 t;
            t.a = QPointF(QVector3D::dotProduct(r, A) * scale, QVector3D::dotProduct(u, A) * scale);
            t.b = QPointF(QVector3D::dotProduct(r, B) * scale, QVector3D::dotProduct(u, B) * scale);
            t.c = QPointF(QVector3D::dotProduct(r, C) * scale, QVector3D::dotProduct(u, C) * scale);
            t.za = QVector3D::dotProduct(d, A) * scale;
            t.zb = QVector3D::dotProduct(d, B) * scale;
            t.zc = QVector3D::dotProduct(d, C) * scale;
            const double x0 = std::min({t.a.x(), t.b.x(), t.c.x()}), x1 = std::max({t.a.x(), t.b.x(), t.c.x()});
            const double y0 = std::min({t.a.y(), t.b.y(), t.c.y()}), y1 = std::max({t.a.y(), t.b.y(), t.c.y()});
            t.box = QRectF(QPointF(x0, y0), QPointF(x1, y1));
            minX = std::min(minX, x0);
            maxX = std::max(maxX, x1);
            minY = std::min(minY, y0);
            maxY = std::max(maxY, y1);
            tris << t;
        }
    }
    if (tris.isEmpty())
        return out;
    const double size = std::max(maxX - minX, maxY - minY);
    const double tolDepth = std::max(1e-6, size * 1e-5);
    const double tol = std::max(1e-6, size * 1e-6);

    // 격자 (가림 판정 후보 줄이기)
    const int N = std::max(16, std::min(256, int(std::sqrt(double(tris.size())) * 2.0)));
    const double cw = std::max(1e-9, (maxX - minX) / N), ch = std::max(1e-9, (maxY - minY) / N);
    QVector<QVector<int>> grid(N * N);
    for (int i = 0; i < tris.size(); ++i) {
        const QRectF &b = tris.at(i).box;
        const int gx0 = std::max(0, std::min(N - 1, int((b.left() - minX) / cw)));
        const int gx1 = std::max(0, std::min(N - 1, int((b.right() - minX) / cw)));
        const int gy0 = std::max(0, std::min(N - 1, int((b.top() - minY) / ch)));
        const int gy1 = std::max(0, std::min(N - 1, int((b.bottom() - minY) / ch)));
        for (int gy = gy0; gy <= gy1; ++gy)
            for (int gx = gx0; gx <= gx1; ++gx)
                grid[gy * N + gx] << i;
    }

    const double featureCos = std::cos(qDegreesToRadians(featureDeg));
    QList<QLineF> vis, hid;
    QList<Circ3> circles;                               // 찾은 3D 원 (모든 메시 공통 — 같은 원은 하나로)
    for (int m = 0; m < meshes.size(); ++m) {
        const IsoMesh &me = meshes.at(m);
        // 면 법선(double) + 변 → 면 목록
        //   불리언 결과에는 아주 가는 삼각형(슬리버)이 섞여 법선이 엉터리 → 가장 긴 변 건너 이웃 면의 법선을 물려받음
        QVector<QVector3D> fn;
        QVector<quint64> longest;                       // 면마다 가장 긴 변
        QVector<bool> sliver;
        QHash<quint64, QVector<int>> edges;
        for (int k = 0; k + 2 < me.f.size(); k += 3) {
            const int fi = k / 3;
            const int ia = me.f.at(k), ib = me.f.at(k + 1), ic = me.f.at(k + 2);
            if (ia < 0 || ib < 0 || ic < 0 || ia >= me.v.size() || ib >= me.v.size() || ic >= me.v.size()) {
                fn << QVector3D();
                longest << 0;
                sliver << true;
                continue;
            }
            const QVector3D A = me.v.at(ia), B = me.v.at(ib), C = me.v.at(ic);
            const double e1x = double(B.x()) - A.x(), e1y = double(B.y()) - A.y(), e1z = double(B.z()) - A.z();
            const double e2x = double(C.x()) - A.x(), e2y = double(C.y()) - A.y(), e2z = double(C.z()) - A.z();
            const double nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
            const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            const int vs[3] = {ia, ib, ic};
            double best = -1.0, maxLen2 = 0.0;
            quint64 bestKey = 0;
            for (int e = 0; e < 3; ++e) {
                const quint32 p = quint32(vs[e]), q = quint32(vs[(e + 1) % 3]);
                const quint64 key = (quint64(std::min(p, q)) << 32) | quint64(std::max(p, q));
                edges[key] << fi;
                const QVector3D dv = me.v.at(vs[(e + 1) % 3]) - me.v.at(vs[e]);
                const double l2 = double(dv.x()) * dv.x() + double(dv.y()) * dv.y() + double(dv.z()) * dv.z();
                maxLen2 = std::max(maxLen2, l2);
                if (l2 > best) {
                    best = l2;
                    bestKey = key;
                }
            }
            const bool thin = len < 1e-12 || (0.5 * len) / std::max(1e-30, maxLen2) < 1e-6;
            fn << (len < 1e-12 ? QVector3D() : QVector3D(float(nx / len), float(ny / len), float(nz / len)));
            longest << bestKey;
            sliver << thin;
        }
        for (int pass = 0; pass < 3; ++pass)
            for (int fi = 0; fi < fn.size(); ++fi) {
                if (!sliver.at(fi))
                    continue;
                const QVector<int> nb = edges.value(longest.at(fi));
                for (int j = 0; j < nb.size(); ++j)
                    if (nb.at(j) != fi && !sliver.at(nb.at(j))) {
                        fn[fi] = fn.at(nb.at(j));
                        sliver[fi] = false;
                        break;
                    }
            }
        QVector<QPair<int, int>> feats;                 // 이 메시의 그릴 모서리
        for (QHash<quint64, QVector<int>>::const_iterator it = edges.constBegin(); it != edges.constEnd(); ++it) {
            const QVector<int> &fs = it.value();
            bool feature = false;
            if (fs.size() != 2) {
                feature = true;                         // 열린 변·여러 면이 만나는 변
            } else {
                const QVector3D n1 = fn.at(fs.at(0)), n2 = fn.at(fs.at(1));
                if (n1.isNull() || n2.isNull())
                    continue;
                if (QVector3D::dotProduct(n1, n2) < featureCos)
                    feature = true;                     // 꺾인 모서리
                else if ((QVector3D::dotProduct(n1, d) > 0.0f) != (QVector3D::dotProduct(n2, d) > 0.0f))
                    feature = true;                     // 굽은 면의 윤곽
            }
            if (!feature)
                continue;
            feats << qMakePair(int(it.key() >> 32), int(it.key() & 0xffffffffu));
        }
        // 원 위의 모서리 → 선 대신 원의 각 구간으로 모음
        QVector<int> edgeCircle;
        detectCircles(me.v, feats, circles, edgeCircle);
        for (int fk = 0; fk < feats.size(); ++fk) {
            const int ia = feats.at(fk).first, ib = feats.at(fk).second;
            const QVector3D A = me.v.at(ia), B = me.v.at(ib);
            const int circ = edgeCircle.at(fk);
            double thA = 0.0, dTh = 0.0;
            if (circ >= 0) {
                thA = circAngle(circles.at(circ), A);
                dTh = wrapPi(circAngle(circles.at(circ), B) - thA);
            }
            const QPointF a(QVector3D::dotProduct(r, A) * scale, QVector3D::dotProduct(u, A) * scale);
            const QPointF b(QVector3D::dotProduct(r, B) * scale, QVector3D::dotProduct(u, B) * scale);
            const double za = QVector3D::dotProduct(d, A) * scale, zb = QVector3D::dotProduct(d, B) * scale;
            const double L = std::hypot(b.x() - a.x(), b.y() - a.y());
            if (L < tol)
                continue;                               // 시선과 나란한 변 (점)
            const int n = std::max(1, std::min(48, int(L / (size / 300.0)) + 1));
            QVector<int> st(n);
            for (int s = 0; s < n; ++s) {
                const double t = (s + 0.5) / n;
                const QPointF q = a + (b - a) * t;
                const double z = za + (zb - za) * t;
                bool occ = false;
                const int gx = std::max(0, std::min(N - 1, int((q.x() - minX) / cw)));
                const int gy = std::max(0, std::min(N - 1, int((q.y() - minY) / ch)));
                const QVector<int> &cell = grid.at(gy * N + gx);
                for (int c = 0; c < cell.size() && !occ; ++c) {
                    const Tri2 &tr = tris.at(cell.at(c));
                    if (!tr.box.contains(q))
                        continue;
                    double zt = 0.0;
                    if (triDepthAt(tr, q, zt) && zt > z + tolDepth)
                        occ = true;
                }
                st[s] = occ ? 2 : 1;
            }
            int s0 = 0;
            while (s0 < n) {
                int s1 = s0;
                while (s1 + 1 < n && st.at(s1 + 1) == st.at(s0))
                    ++s1;
                const double f0 = double(s0) / n, f1 = double(s1 + 1) / n;
                if (circ >= 0) {
                    // 원의 각 구간 (시작 ∈ [0, 2π))
                    double t0 = thA + dTh * f0, t1 = thA + dTh * f1;
                    if (t1 < t0)
                        std::swap(t0, t1);
                    const double len = t1 - t0;
                    t0 = std::fmod(t0, kTwoPi);
                    if (t0 < 0.0)
                        t0 += kTwoPi;
                    Circ3 &cc = circles[circ];
                    (st.at(s0) == 1 ? cc.vis : cc.hid) << qMakePair(t0, t0 + len);
                } else {
                    const QLineF piece(a + (b - a) * f0, a + (b - a) * f1);
                    (st.at(s0) == 1 ? vis : hid) << piece;
                }
                s0 = s1 + 1;
            }
        }
    }
    mergeLines(vis, std::max(1e-6, size * 1e-5));
    mergeLines(hid, std::max(1e-6, size * 1e-5));
    // 가운데를 (0, 0) 으로
    const QPointF c((minX + maxX) * 0.5, (minY + maxY) * 0.5);
    for (int i = 0; i < vis.size(); ++i)
        out.visible << vis.at(i).translated(-c);
    if (showHidden)
        for (int i = 0; i < hid.size(); ++i)
            out.hidden << hid.at(i).translated(-c);
    // 원 → 타원·타원호 (정면이면 원·호)
    for (int k = 0; k < circles.size(); ++k) {
        bool full = false;
        const QList<QPair<double, double>> va = mergeArcs(circles.at(k).vis, full);
        if (full || !va.isEmpty())
            emitEllipses(circles.at(k), va, full, r, u, d, scale, c, out.visibleEllipses);
        if (!showHidden)
            continue;
        const QList<QPair<double, double>> ha = mergeArcs(circles.at(k).hid, full);
        if (full || !ha.isEmpty())
            emitEllipses(circles.at(k), ha, full, r, u, d, scale, c, out.hiddenEllipses);
    }
    dedupEllipses(out.visibleEllipses, out.hiddenEllipses);
    return out;
}
