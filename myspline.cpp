#include "myspline.h"
#include "myline.h"
#include "myarc.h"
#include "mycircle.h"
#include "mypolyline.h"
#include "mymath.h"
#include <cmath>
#include <algorithm>

MySpline::MySpline() : degree(3), isClosed(false) {}

MySpline::MySpline(const std::vector<MyVector> &cp, int d, bool closed)
    : controlPoints(cp), degree(d), isClosed(closed) {}

std::vector<MyVector> MySpline::getPoints(int segmentsPerSegment) const {
    std::vector<MyVector> pts;
    int n = (int)controlPoints.size();
    if (n < 2) return pts;

    if (degree == 1 || n == 2) {
        for(const auto& p : controlPoints) pts.push_back(p);
        return pts;
    }

    int segments = isClosed ? n : n - 1;
    for (int i = 0; i < segments; ++i) {
        MyVector p1 = controlPoints[i % n];
        MyVector p2 = controlPoints[(i + 1) % n];
        MyVector p0 = isClosed ? controlPoints[(i - 1 + n) % n] : ((i == 0) ? p1 : controlPoints[i - 1]);
        MyVector p3 = isClosed ? controlPoints[(i + 2) % n] : ((i + 2 >= n) ? p2 : controlPoints[i + 2]);

        for (int j = 0; j < segmentsPerSegment; ++j) {
            double t = (double)j / segmentsPerSegment;
            double t2 = t * t;
            double t3 = t2 * t;

            // Catmull-Rom formula
            double f1 = -0.5 * t3 + t2 - 0.5 * t;
            double f2 = 1.5 * t3 - 2.5 * t2 + 1.0;
            double f3 = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
            double f4 = 0.5 * t3 - 0.5 * t2;

            pts.push_back(p0 * f1 + p1 * f2 + p2 * f3 + p3 * f4);
        }
    }
    pts.push_back(controlPoints[isClosed ? 0 : n - 1]);
    return pts;
}

std::vector<MyVector> MySpline::intersect(const MyEntity *other) const {
    if (!other) return {};
    if (other->type() == EntityType::Line) {
        return MyMath::intersectLineSpline(*static_cast<const MyLine*>(other), *this);
    } else if (other->type() == EntityType::Arc) {
        return MyMath::intersectArcSpline(*static_cast<const MyArc*>(other), *this);
    } else if (other->type() == EntityType::Circle) {
        const MyCircle* c = static_cast<const MyCircle*>(other);
        MyArc tempArc(c->center, c->radius, 0, 360);
        return MyMath::intersectArcSpline(tempArc, *this);
    } else if (other->type() == EntityType::Spline) {
        return MyMath::intersectSplineSpline(*this, *static_cast<const MySpline*>(other));
    } else if (other->type() == EntityType::Polyline) {
        return MyMath::intersectPolylineSpline(*static_cast<const MyPolyline*>(other), *this);
    }
    return {};
}

MyVector MySpline::getNearestPoint(const MyVector &pos) const {
    std::vector<MyVector> pts = getPoints(20);
    if (pts.empty()) return pos;
    
    double minDist = 1e18;
    MyVector bestP = pts[0];
    for (const auto& p : pts) {
        double d = p.distanceTo(pos);
        if (d < minDist) {
            minDist = d;
            bestP = p;
        }
    }
    return bestP;
}
