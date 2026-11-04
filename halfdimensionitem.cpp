#include "halfdimensionitem.h"
#include "mymath.h"
#include "myline.h"
#include "mygraphicsscene.h"
#include "lineitem.h"
#include "polylineitem.h"
#include "textitem.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <QFontMetricsF>

HalfDimensionItem::HalfDimensionItem(const MyDimension &data, QGraphicsItem *parent)
    : BaseDimensionItem(data, parent) {
    m_data.dimensionType = DimensionType::HalfDimension;
    updateGrips();
    setGripsVisible(false);
}

HalfDimensionItem::~HalfDimensionItem() {}

CadItem *HalfDimensionItem::clone() const {
    auto *item = new HalfDimensionItem(m_data);
    item->setLayerName(layerName());
    item->setPen(pen());
    return item;
}

QList<SnapPoint> HalfDimensionItem::getSnapPoints() const {
    QList<SnapPoint> points = BaseDimensionItem::getSnapPoints();

    // 주요 기하학적 기준점 및 방향 벡터 계산 (paint와 동일 로직)
    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;

    MyVector dir;
    if (m_data.useFixedAngle) {
        double rad = qDegreesToRadians(m_data.fixedAngle);
        dir = MyVector(qCos(rad), qSin(rad));
        if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) dir = dir * -1.0;
    } else {
        if (std::abs(p2.x - p1.x) > std::abs(p2.y - p1.y)) {
            dir = MyVector(p2.x > p1.x ? 1 : -1, 0);
        } else {
            dir = MyVector(0, p2.y > p1.y ? 1 : -1);
        }
    }

    MyVector d1 = p3 + dir * MyMath::dotProduct(p1 - p3, dir);
    // dc point is also relevant for snapping
    MyVector dc = p3 + dir * MyMath::dotProduct(p2 - p3, dir);

    // 화살표 끝점 (중심측) 스냅 추가
    points << SnapPoint{d1, SnapType::Endpoint};
    // 측정 끝점 (치수선 상의 위치) 스냅 추가
    points << SnapPoint{dc, SnapType::Endpoint};

    return points;
}



// void HalfDimensionItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
//     Q_UNUSED(widget);

//     // [통합 로직 1 & 2] 하이라이트 및 펜/스타일 설정
//     drawSelectionHighlight(painter, option);

//     double finalScale;
//     DimensionStyle style = getDimStyle(finalScale);

//     double viewScale = 1.0;
//     if (!painter->transform().isIdentity()) {
//         viewScale = 1.0 / qAbs(painter->transform().m11());
//     }
//     setupDimPen(painter, style, viewScale);

//     MyVector p1 = m_data.start;
//     MyVector p2 = m_data.end;
//     MyVector p3 = m_data.dimLine;

//     // --- 기하학 연산 ---
//     MyVector dir, normal;
//     if (m_data.useFixedAngle) {
//         double rad = qDegreesToRadians(m_data.fixedAngle);
//         dir = MyVector(qCos(rad), qSin(rad));
//         if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) dir = dir * -1.0;
//     } else {
//         if (std::abs(p2.x - p1.x) > std::abs(p2.y - p1.y)) {
//             dir = MyVector(p2.x > p1.x ? 1 : -1, 0);
//         } else {
//             dir = MyVector(0, p2.y > p1.y ? 1 : -1);
//         }
//     }
//     normal = MyVector(-dir.y, dir.x);

//     double measurement = std::abs(MyMath::dotProduct(p2 - p1, dir));
//     double actualValue = measurement * 2.0;

//     double h = MyMath::dotProduct(p3 - p1, normal);
//     MyVector d1 = p3 + dir * MyMath::dotProduct(p1 - p3, dir);
//     MyVector dc = p3 + dir * MyMath::dotProduct(p2 - p3, dir);

//  //   MyVector d2 = dc + dir * (15.0 * finalScale);
//     double extensionLen = measurement  + 12.0;
//     MyVector d2 = dc + dir * extensionLen;

//     // --- 보조선 그리기 ---
//     if (!m_data.hideExt1) {
//         double extOffsetVal = style.extOffset * finalScale;
//         double extExtendVal = style.extExtend * finalScale;
//         double signH = (h >= 0) ? 1.0 : -1.0;
//         MyVector extDir = normal * signH;

