#include "gdtitem.h"
#include "mymath.h"
#include "mygraphicsscene.h"
#include "lineitem.h"
#include "textitem.h"
#include "polylineitem.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "layer.h"
#include "gdtcalculator.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QPainterPathStroker>
#include <QFontMetricsF>
#include <QtMath>
#include <QDebug>

GdtItem::GdtItem(const GdtData &data, QGraphicsItem *parent)
    : CadItem(parent), m_data(data)
{
    setZValue(1);
}

double GdtItem::getScale() const {
    if (m_overrideScale > 0) return m_overrideScale;
    if (MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene())) {
        return sc->drawingScale();
    }
    return 1.0;
}

double GdtItem::cellHeight() const {
    return GdtCalculator::cellHeight(m_data.textHeight * getScale());
}
double GdtItem::symbolCellWidth() const {
    return GdtCalculator::symbolCellWidth(m_data.textHeight * getScale(), m_data.symbol);
}
double GdtItem::toleranceCellWidth() const {
    return GdtCalculator::toleranceCellWidth(m_data.textHeight * getScale(), m_data.toleranceValue, m_data.hasDiameter);
}
double GdtItem::datumCellWidth() const {
    // 대표값 반환 — 실제 너비는 totalWidth() 및 paint()에서 개별 계산
    return GdtCalculator::cellHeight(m_data.textHeight * getScale());
}
double GdtItem::totalWidth() const {
    return GdtCalculator::totalGdtWidth(m_data.textHeight * getScale(), m_data.toleranceValue, m_data.hasDiameter, m_data.datumRefs, m_data.symbol);
}

double GdtItem::getDrawRotation() const {
    double ang = MyMath::normalizeAngle(m_data.rotation);
    if (ang > 90.0 && ang < 270.0) {
        return MyMath::normalizeAngle(m_data.rotation + 180.0);
    }
    return m_data.rotation;
}

QRectF GdtItem::localFrameRect() const {
    double h = cellHeight();
    double w = totalWidth();
    double ang = MyMath::normalizeAngle(m_data.rotation);

    if (ang > 90.0 && ang < 270.0) {
        return QRectF(-w, -h / 2.0, w, h);
    }
    return QRectF(0, -h / 2.0, w, h);
}

QTransform GdtItem::frameTransform() const {
    QTransform t;
    t.translate(m_data.position.x, m_data.position.y);
    t.rotate(getDrawRotation());
    return t;
}

QList<MyVector> GdtItem::getAdjustedLeaderPoints() const {
    QList<MyVector> pts = m_data.leaderPoints;
    if (pts.size() >= 2 && m_data.useLeader) {
        MyVector pPrev = pts[pts.size() - 2];
        MyVector pLast = pts.last();
        
        QTransform invT = frameTransform().inverted();
        QPointF localPrev = invT.map(pPrev.toPointF());
        QPointF localLast = invT.map(pLast.toPointF());
        QRectF fr = localFrameRect();
        
        QLineF ray(localPrev, localLast);
        double rayLen = ray.length();
        if (rayLen > 0.001) {
            // 연장선 (충분히 길게)
            ray.setLength(rayLen + fr.width() + fr.height() + 100.0); 
            
            QLineF edges[4] = {
                QLineF(fr.topLeft(), fr.topRight()),
                QLineF(fr.bottomLeft(), fr.bottomRight()),
                QLineF(fr.topLeft(), fr.bottomLeft()),
                QLineF(fr.topRight(), fr.bottomRight())
            };
            
            double minDist = 1e18;
            bool found = false;
            QPointF bestLocalPt = localLast; // 기본값
            
            for (int i = 0; i < 4; ++i) {
                QPointF intPt;
                // BoundedIntersection으로 렉트 변과 실제 교차하는지 검사
                if (ray.intersects(edges[i], &intPt) == QLineF::BoundedIntersection) {
                    double dist = QLineF(localPrev, intPt).length();
                    if (dist < minDist) {
                        minDist = dist;
                        bestLocalPt = intPt;
                        found = true;
                    }
                }
            }
            if (found) {
                pts.last() = MyVector(frameTransform().map(bestLocalPt));
            }
        }
    }
    return pts;
}

