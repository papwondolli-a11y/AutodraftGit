#ifndef BEARINGROTATION_H
#define BEARINGROTATION_H

#include "mydimension.h"
#include "myvector.h"

#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QTransform>
#include <QtMath>
#include <vector>

// 베어링 3종(깊은홈볼·테이퍼롤러·스러스트) 공용 — 기준점 o 둘러 deg(도, 반시계) 회전하는 보조 함수.
namespace BearingRotation {

struct Rot {
    QPointF o;
    double  cs;
    double  sn;
    Rot(const QPointF &origin, double deg)
        : o(origin), cs(qCos(qDegreesToRadians(deg))), sn(qSin(qDegreesToRadians(deg))) {}
    QPointF pt(const QPointF &p) const {
        const double dx = p.x() - o.x();
        const double dy = p.y() - o.y();
        return QPointF(o.x() + cs * dx - sn * dy, o.y() + sn * dx + cs * dy);
    }
};

inline void rotVec(MyVector &v, const Rot &r) {
    const QPointF q = r.pt(QPointF(v.x, v.y));
    v.x = q.x();
    v.y = q.y();
}

inline void rotVecs(std::vector<MyVector> &list, const Rot &r) {
    for (size_t i = 0; i < list.size(); ++i) rotVec(list[i], r);
}

inline void rotLine(QLineF &l, const Rot &r) { l = QLineF(r.pt(l.p1()), r.pt(l.p2())); }

inline void rotLines(QList<QLineF> &list, const Rot &r) {
    for (int i = 0; i < list.size(); ++i) rotLine(list[i], r);
}

inline void rotPath(QPainterPath &p, const Rot &r) {
    const QTransform t(r.cs, r.sn, -r.sn, r.cs,
                       r.o.x() - r.cs * r.o.x() + r.sn * r.o.y(),
                       r.o.y() - r.sn * r.o.x() - r.cs * r.o.y());
    p = t.map(p);
}

inline void rotDim(MyDimension &d, const Rot &r, double deg) {
    rotVec(d.start, r);
    rotVec(d.end, r);
    rotVec(d.dimLine, r);
    rotVec(d.end2, r);
    if (d.useFixedAngle) d.fixedAngle += deg;
}

inline void rotDims(QList<MyDimension> &list, const Rot &r, double deg) {
    for (int i = 0; i < list.size(); ++i) rotDim(list[i], r, deg);
}

} // namespace BearingRotation

#endif // BEARINGROTATION_H
