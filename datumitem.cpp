#include "datumitem.h"
#include "mymath.h"
#include "mygraphicsscene.h"
#include "lineitem.h"
#include "polylineitem.h"
#include "textitem.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "layer.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QPainterPathStroker>
#include <QFontMetricsF>
#include "gdtcalculator.h"
#include <QtMath>

DatumItem::DatumItem(const DatumData &data, QGraphicsItem *parent)
    : CadItem(parent), m_data(data)
{
    setZValue(1);
}

double DatumItem::getScale() const {
    if (m_overrideScale > 0) return m_overrideScale;
    if (MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene())) {
        return sc->drawingScale();
    }
    return 1.0;
}

QRectF DatumItem::boundingRect() const {
    return shape().boundingRect().adjusted(-2, -2, 2, 2);
}

QPainterPath DatumItem::geometryPath() const {
    QPainterPath path;
    double finalScale = getScale();
    MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
    DimensionStyle style;
    if (sc) style = sc->dimensionStyle();

    QList<MyVector> pts = m_data.leaderPoints;
    if (pts.isEmpty()) pts << m_data.startPoint << m_data.endPoint;
    if (pts.size() < 2) return path;

    MyVector pStart = pts.first();
    MyVector pEnd = pts.last();
    MyVector lastDir = (pEnd - pts[pts.size() - 2]).normalized();

    double arrowSize = style.arrowSize * finalScale * 1.5;
    if (arrowSize < 1e-6) arrowSize = 3.0 * finalScale * 1.5;

    double boxSize = GdtCalculator::datumBoxWidth(m_data.textHeight * finalScale, m_data.text);

    double triHeight = arrowSize * qSqrt(3.0) / 2.0;

    // 1. 삼각형 그리기 (첫 번째 포인트)
    // [수정] 베이스(Base)가 부착점에 있고, 꼭지점(Tip)이 지시선 방향을 향하도록 복구
    MyVector firstDir = (pts[1] - pts[0]).normalized();
    MyVector nFirst(-firstDir.y, firstDir.x);
    MyVector triBaseSide1 = pStart + nFirst * (arrowSize / 2.0);
    MyVector triBaseSide2 = pStart - nFirst * (arrowSize / 2.0);
    MyVector triTopVertex = pStart + firstDir * triHeight;

    QPolygonF triPoly;
    triPoly << triBaseSide1.toPointF() << triBaseSide2.toPointF() << triTopVertex.toPointF();
    path.addPolygon(triPoly);

    // 2. 지시선 그리기 (삼각형의 꼭지점에서 시작)
    path.moveTo(triTopVertex.toPointF());
    for (int i = 1; i < pts.size() - 1; ++i) {
        path.lineTo(pts[i].toPointF());
    }

    // 마지막 세그먼트 (박스 앞에서 멈춤)
    MyVector lineEndPoint = pEnd - lastDir * (boxSize / 2.0);
    path.lineTo(lineEndPoint.toPointF());

    // 3. 데이텀 박스 그리기
    double h = boxSize / 2.0;
    QRectF boxRect(pEnd.x - h, pEnd.y - h, boxSize, boxSize);
    path.addRect(boxRect);

    return path;
}

QPainterPath DatumItem::shape() const {
    QPainterPath path = geometryPath();
    QPainterPathStroker stroker;
    stroker.setWidth(2.0);
    return stroker.createStroke(path);
}

void DatumItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget);
    double finalScale = getScale();

    QPen dimLinePen = m_pen;
    QPen textPen = m_pen;

    MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
    DimensionStyle style;
    if (sc) {
        style = sc->dimensionStyle();
        // GDT/데이텀은 치수 스타일의 레이어 설정을 따르지 않고 자신의 레이어 설정을 우선합니다.
    }

    bool showWeight = sc ? sc->showLineWeight() : false;
    if (painter->device()->devType() != QInternal::Widget) showWeight = true;

    auto applyPenWeight = [&](QPen &pen, const QString &layerName) {
        double weight = 0.0;
        if (sc && sc->context() && sc->context()->layerManager()) {
            if (Layer *layer = sc->context()->layerManager()->getLayer(layerName)) {
                weight = layer->weight();
            }
        }
        if (!showWeight) {
            pen.setCosmetic(true);
            pen.setWidthF(1.0);
        } else {
            pen.setCosmetic(false);
            pen.setWidthF(weight);
            qreal scale = qAbs(painter->transform().m11());
            if (scale > 0.0 && pen.widthF() * scale < 1.0) pen.setWidthF(1.0 / scale);
        }
    };

    applyPenWeight(dimLinePen, style.dimLineLayer.isEmpty() ? layerName() : style.dimLineLayer);
    applyPenWeight(textPen, style.dimTextLayer.isEmpty() ? layerName() : style.dimTextLayer);

    QList<MyVector> pts = m_data.leaderPoints;
    if (pts.isEmpty()) pts << m_data.startPoint << m_data.endPoint;
    if (pts.size() < 2) return;

    MyVector pStart = pts.first();
    MyVector pEnd = pts.last();
    MyVector firstDir = (pts[1] - pts[0]).normalized();
    MyVector lastDir = (pEnd - pts[pts.size() - 2]).normalized();

    double arrowSize = style.arrowSize * finalScale * 1.5;
    if (arrowSize < 1e-6) arrowSize = 3.0 * finalScale * 1.5;
    double triHeight = arrowSize * qSqrt(3.0) / 2.0;
    double boxSize = GdtCalculator::datumBoxWidth(m_data.textHeight * finalScale, m_data.text);

    MyVector nFirst(-firstDir.y, firstDir.x);
    MyVector triBaseSide1 = pStart + nFirst * (arrowSize / 2.0);
    MyVector triBaseSide2 = pStart - nFirst * (arrowSize / 2.0);
    MyVector triTopVertex = pStart + firstDir * triHeight;

    painter->save();
    painter->setPen(dimLinePen);

    // 지시선 그리기 (삼각형 꼭지점에서 시작)
    QPolygonF leaderPoly;
    leaderPoly << triTopVertex.toPointF();
    for (int i = 1; i < pts.size() - 1; ++i) {
        leaderPoly << pts[i].toPointF();
    }
    MyVector lineEndPoint = pEnd - lastDir * (boxSize / 2.0);
    leaderPoly << lineEndPoint.toPointF();
    painter->drawPolyline(leaderPoly);

    // 삼각형 채우기 (베이스가 부착점, 꼭지점이 지시선 방향)
    painter->setBrush(dimLinePen.color());
    QPolygonF triPoly;
    triPoly << triBaseSide1.toPointF() << triBaseSide2.toPointF() << triTopVertex.toPointF();
    painter->drawPolygon(triPoly);

    // 박스 그리기 (배경색으로 채워 지시선 꼬리가 박스 안에 보이지 않도록)
    // [주의] 배경 브러시의 스타일이 NoBrush(투명)인 경우 .color()는 항상 검정을 반환하므로,
    // 이 경우엔 채우지 않고 투명하게 둔다. 인쇄 시 ScenePrinter가 배경을 NoBrush로 바꾸는데,
    // 스타일을 무시하고 색상만 읽어 채우면 박스 속이 검게 꽉 차 보이는 문제가 있었다.
    QBrush boxBrush = Qt::NoBrush;
    if (sc) {
        QBrush sceneBg = sc->backgroundBrush();
        if (sceneBg.style() != Qt::NoBrush) {
            boxBrush = QBrush(sceneBg.color());
        }
    }
    painter->setBrush(boxBrush);
    double h = boxSize / 2.0;
    QRectF boxRect(pEnd.x - h, pEnd.y - h, boxSize, boxSize);
    painter->drawRect(boxRect);
    painter->restore();

    // 텍스트 그리기
    painter->save();
    painter->setPen(textPen);
    painter->translate(pEnd.x, pEnd.y);
    if (painter->transform().m22() < 0) {
        painter->scale(1, -1);
    }

    QFont f = painter->font();
    if (!style.fontFamily.isEmpty()) f.setFamily(style.fontFamily);

    double targetTextHeight = m_data.textHeight * finalScale;
    double dpiY = painter->device()->logicalDpiY();
    f.setPointSizeF(qMax(0.1, targetTextHeight * 72.0 / dpiY));

    painter->setFont(f);
    QRectF textRect(-h, -h, boxSize, boxSize);
    painter->drawText(textRect, Qt::AlignCenter, m_data.text);
    painter->restore();

    if (option && (option->state & QStyle::State_Selected) &&
        painter->device()->devType() == QInternal::Widget) {
        if (!(parentItem() && dynamic_cast<CadItem *>(parentItem()))) {
            painter->save();
            painter->setBrush(Qt::NoBrush);

            QPen glowPen(QColor(0, 255, 255, 80));
            glowPen.setCosmetic(true);
            glowPen.setWidth(5);
            painter->setPen(glowPen);
            painter->drawPath(geometryPath());

            QPen dashPen(Qt::white);
            dashPen.setCosmetic(true);
            dashPen.setWidth(1);
            dashPen.setStyle(Qt::DashLine);
            painter->setPen(dashPen);
            painter->drawPath(geometryPath());

            painter->restore();
        }
    }
}

// ─────────────────────────────────────────────
// 이하 변환/그립 및 explode 구현부 생략 없이 작성
// ─────────────────────────────────────────────

