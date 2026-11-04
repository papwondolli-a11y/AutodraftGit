#include "myhole.h"
#include "myarc.h"
#include "mycircle.h"
#include "myline.h"
#include "mymath.h"
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

std::vector<MyVector> MyHole::intersect(const MyEntity *other) const {
    if (!other)
        return {};

    // 구멍은 기하학적으로 원(지름)과 동일하게 취급
    MyCircle asCircle(position, diameter / 2.0);

    switch (other->type()) {
    case EntityType::Line: {
        const MyLine *l = static_cast<const MyLine *>(other);
        return MyMath::intersectLineCircle(*l, asCircle);
    }
    case EntityType::Circle: {
        const MyCircle *c = static_cast<const MyCircle *>(other);
        return MyMath::intersectCircleCircle(asCircle, *c);
    }
    case EntityType::Arc: {
        const MyArc *a = static_cast<const MyArc *>(other);
        return MyMath::intersectCircleArc(asCircle, *a);
    }
    default:
        return {};
    }
}

MyVector MyHole::getNearestPoint(const MyVector &pos) const {
    MyCircle asCircle(position, diameter / 2.0);
    return MyMath::getNearestPointOnCircle(pos, asCircle);
}

bool MyHole::parseThreadSpec(const QString &spec, double &majorDiameter, double &pitch) {
    static const QRegularExpression re(QStringLiteral("^M?\\s*(\\d+(?:\\.\\d+)?)\\s*[xX]\\s*(\\d+(?:\\.\\d+)?)\\s*$"));
    QRegularExpressionMatch m = re.match(spec.trimmed());
    if (!m.hasMatch()) return false;

    majorDiameter = m.captured(1).toDouble();
    pitch = m.captured(2).toDouble();
    return majorDiameter > 0.0 && pitch > 0.0;
}

double MyHole::threadMajorRadius() const {
    double r = diameter / 2.0;
    double majorDia = 0.0, pitch = 0.0;
    if (parseThreadSpec(threadSpec, majorDia, pitch)) {
        return majorDia / 2.0;
    }
    return r / 0.85;
}

double MyHole::suggestedCbDiameter() const {
    double majorDia = 0.0, pitch = 0.0;
    if (isThreaded && parseThreadSpec(threadSpec, majorDia, pitch)) {
        // ISO 4762 (호칭지름, 카운터보어 지름)
        static const double kTable[][2] = {
            {3, 6.5}, {4, 8}, {5, 10}, {6, 11}, {8, 15}, {10, 18}, {12, 20},
            {14, 24}, {16, 26}, {20, 33}, {24, 40}, {30, 50}};
        for (const auto &row : kTable) {
            if (std::abs(row[0] - majorDia) < 1e-6) return row[1];
        }
        return majorDia * 1.75;
    }
    return diameter * 1.8;
}

double MyHole::suggestedCsDiameter() const {
    double majorDia = 0.0, pitch = 0.0;
    if (isThreaded && parseThreadSpec(threadSpec, majorDia, pitch)) {
        // ISO 10642 접시머리 이론 머리지름 ≈ 2.24 × 호칭지름
        return majorDia * 2.24;
    }
    return diameter * 1.8;
}

double MyHole::outerRadius() const {
    double r = diameter / 2.0;
    if (isThreaded) r = std::max(r, threadMajorRadius());
    if (hasCounterbore) r = std::max(r, cbDiameter / 2.0);
    if (hasCountersink) r = std::max(r, csDiameter / 2.0);
    return r;
}
