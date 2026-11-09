#include "shaftbuilder.h"

#include <QtMath>

namespace {

const double kEps = 1e-9;

// 연속 중복점과 일직선상의 중간점을 제거한다 (열린 점열 기준).
QList<QPointF> simplify(const QList<QPointF> &pts) {
    QList<QPointF> out;
    for (int i = 0; i < pts.size(); ++i) {
        if (!out.isEmpty() && qAbs(out.last().x() - pts.at(i).x()) < kEps &&
            qAbs(out.last().y() - pts.at(i).y()) < kEps)
            continue;
        out.append(pts.at(i));
    }
    bool changed = true;
    while (changed && out.size() >= 3) {
        changed = false;
        for (int i = 1; i + 1 < out.size(); ++i) {
            const QPointF a = out.at(i - 1);
            const QPointF b = out.at(i);
            const QPointF c = out.at(i + 1);
            const double cross = (b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x());
            const double dot   = (b.x() - a.x()) * (c.x() - b.x()) + (b.y() - a.y()) * (c.y() - b.y());
            if (qAbs(cross) < kEps && dot > 0.0) {
                out.removeAt(i);
                changed = true;
                break;
            }
        }
    }
    return out;
}

MyDimension makeVerticalDim(const QPointF &top, const QPointF &bottom, double lineX) {
    MyDimension d{MyVector(top), MyVector(bottom), MyVector(QPointF(lineX, (top.y() + bottom.y()) * 0.5)),
                  QString()};
    d.dimensionType = DimensionType::Linear;
    d.useFixedAngle = true;
    d.fixedAngle    = 90.0;
    return d;
}

MyDimension makeHorizontalDim(const QPointF &a, const QPointF &b, double lineY) {
    MyDimension d{MyVector(a), MyVector(b), MyVector(QPointF((a.x() + b.x()) * 0.5, lineY)), QString()};
    d.dimensionType = DimensionType::Linear;
    d.useFixedAngle = true;
    d.fixedAngle    = 0.0;
    return d;
}

// 지름 치수 문구: 나사부 끝 구간은 "M호칭경", 그 외는 "Ø" + 끼워맞춤 등급
void applyDiameterText(MyDimension &d, const ShaftParams &params, int index, const ShaftSegment &s) {
    const bool threadLeft  = (index == 0) && params.leftEnd.type == ShaftEndType::Thread;
    const bool threadRight = (index == params.segments.size() - 1) && params.rightEnd.type == ShaftEndType::Thread;
    if (threadLeft || threadRight) {
        d.prefix = QStringLiteral("M");
        return;
    }
    d.prefix = QStringLiteral("Ø");
    if (s.showFit && !s.fit.isEmpty()) {
        // 화면 표기는 prefix+값+suffix 한 경로뿐이므로 등급기호는 꼬리말(suffix)에 넣는다.
        d.tolType = ToleranceType::FitCode;
        d.fitCode = s.fit;
        d.suffix  = s.fit;
    }
}

} // namespace

