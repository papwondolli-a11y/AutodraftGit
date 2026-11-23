#include "shaftpick.h"

#include "fittolerancecalculator.h"
#include "mechpartdata.h"
#include "mygraphicsscene.h"
#include "projectionview.h"
#include "scenesymbolregistry.h"

#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QtMath>
#include <cmath>

namespace {

ShaftPickInfo &lastInfo()
{
    static ShaftPickInfo s;
    return s;
}

// 편차 글 (+0.015 / −0.002, 0 은 0)
QString devText(double v)
{
    if (std::fabs(v) < 5e-7)
        return QStringLiteral("0");
    return (v > 0 ? QStringLiteral("+") : QStringLiteral("−")) + QString::number(std::fabs(v), 'f', 3);
}

QString num(double v)
{
    return QString::number(v, 'g', 6);
}

} // namespace

const ShaftPickInfo &ShaftPick::last()
{
    return lastInfo();
}

void ShaftPick::setLast(const ShaftPickInfo &info)
{
    lastInfo() = info;
}

void ShaftPick::clearLast()
{
    lastInfo() = ShaftPickInfo();
}

QString ShaftPick::diameterText(const ShaftSegment &sg, bool withDeviation)
{
    QString t = QStringLiteral("Ø") + num(sg.diameter);
    const QString code = sg.fit.trimmed();
    if (!code.isEmpty()) {
        t += QLatin1Char(' ') + code;
        if (withDeviation) {
            const FitToleranceResult r = FitToleranceCalculator::calculate(sg.diameter, code);
            if (r.valid)
                t += QStringLiteral(" (%1 / %2)").arg(devText(r.upper), devText(r.lower));
        }
    }
    return t;
}

QString ShaftPick::segmentSummary(const ShaftParams &p, int i)
{
    if (i < 0 || i >= p.segments.size())
        return QString();
    const ShaftSegment &sg = p.segments.at(i);
    QString s = diameterText(sg, false) + QStringLiteral(" · 길이 ") + num(sg.length);
    if (sg.keyway.enabled)
        s += QStringLiteral(" · 키홈 %1×%2").arg(num(sg.keyway.width()), num(sg.keyway.length));
    if ((i == 0 && p.hasThread(0)) || (i == p.segments.size() - 1 && p.hasThread(1)))
        s += QStringLiteral(" · 나사");
    return s;
}

