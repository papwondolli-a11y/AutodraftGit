#include "mechmodel3d.h"
#include "depthmovetool.h"
#include "projectionview.h"

#include "fittolerancecalculator.h"

#include "mechpartdata.h"
#include "mechparttag.h"
#include "extrudefeature.h"
#include "holeitem.h"
#include "plateview.h"
#include "model3dmesh.h"
#include "mygraphicsscene.h"
#include "polylineitem.h"
#include "scenesymbolregistry.h"
#include "shaftbuilder.h"
#include "shaftdata.h"

#include <QHash>
#include <QSet>
#include <QSettings>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// 원 둘레 분할 수 = MechModel3D::radialSteps() (3D 창 "정밀도", 기본 64)
const double kEps         = 1e-6;
const double kCutterOver  = 1.0;  // 절삭체를 표면·끝면 밖으로 내미는 길이 (불리언 안정)

// 회전 윤곽의 한 점: 축 방향 x(왼쪽 끝면 = 0), 반지름 r
struct ProfilePt {
    double x;
    double r;
};

void addTri(QVector<QVector3D> &out, const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    out << a << b << c;
}

// 축 회전체 윤곽 (나사 모따기가 반영된 파라미터 사용)
std::vector<ProfilePt> shaftProfile(const ShaftParams &p)
{
    std::vector<ProfilePt> prof;
    double x = 0.0;
    prof.push_back({0.0, 0.0});
    for (int i = 0; i < p.segments.size(); ++i) {
        const ShaftSegment &s = p.segments.at(i);
        const double r = s.diameter * 0.5;
        const double cL = std::max(0.0, std::min(s.chamferLeft, std::min(r, s.length)));
        const double cR = std::max(0.0, std::min(s.chamferRight, std::min(r, s.length - cL)));
        const double x1 = x + s.length;
        if (i > 0) {
            const ShaftSegment &q = p.segments.at(i - 1);
            const double rq = q.diameter * 0.5;
            const double cq = std::max(0.0, std::min(q.chamferRight, rq));
            prof.push_back({x, rq - cq});
        }
        prof.push_back({x, r - cL});
        if (cL > kEps)
            prof.push_back({x + cL, r});
        prof.push_back({x1 - cR, r});
        if (cR > kEps)
            prof.push_back({x1, r - cR});
        x = x1;
    }
    prof.push_back({x, 0.0});

    // 연속 중복 점 제거
    std::vector<ProfilePt> out;
    for (size_t i = 0; i < prof.size(); ++i) {
        if (!out.empty() && std::fabs(out.back().x - prof[i].x) < kEps && std::fabs(out.back().r - prof[i].r) < kEps)
            continue;
        out.push_back(prof[i]);
    }
    return out;
}

// 윤곽의 x = cut 위치에 점을 끼워 넣는다 (나사 띠 경계)
void splitProfileAt(std::vector<ProfilePt> &prof, double cut)
{
    for (size_t i = 0; i + 1 < prof.size(); ++i) {
        const ProfilePt a = prof[i], b = prof[i + 1];
        if ((a.x < cut - kEps && b.x > cut + kEps) || (b.x < cut - kEps && a.x > cut + kEps)) {
            const double t = (cut - a.x) / (b.x - a.x);
            prof.insert(prof.begin() + long(i) + 1, ProfilePt{cut, a.r + t * (b.r - a.r)});
            return;
        }
    }
}

// 키홈 평면 모양 (도면 XY): 직선부 + 막힌 쪽 반원. 위가 +Y. 반시계 다각형.
QPolygonF slotPolygon(double xL, double xR, double yc, double rad, bool roundL, bool roundR)
{
    QPolygonF poly;
    const int n = 16;
    // 위쪽 변: 오른쪽 → 왼쪽, 왼쪽 끝, 아래쪽 변: 왼쪽 → 오른쪽, 오른쪽 끝 (반시계)
    if (roundR) {
        const double cx = xR - rad;
        for (int i = 0; i <= n; ++i) {             // -90° → +90° (아래 → 오른쪽 → 위)
            const double a = -M_PI / 2.0 + M_PI * double(i) / n;
            poly << QPointF(cx + rad * std::cos(a), yc + rad * std::sin(a));
        }
    } else {
        poly << QPointF(xR, yc - rad) << QPointF(xR, yc + rad);
    }
    if (roundL) {
        const double cx = xL + rad;
        for (int i = 0; i <= n; ++i) {             // 90° → 270° (위 → 왼쪽 → 아래)
            const double a = M_PI / 2.0 + M_PI * double(i) / n;
            poly << QPointF(cx + rad * std::cos(a), yc + rad * std::sin(a));
        }
    } else {
        poly << QPointF(xL, yc + rad) << QPointF(xL, yc - rad);
    }
    return poly;
}

// 볼록 다각형(반시계)을 z0~z1 로 세운 닫힌 기둥.
//   윗면·아랫면은 꼭짓점 0 기준 부채꼴 분할 → 옆면과 꼭짓점이 정확히 맞아 닫힌(manifold) 입체가 된다.
// 다각형 부호 넓이 (반시계 +)
double signedArea(const QPolygonF &p)
{
    double a = 0.0;
    for (int i = 0; i < p.size(); ++i) {
        const QPointF &u = p.at(i), &v = p.at((i + 1) % p.size());
        a += u.x() * v.y() - v.x() * u.y();
    }
    return a * 0.5;
}

void addPrism(QVector<QVector3D> &out, const QPolygonF &poly, double z0, double z1)
{
    const QPointF o = poly.at(0);
    for (int i = 1; i + 1 < poly.size(); ++i) {
        const QPointF b = poly.at(i), c = poly.at(i + 1);
        addTri(out, QVector3D(o.x(), o.y(), z1), QVector3D(b.x(), b.y(), z1), QVector3D(c.x(), c.y(), z1));
        addTri(out, QVector3D(o.x(), o.y(), z0), QVector3D(c.x(), c.y(), z0), QVector3D(b.x(), b.y(), z0));
    }
    for (int k = 0; k < poly.size(); ++k) {
        const QPointF p = poly.at(k), q = poly.at((k + 1) % poly.size());
        const QVector3D p0(p.x(), p.y(), z0), q0(q.x(), q.y(), z0);
        const QVector3D q1(q.x(), q.y(), z1), p1(p.x(), p.y(), z1);
        addTri(out, p0, q0, q1);
        addTri(out, p0, q1, p1);
    }
}

// 키홈 자국: 축 표면(반지름 r, 중심 y = oy)을 따라 휘는 띠. y 방향으로 잘게 나눠 표면 바로 위에 붙인다.
//   평면 모양은 slotPolygon 과 같다 (직선부 + 막힌 쪽 반원).
void addSurfaceSlot(QVector<QVector3D> &out, double xL, double xR, double yc, double rad,
                    bool roundL, bool roundR, double oy, double r)
{
    const int n = 16;
    const double lift = 0.03;
    for (int i = 0; i < n; ++i) {
        const double y0 = yc - rad + 2.0 * rad * double(i) / n;
        const double y1 = yc - rad + 2.0 * rad * double(i + 1) / n;
        double xa[2], xb[2], z[2];
        const double ys[2] = {y0, y1};
        for (int j = 0; j < 2; ++j) {
            const double h = std::sqrt(std::max(0.0, rad * rad - (ys[j] - yc) * (ys[j] - yc)));
            xa[j] = roundL ? (xL + rad - h) : xL;
            xb[j] = roundR ? (xR - rad + h) : xR;
            z[j] = std::sqrt(std::max(0.0, r * r - (ys[j] - oy) * (ys[j] - oy))) + lift;
        }
        const QVector3D a(xa[0], y0, z[0]), b(xb[0], y0, z[0]);
        const QVector3D c(xb[1], y1, z[1]), d(xa[1], y1, z[1]);
        addTri(out, a, b, c);
        addTri(out, a, c, d);
    }
}

// ── 배치 좌표계: 부품 로컬 (x = 축 방향, y = 도면 평면 안 수직, z = 보는 사람 쪽) → 도면 ──
struct Frame {
    double ox = 0.0, oy = 0.0, c = 1.0, s = 0.0;
    QVector3D map(double x, double y, double z) const
    {
        return QVector3D(float(ox + x * c - y * s), float(oy + x * s + y * c), float(z));
    }
};

Frame makeFrame(const QPointF &origin, double angleDeg)
{
    Frame f;
    f.ox = origin.x();
    f.oy = origin.y();
    f.c = std::cos(qDegreesToRadians(angleDeg));
    f.s = std::sin(qDegreesToRadians(angleDeg));
    return f;
}

// (x, r) 평면의 닫힌 단면을 x 축 둘레로 돌린 고리 입체. 단면은 어느 방향이든 받아 시계 방향으로 맞춘다.
//   cavity = true 면 단면 안의 빈 구멍(돌리면 내부 공동) → 반대 방향으로 맞춰 법선이 공동 쪽을 향하게 한다.
void revolveClosed(QVector<QVector3D> &out, std::vector<ProfilePt> sec, const Frame &f, bool cavity = false)
{
    double area = 0.0;
    for (size_t i = 0; i < sec.size(); ++i) {
        const ProfilePt &p = sec[i], &q = sec[(i + 1) % sec.size()];
        area += p.x * q.r - q.x * p.r;
    }
    if ((area > 0.0) != cavity)           // 반시계 → 시계 (바깥 법선 규칙: 진행 방향 오른쪽이 재료)
        std::reverse(sec.begin(), sec.end());
    for (size_t i = 0; i < sec.size(); ++i) {
        const ProfilePt a = sec[i], b = sec[(i + 1) % sec.size()];
        for (int k = 0; k < MechModel3D::radialSteps(); ++k) {
            const double t0 = 2.0 * M_PI * double(k) / MechModel3D::radialSteps();
            const double t1 = 2.0 * M_PI * double(k + 1) / MechModel3D::radialSteps();
            const QVector3D A = f.map(a.x, a.r * std::cos(t0), a.r * std::sin(t0));
            const QVector3D B = f.map(b.x, b.r * std::cos(t0), b.r * std::sin(t0));
            const QVector3D C = f.map(b.x, b.r * std::cos(t1), b.r * std::sin(t1));
            const QVector3D D = f.map(a.x, a.r * std::cos(t1), a.r * std::sin(t1));
            if (a.r > kEps)
                addTri(out, A, D, C);
            if (b.r > kEps)
                addTri(out, A, C, B);
        }
    }
}