QStringList ShaftBuilder::validate(const ShaftParams &params) {
    QStringList problems;
    if (params.segments.isEmpty()) {
        problems << QObject::tr("구간이 하나도 없습니다.");
        return problems;
    }
    for (int i = 0; i < params.segments.size(); ++i) {
        const ShaftSegment &s = params.segments.at(i);
        const int no = i + 1;
        if (s.diameter <= 0.0 || s.length <= 0.0) {
            problems << QObject::tr("구간 %1: 지름과 길이는 0보다 커야 합니다.").arg(no);
            continue;
        }
        if (s.chamferLeft < 0.0 || s.chamferRight < 0.0 ||
            s.chamferLeft + s.chamferRight > s.length + kEps ||
            s.chamferLeft >= s.diameter * 0.5 || s.chamferRight >= s.diameter * 0.5) {
            problems << QObject::tr("구간 %1: 모따기가 구간 길이 또는 반지름보다 큽니다.").arg(no);
        }
    }
    if (!problems.isEmpty()) return problems;

    // 끝단 형상 검사: (끝 구간, 끝단 설정, 이름)
    for (int side = 0; side < 2; ++side) {
        const ShaftEnd &e = (side == 0) ? params.leftEnd : params.rightEnd;
        const ShaftSegment &s = (side == 0) ? params.segments.first() : params.segments.last();
        const QString name = (side == 0) ? QObject::tr("왼쪽 끝") : QObject::tr("오른쪽 끝");
        const double r = s.diameter * 0.5;

        if (e.type == ShaftEndType::Keyway) {
            const ShaftKeyway &k = e.keyway;
            if (k.upper <= 0.0 || k.lower <= 0.0 || k.length <= 0.0) {
                problems << QObject::tr("%1 키홈: 길이·상단·하단은 0보다 커야 합니다.").arg(name);
            } else {
                if (k.length > s.length + kEps)
                    problems << QObject::tr("%1 키홈: 길이가 끝 구간 길이(%2)보다 큽니다.").arg(name).arg(s.length);
                if (k.length < (k.upper + k.lower) * 0.5 - kEps)
                    problems << QObject::tr("%1 키홈: 길이가 홈 폭의 절반(반원 끝)보다 작습니다.").arg(name);
                if (k.center + k.upper >= r || k.center - k.lower <= -r)
                    problems << QObject::tr("%1 키홈: 홈이 축 지름 밖으로 나갑니다.").arg(name);
                if (k.showSection && (k.sectionDepth <= kEps || k.sectionDepth >= r * 0.8))
                    problems << QObject::tr("%1 키홈 단면: 홈 깊이는 0보다 크고 반지름의 80%%(%2) 미만이어야 합니다.")
                                    .arg(name).arg(r * 0.8);
            }
        } else if (e.type == ShaftEndType::Thread) {
            const double dp = 0.541266 * ShaftParams::effectivePitch(e.thread, s.diameter);
            if (e.thread.length > s.length + kEps)
                problems << QObject::tr("%1 나사부: 길이가 끝 구간 길이(%2)보다 큽니다.").arg(name).arg(s.length);
            if (e.thread.length < dp - kEps || dp >= r)
                problems << QObject::tr("%1 나사부: 길이가 너무 짧거나 피치가 지름에 비해 너무 큽니다.").arg(name);
        }
    }
    return problems;
}

// 나사부 끝에는 골 깊이(dp)만큼의 모따기가 자동으로 들어간다 (육각볼트 끝단과 같음).
ShaftParams ShaftBuilder::withThreadChamfers(const ShaftParams &params) {
    ShaftParams eff = params;
    if (eff.segments.isEmpty()) return eff;
    if (eff.leftEnd.type == ShaftEndType::Thread) {
        ShaftSegment &s = eff.segments.first();
        s.chamferLeft = 0.541266 * ShaftParams::effectivePitch(eff.leftEnd.thread, s.diameter);
    }
    if (eff.rightEnd.type == ShaftEndType::Thread) {
        ShaftSegment &s = eff.segments.last();
        s.chamferRight = 0.541266 * ShaftParams::effectivePitch(eff.rightEnd.thread, s.diameter);
    }
    return eff;
}

