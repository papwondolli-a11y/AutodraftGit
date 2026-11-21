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

bool HoleFeature::xRange(const QList<RevolveLoop> &loops, double &x0, double &x1) const
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
    const double lo = xs.first(), hi = xs.last();
    if (hole.throughHole) {
        x0 = lo;
        x1 = hi;
    } else if (leftSide) {
        x0 = lo;
        x1 = std::min(hi, lo + std::max(0.0, hole.depth));
    } else {
        x1 = hi;
        x0 = std::max(lo, hi - std::max(0.0, hole.depth));
    }
    return x1 - x0 > 1e-6;
}
