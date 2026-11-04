#include "lineardimensionitem.h"
#include "mymath.h"
#include "mygraphicsscene.h"
#include "lineitem.h"
#include "polylineitem.h"
#include "textitem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

LinearDimensionItem::LinearDimensionItem(const MyDimension &data, QGraphicsItem *parent)
    : BaseDimensionItem(data, parent) {
    m_data.dimensionType = DimensionType::Linear;
    updateGrips();
    setGripsVisible(false);
}

LinearDimensionItem::~LinearDimensionItem() {}

CadItem *LinearDimensionItem::clone() const {
    auto *item = new LinearDimensionItem(m_data);
    item->setLayerName(layerName());
    item->setPen(pen());
    return item;
}

QList<SnapPoint> LinearDimensionItem::getSnapPoints() const {
    QList<SnapPoint> points = BaseDimensionItem::getSnapPoints();

    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;

    MyVector v = p2 - p1;
    double len = v.magnitude();
    if (len < 1e-6) return points;

    MyVector dir;
    if (m_data.useFixedAngle) {
        double rad = qDegreesToRadians(m_data.fixedAngle);
        dir = MyVector(qCos(rad), qSin(rad));
        if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) {
            dir = dir * -1.0;
        }
    } else {
        dir = v / len;
    }
    MyVector normal(-dir.y, dir.x);
    double h = MyMath::dotProduct(p3 - p1, normal);

    MyVector d1 = p1 + normal * h;
    double measurement = m_data.useFixedAngle ? std::abs(MyMath::dotProduct(p2 - p1, dir)) : len;
    MyVector d2 = d1 + dir * measurement;

    // 화살표 끝점 스냅 추가
    points << SnapPoint{d1, SnapType::Endpoint};
    points << SnapPoint{d2, SnapType::Endpoint};

    return points;
}

void LinearDimensionItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
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

    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;

    MyVector v = p2 - p1;
    double len = v.magnitude();
    if (len < 1e-6) return;

    MyVector dir;
    if (m_data.useFixedAngle) {
        double rad = qDegreesToRadians(m_data.fixedAngle);
        dir = MyVector(qCos(rad), qSin(rad));
        if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) {
            dir = dir * -1.0;
        }
    } else {
        dir = v / len;
    }
    MyVector normal(-dir.y, dir.x);
    double h = MyMath::dotProduct(p3 - p1, normal);

    MyVector d1 = p1 + normal * h;
    double measurement = len;
    if (m_data.useFixedAngle) measurement = std::abs(MyMath::dotProduct(p2 - p1, dir));
    MyVector d2 = d1 + dir * (m_data.useFixedAngle ? MyMath::dotProduct(p2 - p1, dir) : len);

    if (std::abs(h) > 1e-6) {
        double extOffset = style.extOffset * finalScale;
        double extExtend = style.extExtend * finalScale;

        double sgn = (h > 0) ? 1.0 : -1.0;
        double from1 = (m_data.extChainH1 > 0.0 && m_data.extChainH1 < std::abs(h))
                           ? m_data.extChainH1 + extExtend : extOffset;
        double from2 = (m_data.extChainH2 > 0.0 && m_data.extChainH2 < std::abs(h))
                           ? m_data.extChainH2 + extExtend : extOffset;
        if (!m_data.hideExt1) {
            MyVector extStart1 = p1 + normal * (sgn * from1);
            MyVector extEnd1 = d1 + normal * (h > 0 ? extExtend : -extExtend);
            painter->drawLine(extStart1.toPointF(), extEnd1.toPointF());
        }
        if (!m_data.hideExt2) {
            MyVector extStart2 = p2 + normal * (sgn * from2);
            MyVector extEnd2 = d2 + normal * (h > 0 ? extExtend : -extExtend);
            painter->drawLine(extStart2.toPointF(), extEnd2.toPointF());
        }
    }

    double arrowSize = style.arrowSize * finalScale;
    bool autoFlip = (measurement < (arrowSize * 3.0));
    bool flip1 = m_data.flipArrow1 || autoFlip;
    bool flip2 = m_data.flipArrow2 || autoFlip;

    double extStart = 0.0;
    double extEnd = 0.0;

    if (flip1 && !m_data.hideArrow1) extStart = arrowSize * 1.5;
    if (flip2 && !m_data.hideArrow2) extEnd = arrowSize * 1.5;

    //  지시선이 아닐 때만 메인 치수선 연장 (DimMoveTextTool 호환)
    if (m_data.useCustomTextPos && !m_data.useLeader) {
        QString baseVal = QString::number(measurement, 'f', style.precision);
        QString mTxt, uTxt, lTxt;
        double mW, tW, totalW;
        calculateTextLayout(measurement, baseVal, style, finalScale, mTxt, uTxt, lTxt, mW, tW, totalW);

        double t = MyMath::dotProduct(m_data.customTextPos - d1, dir);
        double textTailLen = (totalW / 2.0) + (2.0 * finalScale);

        if (t < 0) extStart = qMax(extStart, -t + textTailLen);
        else if (t > measurement) extEnd = qMax(extEnd, (t - measurement) + textTailLen);
    }

    MyVector lineStart = d1 - dir * extStart;
    MyVector lineEnd = d2 + dir * extEnd;
    painter->drawLine(lineStart.toPointF(), lineEnd.toPointF());

    if (!m_data.hideArrow1) {
        MyVector arr1Dir = flip1 ? dir : (dir * -1.0);
        drawArrow(painter, d1, arr1Dir, arrowSize, painter->pen().color(), style.arrowStyle);
    }
    if (!m_data.hideArrow2) {
        MyVector arr2Dir = flip2 ? (dir * -1.0) : dir;
        drawArrow(painter, d2, arr2Dir, arrowSize, painter->pen().color(), style.arrowStyle);
    }

    MyVector mid = (d1 + d2) * 0.5;
    double textGap = style.textGap * finalScale;
    MyVector textNormal = normal;

    if (qAbs(dir.y) > qAbs(dir.x)) {
        if (textNormal.x > 1e-9) { textNormal = textNormal * -1.0; dir = dir * -1.0; }
    } else {
        if (textNormal.y < -1e-9) { textNormal = textNormal * -1.0; dir = dir * -1.0; }
    }

    double angle = qRadiansToDegrees(qAtan2(dir.y, dir.x));
    MyVector textPos = mid + textNormal * textGap;

    // ====================================================================
    // [신규 로직] 지시선(Leader) 렌더링 (DimLeaderTool 모드일 때만 실행)
    // ====================================================================
    if (m_data.useCustomTextPos && m_data.useLeader) {
        QString baseVal = QString::number(measurement, 'f', style.precision);
        QString mTxt, uTxt, lTxt;
        double mW, tW, totalW;
        calculateTextLayout(measurement, baseVal, style, finalScale, mTxt, uTxt, lTxt, mW, tW, totalW);

        // 치수선의 기본 텍스트 위치(textPos)에서 마우스가 클릭한 곳으로 지시선 연결
        painter->drawLine(textPos.toPointF(), m_data.customTextPos.toPointF());

        // 마우스 클릭 위치에서 텍스트 폭만큼 수평선(Landing) 밑줄 그리기
        double sign = (m_data.customTextPos.x >= textPos.x) ? 1.0 : -1.0;
        double landingLen = totalW + (2.0 * finalScale);
        MyVector landingEnd = m_data.customTextPos + MyVector(sign * landingLen, 0);
        painter->drawLine(m_data.customTextPos.toPointF(), landingEnd.toPointF());
    }

    QString baseVal = QString::number(measurement, 'f', style.precision);
    drawDimensionText(painter, measurement, baseVal, textPos, angle, style, finalScale);
}

