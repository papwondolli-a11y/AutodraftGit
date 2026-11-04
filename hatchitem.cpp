#include "hatchitem.h"
#include "mymath.h"
#include "hatchpatternmanager.h"
#include "mygraphicsscene.h"
#include "iappcontext.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

HatchItem::HatchItem(const MyHatch &data, QGraphicsItem *parent)
    : CadItem(parent), m_data(data) {
    m_data.path.setFillRule(Qt::WindingFill);
    setZValue(-1);
}

HatchItem::HatchItem(const QPainterPath &path, const QString& patternName, double scale, double angle, QGraphicsItem *parent)
    : CadItem(parent) {
    m_data.path.setFillRule(Qt::WindingFill);
    m_data.path = path;
    m_data.patternName = patternName;
    m_data.patternScale = scale;
    m_data.patternAngle = angle;
    m_data.style = Qt::SolidPattern;

    if (m_data.patternName.isEmpty()) {
        m_data.style = Qt::DiagCrossPattern;
    }
    setZValue(-1);
}

HatchItem::HatchItem(const QPainterPath &path, Qt::BrushStyle style, QGraphicsItem *parent)
    : CadItem(parent) {
    m_data.path.setFillRule(Qt::WindingFill);
    m_data.path = path;
    m_data.style = style;
    setZValue(-1);
}

CadItem *HatchItem::clone() const {
    auto *item = new HatchItem(m_data);
    item->setLayerName(layerName());
    item->setPen(m_pen);
    return item;
}

QRectF HatchItem::boundingRect() const {
    return m_data.path.boundingRect();
}

void HatchItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget);
    Q_UNUSED(option);

    painter->save();

    QString patternUpper = m_data.patternName.toUpper();

    HatchPatternManager* patManager = nullptr;
    if (scene()) {
        MyGraphicsScene* myScene = qobject_cast<MyGraphicsScene*>(scene());
        if (myScene && myScene->context()) {
            patManager = myScene->context()->hatchPatternManager();
        }
    }


    if (m_data.patternName.isEmpty() || patternUpper == "SOLID") {
        painter->setPen(Qt::NoPen);
        Qt::BrushStyle style = m_data.style;
        if (patternUpper == "SOLID") style = Qt::SolidPattern;

        QBrush brush(m_pen.color(), style);
        painter->setBrush(brush);
        painter->drawPath(m_data.path);
    } else {
        painter->setClipPath(m_data.path);

        // [수정됨] 매니저가 유효할 때만 패턴을 가져오도록 널 체크 추가
        HatchPattern pat;
        if (patManager) {
            pat = patManager->getPattern(m_data.patternName);
        }

        // 매니저가 없거나(nullptr), 패턴을 찾지 못한 경우 방어 코드 실행
        if (!patManager || pat.name.isEmpty()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QBrush(m_pen.color(), Qt::DiagCrossPattern));
            painter->drawPath(m_data.path);
        } else {
            QPen linePen = m_pen;

            MyGraphicsScene *hatchScene = qobject_cast<MyGraphicsScene *>(scene());
            bool showWeight = hatchScene ? hatchScene->showLineWeight() : false;

            if (!showWeight) {
                // [모드 A: 끄기] 도면 작성용 얇고 쨍한 선
                linePen.setCosmetic(true);
                linePen.setWidthF(1.0);
            } else {
                // [모드 B: 켜기] 실제 두께 표시
                linePen.setCosmetic(false);
                qreal scale = qAbs(painter->transform().m11());
                if (scale > 0.0 && linePen.widthF() * scale < 1.0) {
                    linePen.setWidthF(1.0 / scale);
                }
            }

            painter->setBrush(Qt::NoBrush);

            QRectF rect = m_data.path.boundingRect();
            QPointF center = rect.center();
            double diag = std::hypot(rect.width(), rect.height());
            double scale = m_data.patternScale;
            if (scale <= 0.0) scale = 1.0;

            for (int i = 0; i < (int)pat.lines.size(); ++i) {
                const HatchLineDef& def = pat.lines[i];

                double radAngle = qDegreesToRadians(def.angle + m_data.patternAngle);
                double cosA = std::cos(radAngle);
                double sinA = std::sin(radAngle);

                double x0 = def.x0 * scale;
                double y0 = -def.y0 * scale;
                QPointF origin(x0, y0);

                double dy = def.dy * scale;
                if (std::abs(dy) < 1e-5) dy = 10.0;

                QPointF pDir(cosA, -sinA);

                double pRad = radAngle + M_PI_2;
                QPointF pStep(std::cos(pRad), -std::sin(pRad));

                QVector<qreal> dashPattern;
                for (int k = 0; k < (int)def.dashes.size(); ++k) {
                    double d = def.dashes[k] * scale;
                    if (d > 0) dashPattern << d;
                    else if (d < 0) dashPattern << -d;
                    else dashPattern << 0.5;
                }

                bool hasDash = !dashPattern.isEmpty();
                if (hasDash) {
                    if (dashPattern.size() % 2 != 0) dashPattern << dashPattern.last();
                    linePen.setDashPattern(dashPattern);
                } else {
                    linePen.setStyle(Qt::SolidLine);
                }

                double projStep = QPointF::dotProduct(center - origin, pStep);
                int k_center = std::round(projStep / dy);

                double projDir = QPointF::dotProduct(center - origin, pDir);
                int numLines = std::ceil((diag / 2.0) / std::abs(dy)) + 2;

                for (int k = k_center - numLines; k <= k_center + numLines; ++k) {
                    QPointF basePt = origin + pStep * (k * dy);

                    double t1 = projDir - diag;
                    double t2 = projDir + diag;

                    QPointF p1 = basePt + pDir * t1;
                    QPointF p2 = basePt + pDir * t2;

                    if (hasDash) {
                        linePen.setDashOffset(t1);
                    }

                    painter->setPen(linePen);
                    painter->drawLine(p1, p2);
                }
            }
        }
    }

    painter->restore();
    
    // [핵심] 프린터 출력 시에는 선택 하이라이트를 그리지 않음
    if (option && (option->state & QStyle::State_Selected) &&
        painter->device()->devType() == QInternal::Widget) {
        painter->save();

        // 1. 내부를 반투명하게 칠하여 전체적인 면적이 선택되었음을 직관적으로 알림 (알파값 64)
        QColor highlightColor(0, 255, 255, 20);
        painter->setBrush(QBrush(highlightColor));

        // 2. 외곽선은 기존보다 조금 더 두껍고 명확하게 설정
        QPen selectPen(Qt::cyan, 1, Qt::DashLine);
        selectPen.setCosmetic(true);
        painter->setPen(selectPen);

        // 브러시와 펜이 설정된 상태로 패스를 그림
        painter->drawPath(m_data.path);

        painter->restore();
    }
}