QRectF GdtItem::boundingRect() const {
    return shape().boundingRect().adjusted(-2, -2, 2, 2);
}

QPainterPath GdtItem::shape() const {
    QPainterPath p;
    QList<MyVector> adjPoints = getAdjustedLeaderPoints();
    if (adjPoints.size() >= 2 && m_data.useLeader) {
        QPolygonF poly;
        for (int i = 0; i < adjPoints.size(); ++i) {
            poly << adjPoints[i].toPointF();
        }
        QPainterPath linePath;
        linePath.addPolygon(poly);
        QPainterPathStroker stroker;
        stroker.setWidth(2.0);
        p.addPath(stroker.createStroke(linePath));
    }
    QPainterPath framePath;
    framePath.addRect(localFrameRect());
    p.addPath(frameTransform().map(framePath));
    return p;
}

QPainterPath GdtItem::geometryPath() const {
    QPainterPath path;
    QList<MyVector> adjPoints = getAdjustedLeaderPoints();
    
    if (adjPoints.size() >= 2 && m_data.useLeader) {
        QPolygonF poly;
        for (int i = 0; i < adjPoints.size(); ++i) {
            poly << adjPoints[i].toPointF();
        }
        path.addPolygon(poly);
    }

    QPainterPath framePath;
    QRectF fr = localFrameRect();
    framePath.addRect(fr);

    double sw = symbolCellWidth();
    double tw = toleranceCellWidth();
    double x0 = fr.left();

    double currentX = x0 + sw;
    framePath.moveTo(currentX, fr.top());
    framePath.lineTo(currentX, fr.bottom());

    currentX += tw;
    if (!m_data.datumRefs.isEmpty()) {
        framePath.moveTo(currentX, fr.top());
        framePath.lineTo(currentX, fr.bottom());
    }

    double th_scaled = m_data.textHeight * getScale();
    for (int i = 0; i < m_data.datumRefs.size(); ++i) {
        double dw_i = GdtCalculator::datumCellWidth(th_scaled, m_data.datumRefs[i]);
        currentX += dw_i;
        if (i < (int)m_data.datumRefs.size() - 1) {
            framePath.moveTo(currentX, fr.top());
            framePath.lineTo(currentX, fr.bottom());
        }
    }

    path.addPath(frameTransform().map(framePath));
    return path;
}