void ShaftBuilder::buildEnds(const ShaftParams &params, const QPointF &origin, ShaftGeometry &g) {
    const double y0 = origin.y();
    const double xTotal = origin.x() + params.totalLength();
    const double rmax = params.maxDiameter() * 0.5;

    for (int side = 0; side < 2; ++side) {
        const ShaftEnd &e = (side == 0) ? params.leftEnd : params.rightEnd;
        if (e.type == ShaftEndType::None) continue;
        const ShaftSegment &s = (side == 0) ? params.segments.first() : params.segments.last();
        const double r  = s.diameter * 0.5;
        const double xe = (side == 0) ? origin.x() : xTotal;   // 끝면 x
        const double dir = (side == 0) ? 1.0 : -1.0;           // 끝면에서 안쪽으로 가는 방향

        if (e.type == ShaftEndType::Thread) {
            const double dp = 0.541266 * ShaftParams::effectivePitch(e.thread, s.diameter);
            const double rm = r - dp;                           // 골지름 반지름
            const double xs = xe + dir * e.thread.length;       // 나사 시작선
            g.threadLines.append(QLineF(xe, y0 + rm, xs, y0 + rm));
            g.threadLines.append(QLineF(xe, y0 - rm, xs, y0 - rm));
            // 나사가 구간 일부만 차지하면 시작선(외형선)을 전체 폭으로 그린다 (부분나사 표현)
            if (e.thread.length < s.length - kEps)
                g.edgeLines.append(QLineF(xs, y0 - r, xs, y0 + r));
        } else if (e.type == ShaftEndType::Keyway) {
            const ShaftKeyway &k = e.keyway;
            const double yTop = y0 + k.center + k.upper;
            const double yBot = y0 + k.center - k.lower;
            const double rad  = (k.upper + k.lower) * 0.5;
            const double xin  = xe + dir * k.length;            // 홈 안쪽 끝(반원 정점)
            const double xa   = xin - dir * rad;                // 직선부가 끝나고 반원이 시작하는 x

            ShaftKeywayShape shape;
            shape.pts.push_back(MyVector(xe, yTop));
            shape.pts.push_back(MyVector(xa, yTop));
            shape.pts.push_back(MyVector(xa, yBot));
            shape.pts.push_back(MyVector(xe, yBot));
            // 위→아래로 진행할 때 안쪽(dir 방향)으로 볼록한 반원: 왼쪽 끝은 -1, 오른쪽 끝은 +1
            shape.bulges = {0.0, -dir, 0.0, 0.0};
            if (!k.showSection) g.keyways.append(shape);

            // 단면 표시일 때 홈의 x 범위는 위치(앞/중앙/뒤)에 따라 구간 안에서 정해진다.
            const double xi  = xe + dir * s.length;             // 끝 구간 안쪽 경계
            const double xl  = qMin(xe, xi);                    // 구간 왼쪽 x
            const double xr  = qMax(xe, xi);                    // 구간 오른쪽 x
            double sa = qMin(xe, xin);                          // 홈 범위 [sa, sb]
            if (k.showSection) {
                const int pos = (k.sectionPos < 0) ? (side == 0 ? 0 : 2) : k.sectionPos;
                if (pos == 0)      sa = xl;
                else if (pos == 1) sa = (xl + xr - k.length) * 0.5;
                else               sa = xr - k.length;
            }
            const double sb = sa + k.length;

            // 키홈 길이 치수 (도형 위쪽)
            if (params.lengthDims) {
                const double lineY = y0 + rmax + params.dimGap;
                g.lengthDims.append(makeHorizontalDim(QPointF(sa, y0 + r), QPointF(sb, y0 + r), lineY));
            }

            // 단면 표시: 위쪽 절반을 길이 방향 단면으로 보인다 (사선 해칭 + 파단선, 홈은 직사각형 노치)
            if (k.showSection) {
                const double yt  = y0 + r;                          // 축 윗면
                const double yf  = yt - k.sectionDepth;             // 홈 바닥
                const bool wallL = sa > xl + kEps;                  // 끝면에 닿지 않는 쪽만 벽을 그린다
                const bool wallR = sb < xr - kEps;
                g.edgeLines.append(QLineF(sa, yf, sb, yf));
                if (wallL) g.edgeLines.append(QLineF(sa, yf, sa, yt));
                if (wallR) g.edgeLines.append(QLineF(sb, yf, sb, yt));

                // 파단선(물결): 구간 전체 폭, 중심선 근처 (왼쪽→오른쪽)
                ShaftKeywayShape brk;
                const int N = 24;
                for (int i = 0; i <= N; ++i) {
                    const double t = double(i) / N;
                    const double x = xl + (xr - xl) * t;
                    const double y = y0 + 0.05 * r + 0.12 * r * qSin(2.0 * M_PI * t);
                    brk.pts.push_back(MyVector(x, y));
                    brk.bulges.push_back(0.0);
                }
                g.breakLines.append(brk);

                // 해칭 영역: 윗면(홈 노치 제외) ~ 파단선
                QPainterPath hp;
                if (wallL) { hp.moveTo(xl, yt); hp.lineTo(sa, yt); hp.lineTo(sa, yf); }
                else         hp.moveTo(xl, yf);
                hp.lineTo(sb, yf);
                if (wallR) { hp.lineTo(sb, yt); hp.lineTo(xr, yt); }
                else         hp.lineTo(xr, yf);
                for (int i = N; i >= 0; --i) hp.lineTo(brk.pts[i].x, brk.pts[i].y);
                hp.closeSubpath();
                g.hatchPaths.append(hp);
            }
        }
    }
}