// 축 위에서 시작해 축 위로 끝나는 열린 윤곽을 돌린 회전체 (볼트 몸통 등). x 가 커지는 방향으로 주어야 한다.
void revolveOpen(QVector<QVector3D> &out, const std::vector<ProfilePt> &prof, const Frame &f)
{
    for (size_t i = 0; i + 1 < prof.size(); ++i) {
        const ProfilePt a = prof[i], b = prof[i + 1];
        if (a.r < kEps && b.r < kEps)
            continue;
        for (int k = 0; k < MechModel3D::radialSteps(); ++k) {
            const double t0 = 2.0 * M_PI * double(k) / MechModel3D::radialSteps();
            const double t1 = 2.0 * M_PI * double(k + 1) / MechModel3D::radialSteps();
            const QVector3D A = f.map(a.x, a.r * std::cos(t0), a.r * std::sin(t0));
            const QVector3D B = f.map(b.x, b.r * std::cos(t0), b.r * std::sin(t0));
            const QVector3D C = f.map(b.x, b.r * std::cos(t1), b.r * std::sin(t1));
            const QVector3D D = f.map(a.x, a.r * std::cos(t1), a.r * std::sin(t1));
            if (a.r > kEps)
                addTri(out, A, D, C);
            if (b.r > kEps)
                addTri(out, A, C, B);
        }
    }
}

// 수나사 나사골 커터: 미터나사(60°) 홈 단면을 x 축 둘레 나선(피치 P)으로 쓸어 만든 닫힌 입체.
//   홈 중심이 x = a → b 로 나아간다 (부품 로컬). R = 바깥(호칭) 반지름.
//   단면 (축 방향 u, 반지름 r): 바깥지름에서 폭 7P/8 (산마루 P/8 평탄), 깊이 0.6134P, 플랭크 30°, 골 폭 ≈ P/6.
//   바깥지름 밖으로는 같은 폭(수직 벽)으로 0.25P 내밀어 이웃 바퀴와 겹치지 않게 한다 (불리언 안정).
// 로컬 삼각형을 부피 부호로 바깥 법선이 되게 맞춘 뒤 도면 좌표로 옮겨 담는다
void appendOriented(QVector<QVector3D> &out, const QVector<QVector3D> &tris, const Frame &f)
{
    double vol = 0.0;
    for (int i = 0; i + 2 < tris.size(); i += 3)
        vol += QVector3D::dotProduct(tris[i], QVector3D::crossProduct(tris[i + 1], tris[i + 2]));
    const bool flip = vol < 0.0;
    for (int i = 0; i + 2 < tris.size(); i += 3) {
        const QVector3D &p0 = tris[i];
        const QVector3D &p1 = flip ? tris[i + 2] : tris[i + 1];
        const QVector3D &p2 = flip ? tris[i + 1] : tris[i + 2];
        addTri(out, f.map(p0.x(), p0.y(), p0.z()), f.map(p1.x(), p1.y(), p1.z()), f.map(p2.x(), p2.y(), p2.z()));
    }
}

// 구멍·나사의 축 (부품 로컬): 점 = o + e1·t + e2·a + e3·b  (e1 = 축 방향, e2·e3 = 축에 수직인 정규직교)
struct HoleAxis {
    QVector3D o, e1, e2, e3;
    QVector3D at(double t, double a, double b) const
    {
        return o + e1 * float(t) + e2 * float(a) + e3 * float(b);
    }
};

HoleAxis makeAxis(const QVector3D &o, const QVector3D &dir)
{
    HoleAxis ax;
    ax.o = o;
    ax.e1 = dir.normalized();
    const QVector3D ref = (std::fabs(ax.e1.x()) < 0.9f) ? QVector3D(1.0f, 0.0f, 0.0f) : QVector3D(0.0f, 1.0f, 0.0f);
    ax.e2 = QVector3D::crossProduct(ax.e1, ref).normalized();
    ax.e3 = QVector3D::crossProduct(ax.e1, ax.e2).normalized();
    return ax;
}

// 볼록 단면 sec(u = 축 방향 오프셋, r = 반지름)을 축 ax 둘레 나선(피치 P)으로 t = a → b 쓸어 만든 닫힌 입체
void addHelixSweepAxis(QVector<QVector3D> &out, double a, double b, double P, const std::vector<QPointF> &sec,
                       const HoleAxis &ax, const Frame &f)
{
    const int nsec = int(sec.size());
    if (P <= kEps || b <= a || nsec < 3)
        return;
    const int perTurn = std::max(24, MechModel3D::radialSteps());
    const int rings = std::max(2, int(std::ceil((b - a) / P * perTurn)) + 1);
    std::vector<QVector3D> v(size_t(rings) * nsec);
    for (int i = 0; i < rings; ++i) {
        const double uc = a + (b - a) * double(i) / double(rings - 1);
        const double th = 2.0 * M_PI * (uc - a) / P;
        const double c = std::cos(th), s = std::sin(th);
        for (int j = 0; j < nsec; ++j)
            v[size_t(i) * nsec + j] = ax.at(uc + sec[size_t(j)].x(), sec[size_t(j)].y() * c, sec[size_t(j)].y() * s);
    }
    QVector<QVector3D> tris;
    for (int i = 0; i + 1 < rings; ++i) {
        for (int j = 0; j < nsec; ++j) {
            const int jn = (j + 1) % nsec;
            const QVector3D &A = v[size_t(i) * nsec + j], &B = v[size_t(i) * nsec + jn];
            const QVector3D &C = v[size_t(i + 1) * nsec + jn], &D = v[size_t(i + 1) * nsec + j];
            addTri(tris, A, B, C);
            addTri(tris, A, C, D);
        }
    }
    for (int j = 1; j + 1 < nsec; ++j) {   // 양 끝 마개 (볼록 단면 → 부채꼴)
        addTri(tris, v[0], v[size_t(j) + 1], v[size_t(j)]);
        const size_t e = size_t(rings - 1) * nsec;
        addTri(tris, v[e], v[e + j], v[e + j + 1]);
    }
    appendOriented(out, tris, f);
}

// x 축과 나란한 나선 (중심 (cy, cz), x = a → b)
void addHelixSweep(QVector<QVector3D> &out, double a, double b, double P, const std::vector<QPointF> &sec,
                   double cy, double cz, const Frame &f)
{
    addHelixSweepAxis(out, a, b, P, sec, makeAxis(QVector3D(0.0f, float(cy), float(cz)), QVector3D(1.0f, 0.0f, 0.0f)), f);
}

// 암나사(탭) 나사골 커터: 드릴 구멍(반지름 ri) 벽에서 호칭 반지름 R 까지 판 나선 홈. 축 ax 위 t = a → b.
//   단면: 드릴 지름 쪽 폭 3P/4(암나사 산마루 P/4), 플랭크 30°, 호칭 지름 쪽 폭 ≈ P/8. 안쪽(빈 구멍)으로 0.25P 내밈.
void addInternalThreadCutterAxis(QVector<QVector3D> &out, double a, double b, double ri, double R, double P,
                                 const HoleAxis &ax, const Frame &f)
{
    if (P <= kEps || R <= ri + kEps)
        return;
    const double t30 = std::tan(M_PI / 6.0);
    const double hwI = 3.0 * P / 8.0;                               // 드릴 지름에서 반폭
    const double hwO = std::max(hwI - (R - ri) * t30, 0.03 * P);    // 호칭 지름에서 반폭
    const double rin = std::max(0.0, ri - 0.25 * P);
    const std::vector<QPointF> sec = {QPointF(-hwI, rin), QPointF(hwI, rin), QPointF(hwI, ri),
                                      QPointF(hwO, R), QPointF(-hwO, R), QPointF(-hwI, ri)};
    addHelixSweepAxis(out, a, b, P, sec, ax, f);
}

// x 축과 나란한 암나사 커터 (중심 (cy, cz), x = a → b)
void addInternalThreadCutter(QVector<QVector3D> &out, double a, double b, double ri, double R, double P,
                             double cy, double cz, const Frame &f)
{
    addInternalThreadCutterAxis(out, a, b, ri, R, P,
                                makeAxis(QVector3D(0.0f, float(cy), float(cz)), QVector3D(1.0f, 0.0f, 0.0f)), f);
}

// (t, r) 닫힌 단면(축 위 점에서 시작·끝)을 구멍 축 ax 둘레로 돌린 입체 (t = 입구 ax.o 에서 ax.e1 쪽 깊이)
void addHoleRevolveAxis(QVector<QVector3D> &out, const std::vector<QPointF> &prof, const HoleAxis &ax, const Frame &f)
{
    const int n = std::max(24, MechModel3D::radialSteps() / 2);
    QVector<QVector3D> tris;
    for (size_t i = 0; i < prof.size(); ++i) {
        const QPointF a = prof[i], b = prof[(i + 1) % prof.size()];
        if (a.y() < kEps && b.y() < kEps)
            continue;
        for (int k = 0; k < n; ++k) {
            const double t0 = 2.0 * M_PI * double(k) / n, t1 = 2.0 * M_PI * double(k + 1) / n;
            const QVector3D A = ax.at(a.x(), a.y() * std::cos(t0), a.y() * std::sin(t0));
            const QVector3D B = ax.at(b.x(), b.y() * std::cos(t0), b.y() * std::sin(t0));
            const QVector3D C = ax.at(b.x(), b.y() * std::cos(t1), b.y() * std::sin(t1));
            const QVector3D D = ax.at(a.x(), a.y() * std::cos(t1), a.y() * std::sin(t1));
            if (a.y() > kEps)
                addTri(tris, A, D, C);
            if (b.y() > kEps)
                addTri(tris, A, C, B);
        }
    }
    appendOriented(out, tris, f);
}

// x 축과 나란한 구멍: 입구 x = xe, 중심 (cy, cz), dir = ±1 (x = xe + dir·t)
void addHoleRevolve(QVector<QVector3D> &out, const std::vector<QPointF> &prof, double xe, double dir,
                    double cy, double cz, const Frame &f)
{
    addHoleRevolveAxis(out, prof, makeAxis(QVector3D(float(xe), float(cy), float(cz)), QVector3D(float(dir), 0.0f, 0.0f)), f);
}

// 구멍 하나의 3D 커터 모양 — 그리기 탭 "구멍 단면"(HoleSectionGenerator::buildLines)과 같은 규칙
//   prof: (t = 입구에서 안쪽 깊이, r) 닫힌 단면. 축 위 (-over, 0) 에서 시작해 축 위에서 끝난다.
//   · 카운터보어: 입구에서 cbDepth 까지 cbDiameter
//   · 카운터싱크: (카운터보어 바닥 또는 입구에서) csDiameter → 드릴 지름, 반각 csAngle/2
//   · 깊이: 관통 = 재료 두께 + 여유, 막힘 = depth(입구부터, 0 이면 지름), 막힌 탭 = 나사 깊이 + 드릴 여유(M12 이상 3, 미만 2)
//   · 막힌 구멍 바닥: 드릴 끝 118°(반각 59°) 원뿔
//   · 탭: 드릴 지름(=골 쪽) 커터 + 별도 나선 홈(threadStart~threadEnd, 호칭 반지름 majorR, 피치 pitch)
struct HoleCutShape {
    std::vector<QPointF> prof;
    double drillR = 0.0;
    double entryR = 0.0;      // 입구에서 보이는 가장 큰 반지름 (표시용)
    double majorR = 0.0;
    double pitch = 0.0;
    double threadStart = 0.0;
    double threadEnd = 0.0;
};