CadItem *DatumItem::clone() const {
    auto *item = new DatumItem(m_data);
    item->setLayerName(layerName());
    item->setPen(m_pen);
    return item;
}

QList<SnapPoint> DatumItem::getSnapPoints() const {
    QList<SnapPoint> snaps;
    QList<MyVector> pts = m_data.leaderPoints;
    if (pts.isEmpty()) pts << m_data.startPoint << m_data.endPoint;
    for (int i = 0; i < pts.size(); ++i) {
        snaps << SnapPoint{ pts[i], SnapType::Endpoint };
    }
    return snaps;
}

QList<QPointF> DatumItem::getGripPoints() const {
    QList<QPointF> grips;
    QList<MyVector> pts = m_data.leaderPoints;
    if (pts.isEmpty()) pts << m_data.startPoint << m_data.endPoint;
    for (int i = 0; i < pts.size(); ++i) {
        grips << pts[i].toPointF();
    }
    return grips;
}

void DatumItem::applyGripMove(int index, const MyVector &pos) {
    prepareGeometryChange();
    if (m_data.leaderPoints.isEmpty()) {
        m_data.leaderPoints << m_data.startPoint << m_data.endPoint;
    }
    if (index >= 0 && index < m_data.leaderPoints.size()) {
        m_data.leaderPoints[index] = pos;
        // 하위 호환성 필드도 업데이트
        m_data.startPoint = m_data.leaderPoints.first();
        m_data.endPoint = m_data.leaderPoints.last();
    }
}

QPainterPath DatumItem::getGhostShape(int index, const MyVector &pos) const {
    DatumItem ghost(m_data);
    ghost.setOverrideScale(getScale());
    ghost.applyGripMove(index, pos);
    return ghost.shape();
}

double DatumItem::distanceTo(const MyVector &pos) const {
    if (shape().contains(pos.toPointF())) return 0.0;
    return qMin(m_data.startPoint.distanceTo(pos), m_data.endPoint.distanceTo(pos));
}

SnapPoint DatumItem::getNearestSnap(const MyVector &pos) const {
    return { pos, SnapType::Nearest };
}

void DatumItem::move(const MyVector &delta) {
    prepareGeometryChange();
    m_data.startPoint += delta;
    m_data.endPoint += delta;
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) {
        m_data.leaderPoints[i] += delta;
    }
}

void DatumItem::mirror(const MyVector &p1, const MyVector &p2) {
    prepareGeometryChange();
    m_data.startPoint = MyMath::reflectPoint(m_data.startPoint, p1, p2);
    m_data.endPoint = MyMath::reflectPoint(m_data.endPoint, p1, p2);
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) {
        m_data.leaderPoints[i] = MyMath::reflectPoint(m_data.leaderPoints[i], p1, p2);
    }
}

void DatumItem::rotate(const MyVector &base, double angle) {
    prepareGeometryChange();
    m_data.startPoint = MyMath::rotatePoint(m_data.startPoint, base, angle);
    m_data.endPoint = MyMath::rotatePoint(m_data.endPoint, base, angle);
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) {
        m_data.leaderPoints[i] = MyMath::rotatePoint(m_data.leaderPoints[i], base, angle);
    }
}

void DatumItem::scale(const MyVector &base, double scaleFactor) {
    prepareGeometryChange();
    m_data.startPoint = MyMath::scalePoint(m_data.startPoint, base, scaleFactor);
    m_data.endPoint = MyMath::scalePoint(m_data.endPoint, base, scaleFactor);
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) {
        m_data.leaderPoints[i] = MyMath::scalePoint(m_data.leaderPoints[i], base, scaleFactor);
    }
    m_data.textHeight *= scaleFactor;
}

void DatumItem::stretch(const QList<int> &/*indices*/, const MyVector &/*delta*/) {}

QPainterPath DatumItem::getMovedPath(const MyVector &delta) const {
    DatumItem tmp(m_data); tmp.setOverrideScale(getScale()); tmp.move(delta); return tmp.shape();
}
QPainterPath DatumItem::getMirroredPath(const MyVector &p1, const MyVector &p2) const {
    DatumItem tmp(m_data); tmp.setOverrideScale(getScale()); tmp.mirror(p1, p2); return tmp.shape();
}
QPainterPath DatumItem::getRotatedPath(const MyVector &base, double angle) const {
    DatumItem tmp(m_data); tmp.setOverrideScale(getScale()); tmp.rotate(base, angle); return tmp.shape();
}
QPainterPath DatumItem::getScaledPath(const MyVector &base, double scaleFactor) const {
    DatumItem tmp(m_data); tmp.setOverrideScale(getScale()); tmp.scale(base, scaleFactor); return tmp.shape();
}
QPainterPath DatumItem::getStretchedPath(const QList<int> &, const MyVector &) const {
    return shape();
}

