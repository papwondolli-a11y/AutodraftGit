#include "radialdimensionitem.h"
#include "mymath.h"
#include "mygraphicsscene.h"
#include "polylineitem.h"
#include "lineitem.h"
#include "textitem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <QFontMetricsF>

RadialDimensionItem::RadialDimensionItem(const MyDimension &data, QGraphicsItem *parent)
    : BaseDimensionItem(data, parent) {
    updateGrips();
    setGripsVisible(false);
}

RadialDimensionItem::~RadialDimensionItem() {}

CadItem *RadialDimensionItem::clone() const {
    auto *item = new RadialDimensionItem(m_data);
    item->setLayerName(layerName());
    item->setPen(pen());
    return item;
}

QList<SnapPoint> RadialDimensionItem::getSnapPoints() const {
    QList<SnapPoint> points = BaseDimensionItem::getSnapPoints();

    MyVector p1 = m_data.start; // center
    MyVector p2 = m_data.end;   // point on circle
    MyVector p3 = m_data.dimLine; // placement

    double radius = (p2 - p1).magnitude();
    if (radius < 1e-6) return points;

    MyVector dir = (p3 - p1);
    double dist = dir.magnitude();
    if (dist < 1e-6) dir = MyVector(1, 0);
    else dir = dir / dist;

    MyVector q1 = p1 + dir * radius;
    MyVector q2 = p1 - dir * radius;

    // 화살표 끝점 (원의 경사면과의 교점) 스냅 추가
    points << SnapPoint{q1, SnapType::Endpoint};

    if (m_data.dimensionType == DimensionType::Diametric) {
        points << SnapPoint{q2, SnapType::Endpoint};
    }

    return points;
}

void RadialDimensionItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
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

    MyVector p1 = m_data.start; // center
    MyVector p2 = m_data.end;   // point on circle
    MyVector p3 = m_data.dimLine; // placement

    double radius = (p2 - p1).magnitude();
    if (radius < 1e-6) return;

    MyVector dir = (p3 - p1);
    double dist = dir.magnitude();
    if (dist < 1e-6) dir = MyVector(1, 0);
    else dir = dir / dist;

    bool isOutside = dist > radius;
    MyVector q1 = p1 + dir * radius;
    MyVector q2 = p1 - dir * radius;

    double measurement = (m_data.dimensionType == DimensionType::Diametric) ? radius * 2.0 : radius;

    double arrowSize = style.arrowSize * finalScale;
    MyVector lineStart, lineEnd, textBase;
    double extExtend = style.extExtend * finalScale;

    if (m_data.dimensionType == DimensionType::Diametric) {
        if (isOutside) {
            // [수정] 원의 지름 모드 (사용자 요청 이미지 반영: q2 외곽 연장까지 포함)
            lineStart = q2 - dir * (arrowSize + extExtend);
            lineEnd = p3 + dir * extExtend;
            textBase = p3;
            if (!m_data.hideArrow1) drawArrow(painter, q1, dir * -1.0, arrowSize, painter->pen().color(), style.arrowStyle);
            if (!m_data.hideArrow2) drawArrow(painter, q2, dir, arrowSize, painter->pen().color(), style.arrowStyle);
        } else {
            if (m_data.isFullCircle) {
                // [수정] 원의 지름 모드 (내부): 전체 지름선 (q2 ~ q1) 및 양쪽 화살표
                lineStart = q2;
                lineEnd = q1;
                textBase = p1; // 중앙 배치
                if (!m_data.hideArrow1) drawArrow(painter, q1, dir, arrowSize, painter->pen().color(), style.arrowStyle);
                if (!m_data.hideArrow2) drawArrow(painter, q2, dir * -1.0, arrowSize, painter->pen().color(), style.arrowStyle);
            } else {
                // [기존] 호의 지름 모드: 중심을 지나 반대편으로 약간 연장됨
                lineStart = p1 - dir * (5.0 * finalScale);
                lineEnd = q1;
                textBase = (p1 + q1) * 0.5;
                if (!m_data.hideArrow1) drawArrow(painter, q1, dir, arrowSize, painter->pen().color(), style.arrowStyle);
            }
        }
    } else { // Radial
        if (isOutside) {
            lineStart = p1;
            lineEnd = p3 + dir * (arrowSize + extExtend);
            textBase = p3;
            if (!m_data.hideArrow1) drawArrow(painter, q1, dir * -1.0, arrowSize, painter->pen().color(), style.arrowStyle);
        } else {
            lineStart = p1;
            lineEnd = q1;
            textBase = (p1 + q1) * 0.5;
            if (!m_data.hideArrow1) drawArrow(painter, q1, dir, arrowSize, painter->pen().color(), style.arrowStyle);
        }
    }

    painter->drawLine(lineStart.toPointF(), lineEnd.toPointF());

    // [수정] 접두어 하드코딩 제거 (prefix 변수 삭제)
    QString baseVal = QString::number(measurement, 'f', style.precision);

    double textGap = style.textGap * finalScale;
    MyVector textDir = dir;
    MyVector textNormal(-textDir.y, textDir.x);

    if (qAbs(textDir.y) > qAbs(textDir.x)) {
        if (textNormal.x > 1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
    } else {
        if (textNormal.y < -1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
    }
    double angle = qRadiansToDegrees(qAtan2(textDir.y, textDir.x));
    MyVector textPos = textBase + textNormal * textGap;

    drawDimensionText(painter, measurement, baseVal, textPos, angle, style, finalScale);
}

QPainterPath RadialDimensionItem::shape() const {
    QPainterPath path = geometryPath();
    QPainterPathStroker stroker;
    stroker.setWidth(1.0);
    QPainterPath finalPath = stroker.createStroke(path);

    MyGraphicsScene *m_scene = qobject_cast<MyGraphicsScene *>(scene());
    DimensionStyle style = m_scene ? m_scene->dimensionStyle() : DimensionStyle();
    double finalScale = m_scene ? m_scene->drawingScale() : 1.0;

    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;

    double radius = (p2 - p1).magnitude();
    if (radius < 1e-6) return finalPath;

    MyVector dir = (p3 - p1);
    double dist = dir.magnitude();
    if (dist < 1e-6) dir = MyVector(1, 0);
    else dir = dir / dist;

    bool isOutside = dist > radius;
    MyVector q1 = p1 + dir * radius;

    MyVector textBase;
    if (m_data.dimensionType == DimensionType::Diametric) {
        if (isOutside) {
            textBase = p3;
        } else {
            textBase = m_data.isFullCircle ? p1 : (p1 + q1) * 0.5;
        }
    } else {
        if (isOutside) textBase = p3;
        else           textBase = (p1 + q1) * 0.5;
    }

    double measurement = (m_data.dimensionType == DimensionType::Diametric) ? radius * 2.0 : radius;

    // [수정] 접두어 하드코딩 제거 (prefix 변수 삭제)
    QString baseVal = QString::number(measurement, 'f', style.precision);
    
    double textGap = style.textGap * finalScale;
    MyVector textDir = dir;
    MyVector textNormal(-textDir.y, textDir.x);

    if (qAbs(textDir.y) > qAbs(textDir.x)) {
        if (textNormal.x > 1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
    } else {
        if (textNormal.y < -1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
    }

    double angleDeg = qRadiansToDegrees(qAtan2(textDir.y, textDir.x));
    MyVector textPos = textBase + textNormal * textGap;

    // [수정] 공차를 포함한 텍스트 바운딩 박스 생성
    finalPath.addPath(createTextBoundingPath(measurement, baseVal, textPos, angleDeg));
    finalPath.setFillRule(Qt::WindingFill);

    return finalPath;
}

QPainterPath RadialDimensionItem::geometryPath() const {
    QPainterPath path;
    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;

    double radius = (p2 - p1).magnitude();
    MyVector dir = (p3 - p1);
    double dist = dir.magnitude();
    if (dist < 1e-6) dir = MyVector(1, 0);
    else dir = dir / dist;

    bool isOutside = dist > radius;
    MyVector q1 = p1 + dir * radius;
    MyVector q2 = p1 - dir * radius;

    MyGraphicsScene *scenePtr = qobject_cast<MyGraphicsScene *>(scene());
    double drawingScale = scenePtr ? scenePtr->drawingScale() : 1.0;
    DimensionStyle style = scenePtr ? scenePtr->dimensionStyle() : DimensionStyle();
    double extExtend = style.extExtend * drawingScale;

    MyVector lineStart, lineEnd;
    if (m_data.dimensionType == DimensionType::Diametric) {
        if (isOutside) {
            double arrowSize = 2.5 * drawingScale;
            lineStart = q2 - dir * (arrowSize + extExtend);
            lineEnd = p3 + dir * extExtend;
        } else {
            if (m_data.isFullCircle) {
                lineStart = q2;
                lineEnd = q1;
            } else {
                lineStart = p1 - dir * (5.0 * drawingScale);
                lineEnd = q1;
            }
        }
    } else {
        if (isOutside) {
            lineStart = p1;
            lineEnd = p3 + dir * extExtend;
        } else {
            lineStart = p1;
            lineEnd = q1;
        }
    }
    path.moveTo(lineStart.toPointF());
    path.lineTo(lineEnd.toPointF());
    return path;
}

double RadialDimensionItem::distanceTo(const MyVector &pos) const {
    if (shape().contains(pos.toPointF())) {
        return 0.0;
    }
    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;

    double radius = (p2 - p1).magnitude();
    MyVector dir = (p3 - p1);
    double dist = dir.magnitude();
    if (dist < 1e-6) dir = MyVector(1, 0);
    else dir = dir / dist;

    bool isOutside = dist > radius;
    MyVector q1 = p1 + dir * radius;
    MyVector q2 = p1 - dir * radius;

    MyVector lineStart, lineEnd;
    if (m_data.dimensionType == DimensionType::Diametric) {
        if (isOutside) {
            lineStart = q2 - dir * 10.0;
            lineEnd = p3 + dir * 2.0;
        } else {
            if (m_data.isFullCircle) {
                lineStart = q2;
                lineEnd = q1;
            } else {
                lineStart = p1 - dir * 5.0;
                lineEnd = q1;
            }
        }
    } else {
        if (isOutside) {
            lineStart = p1;
            lineEnd = p3 + dir * 2.0;
        } else {
            lineStart = p1;
            lineEnd = q1;
        }
    }

    MyVector ab = lineEnd - lineStart;
    MyVector ap = pos - lineStart;
    double l2 = MyMath::dotProduct(ab, ab);
    if (l2 < 1e-9) return pos.distanceTo(lineStart);
    double t = MyMath::dotProduct(ap, ab) / l2;
    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;

    return pos.distanceTo(lineStart + ab * t);
}

QPainterPath RadialDimensionItem::getGhostShape(int index, const MyVector &pos) const {
    RadialDimensionItem ghost(m_data);
    ghost.applyGripMove(index, pos);
    return ghost.geometryPath();
}

QPainterPath RadialDimensionItem::getMovedPath(const MyVector &delta) const {
    RadialDimensionItem ghost(m_data);
    ghost.move(delta);
    return ghost.geometryPath();
}

QPainterPath RadialDimensionItem::getMirroredPath(const MyVector &p1, const MyVector &p2) const {
    RadialDimensionItem ghost(m_data);
    ghost.mirror(p1, p2);
    return ghost.geometryPath();
}

QPainterPath RadialDimensionItem::getRotatedPath(const MyVector &base, double angle) const {
    RadialDimensionItem ghost(m_data);
    ghost.rotate(base, angle);
    return ghost.geometryPath();
}

QPainterPath RadialDimensionItem::getScaledPath(const MyVector &base, double scaleFactor) const {
    RadialDimensionItem ghost(m_data);
    ghost.scale(base, scaleFactor);
    return ghost.geometryPath();
}

QPainterPath RadialDimensionItem::getStretchedPath(const QList<int> &indices, const MyVector &delta) const {
    RadialDimensionItem ghost(m_data);
    ghost.stretch(indices, delta);
    return ghost.geometryPath();
}

QList<CadItem *> RadialDimensionItem::explode() const {
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

    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;

    double radius = (p2 - p1).magnitude();
    if (radius < 1e-6) return items;

    MyVector dir = (p3 - p1);
    double dist = dir.magnitude();
    if (dist < 1e-6) dir = MyVector(1, 0);
    else dir = dir / dist;

    bool isOutside = dist > radius;
    MyVector q1 = p1 + dir * radius;
    MyVector q2 = p1 - dir * radius;

    double measurement = (m_data.dimensionType == DimensionType::Diametric) ? radius * 2.0 : radius;

    MyVector lineStart, lineEnd, textBase;
    double arrowSize = style.arrowSize * finalScale;
    double extExtend = style.extExtend * finalScale;

    auto addArrow = [&](const MyVector &pos, const MyVector &direction) {
        MyVector n(-direction.y, direction.x);
        MyVector back = pos - direction * arrowSize;
        MyVector side1 = back + n * (arrowSize * 0.35);
        MyVector side2 = back - n * (arrowSize * 0.35);

        PolylineItem *arrow = new PolylineItem({pos, side1, side2}, true);
        LayerStyle ls = applyItemStyle(arrow, style.dimLineLayer);
        arrow->setBrush(QBrush(ls.color));
        items << arrow;
    };

    if (m_data.dimensionType == DimensionType::Diametric) {
        if (isOutside) {
            lineStart = q2 - dir * (arrowSize + extExtend);
            lineEnd = p3 + dir * extExtend;
            textBase = p3;
            if (!m_data.hideArrow1) addArrow(q1, dir * -1.0);
            if (!m_data.hideArrow2) addArrow(q2, dir);
        } else {
            if (m_data.isFullCircle) {
                // [수정] 원 분해 시 전체 지름선 및 양쪽 화살표
                lineStart = q2;
                lineEnd = q1;
                textBase = p1;
                if (!m_data.hideArrow1) addArrow(q1, dir);
                if (!m_data.hideArrow2) addArrow(q2, dir * -1.0);
            } else {
                // [기존] 호 분해 시 중심을 지나 약간 연장됨
                lineStart = p1 - dir * (5.0 * finalScale);
                lineEnd = q1;
                textBase = (p1 + q1) * 0.5;
                if (!m_data.hideArrow1) addArrow(q1, dir);
            }
        }
    } else {
        if (isOutside) {
            lineStart = p1;
            lineEnd = p3 + dir * (arrowSize + extExtend);
            textBase = p3;
            if (!m_data.hideArrow1) addArrow(q1, dir * -1.0);
        } else {
            lineStart = p1;
            lineEnd = q1;
            textBase = (p1 + q1) * 0.5;
            if (!m_data.hideArrow1) addArrow(q1, dir);
        }
    }

    LineItem *dl = new LineItem(lineStart, lineEnd);
    applyItemStyle(dl, style.dimLineLayer);
    items << dl;

    // [수정] 접두어 하드코딩 제거 (prefix 변수 삭제)
    QString baseVal = QString::number(measurement, 'f', style.precision);
    
    QString mainText, upperText, lowerText;
    double mainW, tolW, totalW;
    calculateTextLayout(measurement, baseVal, style, finalScale, mainText, upperText, lowerText, mainW, tolW, totalW);

    QString finalTxt = mainText;
    if (!upperText.isEmpty() || !lowerText.isEmpty()) {
        finalTxt += " " + upperText.trimmed() + " " + lowerText.trimmed();
    }

    double textGap = style.textGap * finalScale;
    MyVector textDir = dir;
    MyVector textNormal(-textDir.y, textDir.x);

    if (qAbs(textDir.y) > qAbs(textDir.x)) {
        if (textNormal.x > 1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
    } else {
        if (textNormal.y < -1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
    }
    double angleDeg = qRadiansToDegrees(qAtan2(textDir.y, textDir.x));
    MyVector textPos = textBase + textNormal * textGap;

    MyText textData;
    textData.text = finalTxt;
    textData.pos = textPos;
    textData.rotation = angleDeg;
    textData.fontSize = style.textSize * finalScale;
    textData.fontFamily = style.fontFamily;

    TextItem *ti = new TextItem(textData);
    LayerStyle tls = applyItemStyle(ti, style.dimTextLayer);
    ti->setPen(QPen(tls.color));
    items << ti;

    return items;
}