HoleCutShape holeCutShape(const MyHole &hole, double thickness)
{
    HoleCutShape s;
    const double curR = hole.diameter * 0.5;
    if (curR <= kEps)
        return s;
    s.drillR = curR;
    double majorDia = 0.0, pitch = 0.0;
    const bool specOk = MyHole::parseThreadSpec(hole.threadSpec, majorDia, pitch);
    // 회전 단면 모양은 정면 해칭 빼기와 같은 함수 (HoleFeature::cutProfile) — 2D·3D 가 같은 모양
    double cur = 0.0;            // 카운터보어·카운터싱크가 끝나는 깊이
    s.prof = HoleFeature::cutProfile(hole, thickness, kCutterOver, &cur);
    s.entryR = (s.prof.size() > 1) ? s.prof[1].y() : curR;
    // 탭 나사 홈 범위
    if (hole.isThreaded && specOk && pitch > kEps && majorDia * 0.5 > curR + kEps) {
        s.majorR = majorDia * 0.5;
        s.pitch = pitch;
        s.threadStart = (cur > kEps) ? cur : -pitch;                       // 입구가 표면이면 밖에서부터
        s.threadEnd = hole.throughHole ? thickness + pitch : cur + std::max(0.0, hole.threadDepth);
    }
    return s;
}

void addThreadCutter(QVector<QVector3D> &out, double a, double b, double R, double P, const Frame &f)
{
    if (P <= kEps || R <= kEps || b <= a)
        return;
    const double t30 = std::tan(M_PI / 6.0);
    const double hwR = 7.0 * P / 16.0;                  // 바깥지름에서 반폭
    const double depth = std::min(0.6134 * P, R * 0.45);
    const double ri = R - depth;
    const double hwI = std::max(hwR - depth * t30, 0.02 * P);
    const double ro = R + 0.25 * P;
    // 단면 꼭짓점 (u, r) — 볼록 육각형
    const std::vector<QPointF> sec = {QPointF(-hwR, ro), QPointF(hwR, ro), QPointF(hwR, R),
                                      QPointF(hwI, ri), QPointF(-hwI, ri), QPointF(-hwR, R)};
    addHelixSweep(out, a, b, P, sec, 0.0, 0.0, f);
}


// 나사 구간 [x0, x1] (x1 쪽이 부품 끝면이면 endAtX1 = true) 의 나사골 커터 부품
Model3DMeshPart threadCutterPart(const QString &partName, int no, double x0, double x1, bool endAtX1,
                                 double R, double P, const Frame &f)
{
    Model3DMeshPart cutter;
    cutter.name = partName + QStringLiteral("_나사커터") + (no > 1 ? QString::number(no) : QString());
    cutter.type = QStringLiteral("Cutter");
    cutter.cutterFor = partName;
    Model3DMeshGroup cg;
    cg.material = QStringLiteral("Cutter");
    const double hwR = 7.0 * P / 16.0;
    // 끝면 쪽은 한 피치 밖까지, 나사 시작선 쪽은 홈이 나사 길이 안에 들도록
    const double a = endAtX1 ? x0 + hwR : x0 - P;
    const double b = endAtX1 ? x1 + P : x1 - hwR;
    addThreadCutter(cg.tris, a, b, R, P, f);
    cutter.groups << cg;
    return cutter;
}

// 구름베어링 0급(보통급, KS B 2014 / ISO 492) 평균 지름 허용차 (위 0, 아래 = 반환값 mm)
double bearingBoreLower(double d)
{
    static const double lim[] = {2.5, 10.0, 18.0, 30.0, 50.0, 80.0, 120.0, 180.0, 250.0, 315.0, 400.0};
    static const double tol[] = {-0.008, -0.008, -0.008, -0.010, -0.012, -0.015, -0.020, -0.025, -0.030, -0.035, -0.040};
    for (int i = 0; i < 11; ++i)
        if (d <= lim[i])
            return tol[i];
    return -0.045;
}

double bearingOdLower(double D)
{
    static const double lim[] = {6.0, 18.0, 30.0, 50.0, 80.0, 120.0, 150.0, 180.0, 250.0, 315.0, 400.0, 500.0};
    static const double tol[] = {-0.008, -0.008, -0.009, -0.011, -0.013, -0.015, -0.018, -0.025, -0.030, -0.035, -0.040, -0.045};
    for (int i = 0; i < 12; ++i)
        if (D <= lim[i])
            return tol[i];
    return -0.050;
}

Model3DFitFeature fitFeature(const QVector3D &o, double dx, double dy, double x0, double x1, double radius, bool hole)
{
    Model3DFitFeature ft;
    ft.ox = o.x();
    ft.oy = o.y();
    ft.oz = o.z();
    ft.dx = dx;
    ft.dy = dy;
    ft.dz = 0.0;
    ft.x0 = x0;
    ft.x1 = x1;
    ft.radius = radius;
    ft.hole = hole;
    return ft;
}

// x 축과 나란한 원기둥 (중심 yc, zc, 반지름 rr, x0~x1, 양 끝 막음) — 구멍 커터
void addCylinderX(QVector<QVector3D> &out, double x0, double x1, double yc, double zc, double rr, const Frame &f)
{
    const int n = std::max(24, MechModel3D::radialSteps() / 2);
    const QVector3D c0 = f.map(x0, yc, zc), c1 = f.map(x1, yc, zc);
    for (int k = 0; k < n; ++k) {
        const double t0 = 2.0 * M_PI * double(k) / n, t1 = 2.0 * M_PI * double(k + 1) / n;
        const QVector3D A = f.map(x0, yc + rr * std::cos(t0), zc + rr * std::sin(t0));
        const QVector3D B = f.map(x1, yc + rr * std::cos(t0), zc + rr * std::sin(t0));
        const QVector3D C = f.map(x1, yc + rr * std::cos(t1), zc + rr * std::sin(t1));
        const QVector3D D = f.map(x0, yc + rr * std::cos(t1), zc + rr * std::sin(t1));
        addTri(out, A, D, C);   // 옆면 (revolveOpen 과 같은 순서 → 바깥 법선)
        addTri(out, A, C, B);
        addTri(out, c0, D, A);  // 왼쪽 끝 (−x)
        addTri(out, c1, B, C);  // 오른쪽 끝 (+x)
    }
}

// x = xe 평면 위 원판 (3D 보기용 구멍 입구 표시). plusX = 법선 +x
void addDiscX(QVector<QVector3D> &out, double xe, double yc, double zc, double rr, bool plusX, const Frame &f)
{
    const int n = 32;
    const QVector3D c = f.map(xe, yc, zc);
    for (int k = 0; k < n; ++k) {
        const double t0 = 2.0 * M_PI * double(k) / n, t1 = 2.0 * M_PI * double(k + 1) / n;
        const QVector3D P0 = f.map(xe, yc + rr * std::cos(t0), zc + rr * std::sin(t0));
        const QVector3D P1 = f.map(xe, yc + rr * std::cos(t1), zc + rr * std::sin(t1));
        if (plusX)
            addTri(out, c, P0, P1);
        else
            addTri(out, c, P1, P0);
    }
}

// 구 (로컬 중심 cx, cy, cz)
void addSphere(QVector<QVector3D> &out, double cx, double cy, double cz, double rad, const Frame &f)
{
    const int lon = std::max(24, MechModel3D::radialSteps() / 2), lat = lon / 2;
    for (int i = 0; i < lat; ++i) {
        const double p0 = M_PI * double(i) / lat - M_PI / 2.0;
        const double p1 = M_PI * double(i + 1) / lat - M_PI / 2.0;
        for (int j = 0; j < lon; ++j) {
            const double q0 = 2.0 * M_PI * double(j) / lon;
            const double q1 = 2.0 * M_PI * double(j + 1) / lon;
            // 로컬 구면점: x = cosφ cosθ, y = cosφ sinθ, z = sinφ
            const QVector3D a = f.map(cx + rad * std::cos(p0) * std::cos(q0), cy + rad * std::cos(p0) * std::sin(q0),
                                      cz + rad * std::sin(p0));
            const QVector3D b = f.map(cx + rad * std::cos(p0) * std::cos(q1), cy + rad * std::cos(p0) * std::sin(q1),
                                      cz + rad * std::sin(p0));
            const QVector3D c = f.map(cx + rad * std::cos(p1) * std::cos(q1), cy + rad * std::cos(p1) * std::sin(q1),
                                      cz + rad * std::sin(p1));
            const QVector3D d = f.map(cx + rad * std::cos(p1) * std::cos(q0), cy + rad * std::cos(p1) * std::sin(q0),
                                      cz + rad * std::sin(p1));
            if (i > 0)
                addTri(out, a, b, c);
            if (i + 1 < lat)
                addTri(out, a, c, d);
        }
    }
}

// 육각기둥: 축 x0~x1, 대변거리 sf, 꼭짓점이 위(+y)를 향함 (측면도에서 대각거리가 세로로 보이는 방향)
void addHexPrism(QVector<QVector3D> &out, double x0, double x1, double sf, const Frame &f)
{
    const double R = sf / std::sqrt(3.0);
    double vy[6], vz[6];
    for (int k = 0; k < 6; ++k) {
        const double t = M_PI / 2.0 + M_PI / 3.0 * k;
        vy[k] = R * std::cos(t);
        vz[k] = R * std::sin(t);
    }
    for (int k = 1; k + 1 < 6; ++k) {
        addTri(out, f.map(x1, vy[0], vz[0]), f.map(x1, vy[k], vz[k]), f.map(x1, vy[k + 1], vz[k + 1]));
        addTri(out, f.map(x0, vy[0], vz[0]), f.map(x0, vy[k + 1], vz[k + 1]), f.map(x0, vy[k], vz[k]));
    }
    for (int k = 0; k < 6; ++k) {
        const int n = (k + 1) % 6;
        const QVector3D A = f.map(x0, vy[k], vz[k]), B = f.map(x1, vy[k], vz[k]);
        const QVector3D C = f.map(x1, vy[n], vz[n]), D = f.map(x0, vy[n], vz[n]);
        addTri(out, A, C, B);
        addTri(out, A, D, C);
    }
}

