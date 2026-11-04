#include "leaderitem.h"
#include "gripitem.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "polylineitem.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <QFontMetricsF>

namespace {
// 여러 줄(\n) 콜아웃 텍스트의 폭/줄높이를 한 번에 계산 — paint()/shape()/geometryPath()가
// 서로 다른 값을 쓰면 클릭영역·착지선 길이·실제 렌더링이 어긋나므로 반드시 공유해서 사용.
struct LeaderTextMetrics {
    QStringList lines;
    double maxWidth = 0.0;
    double lineHeight = 0.0;
};

LeaderTextMetrics measureLeaderText(const QString &text, const QFont &font) {
    LeaderTextMetrics m;
    m.lines = text.split(QLatin1Char('\n'));
    QFontMetricsF fm(font);
    m.lineHeight = fm.height();
    for (const QString &line : std::as_const(m.lines)) {
        m.maxWidth = qMax(m.maxWidth, static_cast<double>(fm.horizontalAdvance(line)));
    }
    return m;
}
}

LeaderItem::LeaderItem(const MyLeader &data, QGraphicsItem *parent)
    : CadItem(parent), m_data(data) {
    setZValue(10);
    LeaderItem::setGripsVisible(false);
}

LeaderItem::~LeaderItem() {}

void LeaderItem::setTextHidden(bool hidden) {
    if (m_textHidden == hidden)
        return;
    m_textHidden = hidden;
    update();
}

// [수정] 레이어·펜을 함께 복사 — 누락 시 복사본 지시선이 기본 검정 펜으로 그려져
// 어두운 배경에서 보이지 않고(텍스트는 dimTextLayer 색이라 보임) 레이어 루트에도 붙지 않음
CadItem *LeaderItem::clone() const {
    MyLeader copy = m_data;
    // 복사본 구멍 콜아웃은 원본 구멍과의 연결을 끊는다 — 유지하면 복사본을 편집할 때
    // 원래 위치의 구멍이 바뀐다. (복사된 구멍은 새 ID를 받으므로 자동 재연결도 불가)
    copy.holeIds.clear();
    auto *item = new LeaderItem(copy);
    item->setLayerName(layerName());
    item->setPen(m_pen);
    return item;
}

// [수정] 텍스트 클릭 영역까지 완벽히 감싸도록 shape() 기반으로 캐싱
QRectF LeaderItem::boundingRect() const {
    if (m_isBoundingRectValid)
        return m_cachedBoundingRect;

    m_cachedBoundingRect = shape().boundingRect().adjusted(-5, -10, 5, 5);
    m_isBoundingRectValid = true;
    return m_cachedBoundingRect;
}

void LeaderItem::drawArrow(QPainter *painter, const MyVector &pos, const MyVector &direction, double arrowSize, const QColor &color,
                            ArrowheadStyle headStyle) const {
    MyVector n(-direction.y, direction.x);
    MyVector head = pos;
    MyVector back = pos + direction * arrowSize;

    switch (headStyle) {
    case ArrowheadStyle::Open: {
        MyVector s1 = back + n * (arrowSize * 0.35);
        MyVector s2 = back - n * (arrowSize * 0.35);
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(head.toPointF(), s1.toPointF());
        painter->drawLine(head.toPointF(), s2.toPointF());
        break;
    }
    case ArrowheadStyle::FilledCircle:
    case ArrowheadStyle::OpenCircle:
    case ArrowheadStyle::DoubleCircle: {
        double r = arrowSize * 0.35;
        QPointF c = pos.toPointF(); // 원의 중심을 치수선 끝점(pos)에 맞춘다.
        painter->setBrush(headStyle == ArrowheadStyle::OpenCircle ? Qt::NoBrush : QBrush(color));
        painter->drawEllipse(c, r, r);
        if (headStyle == ArrowheadStyle::DoubleCircle) {
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(c, r * 1.7, r * 1.7);
        }
        painter->setBrush(Qt::NoBrush);
        break;
    }
    case ArrowheadStyle::Hollow: {
        // 빈화살표: Filled와 동일한 닫힌 삼각형이지만 속을 채우지 않는다(윤곽선만).
        MyVector s1 = back + n * (arrowSize * 0.35);
        MyVector s2 = back - n * (arrowSize * 0.35);
        QPolygonF poly;
        poly << head.toPointF() << s1.toPointF() << s2.toPointF();
        painter->setBrush(Qt::NoBrush);
        painter->drawPolygon(poly);
        break;
    }
    case ArrowheadStyle::Filled:
    default: {
        MyVector s1 = back + n * (arrowSize * 0.35);
        MyVector s2 = back - n * (arrowSize * 0.35);
        QPolygonF poly;
        poly << head.toPointF() << s1.toPointF() << s2.toPointF();
        painter->setBrush(color);
        painter->drawPolygon(poly);
        painter->setBrush(Qt::NoBrush);
        break;
    }
    }
}

void LeaderItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                       QWidget *widget) {
    Q_UNUSED(widget);
    MyGraphicsScene *m_scene = qobject_cast<MyGraphicsScene *>(scene());
    if (!m_scene) return;

    double scale = m_scene->drawingScale();
    DimensionStyle style = m_scene->dimensionStyle();
    bool showWeight = m_scene->showLineWeight();

    // Highlight
    // [핵심] 프린터 출력 시에는 선택 하이라이트를 그리지 않음
    if ((option->state & QStyle::State_Selected) &&
        painter->device()->devType() == QInternal::Widget) {
        painter->save();
        QPen glowPen(QColor(0, 255, 255, 80));
        glowPen.setCosmetic(true);
        glowPen.setWidthF(5.0 * scale);
        painter->setPen(glowPen);
        painter->drawPath(geometryPath());
        painter->restore();
    }

    // Pen setup
    QPen p = pen();
    if (!showWeight) {
        p.setCosmetic(true);
        p.setWidthF(1.0);
    } else {
        p.setCosmetic(false);
        qreal s = qAbs(painter->transform().m11());
        if (s > 0.0 && p.widthF() * s < 1.0) {
            p.setWidthF(1.0 / s);
        }
    }
    painter->setPen(p);

    if (m_data.points.size() < 2)
        return;

    // Arrow
    if (!m_data.hideArrow) {
        MyVector p0 = m_data.points[0];
        MyVector p1 = m_data.points[1];
        MyVector dir = (p1 - p0).normalized();
        double arrowSize = style.arrowSize * scale;
        drawArrow(painter, p0, dir, arrowSize, p.color(), style.arrowStyle);
    }

    // Segments: draw all path segments including the final approach to elbow
    for (int i = 0; i + 1 < m_data.points.size(); ++i) {
        painter->drawLine(m_data.points[i].toPointF(),
                          m_data.points[i + 1].toPointF());
    }

    // Shoulder/landing from the last point (elbow)
    MyVector elbow = m_data.points.last();
    double sign = (m_data.points.size() >= 2 &&
                   m_data.points.last().x >= m_data.points[m_data.points.size()-2].x)
                  ? 1.0 : -1.0;

    double textSizeActual = style.textSize * scale;
    // [수정] 편집기·shape()와 같은 치수 스타일 폰트 사용 — 폰트가 다르면 편집 시 위치/폭이 어긋남
    QFont font = painter->font();
    font.setFamily(style.fontFamily);
    font.setPointSizeF(textSizeActual);

    LeaderTextMetrics tm = measureLeaderText(m_data.text, font);
    double textWidth = tm.maxWidth;
    double hPadding = 2.0 * scale;

    double landingWidth = qMax(m_data.landingLength * scale, textWidth + hPadding);

    MyVector finalEnd(elbow.x + sign * landingWidth, elbow.y);
    painter->drawLine(elbow.toPointF(), finalEnd.toPointF());

    // Text — AutoCAD MLeader처럼 첫 줄이 맨 위, 마지막 줄이 착지선 바로 위(하단 부착)
    if (!m_data.text.isEmpty() && !m_textHidden) {
        QColor textColor = p.color();
        if (!style.dimTextLayer.isEmpty()) {
            textColor = m_scene->getLayerStyle(style.dimTextLayer).color;
        }
        painter->setPen(textColor);
        painter->setFont(font);

        painter->save();
        painter->translate(elbow.toPointF());
        painter->scale(1, -1);

        double textGap = 1.0;
        QFontMetricsF fm(font);

        for (int i = 0; i < tm.lines.size(); ++i) {
            double lineWidth = fm.horizontalAdvance(tm.lines[i]);
            double y = -textGap - (tm.lines.size() - 1 - i) * tm.lineHeight;
            double x = (sign > 0) ? 0.0 : -lineWidth;
            painter->drawText(QPointF(x, y), tm.lines[i]);
        }
        painter->restore();
    }
}