QPainterPath LinearDimensionItem::shape() const {
    QPainterPath path = geometryPath();
    QPainterPathStroker stroker; stroker.setWidth(1.0);
    QPainterPath finalPath = stroker.createStroke(path);

    MyGraphicsScene *m_scene = qobject_cast<MyGraphicsScene *>(scene());
    DimensionStyle style = m_scene ? m_scene->dimensionStyle() : DimensionStyle();
    double finalScale = m_scene ? m_scene->drawingScale() : 1.0;

    MyVector p1 = m_data.start; MyVector p2 = m_data.end; MyVector p3 = m_data.dimLine;
    MyVector v = p2 - p1;
    double len = v.magnitude();
    if (len < 1e-6) return finalPath;

    MyVector dir;
    if (m_data.useFixedAngle) {
        double rad = qDegreesToRadians(m_data.fixedAngle);
        dir = MyVector(qCos(rad), qSin(rad));
        if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) dir = dir * -1.0;
    } else {
        dir = v / len;
    }
    MyVector normal(-dir.y, dir.x);
    double h = MyMath::dotProduct(p3 - p1, normal);
    MyVector d1 = p1 + normal * h;
    MyVector d2 = d1 + dir * (m_data.useFixedAngle ? MyMath::dotProduct(p2 - p1, dir) : len);
    MyVector mid = (d1 + d2) * 0.5;

    double measurement = m_data.useFixedAngle ? std::abs(MyMath::dotProduct(p2 - p1, dir)) : len;
    QString baseVal = QString::number(measurement, 'f', style.precision);
    double textGap = style.textGap * finalScale;
    MyVector textNormal = normal;

    if (qAbs(dir.y) > qAbs(dir.x)) {
        if (textNormal.x > 1e-9) { textNormal = textNormal * -1.0; dir = dir * -1.0; }
    } else {
        if (textNormal.y < -1e-9) { textNormal = textNormal * -1.0; dir = dir * -1.0; }
    }

    double angleDeg = qRadiansToDegrees(qAtan2(dir.y, dir.x));
    MyVector textPos = mid + textNormal * textGap;

    finalPath.addPath(createTextBoundingPath(measurement, baseVal, textPos, angleDeg));
    finalPath.setFillRule(Qt::WindingFill);

    return finalPath;
}