// (cx, cr) 중심 원호 점들 (도, a0 → a1)
void appendArc(std::vector<ProfilePt> &sec, double cx, double cr, double rad, double a0, double a1, int n)
{
    for (int i = 0; i <= n; ++i) {
        const double a = qDegreesToRadians(a0 + (a1 - a0) * double(i) / n);
        sec.push_back({cx + rad * std::cos(a), cr + rad * std::sin(a)});
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 깊은 홈 볼베어링: 2D 도면과 같은 규칙 — 볼 반지름 = (D-d)/6.8, 피치원 = (d+D)/4, 모따기 c = min(r, 0.4·min(B, 링높이))
//   내륜·외륜 = 볼받침선(볼 45° 점) 높이에서 나뉘고 볼 홈(궤도)이 파인 고리, 볼 = 피치원에 n 개
// ─────────────────────────────────────────────────────────────────────────────
namespace {
// 깊은 홈 볼베어링 내륜·외륜 단면 (x = 폭 방향, 가운데 0 / r = 반지름)
void bearingSections(double d, double D, double B, double r, std::vector<ProfilePt> &inner,
                     std::vector<ProfilePt> &outer)
{
    const double rIn = d * 0.5, rOut = D * 0.5, ringH = rOut - rIn;
    const double rp = (rIn + rOut) * 0.5;
    const double br = (D - d) / 6.8;
    const double s45 = br * std::sin(M_PI / 4.0);   // 볼받침선 높이 (볼 45° 점) — 궤도 홈 호의 끝
    const double c = std::min(r, std::min(B, ringH) * 0.4);
    const double hb = B * 0.5;

    // 내륜 (시계 방향: 윗변 왼→오, 오른변 아래로, 아랫변 오→왼, 왼변 위로)
    inner.clear();
    inner.push_back({-hb, rp - s45});
    appendArc(inner, 0.0, rp, br, 225.0, 315.0, 12);        // 궤도 홈 (아래로 파임)
    inner.push_back({hb, rp - s45});
    if (c > kEps) {
        appendArc(inner, hb - c, rIn + c, c, 0.0, -90.0, 4);
        appendArc(inner, -hb + c, rIn + c, c, -90.0, -180.0, 4);
    } else {
        inner.push_back({hb, rIn});
        inner.push_back({-hb, rIn});
    }
    // 외륜
    outer.clear();
    if (c > kEps) {
        appendArc(outer, -hb + c, rOut - c, c, 180.0, 90.0, 4);
        appendArc(outer, hb - c, rOut - c, c, 90.0, 0.0, 4);
    } else {
        outer.push_back({-hb, rOut});
        outer.push_back({hb, rOut});
    }
    outer.push_back({hb, rp + s45});
    appendArc(outer, 0.0, rp, br, 45.0, 135.0, 12);         // 궤도 홈 (위로 파임)
    outer.push_back({-hb, rp + s45});

}

} // namespace

QList<Model3DMeshPart> MechModel3D::deepGrooveBearingParts(double d, double D, double B, double r,
                                                          const QPointF &origin, double angleDeg,
                                                          const QString &name)
{
    QList<Model3DMeshPart> parts;
    if (d <= 0.0 || D <= d || B <= 0.0)
        return parts;
    const Frame f = makeFrame(origin, angleDeg);
    std::vector<ProfilePt> inner, outer;
    bearingSections(d, D, B, r, inner, outer);
    const double rIn = d * 0.5, rOut = D * 0.5;
    const double rp = (rIn + rOut) * 0.5;
    const double br = (D - d) / 6.8;
    const double hb = B * 0.5;

    Model3DMeshGroup steel, ball;
    steel.material = QStringLiteral("Steel");
    ball.material = QStringLiteral("Ball");
    revolveClosed(steel.tris, inner, f);
    revolveClosed(steel.tris, outer, f);

    // 볼: 피치원 둘레 n 개 (볼 사이 간격 ≈ 지름의 20%), 궤도와 닿지 않게 약간 작게
    const int n = std::max(6, int(std::floor(2.0 * M_PI * rp / (2.0 * br * 1.2))));
    for (int k = 0; k < n; ++k) {
        const double phi = 2.0 * M_PI * double(k) / n;
        addSphere(ball.tris, 0.0, rp * std::cos(phi), rp * std::sin(phi), br * 0.98, f);
    }

    Model3DMeshPart part;
    part.name = name;
    part.type = QStringLiteral("MechDeepGrooveBearing");
    part.groups << steel << ball;

    // 끼워맞춤 면: 안지름(구멍) · 바깥지름(축 쪽), 0급 허용차 (위 0)
    const QVector3D c0 = f.map(0.0, 0.0, 0.0);
    Model3DFitFeature bore = fitFeature(c0, f.c, f.s, -hb, hb, rIn, true);
    bore.hasTol = true;
    bore.upper = 0.0;
    bore.lower = bearingBoreLower(d);
    bore.label = QStringLiteral("안지름 Ø%1 (베어링 0급)").arg(d);
    Model3DFitFeature od = fitFeature(c0, f.c, f.s, -hb, hb, rOut, false);
    od.hasTol = true;
    od.upper = 0.0;
    od.lower = bearingOdLower(D);
    od.label = QStringLiteral("바깥지름 Ø%1 (베어링 0급)").arg(D);
    part.fits << bore << od;
    parts << part;
    return parts;
}

// ─────────────────────────────────────────────────────────────────────────────
// 육각볼트: 기준점 = 머리/몸통 경계 중심, 몸통이 angleDeg 방향으로 L 만큼. 머리 = 육각기둥(높이 k, 대변 s)
//   몸통 끝 모따기 = 나사 높이(0.541266·피치), 나사부 = 끝에서 threadLen 만큼 어두운 띠
// ─────────────────────────────────────────────────────────────────────────────
QList<Model3DMeshPart> MechModel3D::hexBoltParts(double dNom, double s, double k, double L, double pitch,
                                                double threadLen, const QPointF &origin, double angleDeg,
                                                const QString &name)
{
    QList<Model3DMeshPart> parts;
    if (dNom <= 0.0 || L <= 0.0 || s <= 0.0 || k <= 0.0)
        return parts;
    const Frame f = makeFrame(origin, angleDeg);
    const double r = dNom * 0.5;
    const double ch = std::min(r * 0.5, 0.541266 * std::max(pitch, 0.0));
    const double tStart = std::max(0.0, L - std::min(threadLen, L));

    Model3DMeshGroup steel, thread;
    steel.material = QStringLiteral("Steel");
    thread.material = QStringLiteral("Thread");
    addHexPrism(steel.tris, -k, 0.0, s, f);

    // 몸통: 하나의 닫힌 회전체. 나사부(tStart~L, 끝 모따기 포함) 옆면만 Thread 재질
    std::vector<ProfilePt> shank;
    shank.push_back({0.0, 0.0});
    shank.push_back({0.0, r});
    if (tStart > kEps)
        shank.push_back({tStart, r});
    shank.push_back({L - ch, r});
    if (ch > kEps)
        shank.push_back({L, r - ch});
    shank.push_back({L, 0.0});
    for (size_t i = 0; i + 1 < shank.size(); ++i) {
        std::vector<ProfilePt> edge;
        edge.push_back(shank[i]);
        edge.push_back(shank[i + 1]);
        const bool isThread = (shank[i + 1].x - shank[i].x) > kEps && shank[i].x >= tStart - kEps;
        revolveOpen(isThread ? thread.tris : steel.tris, edge, f);
    }

    Model3DMeshPart part;
    part.name = name;
    part.type = QStringLiteral("MechHexBolt");
    part.groups << steel << thread;
    parts << part;
    // Blender 용 나사골 커터 (3D 보기에서는 그리지 않음 — 어두운 띠 유지)
    if (pitch > kEps && L - tStart > kEps)
        parts << threadCutterPart(name, 1, tStart, L, true, r, pitch, f);
    return parts;
}

namespace {
// 탭 구멍의 나선 나사골 커터 하나를 list 에 추가 (구멍 커터와 겹치지 않게 별도 객체 "<이름>_나사커터N")
void addHoleThreadPart(QList<Model3DMeshPart> &list, const QString &name, int &no, const HoleCutShape &cs,
                       const HoleAxis &ax, const Frame &f)
{
    if (cs.threadEnd <= cs.threadStart + kEps || cs.pitch <= kEps)
        return;
    Model3DMeshPart tc;
    tc.name = name + QStringLiteral("_나사커터") + (++no > 1 ? QString::number(no) : QString());
    tc.type = QStringLiteral("Cutter");
    tc.cutterFor = name;
    Model3DMeshGroup tg;
    tg.material = QStringLiteral("Cutter");
    addInternalThreadCutterAxis(tg.tris, cs.threadStart, cs.threadEnd, cs.drillR, cs.majorR, cs.pitch, ax, f);
    if (!tg.tris.isEmpty()) {
        tc.groups << tg;
        list << tc;
    }
}
// 회전 부품(단면 회전체·축)의 구멍 커터 — Blender 에서 뺄 "<이름>_구멍커터" + 탭 "<이름>_나사커터N", 3D 보기용 입구 원판(mark)
//   holes    = 측면도에 구멍R·구멍P 로 놓은 구멍 (축 방향, 원주 어디든)
//   secHoles = 정면 단면 위에 "구멍 단면" 도구로 그린 구멍 (축 방향·반경 방향, 절단면 위)
//   threadNo = 나사커터 번호 (부품에 이미 있는 나사커터 다음 번호부터)
QList<Model3DMeshPart> holeCutters(const QString &name, const QList<RevolveLoop> &loops, const Frame &f,
                                   const QList<HoleFeature> &holes, const QList<SectionHole> &secHoles,
                                   Model3DMeshGroup &mark, int &threadNo)
{
    QList<Model3DMeshPart> out;
    Model3DMeshPart cutter;
    cutter.name = name + QStringLiteral("_구멍커터");
    cutter.type = QStringLiteral("Cutter");
    cutter.cutterFor = name;
    Model3DMeshGroup cg;
    cg.material = QStringLiteral("Cutter");
    QList<Model3DMeshPart> threadCutters;

    // 측면도 구멍: 입구 = 그린 쪽 끝면, 안쪽 방향으로 t 증가
    for (int h = 0; h < holes.size(); ++h) {
        const HoleFeature &hf = holes.at(h);
        double lo = 0.0, hi = 0.0;
        if (!hf.materialRange(loops, lo, hi))
            continue;
        const HoleCutShape cs = holeCutShape(hf.hole, hi - lo);
        if (cs.prof.size() < 3)
            continue;
        const double xe = hf.leftSide ? lo : hi;
        const double dir = hf.leftSide ? 1.0 : -1.0;
        const HoleAxis ax = makeAxis(QVector3D(float(xe), float(hf.y), float(hf.z)), QVector3D(float(dir), 0.0f, 0.0f));
        addHoleRevolveAxis(cg.tris, cs.prof, ax, f);
        addHoleThreadPart(threadCutters, name, threadNo, cs, ax, f);
        // 3D 보기용 입구 표시 (보기 창은 커터를 빼지 않으므로 원판으로 위치만 보여 준다)
        addDiscX(mark.tris, xe - dir * 0.02, hf.y, hf.z, cs.entryR, !hf.leftSide, f);
        if (hf.throughHole())
            addDiscX(mark.tris, (hf.leftSide ? hi : lo) + dir * 0.02, hf.y, hf.z, cs.drillR, hf.leftSide, f);
    }

    // 정면 단면 위 구멍 단면: 축 방향(절단면 위 그 자리) · 반경 방향(바깥 → 축). 부품 로컬 x = 축, y = 정면 위, z = 보는 쪽
    for (int h = 0; h < secHoles.size(); ++h) {
        const SectionHole &sh = secHoles.at(h);
        const HoleCutShape cs = holeCutShape(sh.hole, sh.thickness);
        if (cs.prof.size() < 3)
            continue;
        const QVector3D entry(float(sh.x), float(sh.s), 0.0f);
        const QVector3D dir = sh.axial ? QVector3D(float(sh.dirSign), 0.0f, 0.0f)
                                       : QVector3D(0.0f, float(sh.dirSign), 0.0f);
        const HoleAxis ax = makeAxis(entry, dir);
        addHoleRevolveAxis(cg.tris, cs.prof, ax, f);
        addHoleThreadPart(threadCutters, name, threadNo, cs, ax, f);
        if (sh.axial)
            addDiscX(mark.tris, sh.x - sh.dirSign * 0.02, sh.s, 0.0, cs.entryR, sh.dirSign < 0.0, f);
    }

    if (!cg.tris.isEmpty()) {
        cutter.groups << cg;
        out << cutter;
    }
    out << threadCutters;
    return out;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
QList<Model3DMeshPart> MechModel3D::shaftParts(const ShaftParams &params, const QPointF &origin,
                                               const QString &name, const QList<RevolveLoop> &loops,
                                               const QList<HoleFeature> &holes,
                                               const QList<SectionHole> &secHoles)
{
    QList<Model3DMeshPart> parts;
    if (params.segments.isEmpty())
        return parts;
    const ShaftParams eff = ShaftBuilder::withThreadChamfers(params);
    const double L = eff.totalLength();

    // 나사 띠 범위
    double thrL0 = -1.0, thrL1 = -1.0, thrR0 = -1.0, thrR1 = -1.0;
    if (eff.hasThread(0)) {
        thrL0 = 0.0;
        thrL1 = std::min(eff.leftThread.length, eff.segments.first().length);
    }
    if (eff.hasThread(1)) {
        thrR1 = L;
        thrR0 = L - std::min(eff.rightThread.length, eff.segments.last().length);
    }

    std::vector<ProfilePt> prof = shaftProfile(eff);
    if (thrL1 > 0.0)
        splitProfileAt(prof, thrL1);
    if (thrR0 > 0.0)
        splitProfileAt(prof, thrR0);

    Model3DMeshGroup steel, thread;
    steel.material = QStringLiteral("Steel");
    thread.material = QStringLiteral("Thread");
    const double ox = origin.x(), oy = origin.y();
    for (size_t i = 0; i + 1 < prof.size(); ++i) {
        const ProfilePt a = prof[i], b = prof[i + 1];
        if (a.r < kEps && b.r < kEps)
            continue;
        const double xm = (a.x + b.x) * 0.5;
        const bool isThread = std::fabs(b.x - a.x) > kEps &&
                              ((xm > thrL0 && xm < thrL1) || (xm > thrR0 && xm < thrR1));
        QVector<QVector3D> &out = isThread ? thread.tris : steel.tris;
        for (int k = 0; k < MechModel3D::radialSteps(); ++k) {
            const double t0 = 2.0 * M_PI * double(k) / MechModel3D::radialSteps();
            const double t1 = 2.0 * M_PI * double(k + 1) / MechModel3D::radialSteps();
            const QVector3D A(ox + a.x, oy + a.r * std::cos(t0), a.r * std::sin(t0));
            const QVector3D B(ox + b.x, oy + b.r * std::cos(t0), b.r * std::sin(t0));
            const QVector3D C(ox + b.x, oy + b.r * std::cos(t1), b.r * std::sin(t1));
            const QVector3D D(ox + a.x, oy + a.r * std::cos(t1), a.r * std::sin(t1));
            // 윤곽을 왼→오른쪽으로 따라가며 θ 증가 방향으로 돌릴 때 바깥 법선이 되는 순서
            if (a.r > kEps)
                addTri(out, A, D, C);
            if (b.r > kEps)
                addTri(out, A, C, B);
        }
    }

    Model3DMeshPart body;
    body.name = name;
    body.type = QStringLiteral("MechShaft");
    body.groups << steel;

    // 끼워맞춤 면: 구간마다 원통 (끼워맞춤 기호가 있으면 공차표 값)
    {
        double xs = 0.0;
        for (int i = 0; i < eff.segments.size(); ++i) {
            const ShaftSegment &sg = eff.segments.at(i);
            Model3DFitFeature ft = fitFeature(QVector3D(float(ox), float(oy), 0.0f), 1.0, 0.0, xs, xs + sg.length,
                                              sg.diameter * 0.5, false);
            ft.label = QStringLiteral("Ø%1").arg(sg.diameter);
            const QString code = sg.fit.trimmed();
            if (!code.isEmpty()) {
                const FitToleranceResult tr = FitToleranceCalculator::calculate(sg.diameter, code);
                if (tr.valid) {
                    ft.hasTol = true;
                    ft.upper = tr.upper;
                    ft.lower = tr.lower;
                    ft.label += QLatin1Char(' ') + code;
                }
            }
            body.fits << ft;
            xs += sg.length;
        }
    }
    if (!thread.tris.isEmpty())
        body.groups << thread;

    // 키홈: 보는 사람 쪽(+Z) 표면. 3D 보기용 표시면 + Blender 불리언용 절삭체
    Model3DMeshGroup mark;
    mark.material = QStringLiteral("Keyway");
    int keywayNo = 0;
    for (int idx = 0; idx < eff.segments.size(); ++idx) {
        // 홈 범위 [sa, sb] (축 좌표) — 2D 빌더와 같은 규칙
        double sa = 0.0, sb = 0.0;
        bool openL = false, openR = false;
        if (!ShaftBuilder::keywaySpan(eff, idx, sa, sb, openL, openR))
            continue;
        const ShaftSegment &s = eff.segments.at(idx);
        const ShaftKeyway &k = s.keyway;
        const double r = s.diameter * 0.5;
        const double depth = (k.sectionDepth > kEps && k.sectionDepth < r) ? k.sectionDepth : std::min(3.0, r * 0.4);
        const double rad = (k.upper + k.lower) * 0.5;
        if (rad <= kEps || k.length <= kEps)
            continue;
        const double yc = oy + k.center + (k.upper - k.lower) * 0.5;
        ++keywayNo;

        addSurfaceSlot(mark.tris, ox + sa, ox + sb, yc, rad, !openL, !openR, oy, r);

        Model3DMeshPart cutter;
        cutter.name = name + QStringLiteral("_키홈커터") + (keywayNo > 1 ? QString::number(keywayNo) : QString());
        cutter.type = QStringLiteral("Cutter");
        cutter.cutterFor = name;
        Model3DMeshGroup cg;
        cg.material = QStringLiteral("Cutter");
        const QPolygonF cutPoly = slotPolygon(ox + sa - (openL ? kCutterOver : 0.0),
                                              ox + sb + (openR ? kCutterOver : 0.0), yc, rad, !openL, !openR);
        addPrism(cg.tris, cutPoly, r - depth, r + kCutterOver);
        cutter.groups << cg;
        parts << cutter;
    }
    if (!mark.tris.isEmpty())
        body.groups << mark;

    // 나사부: Blender 용 나사골 커터 (3D 보기는 어두운 띠 유지)
    {
        const Frame f = makeFrame(origin, 0.0);
        int threadNo = 0;
        if (eff.hasThread(0)) {
            const ShaftSegment &s = eff.segments.first();
            const double P = ShaftParams::effectivePitch(eff.leftThread, s.diameter);
            parts << threadCutterPart(name, ++threadNo, 0.0, std::min(eff.leftThread.length, s.length), false,
                                      s.diameter * 0.5, P, f);
        }
        if (eff.hasThread(1)) {
            const ShaftSegment &s = eff.segments.last();
            const double P = ShaftParams::effectivePitch(eff.rightThread, s.diameter);
            parts << threadCutterPart(name, ++threadNo, L - std::min(eff.rightThread.length, s.length), L, true,
                                      s.diameter * 0.5, P, f);
        }
        // 구멍 (측면도 구멍R·구멍P + 정면 구멍 단면) — 단면 회전체와 같은 커터
        if (!loops.isEmpty() && (!holes.isEmpty() || !secHoles.isEmpty())) {
            Model3DMeshGroup holeMark;
            holeMark.material = QStringLiteral("Keyway");
            parts << holeCutters(name, loops, f, holes, secHoles, holeMark, threadNo);
            if (!holeMark.tris.isEmpty())
                body.groups << holeMark;
        }
    }
    parts.prepend(body);
    return parts;
}

// ─────────────────────────────────────────────────────────────────────────────
namespace {
// 해칭 단면 회전체: 태그된 해칭들의 현재 경로로 단면을 다시 뽑는다 (해칭을 고치거나 옮기면 3D 도 따라감)
void appendRevolve(Model3D &model, MyGraphicsScene *scene, const QString &id, const SymbolInfo &info,
                   QHash<QString, int> &nameCount)
{
    QList<QPainterPath> paths;
    QPointF ref;
    if (!RevolveProfile::partPaths(scene, id, paths, ref))
        return;
    const QMap<QString, QVariant> &pr = info.props;
    const double dx = ref.x() - pr.value(QStringLiteral("refX")).toDouble();
    const double dy = ref.y() - pr.value(QStringLiteral("refY")).toDouble();
    const QPointF a0(pr.value(QStringLiteral("ox")).toDouble() + dx, pr.value(QStringLiteral("oy")).toDouble() + dy);
    const double ang = qDegreesToRadians(pr.value(QStringLiteral("angleDeg")).toDouble());
    const QPointF a1 = a0 + QPointF(std::cos(ang), std::sin(ang)) * 100.0;
    const QList<RevolveLoop> loops =
        RevolveProfile::sectionLoops(paths, a0, a1, pr.value(QStringLiteral("Side")).toInt());
    QString name = info.name;
    const int n = ++nameCount[name];
    if (n > 1)
        name += QStringLiteral("(%1)").arg(n);
    model.meshes << MechModel3D::revolveParts(loops, a0, pr.value(QStringLiteral("angleDeg")).toDouble(), name,
                                              RevolveProfile::fitsFromJson(pr.value(QStringLiteral("Fits")).toString()),
                                              ProjectionView::holesOf(scene, id),
                                              ProjectionView::sectionHolesOf(scene, id, loops, a0,
                                                                             pr.value(QStringLiteral("angleDeg")).toDouble()));
}
} // namespace

namespace {
const char *kStandUpKey = "MechView3D/StandUp";   // 도면 설정(doc_settings) — 도면 파일에 저장
}

bool MechModel3D::standUp(MyGraphicsScene *scene)
{
    return scene && scene->documentSettings()->value(QString::fromLatin1(kStandUpKey), false).toBool();
}

void MechModel3D::setStandUp(MyGraphicsScene *scene, bool on)
{
    if (scene)
        scene->documentSettings()->setValue(QString::fromLatin1(kStandUpKey), on);
}

Model3D MechModel3D::build(MyGraphicsScene *scene)
{
    Model3D model;
    if (!scene)
        return model;
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    QHash<QString, int> nameCount;

    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        QGraphicsItem *gi = items.at(i);
        const QString id = gi->data(MECH_DATA_PART_ID).toString();
        if (id.isEmpty())
            continue;
        if (!reg->hasSymbol(id))
            continue;
        if (reg->symbolInfo(id).type == QLatin1String("MechRevolve"))
            continue;                             // 회전체는 아래에서 레지스트리 기준으로 (영역 방식은 태그된 도형이 없음)
        QPointF ref;
        if (!MechPartTag::refPoint(gi, ref))      // 기준 도형(폴리라인·선)의 현재 기준점
            continue;
        const SymbolInfo info = reg->symbolInfo(id);
        const QMap<QString, QVariant> &pr = info.props;

        // 기준 도형이 옮겨졌으면 그만큼 부품 기준점도 옮긴다
        const double dx = ref.x() - pr.value(QStringLiteral("refX")).toDouble();
        const double dy = ref.y() - pr.value(QStringLiteral("refY")).toDouble();
        const QPointF origin(pr.value(QStringLiteral("ox")).toDouble() + dx,
                             pr.value(QStringLiteral("oy")).toDouble() + dy);
        const double angleDeg = pr.value(QStringLiteral("angleDeg")).toDouble();

        QString name = info.name;
        const int n = ++nameCount[name];
        if (n > 1)
            name += QStringLiteral("(%1)").arg(n);

        const int firstMesh = model.meshes.size();
        if (info.type == QLatin1String("MechShaft")) {
            const ShaftParams params = ShaftParams::fromJsonString(
                info.props.value(QStringLiteral("Params")).toString(), ShaftParams::sample());
            // 구멍: 측면도에 놓은 구멍R·구멍P, 정면 축 위에 그린 구멍 단면 (단면 회전체와 같은 방식)
            QList<RevolveLoop> loops;
            QPointF a0;
            double ang = 0.0;
            QString nm;
            QList<HoleFeature> holes;
            QList<SectionHole> secHoles;
            if (revolveSection(scene, id, loops, a0, ang, nm)) {
                holes = ProjectionView::holesOf(scene, id);
                secHoles = ProjectionView::sectionHolesOf(scene, id, loops, a0, ang);
            }
            model.meshes << shaftParts(params, origin, name, loops, holes, secHoles);
        } else if (info.type == QLatin1String("MechDeepGrooveBearing")) {
            model.meshes << deepGrooveBearingParts(pr.value(QStringLiteral("d")).toDouble(),
                                                   pr.value(QStringLiteral("D")).toDouble(),
                                                   pr.value(QStringLiteral("B")).toDouble(),
                                                   pr.value(QStringLiteral("r")).toDouble(),
                                                   origin, angleDeg, name);
        } else if (info.type == QLatin1String("MechHexBolt")) {
            model.meshes << hexBoltParts(pr.value(QStringLiteral("d")).toDouble(),
                                         pr.value(QStringLiteral("s")).toDouble(),
                                         pr.value(QStringLiteral("k")).toDouble(),
                                         pr.value(QStringLiteral("L")).toDouble(),
                                         pr.value(QStringLiteral("pitch")).toDouble(),
                                         pr.value(QStringLiteral("threadLen")).toDouble(),
                                         origin, angleDeg, name);
        }
        for (int m = firstMesh; m < model.meshes.size(); ++m)
            model.meshes[m].partId = id;
    }
    // 단면 회전체 (해칭 방식·영역 방식 모두)
    const QMap<QString, SymbolInfo> &syms = reg->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = syms.constBegin(); it != syms.constEnd(); ++it)
        if (it.value().type == QLatin1String("MechRevolve")) {
            const int firstMesh = model.meshes.size();
            appendRevolve(model, scene, it.key(), it.value(), nameCount);
            for (int m = firstMesh; m < model.meshes.size(); ++m)
                model.meshes[m].partId = it.key();
        }
    // 측면도 돌출: 합치기면 "<호스트>_합침N" (Blender 에서 호스트에 Union), 아니면 새 부품
    QHash<QString, int> mergeCount;
    QHash<QString, int> cutCount;
    QList<Model3DMeshPart> cutParts;               // 파내기 커터 — 합치기 뒤에 빼도록 맨 끝에 넣음
    QHash<QString, Model3DMeshGroup> cutMarks;     // 호스트별 파낸 입구 표시 (3D 보기 창은 커터를 빼지 않음)
    for (QMap<QString, SymbolInfo>::const_iterator it = syms.constBegin(); it != syms.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechExtrude"))
            continue;
        ExtrudeInfo ei;
        if (!ExtrudeFeature::resolve(scene, it.key(), ei))
            continue;
        if (ei.cut) {
            if (!reg->hasSymbol(ei.host))
                continue;
            const QString hostName = reg->symbolInfo(ei.host).name;
            const bool plate = ei.plateBoss || ei.plateFront;
            // 커터는 열린 쪽(관통이면 양쪽)으로 조금 더 내밀어 불리언이 면에 걸리지 않게
            ExtrudeInfo c = ei;
            bool openLo = false, openHi = false;
            if (plate) {
                openHi = true;
                openLo = ei.through;
            } else if (ei.offset <= 1e-6) {
                openLo = (ei.viewKind == ProjectionView::Left);
                openHi = !openLo;
            }
            if (openLo)
                c.x0 -= kCutterOver;
            if (openHi)
                c.x1 += kCutterOver;
            Model3DMeshPart cp = extrudePart(c, hostName + QStringLiteral("_파내기") + QString::number(++cutCount[hostName]));
            cp.type = QStringLiteral("Cutter");
            cp.cutterFor = hostName;
            cp.partId = ei.host;
            for (int g = 0; g < cp.groups.size(); ++g)
                cp.groups[g].material = QStringLiteral("Cutter");
            cutParts << cp;
            // 입구 표시: 열린 면에 얇은 판
            const double opens[2] = {ei.x0, ei.x1};
            const bool isOpen[2] = {openLo, openHi};
            for (int o = 0; o < 2; ++o) {
                if (!isOpen[o])
                    continue;
                ExtrudeInfo mk = ei;
                mk.x0 = opens[o] - 0.03;
                mk.x1 = opens[o] + 0.03;
                const Model3DMeshPart mp = extrudePart(mk, QString());
                Model3DMeshGroup &mg = cutMarks[ei.host];
                mg.material = QStringLiteral("Keyway");
                for (int g = 0; g < mp.groups.size(); ++g)
                    mg.tris += mp.groups.at(g).tris;
            }
            continue;
        }
        QString pname;
        if (ei.merge && reg->hasSymbol(ei.host)) {
            const QString hostName = reg->symbolInfo(ei.host).name;
            pname = hostName + QStringLiteral("_합침") + QString::number(++mergeCount[hostName]);
        } else {
            pname = it.value().name;
            const int n = ++nameCount[pname];
            if (n > 1)
                pname += QStringLiteral("(%1)").arg(n);
        }
        // 단독 돌출 본체는 바깥 고리만 (섬 = 관통 구멍은 구멍 커터로 빼서 닫힌 입체 유지)
        ExtrudeInfo solid = ei;
        if (ei.standalone) {
            solid.rings.clear();
            for (int r = 0; r < ei.rings.size(); ++r)
                if (signedArea(ei.rings.at(r)) > 0.0)
                    solid.rings << ei.rings.at(r);
        }
        Model3DMeshPart ep = extrudePart(solid, pname);
        ep.partId = (ei.merge && reg->hasSymbol(ei.host)) ? ei.host : it.key();   // 합침은 호스트를 따라감
        QList<Model3DMeshPart> cutters;
        if (ei.standalone) {
            // 단독 돌출(판·블록): 공간 안의 구멍R·구멍P → Z 방향 구멍
            Model3DMeshGroup mark;
            mark.material = QStringLiteral("Keyway");
            cutters = plateHoleCutters(scene, it.key(), ei, pname, mark);
            if (!mark.tris.isEmpty())
                ep.groups << mark;
            for (int c = 0; c < cutters.size(); ++c)
                cutters[c].partId = ep.partId;
        } else if (!ei.plateBoss && !ei.plateFront) {
            // 회전체에 붙은 돌출(받침대): 회전체 보조 투상도에 놓은 구멍 → 이 돌출 메시에서 뺌
            //   (합침이면 "<호스트>_합침N_구멍커터" — Blender 에서 합침 객체에 먼저 빼고 호스트에 합친다)
            Model3DMeshGroup mark;
            mark.material = QStringLiteral("Keyway");
            cutters = hostExtrudeHoleCutters(scene, it.key(), ei, pname, mark);
            if (!mark.tris.isEmpty())
                ep.groups << mark;
            for (int c = 0; c < cutters.size(); ++c)
                cutters[c].partId = ep.partId;
        }
        model.meshes << ep;
        model.meshes << cutters;
    }
    // 파낸 입구 표시는 호스트 본체 메시에, 파내기 커터는 맨 끝에
    for (QHash<QString, Model3DMeshGroup>::const_iterator mk = cutMarks.constBegin(); mk != cutMarks.constEnd(); ++mk)
        for (int m = 0; m < model.meshes.size(); ++m)
            if (model.meshes.at(m).partId == mk.key() && model.meshes.at(m).cutterFor.isEmpty() &&
                !model.meshes.at(m).name.contains(QStringLiteral("_합침"))) {
                model.meshes[m].groups << mk.value();
                break;
            }
    model.meshes << cutParts;
    model.standUp = standUp(scene);
    return model;
}

// ─────────────────────────────────────────────────────────────────────────────
// 부품 고르기
// ─────────────────────────────────────────────────────────────────────────────
QList<MechModel3D::PartEntry> MechModel3D::partList(const Model3D &model)
{
    // 1차: 본체(커터·합침 아님) 이름, 2차: 본체가 없는 id 는 첫 메시 이름
    QList<PartEntry> out;
    QSet<QString> seen;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < model.meshes.size(); ++i) {
            const Model3DMeshPart &p = model.meshes.at(i);
            if (p.partId.isEmpty() || seen.contains(p.partId))
                continue;
            const bool tool = !p.cutterFor.isEmpty() || p.name.contains(QStringLiteral("_합침"));
            if (pass == 0 && tool)
                continue;
            seen.insert(p.partId);
            PartEntry e;
            e.id = p.partId;
            e.name = p.name;
            out << e;
        }
    }
    return out;
}