// 텍스트가 실제로 그려지는 영역(씬 좌표, 여백 없음). paint()와 같은 계산을 사용.
QRectF LeaderItem::textRect() const {
    if (m_data.text.isEmpty() || m_data.points.size() < 2)
        return QRectF();

    MyGraphicsScene *m_scene = qobject_cast<MyGraphicsScene *>(scene());
    double scale = m_scene ? m_scene->drawingScale() : 1.0;
    DimensionStyle style = m_scene ? m_scene->dimensionStyle() : DimensionStyle();

    MyVector elbow = m_data.points.last();
    double sign = (m_data.points.last().x >= m_data.points[m_data.points.size()-2].x)
                  ? 1.0 : -1.0;

    QFont font;
    font.setFamily(style.fontFamily);
    font.setPointSizeF(style.textSize * scale);
    QFontMetricsF fm(font);

    LeaderTextMetrics tm = measureLeaderText(m_data.text, font);
    double textWidth = tm.maxWidth;
    double textGap = 1.0;

    double left = (sign > 0) ? 0.0 : -textWidth;
    double right = left + textWidth;
    double baselineY = -textGap;

    // 여러 줄이면 마지막 줄 하단(착지선에 가장 가까움)부터 첫 줄 상단까지 전체 범위
    double top = baselineY - (tm.lines.size() - 1) * tm.lineHeight - fm.ascent();
    double bottom = baselineY + fm.descent();

    // 텍스트는 로컬 (1,-1) 반전으로 그려지므로 Y를 뒤집어 씬 좌표로 변환
    return QRectF(QPointF(elbow.x + left, elbow.y - bottom),
                  QPointF(elbow.x + right, elbow.y - top)).normalized();
}

// [수정] 선택 영역을 실제 선/텍스트 범위로 제한 — 예전의 ±4 여백 박스가
// 주변 객체 선택을 가로채던 문제 해결
QPainterPath LeaderItem::shape() const {
    QPainterPath path = geometryPath();
    QPainterPathStroker stroker;
    stroker.setWidth(1.0); // 클릭 마진
    QPainterPath finalPath = stroker.createStroke(path);

    QRectF tr = textRect();
    if (!tr.isEmpty()) {
        QPainterPath textPath;
        textPath.addRect(tr);
        finalPath.addPath(textPath);
        finalPath.setFillRule(Qt::WindingFill); // 구멍 뚫림 방지
    }
    return finalPath;
}

QPainterPath LeaderItem::geometryPath() const {
    QPainterPath path;
    if (m_data.points.size() < 2) return path;

    // Draw all path segments including the final approach to elbow
    path.moveTo(m_data.points[0].toPointF());
    for (int i = 1; i < m_data.points.size(); ++i) {
        path.lineTo(m_data.points[i].toPointF());
    }

    MyGraphicsScene *m_scene = qobject_cast<MyGraphicsScene *>(scene());
    if (!m_scene) return path;

    double scale = m_scene->drawingScale();
    DimensionStyle style = m_scene->dimensionStyle();

    MyVector elbow = m_data.points.last();
    double sign = (m_data.points.size() >= 2 &&
                   m_data.points.last().x >= m_data.points[m_data.points.size()-2].x)
                  ? 1.0 : -1.0;
    double textWidth = 0;
    if (!m_data.text.isEmpty()) {
        QFont font;
        font.setFamily(style.fontFamily);
        font.setPointSizeF(style.textSize * scale);
        textWidth = measureLeaderText(m_data.text, font).maxWidth;
    }

    double hPadding = 2.0 * scale;
    double landingWidth = qMax(m_data.landingLength * scale, textWidth + hPadding);

    MyVector finalEnd(elbow.x + sign * landingWidth, elbow.y);
    path.lineTo(finalEnd.toPointF());
    return path;
}