//         MyVector extStart = p1 + extDir * extOffsetVal;
//         MyVector extEnd = d1 + extDir * extExtendVal;
//         painter->drawLine(extStart.toPointF(), extEnd.toPointF());
//     }

//     painter->drawLine(d1.toPointF(), d2.toPointF());
//     if (!m_data.hideArrow1) drawArrow(painter, d1, dir * -1.0, style.arrowSize * finalScale, painter->pen().color());

//     // --- 텍스트 렌더링 준비 ---
//     // [수정] 복잡한 문자열 조립은 버리고 기본값만 추출
//     QString baseVal = QString::number(actualValue, 'f', style.precision);

//     double textGap = style.textGap * finalScale;
//     MyVector textDir = dir;
//     MyVector textNormal(-textDir.y, textDir.x);

//     if (qAbs(textDir.y) > qAbs(textDir.x)) {
//         if (textNormal.x > 1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
//     } else {
//         if (textNormal.y < -1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
//     }

//     double angle = qRadiansToDegrees(qAtan2(textDir.y, textDir.x));
//     MyVector textPos = dc + textNormal * textGap;

//     // [통합 로직 3] 텍스트 및 공차 그리기 위임
//     drawDimensionText(painter, actualValue, baseVal, textPos, angle, style, finalScale);
// }
void HalfDimensionItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget);

    // 1. 선택 하이라이트 및 펜 스타일 설정
    drawSelectionHighlight(painter, option);

    double finalScale;
    DimensionStyle style = getDimStyle(finalScale);

    double viewScale = 1.0;
    if (!painter->transform().isIdentity()) {
        double m11abs = qAbs(painter->transform().m11());
        if (m11abs > 0.0) viewScale = 1.0 / m11abs;
    }
    setupDimPen(painter, style, viewScale);

    // 2. 주요 기하학적 기준점 및 방향 벡터 계산
    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;

    MyVector dir, normal;
    if (m_data.useFixedAngle) {
        double rad = qDegreesToRadians(m_data.fixedAngle);
        dir = MyVector(qCos(rad), qSin(rad));
        if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) dir = dir * -1.0;
    } else {
        if (std::abs(p2.x - p1.x) > std::abs(p2.y - p1.y)) {
            dir = MyVector(p2.x > p1.x ? 1 : -1, 0);
        } else {
            dir = MyVector(0, p2.y > p1.y ? 1 : -1);
        }
    }
    normal = MyVector(-dir.y, dir.x);

    double measurement = std::abs(MyMath::dotProduct(p2 - p1, dir));
    double actualValue = measurement * 2.0;

    double h = MyMath::dotProduct(p3 - p1, normal);
    MyVector d1 = p3 + dir * MyMath::dotProduct(p1 - p3, dir);
    MyVector dc = p3 + dir * MyMath::dotProduct(p2 - p3, dir);

    // 3. 텍스트 레이아웃 및 폭(Width) 사전 계산
    QString baseVal = QString::number(actualValue, 'f', style.precision);
    QString mainText, upperText, lowerText;
    double mainW, tolW, totalW;
    calculateTextLayout(actualValue, baseVal, style, finalScale, mainText, upperText, lowerText, mainW, tolW, totalW);

    // =========================================================================
    // [핵심 로직] 치수선 연장 및 화살표 반전 스마트 판별
    // =========================================================================
    MyVector lineStart = d1;
    MyVector lineEnd = dc;
    MyVector arrowDir1 = dir * -1.0; // 기본 화살표 방향 (d1에서 안쪽으로)

    if (m_data.useCustomTextPos) {
        // 커스텀 텍스트 위치를 치수선 축에 투영
        double tCustom = MyMath::dotProduct(m_data.customTextPos - d1, dir);
        double tEnd = MyMath::dotProduct(dc - d1, dir);

        // 연장 길이 = 텍스트 절반 폭 + 여백 패딩
        double extensionLen = (totalW / 2.0) + (2.0 * finalScale);

        if (tCustom < 0) {
            // 텍스트가 d1(중심점) 기준 왼쪽 바깥으로 빠짐 -> 왼쪽으로 선 연장
            lineStart = m_data.customTextPos - dir * extensionLen;
            lineEnd = dc;
            arrowDir1 = dir; // 화살표 방향을 바깥에서 안쪽으로 뒤집기
        } else if (tCustom > tEnd) {
            // 텍스트가 dc(끝점) 기준 오른쪽 바깥으로 빠짐 -> 오른쪽으로 선 연장
            lineStart = d1;
            lineEnd = m_data.customTextPos + dir * extensionLen;
            // 왼쪽 화살표는 원래 방향 유지
        } else {
            // 텍스트가 두 점 사이에 있음
            lineStart = d1;
            lineEnd = dc;
        }
    } else {
        // [기본 상태] 텍스트가 이동하지 않았을 때: 측정 길이의 1/10만큼 자동 연장
        double defaultExtensionLen = measurement /10.0;
        lineEnd = dc + dir * defaultExtensionLen;
    }

    // 4. 보조선(Extension Line) 그리기
    if (!m_data.hideExt1) {
        double extOffsetVal = style.extOffset * finalScale;
        double extExtendVal = style.extExtend * finalScale;
        double signH = (h >= 0) ? 1.0 : -1.0;
        MyVector extDir = normal * signH;

        MyVector extStart = p1 + extDir * extOffsetVal;
        MyVector extEnd = d1 + extDir * extExtendVal;
        painter->drawLine(extStart.toPointF(), extEnd.toPointF());
    }

    // 5. 치수선(Dimension Line) 본체 및 화살표 그리기
    painter->drawLine(lineStart.toPointF(), lineEnd.toPointF());

    if (!m_data.hideArrow1) {
        drawArrow(painter, d1, arrowDir1, style.arrowSize * finalScale, painter->pen().color(), style.arrowStyle);
    }

    // 6. 텍스트 정렬 각도 및 기본 기준점(pos) 계산
    double textGap = style.textGap * finalScale;
    MyVector textDir = dir;
    MyVector textNormal(-textDir.y, textDir.x);

    if (qAbs(textDir.y) > qAbs(textDir.x)) {
        if (textNormal.x > 1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
    } else {
        if (textNormal.y < -1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
    }

    double angle = qRadiansToDegrees(qAtan2(textDir.y, textDir.x));

    // 이동하지 않았을 때의 기본 텍스트 앵커 포인트
    MyVector defaultTextPos = dc + textNormal * textGap;

    // 7. BaseDimensionItem의 수정된 drawDimensionText 호출
    // (이 함수 내부에서 m_data.useCustomTextPos 상태를 판별하여 최종 위치를 결정함)
    drawDimensionText(painter, actualValue, baseVal, defaultTextPos, angle, style, finalScale);
}


QPainterPath HalfDimensionItem::geometryPath() const {
    QPainterPath path;
    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;
    MyVector dir, normal;
    if (m_data.useFixedAngle) {
        double rad = qDegreesToRadians(m_data.fixedAngle);
        dir = MyVector(qCos(rad), qSin(rad));
        if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) dir = dir * -1.0;
    } else {
        if (std::abs(p2.x - p1.x) > std::abs(p2.y - p1.y)) {
            dir = MyVector(p2.x > p1.x ? 1 : -1, 0);
        } else {
            dir = MyVector(0, p2.y > p1.y ? 1 : -1);
        }
    }
    normal = MyVector(-dir.y, dir.x);

    double measurement = std::abs(MyMath::dotProduct(p2 - p1, dir));
    double h = MyMath::dotProduct(p3 - p1, normal);
    MyVector d1 = p3 + dir * MyMath::dotProduct(p1 - p3, dir);
    MyVector dc = p3 + dir * MyMath::dotProduct(p2 - p3, dir);

    MyGraphicsScene *scenePtr = qobject_cast<MyGraphicsScene *>(scene());
    double drawingScale = scenePtr ? scenePtr->drawingScale() : 1.0;
    DimensionStyle style = scenePtr ? scenePtr->dimensionStyle() : DimensionStyle();

 //   MyVector d2 = dc + dir * (15.0 * drawingScale);

     double extensionLen = measurement  / 10.0;
     MyVector d2 = dc + dir * extensionLen;

    path.moveTo(d1.toPointF());
    path.lineTo(d2.toPointF());

    // 치수보조선 (Extension Line)
    if (!m_data.hideExt1) {
        double extOffsetVal = style.extOffset * drawingScale;
        double extExtendVal = style.extExtend * drawingScale;
        double signH = (h >= 0) ? 1.0 : -1.0;
        MyVector extDir = normal * signH;

        MyVector extStart = p1 + extDir * extOffsetVal;
        MyVector extEnd = d1 + extDir * extExtendVal;
        path.moveTo(extStart.toPointF());
        path.lineTo(extEnd.toPointF());
    }

    return path;
}



