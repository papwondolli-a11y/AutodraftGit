#include "model3dmesh.h"
#include "buildingmodel3d.h"

#include <algorithm>
#include <vector>

namespace {

// 수평 띠의 한 변: 가운데 X 와 아래/위 X
struct SlabCut {
    double xm;
    double x0;
    double x1;
};

struct SlabCutLess {
    bool operator()(const SlabCut &a, const SlabCut &b) const { return a.xm < b.xm; }
};

} // namespace

QVector<QPointF> Model3DMesh::triangulate(const QList<QPolygonF> &rings)
{
    QVector<QPointF> tris;
    std::vector<double> ys;
    for (int r = 0; r < rings.size(); ++r)
        for (int k = 0; k < rings.at(r).size(); ++k)
            ys.push_back(rings.at(r).at(k).y());
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

    SlabCutLess less;
    for (size_t i = 0; i + 1 < ys.size(); ++i) {
        const double y0 = ys[i], y1 = ys[i + 1];
        if (y1 - y0 < 1e-6)
            continue;
        const double ym = (y0 + y1) / 2.0;
        std::vector<SlabCut> cuts;
        for (int r = 0; r < rings.size(); ++r) {
            const QPolygonF &ring = rings.at(r);
            for (int k = 0; k < ring.size(); ++k) {
                const QPointF p = ring.at(k);
                const QPointF q = ring.at((k + 1) % ring.size());
                if ((p.y() < ym) == (q.y() < ym))
                    continue;
                const double dy = q.y() - p.y();
                SlabCut c;
                c.x0 = p.x() + (y0 - p.y()) / dy * (q.x() - p.x());
                c.x1 = p.x() + (y1 - p.y()) / dy * (q.x() - p.x());
                c.xm = p.x() + (ym - p.y()) / dy * (q.x() - p.x());
                cuts.push_back(c);
            }
        }
        std::sort(cuts.begin(), cuts.end(), less);
        for (size_t k = 0; k + 1 < cuts.size(); k += 2) {
            const QPointF a(cuts[k].x0, y0);
            const QPointF b(cuts[k + 1].x0, y0);
            const QPointF c(cuts[k + 1].x1, y1);
            const QPointF d(cuts[k].x1, y1);
            tris << a << b << c;
            tris << a << c << d;
        }
    }
    return tris;
}

bool Model3DMesh::glassPane(const Model3DOpening &o, QPointF &a, QPointF &b, double &z0, double &z1)
{
    if (!o.isWindow || !o.hosted || o.footprint.size() != 4)
        return false;
    a = (o.footprint.at(0) + o.footprint.at(3)) * 0.5;
    b = (o.footprint.at(1) + o.footprint.at(2)) * 0.5;
    z0 = o.hostZ0 + o.sill;
    z1 = std::min(o.hostZ1, z0 + o.height);
    return z1 > z0;
}