void GdtItem::drawSymbol(QPainter *painter, const QRectF &cell, GdtSymbol sym) const {
    painter->save();
 //   painter->setClipRect(cell.normalized());
 // [수정] ClipRect를 셀 크기보다 약간 여유 있게 설정하여 프린터 출력 시 짤림 방지
    painter->setClipRect(cell.adjusted(-0.5, -0.5, 0.5, 0.5));

    double h = cell.height(); 
    double cx = cell.center().x(); 
    double cy = cell.center().y();
    double r  = h * 0.30;

    QFont symFont("Arial");
    symFont.setPixelSize(qRound(h * 0.65));
    symFont.setBold(false);
    painter->setFont(symFont);

    auto drawSymText = [&](const QString &txt) {
        painter->drawText(cell, Qt::AlignCenter, txt);
    };

    switch (sym) {
    case GdtSymbol::Straightness:
        painter->drawLine(QPointF(cx - r*1.2, cy), QPointF(cx + r*1.2, cy));
        break;
    case GdtSymbol::Flatness: {
        double w2 = r * 1.0; double h2 = r * 0.5; QPolygonF poly;
        poly << QPointF(cx - w2 + h2, cy - h2) << QPointF(cx + w2 + h2, cy - h2)
             << QPointF(cx + w2 - h2, cy + h2) << QPointF(cx - w2 - h2, cy + h2);
        painter->drawPolygon(poly); break;
    }
    case GdtSymbol::Circularity:      painter->drawEllipse(QPointF(cx, cy), r, r); break;
    case GdtSymbol::Cylindricity: {
        painter->drawEllipse(QPointF(cx, cy), r * 0.8, r * 0.8);
        double vr = r * 0.9, slant = r * 0.35, sep = r * 0.55;
        painter->drawLine(QPointF(cx - sep - slant, cy + vr), QPointF(cx - sep + slant, cy - vr));
        painter->drawLine(QPointF(cx + sep - slant, cy + vr), QPointF(cx + sep + slant, cy - vr));
        break;
    }
    case GdtSymbol::LineProfile: {
        QPainterPath arc;
        arc.arcMoveTo(QRectF(cx - r, cy - r, r*2, r*2), 0);
        arc.arcTo(QRectF(cx - r, cy - r, r*2, r*2), 0, 180);
        painter->drawPath(arc); break;
    }
    case GdtSymbol::SurfaceProfile: {
        QPainterPath arc; arc.moveTo(cx - r, cy); arc.arcTo(QRectF(cx - r, cy - r, r*2, r*2), 0, 180); arc.lineTo(cx - r, cy);
        painter->drawPath(arc); break;
    }
    case GdtSymbol::Parallelism: {
        // ISO 1101: two slanted parallel lines (//)
        double vr = r * 0.75, slant = r * 0.28, sep = r * 0.38;
        painter->drawLine(QPointF(cx - sep - slant, cy + vr), QPointF(cx - sep + slant, cy - vr));
        painter->drawLine(QPointF(cx + sep - slant, cy + vr), QPointF(cx + sep + slant, cy - vr));
        break;
    }
    case GdtSymbol::Perpendicularity:
        painter->drawLine(QPointF(cx - r, cy + r*0.5), QPointF(cx + r, cy + r*0.5));
        painter->drawLine(QPointF(cx, cy + r*0.5), QPointF(cx, cy - r*0.8)); break;
    case GdtSymbol::Angularity:       drawSymText(QString::fromUtf8("∠")); break;
    case GdtSymbol::Position:
        painter->drawEllipse(QPointF(cx, cy), r, r);
        painter->drawLine(QPointF(cx - r * 1.3, cy), QPointF(cx + r * 1.3, cy));
        painter->drawLine(QPointF(cx, cy - r * 1.3), QPointF(cx, cy + r * 1.3)); break;
    case GdtSymbol::Concentricity:    painter->drawEllipse(QPointF(cx, cy), r, r); painter->drawEllipse(QPointF(cx, cy), r * 0.4, r * 0.4); break;
    case GdtSymbol::Symmetry:
        for (int i = -1; i <= 1; ++i){
            double hw = (i == 0) ? r : r * 0.5;
            painter->drawLine(QPointF(cx - hw, cy + i * r * 0.4), QPointF(cx + hw, cy + i * r * 0.4));
        }
        break;
    case GdtSymbol::CircularRunout: {
        const double sq2 = 0.7071;
        double len = r * 2.0, hs = r * 0.85;
        QPointF tail(cx - sq2*len*0.5, cy + sq2*len*0.5);
        QPointF head(cx + sq2*len*0.5, cy - sq2*len*0.5);
        // 화살촉 밑변 중심 (선은 여기까지만)
        QPointF back(head.x() - sq2*hs, head.y() + sq2*hs);
        painter->drawLine(tail, back);  // 선이 화살촉 안으로 들어가지 않음
        QPolygonF arr;
        arr << head
            << QPointF(back.x() + sq2*hs*0.5, back.y() + sq2*hs*0.5)
            << QPointF(back.x() - sq2*hs*0.5, back.y() - sq2*hs*0.5);
        painter->save();
        painter->setBrush(Qt::NoBrush);
        painter->drawPolygon(arr);
        painter->restore();
        break;
    }
    case GdtSymbol::TotalRunout: {
        const double sq2 = 0.7071;
        double len = r * 2.0, hs = r * 0.85, gap = r * 1.3;
        QPointF tailPts[2];
        int signs[2] = {-1, 1};
        for (int i = 0; i < 2; ++i) {
            double ox = gap * signs[i] * 0.5;
            QPointF tail(cx - sq2*len*0.5 + ox, cy + sq2*len*0.5);
            QPointF head(cx + sq2*len*0.5 + ox, cy - sq2*len*0.5);
            QPointF back(head.x() - sq2*hs, head.y() + sq2*hs);
            tailPts[i] = tail;
            painter->drawLine(tail, back); // 선이 화살촉 안으로 들어가지 않음
            QPolygonF arr;
            arr << head
                << QPointF(back.x() + sq2*hs*0.5, back.y() + sq2*hs*0.5)
                << QPointF(back.x() - sq2*hs*0.5, back.y() - sq2*hs*0.5);
            painter->save();
            painter->setBrush(Qt::NoBrush);
            painter->drawPolygon(arr);
            painter->restore();
        }
        // 두 꼬리를 직선으로 연결
        painter->drawLine(tailPts[0], tailPts[1]);
        break;
    }
    default: break;
    }
    painter->restore();
}

void GdtItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
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

    QList<MyVector> adjPoints = getAdjustedLeaderPoints();
    if (adjPoints.size() >= 2 && m_data.useLeader) {
        painter->save();
        painter->setPen(dimLinePen);

        QPolygonF poly;
        for (int i = 0; i < adjPoints.size(); ++i) poly << adjPoints[i].toPointF();
        painter->drawPolyline(poly);

        MyVector p1 = adjPoints[0];
        MyVector p2 = adjPoints[1];
        MyVector dir = p2 - p1;
        if (dir.magnitude() > 1e-6) {
            dir = dir.normalized();
            double arrowSize = style.arrowSize * finalScale;
            if (arrowSize < 1e-6) arrowSize = 2.5 * finalScale;

            MyVector n(-dir.y, dir.x);
            MyVector head = p1;
            MyVector back = p1 + dir * arrowSize;
            MyVector side1 = back + n * (arrowSize * 0.35);
            MyVector side2 = back - n * (arrowSize * 0.35);

            QPolygonF arrowPoly;
            arrowPoly << head.toPointF() << side1.toPointF() << side2.toPointF();
            painter->setBrush(dimLinePen.color());
            painter->drawPolygon(arrowPoly);
        }
        painter->restore();
    }

    painter->save();
 //   QTransform t = frameTransform();
    painter->translate(m_data.position.x, m_data.position.y);
    painter->rotate(getDrawRotation());

    if (painter->transform().m22() < 0) {
        painter->scale(1, -1);
    }

    QRectF fr = localFrameRect();
    double h = cellHeight();
    double sw = symbolCellWidth();
    double tw = toleranceCellWidth();
    double x0 = fr.left();

    painter->setPen(dimLinePen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(fr);
    
    double curX = x0 + sw;
    painter->drawLine(QPointF(curX, fr.top()), QPointF(curX, fr.bottom()));
    
    curX += tw;
    if (!m_data.datumRefs.isEmpty()) {
        painter->drawLine(QPointF(curX, fr.top()), QPointF(curX, fr.bottom()));
    }

    double th_s = m_data.textHeight * finalScale;
    for (int i = 0; i < m_data.datumRefs.size(); ++i) {
        double dw_i = GdtCalculator::datumCellWidth(th_s, m_data.datumRefs[i]);
        curX += dw_i;
        if (i < (int)m_data.datumRefs.size() - 1) {
            painter->drawLine(QPointF(curX, fr.top()), QPointF(curX, fr.bottom()));
        }
    }

    QFont f = painter->font();
    if (!m_data.fontName.isEmpty()) {
        QString family = m_data.fontName;
        if (family.toLower().endsWith(".ttf") || family.toLower().endsWith(".shx")) {
            family = family.left(family.lastIndexOf('.'));
        }
        
        QStringList families;
        families << family;
        if (family.contains("돋움")) families << "Dotum" << "돋움" << "돋움체";
        if (family.contains("굴림")) families << "Gulim" << "굴림" << "굴림체";
        if (family.contains("맑은")) families << "Malgun Gothic" << "맑은 고딕";
        
        bool found = false;
  //      qDebug() << "[GdtItem] Searching for font:" << family;
        for (int i = 0; i < families.size(); ++i) {
            const QString& fam = families.at(i);
            f.setFamily(fam);
            QFontInfo info(f);
   //         qDebug() << "  Trying:" << fam << "-> Result:" << info.family();
            if (info.family().toLower().contains(fam.toLower()) || 
                info.family().toLower().contains("dotum") ||
                info.family().toLower().contains("gulim")) {
                found = true;
                break;
            }
        }
        if (!found) {
   //         qDebug() << "  Font not found, using Arial fallback.";
            f.setFamily("Arial");
        }
    } else if (!style.fontFamily.isEmpty()) {
        f.setFamily(style.fontFamily);
    }

    // [프린터 DPI 보정 핵심 로직 적용]
    // double targetTextHeight = m_data.textHeight * finalScale;
    // double dpiY = painter->device()->logicalDpiY();
    // f.setPointSizeF(qMax(0.1, targetTextHeight * 72.0 / dpiY));
    // Scene 단위인 targetTextHeight를 PixelSize로 설정하면 painter 스케일에 맞춰 자동 출력됩니다.
    double targetTextHeight = m_data.textHeight * finalScale;
    f.setPixelSize(qRound(targetTextHeight));
    
    painter->setFont(f);
    painter->setPen(textPen);

    auto drawCenteredText = [&](const QRectF &cell, const QString &txt) {
        painter->drawText(cell, Qt::AlignCenter, txt);
    };

    QRectF symbolCell(x0, fr.top(), sw, h);
    drawSymbol(painter, symbolCell, m_data.symbol);

    QRectF tolCell(x0 + sw, fr.top(), tw, h);
    QString tolText = m_data.hasDiameter ? QString::fromUtf8("Ø") + m_data.toleranceValue : m_data.toleranceValue;
    drawCenteredText(tolCell, tolText);

    double datumX = x0 + sw + tw;
    double th_s_datum = m_data.textHeight * finalScale;
    for (int i = 0; i < m_data.datumRefs.size(); ++i) {
        double dw_i = GdtCalculator::datumCellWidth(th_s_datum, m_data.datumRefs[i]);
        QRectF datumCell(datumX, fr.top(), dw_i, h);
        drawCenteredText(datumCell, m_data.datumRefs[i]);
        datumX += dw_i;
    }
    painter->restore();

    if (option && (option->state & QStyle::State_Selected) &&
        painter->device()->devType() == QInternal::Widget) {
        if (!(parentItem() && dynamic_cast<CadItem *>(parentItem()))) {
            painter->save();

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

CadItem *GdtItem::clone() const {
    auto *item = new GdtItem(m_data);
    item->setLayerName(layerName());
    item->setPen(m_pen);
    return item;
}

QList<SnapPoint> GdtItem::getSnapPoints() const {
    QList<SnapPoint> snaps;
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) {
        snaps << SnapPoint{m_data.leaderPoints[i], SnapType::Endpoint};
    }
    QRectF fr = localFrameRect();
    QTransform t = frameTransform();
    QPointF pts[4] = { fr.topLeft(), fr.topRight(), fr.bottomRight(), fr.bottomLeft() };
    for (int i = 0; i < 4; ++i) {
        snaps << SnapPoint{MyVector(t.map(pts[i])), SnapType::Endpoint};
    }
    if (m_data.leaderPoints.isEmpty() || !m_data.useLeader) {
        snaps << SnapPoint{m_data.position, SnapType::Endpoint};
    }
    return snaps;
}

QList<QPointF> GdtItem::getGripPoints() const {
    QList<QPointF> grips;
    if (m_data.useLeader) {
        for (int i = 0; i < m_data.leaderPoints.size(); ++i) grips << m_data.leaderPoints[i].toPointF();
    }
    if (m_data.leaderPoints.isEmpty() || !m_data.useLeader) grips << m_data.position.toPointF();
    return grips;
}

void GdtItem::applyGripMove(int index, const MyVector &pos) {
    prepareGeometryChange();
    if ((m_data.leaderPoints.isEmpty() || !m_data.useLeader) && index == 0) {
        m_data.position = pos;
    } else if (index >= 0 && index < m_data.leaderPoints.size() && m_data.useLeader) {
        m_data.leaderPoints[index] = pos;
        if (index == m_data.leaderPoints.size() - 1) {
            m_data.position = pos;
            if (index > 0) {
                MyVector dir = pos - m_data.leaderPoints[index - 1];
                m_data.rotation = qRadiansToDegrees(qAtan2(dir.y, dir.x));
            }
        }
        else if (index == m_data.leaderPoints.size() - 2) {
            MyVector dir = m_data.position - pos;
            m_data.rotation = qRadiansToDegrees(qAtan2(dir.y, dir.x));
        }
    }
}

QPainterPath GdtItem::getGhostShape(int index, const MyVector &pos) const {
    GdtItem ghost(m_data);
    ghost.setOverrideScale(getScale());
    ghost.applyGripMove(index, pos);
    return ghost.shape();
}

double GdtItem::distanceTo(const MyVector &pos) const {
    if (shape().contains(pos.toPointF())) return 0.0;
    return m_data.position.distanceTo(pos);
}

SnapPoint GdtItem::getNearestSnap(const MyVector &pos) const {
    QList<SnapPoint> snaps = getSnapPoints();
    if (snaps.isEmpty()) return SnapPoint{m_data.position, SnapType::Endpoint};
    SnapPoint bestSnap = snaps[0];
    double minDist = pos.distanceTo(snaps[0].pos);
    for (int i = 1; i < snaps.size(); ++i) {
        double dist = pos.distanceTo(snaps[i].pos);
        if (dist < minDist) { minDist = dist; bestSnap = snaps[i]; }
    }
    return bestSnap;
}

void GdtItem::move(const MyVector &delta) {
    prepareGeometryChange();
    m_data.position += delta;
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) m_data.leaderPoints[i] += delta;
}

void GdtItem::mirror(const MyVector &p1, const MyVector &p2) {
    prepareGeometryChange();
    m_data.position = MyMath::reflectPoint(m_data.position, p1, p2);
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) {
        m_data.leaderPoints[i] = MyMath::reflectPoint(m_data.leaderPoints[i], p1, p2);
    }
    if (m_data.leaderPoints.size() >= 2 && m_data.useLeader) {
        MyVector dir = m_data.leaderPoints.last() - m_data.leaderPoints[m_data.leaderPoints.size() - 2];
        m_data.rotation = qRadiansToDegrees(qAtan2(dir.y, dir.x));
    } else {
        m_data.rotation = -m_data.rotation;
    }
}

