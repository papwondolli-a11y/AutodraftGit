#include "boltgeometry.h"
#include "lineitem.h"
#include "circleitem.h"
#include "arcitem.h"
#include "caditem.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

const int kBoltThreadDiaLayerDataKey  = 20260910;
const int kBoltCenterlineLayerDataKey = 20260911;
const int kBoltPhantomLayerDataKey    = 20260912;

QPointF fromLisp(double lispX, double lispY) {
    return QPointF(lispX, -lispY);
}

bool circumcenter3(const QPointF &A, const QPointF &B, const QPointF &C, QPointF &outCenter) {
    double D = 2.0 * (A.x()*(B.y()-C.y()) + B.x()*(C.y()-A.y()) + C.x()*(A.y()-B.y()));
    if (std::abs(D) < 1e-9) return false;

    double A2 = A.x()*A.x() + A.y()*A.y();
    double B2 = B.x()*B.x() + B.y()*B.y();
    double C2 = C.x()*C.x() + C.y()*C.y();

    double ux = (A2*(B.y()-C.y()) + B2*(C.y()-A.y()) + C2*(A.y()-B.y())) / D;
    double uy = (A2*(C.x()-B.x()) + B2*(A.x()-C.x()) + C2*(B.x()-A.x())) / D;
    outCenter = QPointF(ux, uy);
    return true;
}

QPointF localToWorld(const QPointF &local, const QPointF &origin, double angleDeg) {
    double rad = qDegreesToRadians(angleDeg);
    double c = qCos(rad);
    double s = qSin(rad);
    double worldX = origin.x() + local.x() * s + local.y() * c;
    double worldY = origin.y() - local.x() * c + local.y() * s;
    return QPointF(worldX, worldY);
}

void applyBoltLineKindStyle(CadItem *item, BoltLineKind kind) {
    QPen p = item->pen();
    switch (kind) {
    case BoltLineKind::ThreadIndicator:
        p.setStyle(Qt::SolidLine); // Lisp 원본도 실선 — 굵기만 얇게(아래)
        p.setWidthF(std::max(0.1, p.widthF() * 0.5));
        break;
    case BoltLineKind::Object:
    case BoltLineKind::Centerline:
    case BoltLineKind::Phantom:
    default:
        break; // 이 스타일은 placeItemAndReset()에서 레이어 펜으로 다시 덮어쓴다
    }
    item->setPen(p);
}

ArcItem *makeMinorBoltArcItem(const QPointF &center, const QPointF &a, const QPointF &b) {
    double aa = qRadiansToDegrees(qAtan2(a.y()-center.y(), a.x()-center.x()));
    double ab = qRadiansToDegrees(qAtan2(b.y()-center.y(), b.x()-center.x()));
    double span = ab - aa; while (span < 0) span += 360.0; while (span > 360.0) span -= 360.0;
    if (span <= 180.0) return new ArcItem(MyVector(center), MyVector(a), MyVector(b));
    return new ArcItem(MyVector(center), MyVector(b), MyVector(a));
}

QPainterPath buildBoltWorldPath(const QList<BoltSegment> &segments,
                                const QList<BoltArcSpec> &arcs,
                                const QList<BoltCircleSpec> &circles,
                                const QPointF &origin, double angleDeg) {
    QPainterPath path;
    for (const BoltSegment &seg : segments) {
        path.moveTo(localToWorld(seg.a, origin, angleDeg));
        path.lineTo(localToWorld(seg.b, origin, angleDeg));
    }
    for (const BoltArcSpec &arc : arcs) {
        double aA = qRadiansToDegrees(qAtan2(arc.a.y()-arc.center.y(), arc.a.x()-arc.center.x()));
        double aB = qRadiansToDegrees(qAtan2(arc.b.y()-arc.center.y(), arc.b.x()-arc.center.x()));
        double span = aB - aA; while (span < 0) span += 360.0; while (span > 360.0) span -= 360.0;
        double startAngle = aA;
        if (span > 180.0) { startAngle = aB; span = 360.0 - span; }
        double radius = std::hypot(arc.a.x()-arc.center.x(), arc.a.y()-arc.center.y());

        const int steps = 16;
        for (int i = 0; i <= steps; ++i) {
            double ang = qDegreesToRadians(startAngle + span * i / steps);
            QPointF localPt(arc.center.x() + radius*qCos(ang), arc.center.y() + radius*qSin(ang));
            QPointF worldPt = localToWorld(localPt, origin, angleDeg);
            if (i == 0) path.moveTo(worldPt);
            else path.lineTo(worldPt);
        }
    }
    for (const BoltCircleSpec &c : circles) {
        QPointF wc = localToWorld(c.center, origin, angleDeg);
        path.addEllipse(wc, c.radius, c.radius);
    }
    return path;
}

QList<QGraphicsItem *> buildBoltGraphicsItems(const QList<BoltSegment> &segments,
                                              const QList<BoltArcSpec> &arcs,
                                              const QList<BoltCircleSpec> &circles,
                                              const QPointF &origin, double angleDeg,
                                              bool gripsEnabled) {
    QList<QGraphicsItem *> items;

    for (const BoltSegment &seg : segments) {
        QPointF wa = localToWorld(seg.a, origin, angleDeg);
        QPointF wb = localToWorld(seg.b, origin, angleDeg);
        LineItem *li = new LineItem(MyVector(wa), MyVector(wb));
        applyBoltLineKindStyle(li, seg.kind);
        li->setData(kBoltThreadDiaLayerDataKey, seg.isThreadDiaLayer);
        li->setData(kBoltCenterlineLayerDataKey, seg.kind == BoltLineKind::Centerline);
        li->setData(kBoltPhantomLayerDataKey, seg.kind == BoltLineKind::Phantom);
        li->setGripsEnabled(gripsEnabled);
        items.append(li);
    }

    for (const BoltArcSpec &arc : arcs) {
        QPointF wc = localToWorld(arc.center, origin, angleDeg);
        QPointF wa = localToWorld(arc.a, origin, angleDeg);
        QPointF wb = localToWorld(arc.b, origin, angleDeg);
        ArcItem *ai = makeMinorBoltArcItem(wc, wa, wb);
        applyBoltLineKindStyle(ai, arc.kind);
        ai->setData(kBoltThreadDiaLayerDataKey, arc.isThreadDiaLayer);
        ai->setData(kBoltPhantomLayerDataKey, arc.kind == BoltLineKind::Phantom);
        ai->setGripsEnabled(gripsEnabled);
        items.append(ai);
    }

    for (const BoltCircleSpec &c : circles) {
        QPointF wc = localToWorld(c.center, origin, angleDeg);
        CircleItem *ci = new CircleItem(MyVector(wc), c.radius);
        applyBoltLineKindStyle(ci, c.kind);
        ci->setData(kBoltPhantomLayerDataKey, c.kind == BoltLineKind::Phantom);
        ci->setGripsEnabled(gripsEnabled);
        items.append(ci);
    }

    return items;
}
