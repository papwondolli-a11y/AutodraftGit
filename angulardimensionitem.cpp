#include "angulardimensionitem.h"
#include "cadutils.h"
#include "mymath.h"
#include "mygraphicsscene.h"
#include "lineitem.h"
#include "arcitem.h"
#include "polylineitem.h"
#include "textitem.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <QFontMetricsF>

AngularDimensionItem::AngularDimensionItem(const MyDimension &data, QGraphicsItem *parent)
    : BaseDimensionItem(data, parent) {
    m_data.dimensionType = DimensionType::Angular;
    updateGrips();
    setGripsVisible(false);
}

AngularDimensionItem::~AngularDimensionItem() {}

CadItem *AngularDimensionItem::clone() const {
    auto *item = new AngularDimensionItem(m_data);
    item->setLayerName(layerName());
    item->setPen(pen());
    return item;
}

QList<SnapPoint> AngularDimensionItem::getSnapPoints() const {
    QList<SnapPoint> points = BaseDimensionItem::getSnapPoints();

    MyVector vertex = m_data.start;
    MyVector p1 = m_data.end;
    MyVector p2 = m_data.end2;
    MyVector pArc = m_data.dimLine;

    double radius = (pArc - vertex).magnitude();
    if (radius < 1e-6) return points;

    double ang1 = qRadiansToDegrees(qAtan2(p1.y - vertex.y, p1.x - vertex.x));
    double ang2 = qRadiansToDegrees(qAtan2(p2.y - vertex.y, p2.x - vertex.x));
    double angArc = qRadiansToDegrees(qAtan2(pArc.y - vertex.y, pArc.x - vertex.x));

    ang1 = MyMath::normalizeAngle(ang1);
    ang2 = MyMath::normalizeAngle(ang2);
    angArc = MyMath::normalizeAngle(angArc);

    double startAng = ang1;
    double endAng = ang2;
    double span = endAng - startAng;

    if (span < 0) span += 360.0;
    if (span > 360.0) span -= 360.0;

    if (!MyMath::isAngleBetween(angArc, startAng, span)) {
        startAng = ang2;
        endAng = ang1;
        span = endAng - startAng;
        if (span < 0) span += 360.0;
    }

    // 화살표 끝점 (호의 양 끝) 스냅 추가
    double rad1 = qDegreesToRadians(startAng);
    MyVector pos1(vertex.x + qCos(rad1) * radius, vertex.y + qSin(rad1) * radius);
    points << SnapPoint{pos1, SnapType::Endpoint};

    double rad2 = qDegreesToRadians(startAng + span);
    MyVector pos2(vertex.x + qCos(rad2) * radius, vertex.y + qSin(rad2) * radius);
    points << SnapPoint{pos2, SnapType::Endpoint};

    return points;
}

void AngularDimensionItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget);

    drawSelectionHighlight(painter, option);

    double finalScale;
    DimensionStyle style = getDimStyle(finalScale);

    double viewScale = 1.0;
    if (!painter->transform().isIdentity()) {
        double m11abs = qAbs(painter->transform().m11());
        if (m11abs > 0.0) viewScale = 1.0 / m11abs;
    }
    setupDimPen(painter, style, viewScale);

    MyVector vertex = m_data.start;
    MyVector p1 = m_data.end;
    MyVector p2 = m_data.end2;
    MyVector pArc = m_data.dimLine;

    double radius = (pArc - vertex).magnitude();
    if (radius < 1e-6) return;

    double ang1 = qRadiansToDegrees(qAtan2(p1.y - vertex.y, p1.x - vertex.x));
    double ang2 = qRadiansToDegrees(qAtan2(p2.y - vertex.y, p2.x - vertex.x));
    double angArc = qRadiansToDegrees(qAtan2(pArc.y - vertex.y, pArc.x - vertex.x));

    ang1 = MyMath::normalizeAngle(ang1);
    ang2 = MyMath::normalizeAngle(ang2);
    angArc = MyMath::normalizeAngle(angArc);

    double startAng = ang1;
    double endAng = ang2;
    double span = endAng - startAng;

    if (span < 0) span += 360.0;
    if (span > 360.0) span -= 360.0;

    if (!MyMath::isAngleBetween(angArc, startAng, span)) {
        startAng = ang2;
        endAng = ang1;
        span = endAng - startAng;
        if (span < 0) span += 360.0;
    }

    double measurement = span;

    QRectF arcRect(vertex.x - radius, vertex.y - radius, radius * 2, radius * 2);
    painter->drawArc(arcRect, qRound(-startAng * 16), qRound(-span * 16));

    double arrowSize = style.arrowSize * finalScale;
    auto drawArcArrow = [&](double angle, bool clockwise) {
        double rad = qDegreesToRadians(angle);
        MyVector pos(vertex.x + qCos(rad) * radius, vertex.y + qSin(rad) * radius);
        MyVector dir(qSin(rad), -qCos(rad));
        if (clockwise) dir = dir * -1.0;
        drawArrow(painter, pos, dir, arrowSize, painter->pen().color(), style.arrowStyle);
    };

    if (!m_data.hideArrow1) drawArcArrow(startAng, false);
    if (!m_data.hideArrow2) drawArcArrow(startAng + span, true);

    // [수정] 복잡한 문자열 조합 로직 제거 및 엔진 사용
    QString baseVal = QString::number(measurement, 'f', style.precision) + QString::fromUtf8("°");

    double midAng = startAng + span * 0.5;
    double textGap = style.textGap * finalScale;
    double radMid = qDegreesToRadians(midAng);
    MyVector textPos(vertex.x + qCos(radMid) * (radius + textGap),
                     vertex.y + qSin(radMid) * (radius + textGap));

    double rotation = midAng + 90.0;
    if (rotation > 90.0 && rotation < 270.0) {
        rotation -= 180.0;
    }

    drawDimensionText(painter, measurement, baseVal, textPos, rotation, style, finalScale);

    MyGraphicsScene *m_scene = qobject_cast<MyGraphicsScene *>(scene());
    if (m_scene) {
        double extOffset = style.extOffset * finalScale;
        double extExtend = style.extExtend * finalScale;

        auto drawExt = [&](const MyVector &pt, double angle) {
            double rad = qDegreesToRadians(angle);
            MyVector dir(qCos(rad), qSin(rad));
            double dist = (pt - vertex).magnitude();
            if (radius > dist) {
                MyVector s = pt + dir * extOffset;
                MyVector e = vertex + dir * (radius + extExtend);
                painter->drawLine(s.toPointF(), e.toPointF());
            }
        };

        if (!m_data.hideExt1) drawExt(p1, ang1);
        if (!m_data.hideExt2) drawExt(p2, ang2);
    }
}

QPainterPath AngularDimensionItem::shape() const {
    QPainterPath path = geometryPath();
    QPainterPathStroker stroker;
    stroker.setWidth(4.0);
    QPainterPath finalPath = stroker.createStroke(path);

    MyGraphicsScene *m_scene = qobject_cast<MyGraphicsScene *>(scene());
    DimensionStyle style = m_scene ? m_scene->dimensionStyle() : DimensionStyle();
    double finalScale = m_scene ? m_scene->drawingScale() : 1.0;

    MyVector vertex = m_data.start;
    MyVector p1 = m_data.end;
    MyVector p2 = m_data.end2;
    MyVector pArc = m_data.dimLine;

    double radius = (pArc - vertex).magnitude();
    if (radius < 1e-6) return finalPath;

    double ang1 = qRadiansToDegrees(qAtan2(p1.y - vertex.y, p1.x - vertex.x));
    double ang2 = qRadiansToDegrees(qAtan2(p2.y - vertex.y, p2.x - vertex.x));
    double angArc = qRadiansToDegrees(qAtan2(pArc.y - vertex.y, pArc.x - vertex.x));

    ang1 = MyMath::normalizeAngle(ang1);
    ang2 = MyMath::normalizeAngle(ang2);
    angArc = MyMath::normalizeAngle(angArc);

    double startAng = ang1;
    double endAng = ang2;
    double span = endAng - startAng;

    if (span < 0) span += 360.0;
    if (span > 360.0) span -= 360.0;

    if (!MyMath::isAngleBetween(angArc, startAng, span)) {
        startAng = ang2;
        endAng = ang1;
        span = endAng - startAng;
        if (span < 0) span += 360.0;
    }

    // [수정] 복잡한 조합 없이 baseVal 생성
    QString baseVal = QString::number(span, 'f', style.precision) + QString::fromUtf8("°");

    double midAng = startAng + span * 0.5;
    double textGap = style.textGap * finalScale;
    double radMid = qDegreesToRadians(midAng);

    MyVector textPos(vertex.x + qCos(radMid) * (radius + textGap),
                     vertex.y + qSin(radMid) * (radius + textGap));

    double rotation = midAng + 90.0;
    if (rotation > 90.0 && rotation < 270.0) {
        rotation -= 180.0;
    }

    // [수정] 공차가 포함된 전체 바운딩 박스 생성
    finalPath.addPath(createTextBoundingPath(span, baseVal, textPos, rotation));
    finalPath.setFillRule(Qt::WindingFill);

    return finalPath;
}