QPainterPath LinearDimensionItem::geometryPath() const {
    QPainterPath path;
    MyVector p1 = m_data.start; MyVector p2 = m_data.end; MyVector p3 = m_data.dimLine;
    MyVector v = p2 - p1;
    double len = v.magnitude();
    if (len < 1e-6) return path;

    MyVector dir;
    if (m_data.useFixedAngle) {
        double rad = qDegreesToRadians(m_data.fixedAngle);
        dir = MyVector(qCos(rad), qSin(rad));
        if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) dir = dir * -1.0;
    } else {
        dir = v / len;
    }
    MyVector normal(-dir.y, dir.x);
    double h = MyMath::dotProduct(p3 - p1, normal);
    MyVector d1 = p1 + normal * h;
    MyVector d2 = d1 + dir * (m_data.useFixedAngle ? MyMath::dotProduct(p2 - p1, dir) : len);

    double measurement = m_data.useFixedAngle ? std::abs(MyMath::dotProduct(p2 - p1, dir)) : len;

    MyGraphicsScene *m_scene = qobject_cast<MyGraphicsScene *>(scene());
    DimensionStyle style = m_scene ? m_scene->dimensionStyle() : DimensionStyle();
    double finalScale = m_scene ? m_scene->drawingScale() : 1.0;
    double arrowSize = style.arrowSize * finalScale;

    bool autoFlip = (measurement < (arrowSize * 3.0));
    bool flip1 = m_data.flipArrow1 || autoFlip;
    bool flip2 = m_data.flipArrow2 || autoFlip;

    double extStart = 0.0;
    double extEnd = 0.0;
    if (flip1 && !m_data.hideArrow1) extStart = arrowSize * 1.5;
    if (flip2 && !m_data.hideArrow2) extEnd = arrowSize * 1.5;

    // [분기 추가] 지시선이 아닐 때만 선택 Path 연장
    if (m_data.useCustomTextPos && !m_data.useLeader) {
        double t = MyMath::dotProduct(m_data.customTextPos - d1, dir);
        if (t < 0) extStart = qMax(extStart, -t + 20.0);
        else if (t > measurement) extEnd = qMax(extEnd, (t - measurement) + 20.0);
    }

    MyVector lineStart = d1 - dir * extStart;
    MyVector lineEnd = d2 + dir * extEnd;

    path.moveTo(p1.toPointF());
    path.lineTo(d1.toPointF());
    path.moveTo(lineStart.toPointF());
    path.lineTo(lineEnd.toPointF());
    path.moveTo(d2.toPointF());
    path.lineTo(p2.toPointF());

    // [신규] 지시선(Leader) 객체 선택 판정용 Path 추가
    if (m_data.useCustomTextPos && m_data.useLeader) {
        MyVector mid = (d1 + d2) * 0.5;
        MyVector textNormal2 = normal;
        if (qAbs(dir.y) > qAbs(dir.x)) {
            if (textNormal2.x > 1e-9) { textNormal2 = textNormal2 * -1.0; }
        } else {
            if (textNormal2.y < -1e-9) { textNormal2 = textNormal2 * -1.0; }
        }
        MyVector textPos = mid + textNormal2 * (style.textGap * finalScale);

        double sign = (m_data.customTextPos.x >= textPos.x) ? 1.0 : -1.0;
        MyVector landingEnd = m_data.customTextPos + MyVector(sign * 20.0, 0);

        path.moveTo(textPos.toPointF());
        path.lineTo(m_data.customTextPos.toPointF());
        path.lineTo(landingEnd.toPointF());
    }

    return path;
}

double LinearDimensionItem::distanceTo(const MyVector &pos) const {
    if (shape().contains(pos.toPointF())) {
        return 0.0;
    }

    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;

    MyVector v = p2 - p1;
    double len = v.magnitude();
    if (len < 1e-6) return pos.distanceTo(p1);

    MyVector dir;
    if (m_data.useFixedAngle) {
        double rad = qDegreesToRadians(m_data.fixedAngle);
        dir = MyVector(qCos(rad), qSin(rad));
        if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) {
            dir = dir * -1.0;
        }
    } else {
        dir = v / len;
    }
    MyVector normal(-dir.y, dir.x);
    double h = MyMath::dotProduct(p3 - p1, normal);

    MyVector d1 = p1 + normal * h;
    MyVector d2 = d1 + dir * (m_data.useFixedAngle ? MyMath::dotProduct(p2 - p1, dir) : len);

    auto distToSegment = [&](const MyVector &s, const MyVector &e, const MyVector &p) {
        MyVector ab = e - s;
        MyVector ap = p - s;
        double l2 = MyMath::dotProduct(ab, ab);
        if (l2 < 1e-9) return p.distanceTo(s);
        double t = MyMath::dotProduct(ap, ab) / l2;
        if (t < 0.0) t = 0.0;
        else if (t > 1.0) t = 1.0;
        return p.distanceTo(s + ab * t);
    };

    double d = distToSegment(p1, d1, pos);
    d = qMin(d, distToSegment(p2, d2, pos));
    d = qMin(d, distToSegment(d1, d2, pos));
    return d;
}