Model3D MechModel3D::filtered(const Model3D &model, const QSet<QString> &hiddenIds)
{
    if (hiddenIds.isEmpty())
        return model;
    Model3D out = model;
    out.meshes.clear();
    for (int i = 0; i < model.meshes.size(); ++i)
        if (model.meshes.at(i).partId.isEmpty() || !hiddenIds.contains(model.meshes.at(i).partId))
            out.meshes << model.meshes.at(i);
    return out;
}

QSet<QString> MechModel3D::partIdsOfItems(MyGraphicsScene *scene, const QList<QGraphicsItem *> &items)
{
    QSet<QString> ids;
    if (!scene)
        return ids;
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    const QMap<QString, SymbolInfo> &all = reg->allSymbols();

    // 부품 외형(태그된 기준 도형) — 태그 없는 선이 어느 부품 안에 있는지 찾을 때 씀
    QList<QPair<QString, QRectF>> outlines;
    const QList<QGraphicsItem *> sceneItems = scene->items();
    for (int i = 0; i < sceneItems.size(); ++i) {
        const QString id = sceneItems.at(i)->data(MECH_DATA_PART_ID).toString();
        if (!id.isEmpty() && reg->hasSymbol(id))
            outlines << qMakePair(id, sceneItems.at(i)->sceneBoundingRect());
    }

    for (int i = 0; i < items.size(); ++i) {
        QGraphicsItem *gi = items.at(i);
        // 1) 부품 기준 도형 (축·베어링·볼트 외형, 회전체 해칭)
        QString id = gi->data(MECH_DATA_PART_ID).toString();
        if (!id.isEmpty() && reg->hasSymbol(id)) { ids.insert(id); continue; }
        // 2) 돌출 투상 선
        id = gi->data(MECH_EXTRUDE_ID).toString();
        if (!id.isEmpty() && reg->hasSymbol(id)) { ids.insert(id); continue; }
        // 3) 투상도·미러 선 → 그 뷰를 가진 부품
        const QString viewId = gi->data(MECH_VIEW_ID).toString();
        if (!viewId.isEmpty()) {
            bool found = false;
            for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd() && !found; ++it)
                if (it.value().props.value(QStringLiteral("Views")).toString().contains(viewId)) {
                    ids.insert(it.key());
                    found = true;
                }
            if (found) continue;
        }
        // 4) 태그 없는 선: 중심이 영역 회전체·돌출 공간 안이거나, 부품 외형 상자 안
        const QPointF c = gi->sceneBoundingRect().center();
        bool isExtrude = false;
        id = DepthMove::partAt(scene, c, isExtrude);
        if (!id.isEmpty()) { ids.insert(id); continue; }
        const QRectF br = gi->sceneBoundingRect();
        for (int k = 0; k < outlines.size(); ++k)
            if (outlines.at(k).second.adjusted(-0.01, -0.01, 0.01, 0.01).contains(br)) {
                ids.insert(outlines.at(k).first);
                break;
            }
    }
    return ids;
}

