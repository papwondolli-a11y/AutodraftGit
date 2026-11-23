#include "holefeature.h"

#include <algorithm>
#include <cmath>

double HoleFeature::radiusFromAxis() const
{
    return std::hypot(y, z);
}

bool HoleFeature::openToward(bool leftSideView) const
{
    return hole.throughHole || (leftSideView == leftSide);
}

bool HoleFeature::materialRange(const QList<RevolveLoop> &loops, double &lo, double &hi) const
{
    // 구멍 중심 높이 R 의 수평선이 재료를 지나는 구간
    const double R = radiusFromAxis();
    QList<double> xs;
    for (int i = 0; i < loops.size(); ++i) {
        const QPolygonF &p = loops.at(i).pts;
        for (int k = 0; k < p.size(); ++k) {
            const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
            if ((a.y() > R) == (b.y() > R))
                continue;
            xs << a.x() + (R - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
        }
    }
    std::sort(xs.begin(), xs.end());
    if (xs.size() < 2)
        return false;
    lo = xs.first();
    hi = xs.last();
    return hi - lo > 1e-6;
}

bool HoleFeature::xRange(const QList<RevolveLoop> &loops, double &x0, double &x1) const
{
    double lo = 0.0, hi = 0.0;
    if (!materialRange(loops, lo, hi))
        return false;
    const double depth = drillDepth(hi - lo);
    if (hole.throughHole) {
        x0 = lo;
        x1 = hi;
    } else if (leftSide) {
        x0 = lo;
        x1 = std::min(hi, lo + depth);
    } else {
        x1 = hi;
        x0 = std::max(lo, hi - depth);
    }
    return x1 - x0 > 1e-6;
}

std::vector<QPointF> HoleFeature::cutProfile(const MyHole &hole, double thickness, double over, double *headDepth)
{
    std::vector<QPointF> p;
    const double curR = hole.diameter * 0.5;
    if (curR <= 1e-9)
        return p;
    double totalDepth = hole.throughHole ? thickness : hole.depth;
    if (totalDepth <= 0.0)
        totalDepth = hole.diameter;
    p.push_back(QPointF(-over, 0.0));
    double cur = 0.0;
    bool started = false;
    // 1) 카운터보어
    if (hole.hasCounterbore && hole.cbDiameter > hole.diameter) {
        const double cbR = hole.cbDiameter * 0.5;
        const double cbBottom = std::min(std::max(0.0, hole.cbDepth), totalDepth);
        p.push_back(QPointF(-over, cbR));
        p.push_back(QPointF(cbBottom, cbR));
        cur = cbBottom;
        started = true;
    }
    // 2) 카운터싱크 (입구 밖으로 연장)
    if (hole.hasCountersink && hole.csDiameter > hole.diameter) {
        const double csR = hole.csDiameter * 0.5;
        const double half = std::clamp(hole.csAngle, 10.0, 170.0) * 0.5 * 3.14159265358979323846 / 180.0;
        const double depth = std::min((csR - curR) / std::tan(half), std::max(0.0, totalDepth - cur));
        if (!started) {
            p.push_back(QPointF(-over, csR + over * std::tan(half)));
            started = true;
        } else {
            p.push_back(QPointF(cur, csR));
        }
        cur += depth;
        p.push_back(QPointF(cur, std::max(curR, csR - depth * std::tan(half))));
    }
    if (!started)
        p.push_back(QPointF(-over, curR));
    else
        p.push_back(QPointF(cur, curR));
    if (headDepth)
        *headDepth = cur;
    // 3) 드릴 깊이
    if (hole.throughHole) {
        const double bottom = thickness + over;
        p.push_back(QPointF(bottom, curR));
        p.push_back(QPointF(bottom, 0.0));
    } else {
        HoleFeature tmp;
        tmp.hole = hole;
        const double bottom = std::max(cur, tmp.drillDepth(thickness));
        p.push_back(QPointF(bottom, curR));
        p.push_back(QPointF(bottom + curR / std::tan(59.0 * 3.14159265358979323846 / 180.0), 0.0));   // 드릴 끝 원뿔
    }
    return p;
}

QPainterPath HoleFeature::frontArea(const QPointF &top, const QPointF &dir, double thickness) const
{
    QPainterPath path;
    const std::vector<QPointF> prof = cutProfile(hole, thickness, 0.0);
    if (prof.size() < 3)
        return path;
    const double len = std::hypot(dir.x(), dir.y());
    if (len < 1e-12)
        return path;
    const QPointF d(dir.x() / len, dir.y() / len);
    const QPointF w(-d.y(), d.x());                      // 폭 방향
    QPolygonF poly;
    for (size_t i = 0; i < prof.size(); ++i)             // 한쪽 벽 (입구 → 바닥)
        poly << top + d * prof[i].x() + w * prof[i].y();
    for (size_t i = prof.size(); i-- > 0;)               // 반대쪽 벽 (바닥 → 입구)
        poly << top + d * prof[i].x() - w * prof[i].y();
    poly << poly.first();
    path.addPolygon(poly);
    path.closeSubpath();
    return path;
}

double HoleFeature::drillDepth(double thickness) const
{
    // 그리기 탭 "구멍 단면"(HoleSectionGenerator)·3D 커터와 같은 규칙
    if (hole.throughHole)
        return thickness;
    double totalDepth = hole.depth > 0.0 ? hole.depth : hole.diameter;
    // 카운터보어·카운터싱크 깊이 (입구부터)
    double cur = 0.0;
    const double curR = hole.diameter * 0.5;
    if (hole.hasCounterbore && hole.cbDiameter > hole.diameter)
        cur = std::min(std::max(0.0, hole.cbDepth), totalDepth);
    if (hole.hasCountersink && hole.csDiameter > hole.diameter) {
        const double half = std::clamp(hole.csAngle, 10.0, 170.0) * 0.5 * 3.14159265358979323846 / 180.0;
        cur += std::min((hole.csDiameter * 0.5 - curR) / std::tan(half), std::max(0.0, totalDepth - cur));
    }
    if (hole.isThreaded) {
        // 막힌 탭: 드릴 깊이 = 나사 깊이 + 탭 드릴 여유 (호칭 M12 이상 3, 미만 2) — depth 필드와 무관
        double majorDia = 0.0, pitch = 0.0;
        const bool ok = MyHole::parseThreadSpec(hole.threadSpec, majorDia, pitch);
        return cur + std::max(0.0, hole.threadDepth) + ((ok && majorDia >= 12.0) ? 3.0 : 2.0);
    }
    return std::max(cur, totalDepth);
}