QList<SnapPoint> LeaderItem::getSnapPoints() const {
    QList<SnapPoint> snaps;
    for (int i = 0; i < m_data.points.size(); ++i) {
        snaps.append({m_data.points[i], SnapType::Endpoint});
    }
    return snaps;
}

QList<QPointF> LeaderItem::getGripPoints() const {
    QList<QPointF> grips;
    for (int i = 0; i < m_data.points.size(); ++i) {
        grips.append(m_data.points[i].toPointF());
    }
    return grips;
}

void LeaderItem::updateGrips() {}
void LeaderItem::applyGripMove(int index, const MyVector &pos) {
    if (index >= 0 && index < m_data.points.size()) {
        prepareGeometryChange();
        m_data.points[index] = pos;
        m_isBoundingRectValid = false;
        update();
    }
}
void LeaderItem::setGripsVisible(bool visible) { Q_UNUSED(visible); }

// [수정] Property Dock에서 올바르게 표시되도록 보강
QMap<QString, QVariant> LeaderItem::getProperties() const {
    QMap<QString, QVariant> props;
    props["Type"] = "Leader";
    props["Content"] = m_data.text; // Property Dock의 "Content" 키와 연결

    if (!m_data.points.isEmpty()) {
        props["StartX"] = m_data.points.first().x;
        props["StartY"] = m_data.points.first().y;
        props["EndX"] = m_data.points.last().x;
        props["EndY"] = m_data.points.last().y;
    }
    return props;
}

// [수정] Property Dock에서 텍스트 수정을 지원
bool LeaderItem::setProperty(const QString &key, const QVariant &value) {
    if (key == "Content") {
        prepareGeometryChange();
        m_data.text = value.toString();
        m_isBoundingRectValid = false;
        update();
        return true;
    }
    // 구멍 콜아웃 연결 목록 (ChangePropertyCommand로 연결 해제/복원을 undo 가능하게)
    if (key == "HoleLinks") {
        m_data.holeIds = value.toStringList();
        return true;
    }
    return false;
}

// [수정] 텍스트 클릭 시 명중(0.0)으로 처리하여 즉시 선택되도록 보강
// [수정] 실제 거리 반환 — 예전에는 shape()(여백 박스) 안이면 무조건 0을 돌려줘
// 같은 위치의 선/치수보다 항상 우선 선택되었다. 착지선도 거리 계산에 포함.
double LeaderItem::distanceTo(const MyVector &pos) const {
    double minDist = 1e18;

    QPainterPath path = geometryPath();
    for (int i = 1; i < path.elementCount(); ++i) {
        const QPainterPath::Element a = path.elementAt(i - 1);
        const QPainterPath::Element b = path.elementAt(i);
        if (!b.isLineTo())
            continue;
        MyVector p = MyMath::getNearestPointOnSegment(pos, MyLine(MyVector(a.x, a.y), MyVector(b.x, b.y)));
        minDist = qMin(minDist, (pos - p).magnitude());
    }

    QRectF tr = textRect();
    if (!tr.isEmpty()) {
        double dx = qMax(qMax(tr.left() - pos.x, 0.0), pos.x - tr.right());
        double dy = qMax(qMax(tr.top() - pos.y, 0.0), pos.y - tr.bottom());
        minDist = qMin(minDist, qSqrt(dx * dx + dy * dy));
    }
    return minDist;
}