QPainterPath HalfDimensionItem::shape() const {
    QPainterPath path = geometryPath();
    QPainterPathStroker stroker;
    stroker.setWidth(2.0);
    QPainterPath finalPath = stroker.createStroke(path);

    MyGraphicsScene *m_scene = qobject_cast<MyGraphicsScene *>(scene());
    DimensionStyle style = m_scene ? m_scene->dimensionStyle() : DimensionStyle();
    double finalScale = m_scene ? m_scene->drawingScale() : 1.0;

    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;

    MyVector dir, normal;
    if (m_data.useFixedAngle) {
        double rad = qDegreesToRadians(m_data.fixedAngle);
        dir = MyVector(qCos(rad), qSin(rad));
        if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) dir = dir * -1.0;
    } else {
        if (std::abs(p2.x - p1.x) > std::abs(p2.y - p1.y)) {
            dir = MyVector(p2.x > p1.x ? 1 : -1, 0);
        } else {
            dir = MyVector(0, p2.y > p1.y ? 1 : -1);
        }
    }
    normal = MyVector(-dir.y, dir.x);
 //   double h = MyMath::dotProduct(p3 - p1, normal);
    MyVector dc = p3 + dir * MyMath::dotProduct(p2 - p3, dir);

    double actualValue = std::abs(MyMath::dotProduct(p2 - p1, dir)) * 2.0;

    // [수정] 기본값만 추출
    QString baseVal = QString::number(actualValue, 'f', style.precision);

    double textGap = style.textGap * finalScale;
    MyVector textDir = dir;
    MyVector textNormal(-textDir.y, textDir.x);

    if (qAbs(textDir.y) > qAbs(textDir.x)) {
        if (textNormal.x > 1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
    } else {
        if (textNormal.y < -1e-9) { textNormal = textNormal * -1.0; textDir = textDir * -1.0; }
    }

    double angleDeg = qRadiansToDegrees(qAtan2(textDir.y, textDir.x));
    MyVector textPos = dc + textNormal * textGap;

    // [수정] 공차 폭까지 반영된 새로운 영역 계산 함수 호출
    finalPath.addPath(createTextBoundingPath(actualValue, baseVal, textPos, angleDeg));

    finalPath.setFillRule(Qt::WindingFill);
    return finalPath;
}