std::vector<MyVector> ShaftBuilder::buildOutline(const ShaftParams &params, const QPointF &origin,
                                                 QList<QLineF> &edgeLines) {
    // 위쪽 절반 윤곽(왼쪽→오른쪽)을 만든 뒤 아래쪽은 뒤집어 대칭으로 이어 붙인다.
    QList<QPointF> top;
    double x = origin.x();
    const double y0 = origin.y();

    for (int i = 0; i < params.segments.size(); ++i) {
        const ShaftSegment &s = params.segments.at(i);
        const double r  = s.diameter * 0.5;
        const double x1 = x + s.length;

        // 왼쪽 끝: 이전 구간의 오른쪽 모따기 끝점 → 이 구간의 왼쪽 모따기 시작점
        if (i > 0) {
            const ShaftSegment &p = params.segments.at(i - 1);
            const double rp = p.diameter * 0.5;
            top.append(QPointF(x, y0 + rp - p.chamferRight));

            // 구간 경계선: 실루엣의 단(수직) 부분은 외형이 이미 그리므로,
            // 그 안쪽(작은 쪽 반지름까지)을 전체 높이 선으로 이어 구간을 분리한다.
            const double h = qMin(rp - p.chamferRight, r - s.chamferLeft);
            if (h > kEps)
                edgeLines.append(QLineF(x, y0 - h, x, y0 + h));
        }
        top.append(QPointF(x, y0 + r - s.chamferLeft));
        top.append(QPointF(x + s.chamferLeft, y0 + r));
        top.append(QPointF(x1 - s.chamferRight, y0 + r));

        // 모따기 경계의 전체 높이 수직선 (실제 도면에서 모따기 끝에 보이는 선)
        if (s.chamferLeft > kEps)
            edgeLines.append(QLineF(x + s.chamferLeft, y0 - r, x + s.chamferLeft, y0 + r));
        if (s.chamferRight > kEps)
            edgeLines.append(QLineF(x1 - s.chamferRight, y0 - r, x1 - s.chamferRight, y0 + r));

        x = x1;
    }
    const ShaftSegment &last = params.segments.last();
    top.append(QPointF(x, y0 + last.diameter * 0.5 - last.chamferRight));

    const QList<QPointF> half = simplify(top);

    std::vector<MyVector> outline;
    for (int i = 0; i < half.size(); ++i) outline.push_back(MyVector(half.at(i)));
    for (int i = half.size() - 1; i >= 0; --i)
        outline.push_back(MyVector(QPointF(half.at(i).x(), 2.0 * y0 - half.at(i).y())));
    return outline;
}

void ShaftBuilder::buildDiameterDims(const ShaftParams &params, const QPointF &origin,
                                     QList<MyDimension> &out) {
    const int n = params.segments.size();
    const double total = params.totalLength();
    const double y0 = origin.y();

    QList<double> xStart;
    double x = origin.x();
    for (int i = 0; i < n; ++i) {
        xStart.append(x);
        x += params.segments.at(i).length;
    }
    const double xEnd = x;

    // 왼쪽: 구간 순서대로 안쪽→바깥쪽, 오른쪽: 반대 순서
    int leftCount = 0;
    int rightCount = 0;
    QList<int> leftIdx, rightIdx;
    for (int i = 0; i < n; ++i) {
        const double mid = xStart.at(i) - origin.x() + params.segments.at(i).length * 0.5;
        if (mid <= total * 0.5) leftIdx.append(i);
        else                    rightIdx.prepend(i);
    }

    for (int k = 0; k < leftIdx.size(); ++k) {
        const int i = leftIdx.at(k);
        const ShaftSegment &s = params.segments.at(i);
        if (!s.dimDiameter) continue;
        const double r = s.diameter * 0.5;
        const double lineX = origin.x() - params.dimGap - leftCount * params.dimStep;
        ++leftCount;
        MyDimension d = makeVerticalDim(QPointF(xStart.at(i), y0 + r), QPointF(xStart.at(i), y0 - r), lineX);
        applyDiameterText(d, params, i, s);
        out.append(d);
    }
    for (int k = 0; k < rightIdx.size(); ++k) {
        const int i = rightIdx.at(k);
        const ShaftSegment &s = params.segments.at(i);
        if (!s.dimDiameter) continue;
        const double r = s.diameter * 0.5;
        const double xr = xStart.at(i) + s.length;
        const double lineX = xEnd + params.dimGap + rightCount * params.dimStep;
        ++rightCount;
        MyDimension d = makeVerticalDim(QPointF(xr, y0 + r), QPointF(xr, y0 - r), lineX);
        applyDiameterText(d, params, i, s);
        out.append(d);
    }
}