namespace {
int g_radialSteps = -1;   // -1 = 아직 설정에서 읽지 않음
}

int MechModel3D::radialSteps()
{
    if (g_radialSteps < 0) {
        QSettings st;
        g_radialSteps = std::clamp(st.value(QStringLiteral("mech3d/radialSteps"), 64).toInt(), 32, 512);
    }
    return g_radialSteps;
}

void MechModel3D::setRadialSteps(int steps)
{
    g_radialSteps = std::clamp(steps, 32, 512);
    QSettings st;
    st.setValue(QStringLiteral("mech3d/radialSteps"), g_radialSteps);
}

double MechModel3D::facetError(double radius, int steps)
{
    return radius * (1.0 - std::cos(M_PI / double(std::max(3, steps))));
}

QList<Model3DMeshPart> MechModel3D::revolveParts(const QList<RevolveLoop> &loops, const QPointF &origin,
                                                 double angleDeg, const QString &name,
                                                 const QMap<QString, QString> &fits,
                                                 const QList<HoleFeature> &holes,
                                                 const QList<SectionHole> &secHoles)
{
    QList<Model3DMeshPart> parts;
    if (loops.isEmpty())
        return parts;
    const Frame f = makeFrame(origin, angleDeg);
    Model3DMeshGroup steel;
    steel.material = QStringLiteral("Steel");
    for (int i = 0; i < loops.size(); ++i) {
        std::vector<ProfilePt> sec;
        const QPolygonF &p = loops.at(i).pts;
        for (int k = 0; k < p.size(); ++k)
            sec.push_back({p.at(k).x(), p.at(k).y()});
        revolveClosed(steel.tris, sec, f, loops.at(i).hole);
    }
    Model3DMeshPart part;
    part.name = name;
    part.type = QStringLiteral("MechRevolve");
    part.groups << steel;

    // 구멍 (측면도 구멍R·구멍P + 정면 구멍 단면): Blender 커터 + 3D 보기용 입구 원판
    Model3DMeshGroup mark;
    mark.material = QStringLiteral("Keyway");
    int threadNo = 0;
    const QList<Model3DMeshPart> cutters = holeCutters(name, loops, f, holes, secHoles, mark, threadNo);
    if (!mark.tris.isEmpty())
        part.groups << mark;

    // 끼워맞춤 원통면 (단면에서 축과 나란한 변)
    const QVector3D c0 = f.map(0.0, 0.0, 0.0);
    const QList<RevolveCylinder> cyls = RevolveProfile::cylinders(loops);
    for (int i = 0; i < cyls.size(); ++i) {
        const RevolveCylinder &cy = cyls.at(i);
        Model3DFitFeature ft = fitFeature(c0, f.c, f.s, cy.x0, cy.x1, cy.diameter * 0.5, cy.hole);
        ft.label = QStringLiteral("%1 Ø%2").arg(cy.hole ? QStringLiteral("구멍") : QStringLiteral("바깥")).arg(cy.diameter);
        const QString code = fits.value(cy.key()).trimmed();
        if (!code.isEmpty()) {
            const FitToleranceResult tr = FitToleranceCalculator::calculate(cy.diameter, code);
            if (tr.valid) {
                ft.hasTol = true;
                ft.upper = tr.upper;
                ft.lower = tr.lower;
                ft.label += QLatin1Char(' ') + code;
            }
        }
        part.fits << ft;
    }
    parts << part;
    parts << cutters;
    return parts;
}