std::pair<MyVector, MyVector> HalfDimensionItem::d_p1p2_on_dimline() const {
    MyVector p1 = m_data.start;
    MyVector p2 = m_data.end;
    MyVector p3 = m_data.dimLine;
    bool isHorizontal = std::abs(p1.x - p2.x) > std::abs(p1.y - p2.y);
    if (isHorizontal) {
        return {MyVector(p1.x, p3.y), MyVector(p2.x, p3.y)};
    } else {
        return {MyVector(p3.x, p1.y), MyVector(p3.x, p2.y)};
    }
}

double HalfDimensionItem::distanceTo(const MyVector &pos) const {
    if (shape().contains(pos.toPointF())) {
        return 0.0;
    }

    auto pts = d_p1p2_on_dimline();
    MyLine line(pts.first, pts.second);
    return MyMath::getNearestPointOnSegment(pos, line).distanceTo(pos);
}

QPainterPath HalfDimensionItem::getGhostShape(int index, const MyVector &pos) const {
    HalfDimensionItem ghost(m_data);
    ghost.applyGripMove(index, pos);
    return ghost.geometryPath();
}

QPainterPath HalfDimensionItem::getMovedPath(const MyVector &delta) const {
    HalfDimensionItem ghost(m_data);
    ghost.move(delta);
    return ghost.geometryPath();
}