QMap<QString, QVariant> DatumItem::getProperties() const {
    QMap<QString, QVariant> props;
    props["Type"] = "Datum";
    props["Layer"] = layerName();
    props["Text"] = m_data.text;
    props["TextHeight"] = m_data.textHeight;
    return props;
}

bool DatumItem::setProperty(const QString &key, const QVariant &value) {
    if (key == "Text") { m_data.text = value.toString(); prepareGeometryChange(); update(); return true; }
    if (key == "TextHeight") { m_data.textHeight = value.toDouble(); prepareGeometryChange(); update(); return true; }
    return false;
}
void DatumItem::setGripsVisible(bool) {}

QList<CadItem*> DatumItem::explode() const {
    QList<CadItem*> primitives;
    MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
    if (!sc) return primitives;

    double finalScale = getScale();
    DimensionStyle style = sc->dimensionStyle();

    auto applyItemStyle = [&](CadItem *item, const QString &layerName) {
        QString targetLayer = layerName.isEmpty() ? this->layerName() : layerName;
        LayerStyle ls = sc->getLayerStyle(targetLayer);
        QPen p(ls.color); p.setCosmetic(true); p.setWidthF(1.0);
        if (!ls.dashPattern.isEmpty()) {
            p.setStyle(Qt::CustomDashLine); p.setDashPattern(ls.dashPattern);
        }
        item->setLayerName(targetLayer); item->setPen(p);
        return ls;
    };

    MyVector p1 = m_data.startPoint;
    MyVector p2 = m_data.endPoint;
    MyVector dir = (p2 - p1);
    double dist = dir.magnitude();
    if (dist > 1e-6) {
        dir = dir.normalized();
        double arrowSize = style.arrowSize * finalScale * 1.5;
        if (arrowSize < 1e-6) arrowSize = 3.0 * finalScale * 1.5;
        double triHeight = arrowSize * qSqrt(3.0) / 2.0;

        double boxSize = GdtCalculator::datumBoxWidth(m_data.textHeight * finalScale, m_data.text);

        MyVector n(-dir.y, dir.x);
        MyVector triBaseSide1 = p1 + n * (arrowSize / 2.0);
        MyVector triBaseSide2 = p1 - n * (arrowSize / 2.0);
        MyVector triTopVertex = p1 + dir * triHeight;

        auto *arrow = new PolylineItem({triBaseSide1, triBaseSide2, triTopVertex}, true);
        LayerStyle ls = applyItemStyle(arrow, style.dimLineLayer);
        arrow->setBrush(QBrush(ls.color));
        primitives << arrow;

        MyVector lineStart = triTopVertex;
        MyVector lineEnd = p2 - dir * (boxSize / 2.0);
        if (dist > triHeight + boxSize / 2.0) {
            auto *line = new LineItem(lineStart, lineEnd);
            applyItemStyle(line, style.dimLineLayer);
            primitives << line;
        }

        double h = boxSize / 2.0;
        LineItem *lines[] = {
            new LineItem(MyVector(p2.x - h, p2.y + h), MyVector(p2.x + h, p2.y + h)),
            new LineItem(MyVector(p2.x + h, p2.y + h), MyVector(p2.x + h, p2.y - h)),
            new LineItem(MyVector(p2.x + h, p2.y - h), MyVector(p2.x - h, p2.y - h)),
            new LineItem(MyVector(p2.x - h, p2.y - h), MyVector(p2.x - h, p2.y + h))
        };
        for (int i = 0; i < 4; ++i) {
            applyItemStyle(lines[i], style.dimLineLayer);
            primitives << lines[i];
        }

        MyText txtData;
        txtData.text = m_data.text;
        txtData.fontSize = m_data.textHeight * finalScale;
        txtData.fontFamily = style.fontFamily.isEmpty() ? "Arial" : style.fontFamily;
        txtData.rotation = 0.0;

        QFont tmpFont(txtData.fontFamily);
        tmpFont.setPointSizeF(txtData.fontSize);
        QFontMetricsF fm(tmpFont);

        double localBaselineX = p2.x - fm.horizontalAdvance(txtData.text) / 2.0;
        double localBaselineY = p2.y - (fm.ascent() - fm.descent()) / 2.0;
        txtData.pos = MyVector(localBaselineX, localBaselineY);

        auto *tItem = new TextItem(txtData);
        applyItemStyle(tItem, style.dimTextLayer);
        primitives << tItem;
    }

    for (int i = 0; i < primitives.size(); ++i) {
        primitives[i]->setParentItem(nullptr);
    }
    return primitives;
}