SnapPoint LeaderItem::getNearestSnap(const MyVector &pos) const {
    MyVector nearest;
    double minDist = 1e18;
    for (int i = 0; i < m_data.points.size() - 1; ++i) {
        MyVector p = MyMath::getNearestPointOnSegment(pos, MyLine(m_data.points[i], m_data.points[i+1]));
        double d = (p - pos).magnitude();
        if (d < minDist) { minDist = d; nearest = p; }
    }
    return {nearest, SnapType::Nearest};
}

void LeaderItem::move(const MyVector &delta) {
    prepareGeometryChange();
    for (int i = 0; i < m_data.points.size(); ++i) m_data.points[i] = m_data.points[i] + delta;
    m_isBoundingRectValid = false;
    update();
}

QPainterPath LeaderItem::getMovedPath(const MyVector &delta) const {
    QPainterPath p = geometryPath();
    QTransform t; t.translate(delta.x, delta.y);
    return t.map(p);
}

void LeaderItem::mirror(const MyVector &p1, const MyVector &p2) {
    prepareGeometryChange();
    for (int i = 0; i < m_data.points.size(); ++i) m_data.points[i] = MyMath::reflectPoint(m_data.points[i], p1, p2);
    m_isBoundingRectValid = false;
    update();
}

QPainterPath LeaderItem::getMirroredPath(const MyVector &p1, const MyVector &p2) const {
    Q_UNUSED(p1)
    Q_UNUSED(p2)
    return geometryPath();
}

void LeaderItem::rotate(const MyVector &base, double angle) {
    prepareGeometryChange();
    for (int i = 0; i < m_data.points.size(); ++i) m_data.points[i] = MyMath::rotatePoint(m_data.points[i], base, angle);
    m_isBoundingRectValid = false;
    update();
}

QPainterPath LeaderItem::getRotatedPath(const MyVector &base, double angle) const {
    QPainterPath p = geometryPath();
    QTransform t; t.translate(base.x, base.y); t.rotate(angle); t.translate(-base.x, -base.y);
    return t.map(p);
}

void LeaderItem::scale(const MyVector &base, double scaleFactor) {
    prepareGeometryChange();
    for (int i = 0; i < m_data.points.size(); ++i) m_data.points[i] = MyMath::scalePoint(m_data.points[i], base, scaleFactor);
    m_isBoundingRectValid = false;
    update();
}

QPainterPath LeaderItem::getScaledPath(const MyVector &base, double scaleFactor) const {
    QPainterPath p = geometryPath();
    QTransform t; t.translate(base.x, base.y); t.scale(scaleFactor, scaleFactor); t.translate(-base.x, -base.y);
    return t.map(p);
}

void LeaderItem::stretch(const QList<int> &indices, const MyVector &delta) {
    prepareGeometryChange();
    for (int i = 0; i < indices.size(); ++i) {
        int idx = indices[i];
        if (idx >= 0 && idx < m_data.points.size()) m_data.points[idx] = m_data.points[idx] + delta;
    }
    m_isBoundingRectValid = false;
    update();
}

QPainterPath LeaderItem::getStretchedPath(const QList<int> &indices, const MyVector &delta) const {
    Q_UNUSED(indices)
    Q_UNUSED(delta)
    return geometryPath();
}

QPainterPath LeaderItem::getGhostShape(int index, const MyVector &pos) const {
    QPainterPath path;
    if (m_data.points.isEmpty()) return path;

    path.moveTo((0 == index) ? pos.toPointF() : m_data.points[0].toPointF());
    for (int i = 1; i < m_data.points.size(); ++i) {
        path.lineTo((i == index) ? pos.toPointF() : m_data.points[i].toPointF());
    }
    return path;
}

QList<CadItem *> LeaderItem::explode() const {
    QList<CadItem *> items;
    std::vector<MyVector> pts;
    for(int i = 0; i < m_data.points.size(); ++i) {
        pts.push_back(m_data.points[i]);
    }
    if (!pts.empty()) {
        PolylineItem *pli = new PolylineItem(pts, false);
        items << pli;
    }
    return items;
}