QPainterPath LinearDimensionItem::getGhostShape(int index, const MyVector &pos) const {
    LinearDimensionItem ghost(m_data); ghost.applyGripMove(index, pos); return ghost.geometryPath();
}
QPainterPath LinearDimensionItem::getMovedPath(const MyVector &delta) const {
    LinearDimensionItem ghost(m_data); ghost.move(delta); return ghost.geometryPath();
}
QPainterPath LinearDimensionItem::getMirroredPath(const MyVector &p1, const MyVector &p2) const {
    LinearDimensionItem ghost(m_data); ghost.mirror(p1, p2); return ghost.geometryPath();
}
QPainterPath LinearDimensionItem::getRotatedPath(const MyVector &base, double angle) const {
    LinearDimensionItem ghost(m_data); ghost.rotate(base, angle); return ghost.geometryPath();
}
QPainterPath LinearDimensionItem::getScaledPath(const MyVector &base, double scaleFactor) const {
    LinearDimensionItem ghost(m_data); ghost.scale(base, scaleFactor); return ghost.geometryPath();
}
QPainterPath LinearDimensionItem::getStretchedPath(const QList<int> &indices, const MyVector &delta) const {
    LinearDimensionItem ghost(m_data); ghost.stretch(indices, delta); return ghost.geometryPath();
}