namespace {
RevolveLoop profileLoop(const std::vector<ProfilePt> &sec)
{
    RevolveLoop lp;
    for (size_t i = 0; i < sec.size(); ++i)
        lp.pts << QPointF(sec[i].x, std::max(0.0, sec[i].r));
    double a = 0.0;
    for (int i = 0; i < lp.pts.size(); ++i) {
        const QPointF &u = lp.pts.at(i), &v = lp.pts.at((i + 1) % lp.pts.size());
        a += u.x() * v.y() - v.x() * u.y();
    }
    if (a < 0.0)
        std::reverse(lp.pts.begin(), lp.pts.end());   // 바깥 고리 = 반시계
    return lp;
}
} // namespace

bool MechModel3D::revolveSection(MyGraphicsScene *scene, const QString &uuid, QList<RevolveLoop> &loops,
                                 QPointF &origin, double &angleDeg, QString &name, QPointF *refOut)
{
    loops.clear();
    if (!scene || uuid.isEmpty())
        return false;
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!reg->hasSymbol(uuid))
        return false;
    const SymbolInfo info = reg->symbolInfo(uuid);
    const QMap<QString, QVariant> &pr = info.props;
    name = info.name;
    angleDeg = pr.value(QStringLiteral("angleDeg")).toDouble();

    if (info.type == QLatin1String("MechRevolve")) {
        QList<QPainterPath> paths;
        QPointF ref;
        if (!RevolveProfile::partPaths(scene, uuid, paths, ref))
            return false;
        if (refOut)
            *refOut = ref;
        origin = QPointF(pr.value(QStringLiteral("ox")).toDouble() + ref.x() - pr.value(QStringLiteral("refX")).toDouble(),
                         pr.value(QStringLiteral("oy")).toDouble() + ref.y() - pr.value(QStringLiteral("refY")).toDouble());
        const double ang = qDegreesToRadians(angleDeg);
        loops = RevolveProfile::sectionLoops(paths, origin, origin + QPointF(std::cos(ang), std::sin(ang)) * 100.0,
                                             pr.value(QStringLiteral("Side")).toInt());
        return !loops.isEmpty();
    }

    // 기준 도형의 현재 위치로 이동량 반영
    QPointF ref;
    bool found = false;
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size() && !found; ++i)
        if (items.at(i)->data(MECH_DATA_PART_ID).toString() == uuid && MechPartTag::refPoint(items.at(i), ref))
            found = true;
    if (!found)
        return false;
    if (refOut)
        *refOut = ref;
    origin = QPointF(pr.value(QStringLiteral("ox")).toDouble() + ref.x() - pr.value(QStringLiteral("refX")).toDouble(),
                     pr.value(QStringLiteral("oy")).toDouble() + ref.y() - pr.value(QStringLiteral("refY")).toDouble());

    if (info.type == QLatin1String("MechShaft")) {
        const ShaftParams params =
            ShaftParams::fromJsonString(pr.value(QStringLiteral("Params")).toString(), ShaftParams::sample());
        loops << profileLoop(shaftProfile(ShaftBuilder::withThreadChamfers(params)));
        return true;
    }
    if (info.type == QLatin1String("MechDeepGrooveBearing")) {
        std::vector<ProfilePt> inner, outer;
        bearingSections(pr.value(QStringLiteral("d")).toDouble(), pr.value(QStringLiteral("D")).toDouble(),
                        pr.value(QStringLiteral("B")).toDouble(), pr.value(QStringLiteral("r")).toDouble(), inner, outer);
        loops << profileLoop(inner) << profileLoop(outer);
        return true;
    }
    return false;
}