void GdtItem::rotate(const MyVector &base, double angle) {
    prepareGeometryChange();
    m_data.position = MyMath::rotatePoint(m_data.position, base, angle);
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) {
        m_data.leaderPoints[i] = MyMath::rotatePoint(m_data.leaderPoints[i], base, angle);
    }
    m_data.rotation = MyMath::normalizeAngle(m_data.rotation + angle);
}

void GdtItem::scale(const MyVector &base, double factor) {
    prepareGeometryChange();
    m_data.position = MyMath::scalePoint(m_data.position, base, factor);
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) {
        m_data.leaderPoints[i] = MyMath::scalePoint(m_data.leaderPoints[i], base, factor);
    }
    m_data.textHeight *= factor;
}

void GdtItem::stretch(const QList<int> &/*indices*/, const MyVector &/*delta*/) {}

QPainterPath GdtItem::getMovedPath(const MyVector &delta) const {
    GdtItem tmpItem(m_data); tmpItem.setOverrideScale(getScale()); tmpItem.move(delta); return tmpItem.shape();
}
QPainterPath GdtItem::getMirroredPath(const MyVector &p1, const MyVector &p2) const {
    GdtItem tmpItem(m_data); tmpItem.setOverrideScale(getScale()); tmpItem.mirror(p1, p2); return tmpItem.shape();
}
QPainterPath GdtItem::getRotatedPath(const MyVector &base, double angle) const {
    GdtItem tmpItem(m_data); tmpItem.setOverrideScale(getScale()); tmpItem.rotate(base, angle); return tmpItem.shape();
}
QPainterPath GdtItem::getScaledPath(const MyVector &base, double factor) const {
    GdtItem tmpItem(m_data); tmpItem.setOverrideScale(getScale()); tmpItem.scale(base, factor); return tmpItem.shape();
}
QPainterPath GdtItem::getStretchedPath(const QList<int> &, const MyVector &) const { return shape(); }