QPainterPath HatchItem::shape() const {
    return m_data.path;
}

QPainterPath HatchItem::geometryPath() const {
    return m_data.path;
}

QList<SnapPoint> HatchItem::getSnapPoints() const {
    return {};
}

QList<QPointF> HatchItem::getGripPoints() const {
    return {};
}

void HatchItem::applyGripMove(int index, const MyVector &pos) {
    Q_UNUSED(index);
    Q_UNUSED(pos);
}

QPainterPath HatchItem::getGhostShape(int index, const MyVector &pos) const {
    Q_UNUSED(index);
    Q_UNUSED(pos);
    return m_data.path;
}

double HatchItem::distanceTo(const MyVector &pos) const {
    QPointF qpos = pos.toPointF();

    if (m_data.path.contains(qpos)) return 0.0;

    double minDist = std::numeric_limits<double>::max();
    QList<QPolygonF> polys = m_data.path.toSubpathPolygons();
    for (int p = 0; p < polys.size(); ++p) {
        const QPolygonF &poly = polys[p];
        for (int i = 0; i < poly.size() - 1; ++i) {
            QLineF seg(poly[i], poly[i + 1]);
            double len2 = seg.dx() * seg.dx() + seg.dy() * seg.dy();
            double dist;
            if (len2 < 1e-12) {
                dist = QLineF(qpos, poly[i]).length();
            } else {
                double t = ((qpos.x() - poly[i].x()) * seg.dx() +
                            (qpos.y() - poly[i].y()) * seg.dy()) / len2;
                t = qBound(0.0, t, 1.0);
                QPointF foot(poly[i].x() + t * seg.dx(), poly[i].y() + t * seg.dy());
                dist = QLineF(qpos, foot).length();
            }
            if (dist < minDist) minDist = dist;
        }
    }
    return (minDist == std::numeric_limits<double>::max()) ? 1000.0 : minDist;
}

SnapPoint HatchItem::getNearestSnap(const MyVector &pos) const {
    return {pos, SnapType::Nearest};
}

void HatchItem::move(const MyVector &delta) {
    prepareGeometryChange();
    QTransform t;
    t.translate(delta.x, delta.y);
    m_data.path = t.map(m_data.path);
}

