#ifndef MYHATCH_H
#define MYHATCH_H

#include "myentity.h"
#include "myvector.h"
#include <QPainterPath>
#include <QPolygonF>
#include <limits>

class MyHatch : public MyEntity {
public:
    QPainterPath path;
    Qt::BrushStyle style;

    QString patternName;
    double patternScale;
    double patternAngle;

    MyHatch() : style(Qt::SolidPattern), patternScale(1.0), patternAngle(0.0) {}
    MyHatch(const QPainterPath& p, const QString& pName = "", double pScale = 1.0, double pAngle = 0.0)
        : path(p), style(Qt::SolidPattern), patternName(pName), patternScale(pScale), patternAngle(pAngle) {}

    EntityType type() const override { return EntityType::Hatch; }

    // [#13 주석] 해치 자체는 교차 연산이 복잡합니다.
    // 필요 시 path.intersected()를 활용하거나 경계 폴리곤 교차 검사를 구현하세요.
    std::vector<MyVector> intersect(const MyEntity* /*other*/) const override { return {}; }

    // [#13 수정] 스텁(pos 그대로 반환) 대신 실제 경계선상 최근점을 계산합니다.
    MyVector getNearestPoint(const MyVector& pos) const override {
        QPointF qpos = pos.toPointF();

        // 내부에 있으면 pos 자체가 가장 가까운 내부점
        if (path.contains(qpos)) return pos;

        QPointF nearest = qpos;
        double minDist = std::numeric_limits<double>::max();

        QList<QPolygonF> polys = path.toSubpathPolygons();
        for (int p = 0; p < polys.size(); ++p) {
            const QPolygonF &poly = polys[p];
            for (int i = 0; i < poly.size() - 1; ++i) {
                QPointF a = poly[i];
                QPointF b = poly[i + 1];
                double dx = b.x() - a.x();
                double dy = b.y() - a.y();
                double len2 = dx * dx + dy * dy;
                QPointF foot;
                if (len2 < 1e-12) {
                    foot = a;
                } else {
                    double t = ((qpos.x() - a.x()) * dx + (qpos.y() - a.y()) * dy) / len2;
                    t = (t < 0.0) ? 0.0 : (t > 1.0) ? 1.0 : t;
                    foot = QPointF(a.x() + t * dx, a.y() + t * dy);
                }
                double d = (qpos.x()-foot.x())*(qpos.x()-foot.x())
                         + (qpos.y()-foot.y())*(qpos.y()-foot.y());
                if (d < minDist) {
                    minDist = d;
                    nearest = foot;
                }
            }
        }
        return MyVector(nearest);
    }
};

#endif // MYHATCH_H