QList<CadItem *> LinearDimensionItem::explode() const {
    QList<CadItem *> items;
    MyGraphicsScene *scenePtr = qobject_cast<MyGraphicsScene *>(scene());
    if (!scenePtr) return items;

    double sceneScale = scenePtr->drawingScale();
    DimensionStyle style = scenePtr->dimensionStyle();
    double finalScale = sceneScale;

    auto applyItemStyle = [&](CadItem *item, const QString &layerName) {
        QString targetLayer = layerName.isEmpty() ? m_layerName : layerName;
        LayerStyle ls = scenePtr->getLayerStyle(targetLayer);
        QPen p(ls.color); p.setCosmetic(true); p.setWidthF(1.0);
        if (!ls.dashPattern.isEmpty()) { p.setStyle(Qt::CustomDashLine); p.setDashPattern(ls.dashPattern); }
        item->setLayerName(targetLayer); item->setPen(p);
        return ls;
    };

    MyVector p1 = m_data.start; MyVector p2 = m_data.end; MyVector p3 = m_data.dimLine;
    MyVector v = p2 - p1;
    double len = v.magnitude();
    if (len < 1e-6) return items;

    MyVector dir;
    if (m_data.useFixedAngle) {
        double rad = qDegreesToRadians(m_data.fixedAngle); dir = MyVector(qCos(rad), qSin(rad));
        if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) dir = dir * -1.0;
    } else { dir = v / len; }

    MyVector normal(-dir.y, dir.x);
    double h = MyMath::dotProduct(p3 - p1, normal);
    MyVector d1 = p1 + normal * h;
    double measurement = m_data.useFixedAngle ? std::abs(MyMath::dotProduct(p2 - p1, dir)) : len;
    MyVector d2 = d1 + dir * measurement;

    if (std::abs(h) > 1e-6) {
        double extOffset = style.extOffset * finalScale; double extExtend = style.extExtend * finalScale;
        double sgn = (h > 0) ? 1.0 : -1.0;
        double from1 = (m_data.extChainH1 > 0.0 && m_data.extChainH1 < std::abs(h)) ? m_data.extChainH1 + extExtend : extOffset;
        double from2 = (m_data.extChainH2 > 0.0 && m_data.extChainH2 < std::abs(h)) ? m_data.extChainH2 + extExtend : extOffset;
        if (!m_data.hideExt1) {
            LineItem *l1 = new LineItem(p1 + normal * (sgn * from1), d1 + normal * (h > 0 ? extExtend : -extExtend));
            applyItemStyle(l1, style.dimLineLayer); items << l1;
        }
        if (!m_data.hideExt2) {
            LineItem *l2 = new LineItem(p2 + normal * (sgn * from2), d2 + normal * (h > 0 ? extExtend : -extExtend));
            applyItemStyle(l2, style.dimLineLayer); items << l2;
        }
    }

    double arrowSize = style.arrowSize * finalScale;
    bool autoFlip = (measurement < (arrowSize * 3.0));
    bool flip1 = m_data.flipArrow1 || autoFlip;
    bool flip2 = m_data.flipArrow2 || autoFlip;

    double extStart = 0.0; double extEnd = 0.0;
    if (flip1 && !m_data.hideArrow1) extStart = arrowSize * 1.5;
    if (flip2 && !m_data.hideArrow2) extEnd = arrowSize * 1.5;

    // [분기] 지시선이 아닐 때만 라인 분해에 연장선 포함
    if (m_data.useCustomTextPos && !m_data.useLeader) {
        QString baseVal = QString::number(measurement, 'f', style.precision);
        QString mTxt, uTxt, lTxt; double mW, tW, totalW;
        calculateTextLayout(measurement, baseVal, style, finalScale, mTxt, uTxt, lTxt, mW, tW, totalW);

        double t = MyMath::dotProduct(m_data.customTextPos - d1, dir);
        double textTailLen = (totalW / 2.0) + (2.0 * finalScale);

        if (t < 0) extStart = qMax(extStart, -t + textTailLen);
        else if (t > measurement) extEnd = qMax(extEnd, (t - measurement) + textTailLen);
    }

    MyVector lineStart = d1 - dir * extStart;
    MyVector lineEnd = d2 + dir * extEnd;

    LineItem *dl = new LineItem(lineStart, lineEnd);
    applyItemStyle(dl, style.dimLineLayer);
    items << dl;

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

    if (!m_data.hideArrow1) addArrow(d1, flip1 ? dir : (dir * -1.0));
    if (!m_data.hideArrow2) addArrow(d2, flip2 ? (dir * -1.0) : dir);

    QString baseVal = QString::number(measurement, 'f', style.precision);
    QString mainText, upperText, lowerText;
    double mainW, tolW, totalW;
    calculateTextLayout(measurement, baseVal, style, finalScale, mainText, upperText, lowerText, mainW, tolW, totalW);

    MyVector mid = (d1 + d2) * 0.5;
    MyVector textNormal = normal;
    if (qAbs(dir.y) > qAbs(dir.x)) {
        if (textNormal.x > 1e-9) { textNormal = textNormal * -1.0; dir = dir * -1.0; }
    } else {
        if (textNormal.y < -1e-9) { textNormal = textNormal * -1.0; dir = dir * -1.0; }
    }
    double angleDeg = qRadiansToDegrees(qAtan2(dir.y, dir.x));
    MyVector textPos = mid + textNormal * (style.textGap * finalScale);

    // [신규 분해] 지시선 객체 선분 추가 분해
    if (m_data.useCustomTextPos && m_data.useLeader) {
        double sign = (m_data.customTextPos.x >= textPos.x) ? 1.0 : -1.0;
        double landingLen = totalW + (2.0 * finalScale);
        MyVector landingEnd = m_data.customTextPos + MyVector(sign * landingLen, 0);

        LineItem *leaderLine = new LineItem(textPos, m_data.customTextPos);
        applyItemStyle(leaderLine, style.dimLineLayer);
        items << leaderLine;

        LineItem *landingLine = new LineItem(m_data.customTextPos, landingEnd);
        applyItemStyle(landingLine, style.dimLineLayer);
        items << landingLine;
    }

    QString finalTxt = mainText;
    if (!upperText.isEmpty() || !lowerText.isEmpty()) {
        finalTxt += " " + upperText.trimmed() + " " + lowerText.trimmed();
    }

    MyText textData;
    textData.text = finalTxt;
    textData.pos = textPos;
    textData.rotation = angleDeg;
    textData.fontSize = style.textSize * finalScale;
    textData.fontFamily = style.fontFamily;

    // 지시선(Leader) 모드일 땐 텍스트 위치를 커스텀으로 분해
    if (m_data.useCustomTextPos && m_data.useLeader) {
        textData.pos = m_data.customTextPos;
        textData.rotation = 0.0;
    }

    TextItem *ti = new TextItem(textData);
    LayerStyle tls = applyItemStyle(ti, style.dimTextLayer);
    ti->setPen(QPen(tls.color));
    items << ti;

    return items;
}