void HatchItem::mirror(const MyVector &p1, const MyVector &p2) {
    prepareGeometryChange();
    QPainterPath newPath;
    newPath.setFillRule(m_data.path.fillRule()); // 속성 보존

    for (int i = 0; i < m_data.path.elementCount(); ++i) {
        QPainterPath::Element el = m_data.path.elementAt(i);
        MyVector v = MyMath::reflectPoint(MyVector(el.x, el.y), p1, p2);
        if (el.isMoveTo()) {
            newPath.moveTo(v.toPointF());
        } else if (el.isLineTo()) {
            newPath.lineTo(v.toPointF());
        } else if (el.type == QPainterPath::CurveToElement) {
            if (i + 2 < m_data.path.elementCount()) {
                QPainterPath::Element el2 = m_data.path.elementAt(i + 1);
                QPainterPath::Element el3 = m_data.path.elementAt(i + 2);
                MyVector v2 = MyMath::reflectPoint(MyVector(el2.x, el2.y), p1, p2);
                MyVector v3 = MyMath::reflectPoint(MyVector(el3.x, el3.y), p1, p2);
                newPath.cubicTo(v.toPointF(), v2.toPointF(), v3.toPointF());
                i += 2;
            }
        }
    }
    m_data.path = newPath;
}

QPainterPath HatchItem::getMovedPath(const MyVector &delta) const {
    QTransform t;
    t.translate(delta.x, delta.y);
    return t.map(m_data.path);
}

QPainterPath HatchItem::getMirroredPath(const MyVector &p1, const MyVector &p2) const {
    QPainterPath newPath;
    for (int i = 0; i < m_data.path.elementCount(); ++i) {
        QPainterPath::Element el = m_data.path.elementAt(i);
        MyVector v = MyMath::reflectPoint(MyVector(el.x, el.y), p1, p2);
        if (el.isMoveTo()) {
            newPath.moveTo(v.toPointF());
        } else if (el.isLineTo()) {
            newPath.lineTo(v.toPointF());
        } else if (el.type == QPainterPath::CurveToElement) {
            if (i + 2 < m_data.path.elementCount()) {
                QPainterPath::Element el2 = m_data.path.elementAt(i + 1);
                QPainterPath::Element el3 = m_data.path.elementAt(i + 2);
                MyVector v2 = MyMath::reflectPoint(MyVector(el2.x, el2.y), p1, p2);
                MyVector v3 = MyMath::reflectPoint(MyVector(el3.x, el3.y), p1, p2);
                newPath.cubicTo(v.toPointF(), v2.toPointF(), v3.toPointF());
                i += 2;
            }
        }
    }
    return newPath;
}

void HatchItem::rotate(const MyVector &base, double angle) {
    prepareGeometryChange();
    QTransform t;
    t.translate(base.x, base.y);
    t.rotate(angle);
    t.translate(-base.x, -base.y);
    m_data.path = t.map(m_data.path);
}

QPainterPath HatchItem::getRotatedPath(const MyVector &base, double angle) const {
    QTransform t;
    t.translate(base.x, base.y);
    t.rotate(angle);
    t.translate(-base.x, -base.y);
    return t.map(m_data.path);
}

void HatchItem::scale(const MyVector &base, double scaleFactor) {
    prepareGeometryChange();
    QTransform t;
    t.translate(base.x, base.y);
    t.scale(scaleFactor, scaleFactor);
    t.translate(-base.x, -base.y);
    m_data.path = t.map(m_data.path);
}

QPainterPath HatchItem::getScaledPath(const MyVector &base, double scaleFactor) const {
    QTransform t;
    t.translate(base.x, base.y);
    t.scale(scaleFactor, scaleFactor);
    t.translate(-base.x, -base.y);
    return t.map(m_data.path);
}

void HatchItem::stretch(const QList<int> &indices, const MyVector &delta) {
    Q_UNUSED(indices);
    Q_UNUSED(delta);
}

QPainterPath HatchItem::getStretchedPath(const QList<int> &indices, const MyVector &delta) const {
    Q_UNUSED(indices);
    Q_UNUSED(delta);
    return m_data.path;
}

QMap<QString, QVariant> HatchItem::getProperties() const {
    QMap<QString, QVariant> props;
    props["Type"] = "Hatch";
    props["Pattern"] = m_data.patternName;
    props["Scale"] = m_data.patternScale;
    props["Angle"] = m_data.patternAngle;
    return props;
}

void HatchItem::setGripsVisible(bool visible) {
    Q_UNUSED(visible);
}

bool HatchItem::setProperty(const QString &key, const QVariant &value) {
    if (key == "Pattern") {
        m_data.patternName = value.toString();
        update();
        return true;
    } else if (key == "Scale") {
        m_data.patternScale = value.toDouble();
        update();
        return true;
    } else if (key == "Angle") {
        m_data.patternAngle = value.toDouble();
        update();
        return true;
    }
    return false;
}