QPainterPath HalfDimensionItem::getMirroredPath(const MyVector &p1, const MyVector &p2) const {
    HalfDimensionItem ghost(m_data);
    ghost.mirror(p1, p2);
    return ghost.geometryPath();
}

QPainterPath HalfDimensionItem::getRotatedPath(const MyVector &base, double angle) const {
    HalfDimensionItem ghost(m_data);
    ghost.rotate(base, angle);
    return ghost.geometryPath();
}

QPainterPath HalfDimensionItem::getScaledPath(const MyVector &base, double factor) const {
    HalfDimensionItem ghost(m_data);
    ghost.scale(base, factor);
    return ghost.geometryPath();
}

QPainterPath HalfDimensionItem::getStretchedPath(const QList<int> &indices, const MyVector &delta) const {
    HalfDimensionItem ghost(m_data);
    ghost.stretch(indices, delta);
    return ghost.geometryPath();
}

QList<CadItem *> HalfDimensionItem::explode() const {
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

    MyVector dir, normal;
    if (m_data.useFixedAngle) {
        double rad = qDegreesToRadians(m_data.fixedAngle);
        dir = MyVector(qCos(rad), qSin(rad));
        if (MyMath::dotProduct(p2 - p1, dir) < -1e-9) dir = dir * -1.0;
    } else {
        if (std::abs(p2.x - p1.x) > std::abs(p2.y - p1.y)) {
            dir = MyVector(p2.x > p1.x ? 1 : -1, 0);
        } else {
            dir = MyVector(0, p2.y > p1.y ? 1 : -1);
        }
    }
    normal = MyVector(-dir.y, dir.x);

    double measurement = std::abs(MyMath::dotProduct(p2 - p1, dir));
    double actualValue = measurement * 2.0;

    double h = MyMath::dotProduct(p3 - p1, normal);
    MyVector d1 = p3 + dir * MyMath::dotProduct(p1 - p3, dir);
    MyVector dc = p3 + dir * MyMath::dotProduct(p2 - p3, dir);
//    MyVector d2 = dc + dir * (15.0 * finalScale);

      double extensionLen = measurement  / 10.0;
      MyVector d2 = dc + dir * extensionLen;

    if (!m_data.hideExt1) {
        double extOffsetVal = style.extOffset * finalScale;
        double extExtendVal = style.extExtend * finalScale;
        double signH = (h >= 0) ? 1.0 : -1.0;
        MyVector extDir = normal * signH;

        MyVector extStart = p1 + extDir * extOffsetVal;
        MyVector extEnd = d1 + extDir * extExtendVal;

        LineItem *extLine = new LineItem(extStart, extEnd);
        applyItemStyle(extLine, style.dimLineLayer);
        items << extLine;
    }

    LineItem *dl = new LineItem(d1, d2);
    applyItemStyle(dl, style.dimLineLayer);
    items << dl;

    double arrowSize = style.arrowSize * finalScale;
    if (!m_data.hideArrow1) {
        MyVector n(-dir.y, dir.x);
        MyVector back = d1 - (dir * -1.0) * arrowSize;
        MyVector side1 = back + n * (arrowSize * 0.35);
        MyVector side2 = back - n * (arrowSize * 0.35);
        PolylineItem *arrow = new PolylineItem({d1, side1, side2}, true);
        LayerStyle ls = applyItemStyle(arrow, style.dimLineLayer);
        arrow->setBrush(QBrush(ls.color));
        items << arrow;
    }

    // [수정] Explode 시 공차가 포함된 전체 텍스트 추출
    QString baseVal = QString::number(actualValue, 'f', style.precision);
    QString mainText, upperText, lowerText;
    double mainW, tolW, totalW;
    calculateTextLayout(actualValue, baseVal, style, finalScale, mainText, upperText, lowerText, mainW, tolW, totalW);

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

    MyVector textCenter = dc;
    MyVector textPos = textCenter + textNormal * textGap;

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