QPainterPath AngularDimensionItem::geometryPath() const {
    QPainterPath path;
    MyVector vertex = m_data.start;
    MyVector p1 = m_data.end;
    MyVector p2 = m_data.end2;
    MyVector pArc = m_data.dimLine;

    double radius = (pArc - vertex).magnitude();
    if (radius < 1e-6) return path;

    double ang1 = qRadiansToDegrees(qAtan2(p1.y - vertex.y, p1.x - vertex.x));
    double ang2 = qRadiansToDegrees(qAtan2(p2.y - vertex.y, p2.x - vertex.x));
    double angArc = qRadiansToDegrees(qAtan2(pArc.y - vertex.y, pArc.x - vertex.x));

    ang1 = MyMath::normalizeAngle(ang1);
    ang2 = MyMath::normalizeAngle(ang2);
    angArc = MyMath::normalizeAngle(angArc);

    double startAng = ang1;
    double span = ang2 - ang1;
    if (span < 0) span += 360.0;

    if (!MyMath::isAngleBetween(angArc, startAng, span)) {
        startAng = ang2;
        span = 360.0 - span;
    }

    QRectF arcRect(vertex.x - radius, vertex.y - radius, radius * 2, radius * 2);
    path.arcMoveTo(arcRect, -startAng);
    path.arcTo(arcRect, -startAng, -span);
    return path;
}

double AngularDimensionItem::distanceTo(const MyVector &pos) const {
    if (shape().contains(pos.toPointF())) {
        return 0.0;
    }
    MyVector vertex = m_data.start;
    MyVector pArc = m_data.dimLine;
    double radius = (pArc - vertex).magnitude();

    if (radius > 1e-6) {
        double distToCenter = pos.distanceTo(vertex);
        return std::abs(distToCenter - radius);
    }
    return pos.distanceTo(vertex);
}

QPainterPath AngularDimensionItem::getGhostShape(int index, const MyVector &pos) const {
    AngularDimensionItem ghost(m_data);
    ghost.applyGripMove(index, pos);
    return ghost.geometryPath();
}

QPainterPath AngularDimensionItem::getMovedPath(const MyVector &delta) const {
    AngularDimensionItem ghost(m_data);
    ghost.move(delta);
    return ghost.geometryPath();
}

QPainterPath AngularDimensionItem::getMirroredPath(const MyVector &p1, const MyVector &p2) const {
    AngularDimensionItem ghost(m_data);
    ghost.mirror(p1, p2);
    return ghost.geometryPath();
}

QPainterPath AngularDimensionItem::getRotatedPath(const MyVector &base, double angle) const {
    AngularDimensionItem ghost(m_data);
    ghost.rotate(base, angle);
    return ghost.geometryPath();
}

QPainterPath AngularDimensionItem::getScaledPath(const MyVector &base, double scaleFactor) const {
    AngularDimensionItem ghost(m_data);
    ghost.scale(base, scaleFactor);
    return ghost.geometryPath();
}

QPainterPath AngularDimensionItem::getStretchedPath(const QList<int> &indices, const MyVector &delta) const {
    AngularDimensionItem ghost(m_data);
    ghost.stretch(indices, delta);
    return ghost.geometryPath();
}