namespace {
// 귀 자르기 삼각분할 (반시계 단순 다각형, 원래 꼭짓점만 사용 → 옆면과 꼭짓점이 맞아 닫힌 입체)
bool pointInTri(const QPointF &p, const QPointF &a, const QPointF &b, const QPointF &c)
{
    const double d1 = (p.x() - b.x()) * (a.y() - b.y()) - (a.x() - b.x()) * (p.y() - b.y());
    const double d2 = (p.x() - c.x()) * (b.y() - c.y()) - (b.x() - c.x()) * (p.y() - c.y());
    const double d3 = (p.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (p.y() - a.y());
    const bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0), pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

QVector<QPointF> earClip(const QPolygonF &ring)
{
    QVector<QPointF> out;
    QList<int> idx;
    for (int i = 0; i < ring.size(); ++i)
        idx << i;
    int guard = 0;
    while (idx.size() > 3 && guard < 100000) {
        ++guard;
        bool cut = false;
        for (int i = 0; i < idx.size(); ++i) {
            const QPointF a = ring.at(idx.at((i + idx.size() - 1) % idx.size()));
            const QPointF b = ring.at(idx.at(i));
            const QPointF c = ring.at(idx.at((i + 1) % idx.size()));
            const double cross = (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
            if (cross <= 1e-12)
                continue;                                  // 오목하거나 일직선
            bool inside = false;
            for (int k = 0; k < idx.size() && !inside; ++k) {
                const int v = idx.at(k);
                if (v == idx.at((i + idx.size() - 1) % idx.size()) || v == idx.at(i) ||
                    v == idx.at((i + 1) % idx.size()))
                    continue;
                inside = pointInTri(ring.at(v), a, b, c);
            }
            if (inside)
                continue;
            out << a << b << c;
            idx.removeAt(i);
            cut = true;
            break;
        }
        if (!cut) {                                        // 일직선 꼭짓점만 남은 경우: 하나 제거
            bool removed = false;
            for (int i = 0; i < idx.size() && !removed; ++i) {
                const QPointF a = ring.at(idx.at((i + idx.size() - 1) % idx.size()));
                const QPointF b = ring.at(idx.at(i));
                const QPointF c = ring.at(idx.at((i + 1) % idx.size()));
                if (std::fabs((b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x())) <= 1e-12) {
                    out << a << b << c;                    // 넓이 0 삼각형 — 꼭짓점 연결 유지
                    idx.removeAt(i);
                    removed = true;
                }
            }
            if (!removed)
                return QVector<QPointF>();
        }
    }
    if (idx.size() == 3)
        out << ring.at(idx.at(0)) << ring.at(idx.at(1)) << ring.at(idx.at(2));
    return out;
}
} // namespace

namespace {
// 돌출 점: 호스트 돌출 = 호스트 축 x + 로컬 (y, z) / 단독 돌출 = 도면 (X, Y) + Z
//   (x, a, b) → (a, b, x) 는 순환 치환이라 면 방향(바깥 법선)이 그대로 유지된다
QVector3D extrudePoint(const ExtrudeInfo &info, const Frame &f, double x, double a, double b)
{
    if (info.standalone)
        return QVector3D(float(a), float(b), float(x));
    return f.map(x, a, b);
}

// 임의 축 구멍의 표시 원판: 축 위 t 위치, 반지름 r. outward = true 면 −e1 쪽(입구 바깥)을 향함
void addDiscAxis(QVector<QVector3D> &out, const HoleAxis &ax, double t, double r, bool outward)
{
    const int n = std::max(24, MechModel3D::radialSteps() / 2);
    const QVector3D c = ax.at(t, 0.0, 0.0);
    for (int k = 0; k < n; ++k) {
        const double t0 = 2.0 * M_PI * k / n, t1 = 2.0 * M_PI * (k + 1) / n;
        const QVector3D p0 = ax.at(t, r * std::cos(t0), r * std::sin(t0));
        const QVector3D p1 = ax.at(t, r * std::cos(t1), r * std::sin(t1));
        if (outward)
            addTri(out, c, p1, p0);   // e2→e3 반시계 = +e1 이므로 뒤집어 −e1
        else
            addTri(out, c, p0, p1);
    }
}
} // namespace

Model3DMeshPart MechModel3D::extrudePart(const ExtrudeInfo &info, const QString &partName)
{
    Model3DMeshPart part;
    part.name = partName;
    part.type = QStringLiteral("MechExtrude");
    const Frame f = makeFrame(info.a0, info.angleDeg);
    Model3DMeshGroup steel;
    steel.material = QStringLiteral("Steel");
    // 끝 뚜껑: (y, z) 반시계 삼각형 → x1 쪽은 +x 법선, x0 쪽은 뒤집음
    // 구멍 없는 단면은 귀 자르기(닫힌 입체), 구멍이 있으면 띠 분할
    QVector<QPointF> tri;
    // 바깥 고리만(구멍 없음)이면 고리마다 귀 자르기 — 꼭짓점이 옆면과 맞아 닫힌 입체
    bool allOuter = true;
    for (int r = 0; r < info.rings.size() && allOuter; ++r)
        allOuter = signedArea(info.rings.at(r)) > 0.0;
    if (allOuter) {
        for (int r = 0; r < info.rings.size(); ++r) {
            const QVector<QPointF> t = earClip(info.rings.at(r));
            if (t.isEmpty()) {
                tri.clear();
                break;
            }
            tri += t;
        }
    }
    if (tri.isEmpty())
        tri = Model3DMesh::triangulate(info.rings);
    for (int i = 0; i + 2 < tri.size(); i += 3) {
        const QPointF &a = tri.at(i), &b = tri.at(i + 1), &c = tri.at(i + 2);
        addTri(steel.tris, extrudePoint(info, f, info.x1, a.x(), a.y()), extrudePoint(info, f, info.x1, b.x(), b.y()),
               extrudePoint(info, f, info.x1, c.x(), c.y()));
        addTri(steel.tris, extrudePoint(info, f, info.x0, a.x(), a.y()), extrudePoint(info, f, info.x0, c.x(), c.y()),
               extrudePoint(info, f, info.x0, b.x(), b.y()));
    }
    // 옆면: 반시계 고리의 변 a→b 는 (A0, B0, B1)(A0, B1, A1) 이 바깥 법선 (구멍 고리는 시계라 자동으로 안쪽)
    for (int r = 0; r < info.rings.size(); ++r) {
        const QPolygonF &p = info.rings.at(r);
        for (int k = 0; k < p.size(); ++k) {
            const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
            const QVector3D A0 = extrudePoint(info, f, info.x0, a.x(), a.y()), B0 = extrudePoint(info, f, info.x0, b.x(), b.y());
            const QVector3D A1 = extrudePoint(info, f, info.x1, a.x(), a.y()), B1 = extrudePoint(info, f, info.x1, b.x(), b.y());
            addTri(steel.tris, A0, B0, B1);
            addTri(steel.tris, A0, B1, A1);
        }
    }
    part.groups << steel;
    return part;
}

QList<Model3DMeshPart> MechModel3D::plateHoleCutters(MyGraphicsScene *scene, const QString &uuid,
                                                     const ExtrudeInfo &info, const QString &partName,
                                                     Model3DMeshGroup &mark)
{
    QList<Model3DMeshPart> out;
    if (!scene || !info.standalone || info.rings.isEmpty())
        return out;
    const Frame f = makeFrame(QPointF(0.0, 0.0), 0.0);   // 도면 좌표 그대로
    Model3DMeshPart cutter;
    cutter.name = partName + QStringLiteral("_구멍커터");
    cutter.type = QStringLiteral("Cutter");
    cutter.cutterFor = partName;
    Model3DMeshGroup cg;
    cg.material = QStringLiteral("Cutter");
    QList<Model3DMeshPart> threads;
    int threadNo = 0;
    // 공간 안의 닫힌 모양(섬, 시계 고리) = 관통 구멍 → 판보다 조금 더 긴 기둥 커터 (판 본체는 바깥 고리만으로 만듦)
    for (int r = 0; r < info.rings.size(); ++r) {
        if (signedArea(info.rings.at(r)) >= 0.0)
            continue;
        ExtrudeInfo isl = info;
        QPolygonF ring = info.rings.at(r);
        std::reverse(ring.begin(), ring.end());          // 반시계로
        isl.rings = QList<QPolygonF>{ring};
        isl.x0 = info.x0 - kCutterOver;
        isl.x1 = info.x1 + kCutterOver;
        const Model3DMeshPart prism = extrudePart(isl, partName);
        if (!prism.groups.isEmpty())
            cg.tris += prism.groups.first().tris;
        // 3D 보기 창은 커터를 빼지 않으므로 섬(관통 구멍) 자리 앞·뒷면에 얇은 표시판 (구멍 입구 원판과 같은 방식)
        const double faces[2] = {info.x1, info.x0};
        for (int k = 0; k < 2; ++k) {
            ExtrudeInfo mk = isl;
            mk.x0 = faces[k] - 0.03;
            mk.x1 = faces[k] + 0.03;
            const Model3DMeshPart mp = extrudePart(mk, partName);
            for (int g = 0; g < mp.groups.size(); ++g)
                mark.tris += mp.groups.at(g).tris;
        }
    }
    // 구멍 = 정면·측면도·평면도에 놓은 구멍R·구멍P + 정면 구멍 단면(옆면 구멍) — 2D 투상도와 같은 목록
    const QList<PlateHole> holes = PlateView::holes(scene, uuid);
    for (int h = 0; h < holes.size(); ++h) {
        const PlateHole &ph = holes.at(h);
        const HoleCutShape cs = holeCutShape(ph.hole, ph.thickness);
        if (cs.prof.size() < 3)
            continue;
        const HoleAxis ax = makeAxis(ph.entry, ph.dir);
        addHoleRevolveAxis(cg.tris, cs.prof, ax, f);
        addHoleThreadPart(threads, partName, threadNo, cs, ax, f);
        // 3D 보기용 입구 표시 (보기 창은 커터를 빼지 않음) — 앞면·옆면·보조 투상도 면 구멍 모두, 관통이면 반대쪽도
        addDiscAxis(mark.tris, ax, -0.02, cs.entryR, true);
        if (ph.hole.throughHole)
            addDiscAxis(mark.tris, ax, ph.thickness + 0.02, cs.drillR, false);
    }
    if (!cg.tris.isEmpty()) {
        cutter.groups << cg;
        out << cutter;
    }
    out << threads;
    return out;
}

QList<Model3DMeshPart> MechModel3D::hostExtrudeHoleCutters(MyGraphicsScene *scene, const QString &extrudeUuid,
                                                          const ExtrudeInfo &info, const QString &partName,
                                                          Model3DMeshGroup &mark)
{
    QList<Model3DMeshPart> out;
    if (!scene || info.standalone || info.host.isEmpty())
        return out;
    const QList<ProjectionView::AuxHole> holes = ProjectionView::auxHolesOf(scene, info.host);
    // 돌출 로컬(호스트 축 좌표) → 도면·3D 좌표
    const Frame f = makeFrame(info.a0, info.angleDeg);
    Model3DMeshPart cutter;
    cutter.name = partName + QStringLiteral("_구멍커터");
    cutter.type = QStringLiteral("Cutter");
    cutter.cutterFor = partName;
    Model3DMeshGroup cg;
    cg.material = QStringLiteral("Cutter");
    QList<Model3DMeshPart> threads;
    int threadNo = 0;
    for (int h = 0; h < holes.size(); ++h) {
        const ProjectionView::AuxHole &ah = holes.at(h);
        if (ah.extrudeUuid != extrudeUuid)
            continue;
        const HoleCutShape cs = holeCutShape(ah.hole, ah.thickness);
        if (cs.prof.size() < 3)
            continue;
        // 입구 = (x, y, z) 로컬, 방향 = (0, dy, dz)
        const HoleAxis ax = makeAxis(QVector3D(float(ah.x), float(ah.entry.x()), float(ah.entry.y())),
                                     QVector3D(0.0f, float(ah.dir.x()), float(ah.dir.y())));
        addHoleRevolveAxis(cg.tris, cs.prof, ax, f);
        addHoleThreadPart(threads, partName, threadNo, cs, ax, f);
        // 3D 보기용 입구 표시 (보기 창은 커터를 빼지 않음) — 로컬에서 만들어 좌표 변환
        QVector<QVector3D> disc;
        addDiscAxis(disc, ax, -0.02, cs.entryR, true);
        if (ah.hole.throughHole)
            addDiscAxis(disc, ax, ah.thickness + 0.02, cs.drillR, false);
        for (int k = 0; k < disc.size(); ++k)
            mark.tris << f.map(disc.at(k).x(), disc.at(k).y(), disc.at(k).z());
    }
    if (!cg.tris.isEmpty()) {
        cutter.groups << cg;
        out << cutter;
    }
    out << threads;
    return out;
}

QString MechModel3D::summaryText(const Model3D &model)
{
    int parts = 0;
    for (int i = 0; i < model.meshes.size(); ++i)
        if (model.meshes.at(i).cutterFor.isEmpty())
            ++parts;
    if (parts == 0)
        return QStringLiteral("3D 로 만들 기계 부품이 없습니다. 기계 Tools(축·깊은홈볼베어링·육각볼트)로 새로 배치한 부품만 3D 가 됩니다.");
    return QStringLiteral("기계 부품 %1개 · 원 분할 %2 (Ø20 면 오차 %3 mm)")
        .arg(parts)
        .arg(radialSteps())
        .arg(facetError(10.0), 0, 'f', 4);
}