void ShaftBuilder::buildLengthDims(const ShaftParams &params, const QPointF &origin,
                                   QList<MyDimension> &out) {
    const int n = params.segments.size();
    const double y0 = origin.y();
    const double rmax = params.maxDiameter() * 0.5;

    // 각 경계 x 와, 그 경계에서 보이는 윤곽의 아래쪽 y (이웃 두 구간 중 큰 반지름)
    QList<double> bx;
    QList<double> by;
    double x = origin.x();
    for (int i = 0; i <= n; ++i) {
        const double rl = (i > 0)  ? params.segments.at(i - 1).diameter * 0.5 : 0.0;
        const double rr = (i < n)  ? params.segments.at(i).diameter * 0.5     : 0.0;
        bx.append(x);
        by.append(y0 - qMax(rl, rr));
        if (i < n) x += params.segments.at(i).length;
    }

    const double row1 = y0 - rmax - params.dimGap;
    const double row2 = row1 - params.dimStep;

    for (int i = 0; i < n; ++i) {
        if (!params.segments.at(i).dimLength) continue;
        MyDimension d = makeHorizontalDim(QPointF(bx.at(i), by.at(i)), QPointF(bx.at(i + 1), by.at(i + 1)), row1);
        const ShaftSegment &seg = params.segments.at(i);
        if (seg.lenTolUse) {
            // 편차 공차: 하한은 (표시 부호 반대) 값으로 저장된다 → 하한 편차 -0.02 는 tolLower=+0.02
            d.tolType  = ToleranceType::Deviation;
            d.tolUpper = seg.lenTolUpper;
            d.tolLower = -seg.lenTolLower;
        }
        out.append(d);
    }
    if (n >= 2) {
        out.append(makeHorizontalDim(QPointF(bx.first(), by.first()), QPointF(bx.last(), by.last()), row2));
    }
}

ShaftGeometry ShaftBuilder::build(const ShaftParams &params, const QPointF &origin) {
    ShaftGeometry g;
    if (!validate(params).isEmpty()) return g;

    const ShaftParams eff = withThreadChamfers(params);
    g.outline = buildOutline(eff, origin, g.edgeLines);
    buildEnds(params, origin, g);

    if (params.drawCenterLine) {
        g.hasCenterLine = true;
        g.centerLine = QLineF(origin.x() - params.centerExtend, origin.y(),
                              origin.x() + params.totalLength() + params.centerExtend, origin.y());
    }
    if (params.diameterDims) buildDiameterDims(params, origin, g.diameterDims);
    if (params.lengthDims)   buildLengthDims(params, origin, g.lengthDims);
    // buildEnds 가 만든 키홈 길이 치수는 이미 g.lengthDims 에 들어 있다 (buildLengthDims 는 뒤에 추가)
    return g;
}

QPainterPath ShaftBuilder::buildPath(const ShaftParams &params, const QPointF &origin) {
    QPainterPath path;
    const ShaftGeometry g = build(params, origin);
    if (g.outline.empty()) return path;

    path.moveTo(g.outline.front().x, g.outline.front().y);
    for (size_t i = 1; i < g.outline.size(); ++i) path.lineTo(g.outline[i].x, g.outline[i].y);
    path.closeSubpath();

    for (int i = 0; i < g.edgeLines.size(); ++i) {
        path.moveTo(g.edgeLines.at(i).p1());
        path.lineTo(g.edgeLines.at(i).p2());
    }
    for (int i = 0; i < g.threadLines.size(); ++i) {
        path.moveTo(g.threadLines.at(i).p1());
        path.lineTo(g.threadLines.at(i).p2());
    }
    for (int i = 0; i < g.keyways.size(); ++i) {
        const ShaftKeywayShape &k = g.keyways.at(i);
        if (k.pts.size() < 4) continue;
        // 미리보기: 직선부 + 안쪽 반원을 선분으로 근사
        const MyVector &a = k.pts[1];
        const MyVector &b = k.pts[2];
        const double rad = qAbs(a.y - b.y) * 0.5;
        const double cy = (a.y + b.y) * 0.5;
        const double dirOut = (k.bulges.size() > 1 && k.bulges[1] < 0.0) ? 1.0 : -1.0; // 안쪽 방향
        path.moveTo(k.pts[0].x, k.pts[0].y);
        path.lineTo(a.x, a.y);
        for (int s = 1; s <= 12; ++s) {
            const double t = M_PI * 0.5 - M_PI * s / 12.0;     // 위(+90°) → 아래(-90°)
            path.lineTo(a.x + dirOut * rad * qCos(t), cy + rad * qSin(t));
        }
        path.lineTo(k.pts[3].x, k.pts[3].y);
    }
    if (g.hasCenterLine) {
        path.moveTo(g.centerLine.p1());
        path.lineTo(g.centerLine.p2());
    }
    return path;
}