QList<CadItem *> AngularDimensionItem::explode() const {
    QList<CadItem *> items;
    MyGraphicsScene *scenePtr = qobject_cast<MyGraphicsScene *>(scene());
    if (!scenePtr) return items;

    double sceneScale = scenePtr->drawingScale();
    DimensionStyle style = scenePtr->dimensionStyle();
    double finalScale = sceneScale;

    auto applyItemStyle = [&](CadItem *item, const QString &layerName) {
        QString targetLayer = layerName.isEmpty() ? m_layerName : layerName;
        LayerStyle ls = scenePtr->getLayerStyle(targetLayer);
        QPen p(ls.color);
        p.setCosmetic(true);
        p.setWidthF(1.0);
        if (!ls.dashPattern.isEmpty()) {
            p.setStyle(Qt::CustomDashLine);
            p.setDashPattern(ls.dashPattern);
        }
        item->setLayerName(targetLayer);
        item->setPen(p);
        return ls;
    };

    MyVector vertex = m_data.start;
    MyVector p1 = m_data.end;
    MyVector p2 = m_data.end2;
    MyVector pArc = m_data.dimLine;

    double radius = (pArc - vertex).magnitude();
    if (radius < 1e-6) return items;

    double ang1 = qRadiansToDegrees(qAtan2(p1.y - vertex.y, p1.x - vertex.x));
    double ang2 = qRadiansToDegrees(qAtan2(p2.y - vertex.y, p2.x - vertex.x));
    double angArc = qRadiansToDegrees(qAtan2(pArc.y - vertex.y, pArc.x - vertex.x));

    ang1 = MyMath::normalizeAngle(ang1);
    ang2 = MyMath::normalizeAngle(ang2);
    angArc = MyMath::normalizeAngle(angArc);

    double startAng = ang1;
    double span = ang2 - ang1;
    if (span < 0) span += 360.0;
    if (!MyMath::isAngleBetween(angArc, startAng, span)) {
        startAng = ang2;
        span = ang1 - ang2;
        if (span < 0) span += 360.0;
    }

    ArcItem *ai = new ArcItem(MyArc(vertex, radius, startAng, span));
    applyItemStyle(ai, style.dimLineLayer);
    items << ai;

    double extOffset = style.extOffset * sceneScale;
    double extExtend = style.extExtend * sceneScale;

    auto addExt = [&](const MyVector &pt, double angle) {
        double rad = qDegreesToRadians(angle);
        MyVector dir(qCos(rad), qSin(rad));
        double dist = (pt - vertex).magnitude();
        if (radius > dist) {
            MyVector s = pt + dir * extOffset;
            MyVector e = vertex + dir * (radius + extExtend);
            LineItem *li = new LineItem(MyLine(s, e));
            applyItemStyle(li, style.dimLineLayer);
            items << li;
        }
    };

    if (!m_data.hideExt1) addExt(p1, ang1);
    if (!m_data.hideExt2) addExt(p2, ang2);

    double arrowSize = style.arrowSize * finalScale;
    auto addArrow = [&](double angle, bool clockwise) {
        double rad = qDegreesToRadians(angle);
        MyVector pos(vertex.x + qCos(rad) * radius, vertex.y + qSin(rad) * radius);
        MyVector dir(qSin(rad), -qCos(rad));
        if (clockwise) dir = dir * -1.0;
        MyVector n(-dir.y, dir.x);
        MyVector back = pos - dir * arrowSize;
        MyVector side1 = back + n * (arrowSize * 0.35);
        MyVector side2 = back - n * (arrowSize * 0.35);

        PolylineItem *arrow = new PolylineItem({pos, side1, side2}, true);
        LayerStyle ls = applyItemStyle(arrow, style.dimLineLayer);
        arrow->setBrush(QBrush(ls.color));
        items << arrow;
    };

    if (!m_data.hideArrow1) addArrow(startAng, false);
    if (!m_data.hideArrow2) addArrow(startAng + span, true);

    // [수정] 분해 시 공차가 포함된 최종 텍스트 추출
    QString baseVal = QString::number(span, 'f', style.precision) + QString::fromUtf8("°");
    QString mainText, upperText, lowerText;
    double mainW, tolW, totalW;
    calculateTextLayout(span, baseVal, style, finalScale, mainText, upperText, lowerText, mainW, tolW, totalW);

    QString finalTxt = mainText;
    if (!upperText.isEmpty() || !lowerText.isEmpty()) {
        finalTxt += " " + upperText.trimmed() + " " + lowerText.trimmed();
    }

    double midAng = startAng + span * 0.5;
    double textGap = style.textGap * finalScale;
    double radMid = qDegreesToRadians(midAng);
    MyVector textPos(vertex.x + qCos(radMid) * (radius + textGap),
                     vertex.y + qSin(radMid) * (radius + textGap));

    double rotation = midAng + 90.0;
    if (rotation > 90.0 && rotation < 270.0) rotation -= 180.0;

    MyText textData;
    textData.text = finalTxt;
    textData.pos = textPos;
    textData.rotation = rotation;
    textData.fontSize = style.textSize * finalScale;
    textData.fontFamily = style.fontFamily;

    TextItem *ti = new TextItem(textData);
    LayerStyle tls = applyItemStyle(ti, style.dimTextLayer);
    ti->setPen(QPen(tls.color));
    items << ti;

    return items;
}