QMap<QString, QVariant> GdtItem::getProperties() const {
    QMap<QString, QVariant> props;
    props["Type"]       = "Gdt";
    props["Layer"]      = layerName();
    props["Symbol"]     = (int)m_data.symbol;
    props["Tolerance"]  = m_data.toleranceValue;
    props["Rotation"]   = m_data.rotation;
    props["TextHeight"] = m_data.textHeight;
    return props;
}

bool GdtItem::setProperty(const QString &key, const QVariant &value) {
    if (key == "Tolerance") { m_data.toleranceValue = value.toString(); prepareGeometryChange(); update(); return true; }
    if (key == "TextHeight") { m_data.textHeight = value.toDouble(); prepareGeometryChange(); update(); return true; }
    return false;
}
void GdtItem::setGripsVisible(bool) {}

QList<CadItem*> GdtItem::explode() const {
    QList<CadItem*> primitives;
    MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
    if (!sc) return primitives;

    double finalScale = getScale();
    DimensionStyle style = sc->dimensionStyle();

    auto applyItemStyle = [&](CadItem *item, const QString &layerName) {
        QString targetLayer = layerName.isEmpty() ? this->layerName() : layerName;
        LayerStyle ls = sc->getLayerStyle(targetLayer);
        QPen p(ls.color); p.setCosmetic(true); p.setWidthF(1.0);
        if (!ls.dashPattern.isEmpty()) { p.setStyle(Qt::CustomDashLine); p.setDashPattern(ls.dashPattern); }
        item->setLayerName(targetLayer); item->setPen(p);
        return ls;
    };

    if (m_data.leaderPoints.size() >= 2 && m_data.useLeader) {
        QList<MyVector> adjPoints = getAdjustedLeaderPoints();
        for (int i = 0; i < adjPoints.size() - 1; ++i) {
            auto *line = new LineItem(adjPoints[i], adjPoints[i+1]);
            applyItemStyle(line, style.dimLineLayer);
            primitives << line;
        }

        MyVector p1 = adjPoints[0];
        MyVector p2 = adjPoints[1];
        MyVector dir = (p2 - p1);
        if (dir.magnitude() > 1e-6) {
            dir = dir.normalized();
            double arrowSize = style.arrowSize * finalScale;
            if (arrowSize < 1e-6) arrowSize = 2.5 * finalScale;
            MyVector n(-dir.y, dir.x);
            MyVector head = p1;
            MyVector back = p1 + dir * arrowSize;
            MyVector side1 = back + n * (arrowSize * 0.35);
            MyVector side2 = back - n * (arrowSize * 0.35);

            auto *arrow = new PolylineItem({head, side1, side2}, true);
            LayerStyle ls = applyItemStyle(arrow, style.dimLineLayer);
            arrow->setBrush(QBrush(ls.color));
            primitives << arrow;
        }
    }

    QTransform tPaint = frameTransform();
    tPaint.scale(1, -1);
    auto mapPoint = [&](const QPointF &pt) -> MyVector { return MyVector(tPaint.map(pt)); };

    QRectF fr = localFrameRect();
    double h = cellHeight();
    double sw = symbolCellWidth();
    double tw = toleranceCellWidth();
    double x0 = fr.left();

    QList<LineItem*> frameLines;
    frameLines << new LineItem(mapPoint(fr.topLeft()), mapPoint(fr.topRight()))
               << new LineItem(mapPoint(fr.topRight()), mapPoint(fr.bottomRight()))
               << new LineItem(mapPoint(fr.bottomRight()), mapPoint(fr.bottomLeft()))
               << new LineItem(mapPoint(fr.bottomLeft()), mapPoint(fr.topLeft()));

    double th_s = m_data.textHeight * finalScale;
    double curX = x0 + sw;
    frameLines << new LineItem(mapPoint(QPointF(curX, fr.top())), mapPoint(QPointF(curX, fr.bottom())));
    
    curX += tw;
    if (!m_data.datumRefs.isEmpty()) {
        frameLines << new LineItem(mapPoint(QPointF(curX, fr.top())), mapPoint(QPointF(curX, fr.bottom())));
    }

    for (int i = 0; i < m_data.datumRefs.size(); ++i) {
        double dw_i = GdtCalculator::datumCellWidth(th_s, m_data.datumRefs[i]);
        curX += dw_i;
        if (i < (int)m_data.datumRefs.size() - 1) {
            frameLines << new LineItem(mapPoint(QPointF(curX, fr.top())), mapPoint(QPointF(curX, fr.bottom())));
        }
    }

    for (int i = 0; i < frameLines.size(); ++i) {
        LineItem* l = frameLines.at(i);
        applyItemStyle(l, style.dimLineLayer);
        primitives << l;
    }

    auto createTextItem = [&](const QString &txt, const QRectF &cellRect, double fSize) {
        MyText txtData;
        txtData.text = txt;
        txtData.fontSize = fSize;
        txtData.fontFamily = style.fontFamily.isEmpty() ? "Arial" : style.fontFamily;
        txtData.rotation = getDrawRotation();

        QPointF localCenter = cellRect.center();
        QFont tmpFont(txtData.fontFamily);
        tmpFont.setPointSizeF(fSize);
        QFontMetricsF fm(tmpFont);

        double localBaselineX = localCenter.x() - fm.horizontalAdvance(txt) / 2.0;
        double localBaselineY = localCenter.y() + (fm.ascent() - fm.descent()) / 2.0;
        txtData.pos = mapPoint(QPointF(localBaselineX, localBaselineY));

        auto *tItem = new TextItem(txtData);
        applyItemStyle(tItem, style.dimTextLayer);
        return tItem;
    };

    QRectF symbolCell(x0, fr.top(), sw, h);
    QString symStr;
    switch(m_data.symbol) {
    case GdtSymbol::Straightness: symStr = QString::fromUtf8("―"); break;
    case GdtSymbol::Flatness: symStr = QString::fromUtf8("▱"); break;
    case GdtSymbol::Circularity: symStr = QString::fromUtf8("○"); break;
    case GdtSymbol::Cylindricity: symStr = QString::fromUtf8("⌀̈"); break;
    case GdtSymbol::LineProfile: symStr = QString::fromUtf8("⌒"); break;
    case GdtSymbol::SurfaceProfile: symStr = QString::fromUtf8("⌓"); break;
    case GdtSymbol::Parallelism: symStr = QString::fromUtf8("∥"); break;
    case GdtSymbol::Perpendicularity: symStr = QString::fromUtf8("⊥"); break;
    case GdtSymbol::Angularity: symStr = QString::fromUtf8("∠"); break;
    case GdtSymbol::Position: symStr = QString::fromUtf8("⊕"); break;
    case GdtSymbol::Concentricity: symStr = QString::fromUtf8("◎"); break;
    case GdtSymbol::Symmetry: symStr = QString::fromUtf8("≡"); break;
    case GdtSymbol::CircularRunout: symStr = QString::fromUtf8("↗"); break;
    case GdtSymbol::TotalRunout: symStr = QString::fromUtf8("⇗"); break;
    default: symStr = "?"; break;
    }
    primitives << createTextItem(symStr, symbolCell, h * 0.55);

    QRectF tolCell(x0 + sw, fr.top(), tw, h);
    QString tolText = m_data.hasDiameter ? QString::fromUtf8("Ø") + m_data.toleranceValue : m_data.toleranceValue;
    primitives << createTextItem(tolText, tolCell, m_data.textHeight * finalScale);

    double dX = x0 + sw + tw;
    for (int i = 0; i < m_data.datumRefs.size(); ++i) {
        double dw_i = GdtCalculator::datumCellWidth(th_s, m_data.datumRefs[i]);
        QRectF datumCell(dX, fr.top(), dw_i, h);
        primitives << createTextItem(m_data.datumRefs[i], datumCell, m_data.textHeight * finalScale);
        dX += dw_i;
    }

    for (int i = 0; i < primitives.size(); ++i) { primitives[i]->setParentItem(nullptr); }
    return primitives;
}