bool ShaftPick::pick(MyGraphicsScene *scene, QGraphicsItem *item, const QPointF &pos, ShaftPickInfo &out)
{
    out = ShaftPickInfo();
    if (!scene || !item)
        return false;
    const QString uuid = item->data(MECH_DATA_PART_ID).toString();
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (uuid.isEmpty() || !reg->hasSymbol(uuid))
        return false;
    const SymbolInfo info = reg->symbolInfo(uuid);
    if (info.type != QLatin1String("MechShaft"))
        return false;
    QPointF a0;
    double ang = 0.0;
    QString name;
    if (!ProjectionView::partFrame(scene, uuid, a0, ang, name))
        return false;
    const ShaftParams p = ShaftParams::fromJsonString(info.props.value(QStringLiteral("Params")).toString(),
                                                      ShaftParams::sample());
    if (p.segments.isEmpty())
        return false;
    const double a = qDegreesToRadians(ang);
    const QPointF u(std::cos(a), std::sin(a)), n(-std::sin(a), std::cos(a));
    const QPointF d = pos - a0;
    double x = d.x() * u.x() + d.y() * u.y();
    // 구간 찾기 (양 끝 밖이면 가까운 끝 구간)
    double total = 0.0;
    for (int i = 0; i < p.segments.size(); ++i)
        total += p.segments.at(i).length;
    x = std::max(0.0, std::min(total, x));
    int seg = p.segments.size() - 1;
    double xs = 0.0;
    for (int i = 0; i < p.segments.size(); ++i) {
        if (x <= xs + p.segments.at(i).length + 1e-9) {
            seg = i;
            break;
        }
        xs += p.segments.at(i).length;
    }
    xs = 0.0;
    for (int i = 0; i < seg; ++i)
        xs += p.segments.at(i).length;
    const ShaftSegment &sg = p.segments.at(seg);

    out.ok = true;
    out.uuid = uuid;
    out.name = info.name;
    out.params = p;
    out.seg = seg;
    out.xFrom = xs;
    out.xTo = xs + sg.length;
    out.headline = diameterText(sg, false) + QStringLiteral(" · 구간 %1").arg(seg + 1);
    out.details << QStringLiteral("지름 ") + diameterText(sg, true);
    out.details << QStringLiteral("길이 %1 · 왼쪽 끝에서 %2~%3").arg(num(sg.length), num(xs), num(xs + sg.length));
    if (sg.keyway.enabled) {
        const QString kt = sg.keyway.type == ShaftKeywayType::Open ? QStringLiteral("열림") : QStringLiteral("막힘");
        out.details << QStringLiteral("키홈 %1×%2 %3 · 깊이 %4")
                           .arg(num(sg.keyway.width()), num(sg.keyway.length), kt, num(sg.keyway.sectionDepth));
    }
    if (sg.chamferLeft > 0.0 || sg.chamferRight > 0.0)
        out.details << QStringLiteral("모따기 왼쪽 C%1 · 오른쪽 C%2").arg(num(sg.chamferLeft), num(sg.chamferRight));
    if (seg == 0 && p.hasThread(0))
        out.details << QStringLiteral("나사 M%1 · 길이 %2").arg(num(sg.diameter), num(p.leftThread.length));
    if (seg == p.segments.size() - 1 && p.hasThread(1))
        out.details << QStringLiteral("나사 M%1 · 길이 %2").arg(num(sg.diameter), num(p.rightThread.length));
    out.commandLine = QStringLiteral("%1 · 구간 %2/%3 · %4").arg(info.name).arg(seg + 1).arg(p.segments.size())
                          .arg(out.details.join(QStringLiteral(" · ")));

    // 도면 강조: 구간 띠 (축선 기준 ±반지름)
    const double r = sg.diameter * 0.5;
    QPolygonF band;
    band << a0 + u * xs + n * r << a0 + u * (xs + sg.length) + n * r << a0 + u * (xs + sg.length) - n * r
         << a0 + u * xs - n * r << a0 + u * xs + n * r;
    out.highlight.addPolygon(band);
    out.balloonAt = a0 + u * (xs + sg.length * 0.5) + n * (r * 1.6 + 2.0);
    return true;
}

void ShaftPick::showOverlay(MyGraphicsScene *scene, const ShaftPickInfo &info)
{
    if (!scene || !info.ok)
        return;
    QPainterPath p = info.highlight;
    // 말풍선 글: 화면에서 약 12px 높이가 되게 도면 크기로 환산
    double scale = 1.0;
    if (!scene->views().isEmpty()) {
        const double m = std::fabs(scene->views().constFirst()->transform().m11());
        if (m > 1e-9)
            scale = m;
    }
    const double h = 12.0 / scale;
    QFont f;
    f.setPointSizeF(10.0);
    QPainterPath text;
    text.addText(QPointF(0.0, 0.0), f, info.headline);
    const QRectF tb = text.boundingRect();
    const double k = h / std::max(1e-9, tb.height());
    QTransform t;
    t.translate(info.balloonAt.x() - tb.center().x() * k, info.balloonAt.y());
    t.scale(k, -k);                                     // 도면 Y 위쪽 = 화면 위
    QPainterPath tp = t.map(text);
    const QRectF br = tp.boundingRect().adjusted(-h * 0.4, -h * 0.3, h * 0.4, h * 0.3);
    p.addPath(tp);
    p.addRect(br);
    scene->setGhostPath(p, true, QColor(55, 138, 221), 1.4);
}

void ShaftPick::clearOverlay(MyGraphicsScene *scene)
{
    if (scene)
        scene->setGhostPath(QPainterPath(), false);
}
