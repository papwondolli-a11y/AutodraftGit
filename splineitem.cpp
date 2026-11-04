#include "splineitem.h"
#include "mymath.h"
#include "myline.h"
#include "mygraphicsscene.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>

SplineItem::SplineItem(const std::vector<MyVector> &controlPoints, int degree, bool isClosed, QGraphicsItem *parent)
    : CadItem(parent) {
    m_data.controlPoints = controlPoints;
    m_data.degree = degree;
    m_data.isClosed = isClosed;
    m_pen = QPen(Qt::white, 0);
    updatePath();
}

SplineItem::SplineItem(const MySpline &spline, QGraphicsItem *parent)
    : CadItem(parent), m_data(spline) {
    m_pen = QPen(Qt::white, 0);
    updatePath();
}

CadItem *SplineItem::clone() const {
    SplineItem *cloned = new SplineItem(m_data);
    cloned->setPen(pen());
    cloned->setLayerName(layerName());
    return cloned;
}

QMap<QString, QVariant> SplineItem::getProperties() const {
    QMap<QString, QVariant> props;
    props["Type"] = "Spline";
    props["Layer"] = layerName();
    props["Points"] = (int)m_data.controlPoints.size();
    props["Degree"] = m_data.degree;
    props["Closed"] = m_data.isClosed;
    return props;
}

bool SplineItem::setProperty(const QString &key, const QVariant &value) {
    if (key == "Layer") {
        setLayerName(value.toString());
        return true;
    }
    if (key == "Closed") {
        m_data.isClosed = value.toBool();
        updatePath();
        return true;
    }
    return false;
}

QRectF SplineItem::boundingRect() const {
    qreal margin = m_pen.widthF() / 2.0 + 2.0;
    return m_path.boundingRect().adjusted(-margin, -margin, margin, margin);
}

void SplineItem::setPen(const QPen &pen) {
    prepareGeometryChange(); // 펜 폭 변화가 boundingRect 여백을 바꾸므로 통지
    CadItem::setPen(pen);
}

void SplineItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget);

    QPen currentPen = m_pen;
    currentPen.setCosmetic(true);

    // 씬에서 선가중치 표시 여부 확인
    MyGraphicsScene *myScene = qobject_cast<MyGraphicsScene *>(scene());
    bool showWeight = myScene ? myScene->showLineWeight() : false;

    if (!showWeight) {
        // 가중치 끄기: 무조건 1픽셀 코스메틱 펜
        currentPen.setCosmetic(true);
        currentPen.setWidthF(1.0);
    } else {
        // 가중치 켜기: 실제 두께 반영하되 화면에서 1픽셀 미만으로 흐려지는 것 방지
        currentPen.setCosmetic(false);
        qreal scale = qAbs(painter->transform().m11());
        if (scale > 0.0 && currentPen.widthF() * scale < 1.0) {
            currentPen.setWidthF(1.0 / scale);
        }
    }

    // 하이라이트(선택 시)
    if ((option->state & QStyle::State_Selected) &&
        painter->device()->devType() == QInternal::Widget) {
        painter->save();
        QPen glowPen(QColor(0, 255, 255, 80));
        glowPen.setCosmetic(true);
        glowPen.setWidthF(currentPen.widthF() + 4.0);
        painter->setPen(glowPen);
        painter->drawPath(m_path);

        QPen dashPen(Qt::white);
        dashPen.setCosmetic(true);
        dashPen.setWidthF(1.0);
        dashPen.setStyle(Qt::DashLine);
        painter->setPen(dashPen);
        painter->drawPath(m_path);
        painter->restore();
    }

    painter->setPen(currentPen);
    painter->drawPath(m_path);
}

QPainterPath SplineItem::shape() const {
    QPainterPathStroker stroker;
    stroker.setWidth(10.0); // 선택 감도 (픽박스 크기 고려)
    return stroker.createStroke(m_path);
}

QPainterPath SplineItem::geometryPath() const {
    return m_path;
}

QList<SnapPoint> SplineItem::getSnapPoints() const {
    QList<SnapPoint> snaps;
    for (const auto &p : m_data.controlPoints) {
        snaps.append(SnapPoint(p, SnapType::Endpoint));
    }
    return snaps;
}

QList<QPointF> SplineItem::getGripPoints() const {
    QList<QPointF> grips;
    for (const auto &p : m_data.controlPoints) {
        grips.append(QPointF(p.x, p.y));
    }
    return grips;
}

void SplineItem::applyGripMove(int index, const MyVector &pos) {
    if (index >= 0 && index < (int)m_data.controlPoints.size()) {
        m_data.controlPoints[index] = pos;
        updatePath();
    }
}

QPainterPath SplineItem::getGhostShape(int index, const MyVector &pos) const {
    MySpline temp = m_data;
    if (index >= 0 && index < (int)temp.controlPoints.size()) {
        temp.controlPoints[index] = pos;
    }
    // Simplification: return original path or updated path if not too slow
    return m_path; 
}

double SplineItem::distanceTo(const MyVector &pos) const {
    if (m_path.isEmpty()) return 1e10;

    double minDist = 1e10;
    QPolygonF poly = m_path.toFillPolygon();
    
    if (poly.isEmpty() || poly.size() < 2) {
        // 폴리곤화가 실패한 경우 element 단위로 직접 선분 거리 체크
        QPointF lastPt;
        bool hasLast = false;
        for (int i = 0; i < m_path.elementCount(); ++i) {
            auto el = m_path.elementAt(i);
            QPointF currPt(el.x, el.y);
            if (hasLast && !el.isMoveTo()) {
                // 선분 (lastPt, currPt)와 pos 사이의 거리 계산
                MyVector v1(lastPt);
                MyVector v2(currPt);
                MyVector ab = v2 - v1;
                MyVector ap = pos - v1;
                double lenSq = ab.x * ab.x + ab.y * ab.y;
                double d;
                if (lenSq < 1e-9) {
                    d = pos.distanceTo(v1);
                } else {
                    double t = (ap.x * ab.x + ap.y * ab.y) / lenSq;
                    if (t < 0.0) t = 0.0;
                    else if (t > 1.0) t = 1.0;
                    MyVector closest = v1 + ab * t;
                    d = pos.distanceTo(closest);
                }
                if (d < minDist) minDist = d;
            }
            lastPt = currPt;
            hasLast = true;
        }
    } else {
        // 폴리곤화된 선분들 순회
        for (int i = 0; i < poly.size() - 1; ++i) {
            MyVector v1(poly[i]);
            MyVector v2(poly[i+1]);
            MyVector ab = v2 - v1;
            MyVector ap = pos - v1;
            double lenSq = ab.x * ab.x + ab.y * ab.y;
            double d;
            if (lenSq < 1e-9) {
                d = pos.distanceTo(v1);
            } else {
                double t = (ap.x * ab.x + ap.y * ab.y) / lenSq;
                if (t < 0.0) t = 0.0;
                else if (t > 1.0) t = 1.0;
                MyVector closest = v1 + ab * t;
                d = pos.distanceTo(closest);
            }
            if (d < minDist) minDist = d;
        }
    }
    
    return minDist;
}

SnapPoint SplineItem::getNearestSnap(const MyVector &pos) const {
    double minD = 1e10;
    MyVector bestP;
    for (const auto &p : m_data.controlPoints) {
        double d = pos.distanceTo(p);
        if (d < minD) {
            minD = d;
            bestP = p;
        }
    }
    return SnapPoint(bestP, SnapType::Endpoint);
}

void SplineItem::move(const MyVector &delta) {
    for (auto &p : m_data.controlPoints) {
        p = p + delta;
    }
    updatePath();
}

void SplineItem::mirror(const MyVector &p1, const MyVector &p2) {
    for (auto &p : m_data.controlPoints) {
        p = MyMath::reflectPoint(p, p1, p2);
    }
    updatePath();
}

void SplineItem::rotate(const MyVector &base, double angle) {
    for (auto &p : m_data.controlPoints) {
        p = MyMath::rotatePoint(p, base, angle);
    }
    updatePath();
}

void SplineItem::scale(const MyVector &base, double scaleFactor) {
    for (auto &p : m_data.controlPoints) {
        p = MyMath::scalePoint(p, base, scaleFactor);
    }
    updatePath();
}

void SplineItem::stretch(const QList<int> &indices, const MyVector &delta) {
    for (int idx : indices) {
        if (idx >= 0 && idx < (int)m_data.controlPoints.size()) {
            m_data.controlPoints[idx] = m_data.controlPoints[idx] + delta;
        }
    }
    updatePath();
}

QPainterPath SplineItem::getMovedPath(const MyVector &delta) const {
    QTransform trans;
    trans.translate(delta.x, delta.y);
    return trans.map(m_path);
}

QPainterPath SplineItem::getMirroredPath(const MyVector &p1, const MyVector &p2) const {
    Q_UNUSED(p1)
    Q_UNUSED(p2)

    // Simplified: apply mirror to all points and regenerate path is too complex here
    // Use QTransform if possible, but mirror line is arbitrary.
    // For ghosting, we can just return the current path or a simple transform
    return m_path; 
}

QPainterPath SplineItem::getRotatedPath(const MyVector &base, double angle) const {
    QTransform trans;
    trans.translate(base.x, base.y);
    trans.rotate(angle);
    trans.translate(-base.x, -base.y);
    return trans.map(m_path);
}

QPainterPath SplineItem::getScaledPath(const MyVector &base, double scaleFactor) const {
    QTransform trans;
    trans.translate(base.x, base.y);
    trans.scale(scaleFactor, scaleFactor);
    trans.translate(-base.x, -base.y);
    return trans.map(m_path);
}

QPainterPath SplineItem::getStretchedPath(const QList<int> &indices, const MyVector &delta) const {
    Q_UNUSED(indices)
    Q_UNUSED(delta)

    return m_path; // Simplified for ghost
}

void SplineItem::setGripsVisible(bool visible) {
    Q_UNUSED(visible)

    // Implement if spline has custom grip items
}

void SplineItem::updatePath() {
    // [필수] m_path가 바뀌면 boundingRect()도 바뀐다. Qt에 알리지 않으면
    // 캐시된 이전 boundingRect로 페인트가 클리핑되고 공간 인덱스도 옛 위치를
    // 가리켜, move/rotate/scale/copy 후 스플라인이 사라진 것처럼 보인다.
    // (다른 도형 클래스는 모두 하고 있으나 SplineItem만 누락되어 있었음)
    prepareGeometryChange();

    m_path = QPainterPath();
    int n = (int)m_data.controlPoints.size();
    if (n < 2) return;

    m_path.moveTo(QPointF(m_data.controlPoints[0].x, m_data.controlPoints[0].y));

    if (m_data.degree == 1 || n == 2) {
        for (int i = 1; i < n; ++i)
            m_path.lineTo(QPointF(m_data.controlPoints[i].x, m_data.controlPoints[i].y));
    } else if (!m_data.knots.empty()) {
        // 노트 벡터가 있으면 de Boor B-스플라인 평가 (DXF 원본과 정확히 일치)
        int deg = m_data.degree;
        if (n < deg + 1) deg = n - 1;

        std::vector<double> knots = m_data.knots;
        int required = n + deg + 1;
        while ((int)knots.size() < required) knots.push_back(knots.back());
        while ((int)knots.size() > required) knots.pop_back();

        double tMin = knots[deg], tMax = knots[n];
        int nPts = std::max(50, n * 15);

        auto evalAt = [&](double t) -> QPointF {
            // 노트 스팬 탐색
            int k = deg;
            for (int i = deg; i < n; ++i) {
                if (t >= knots[i] && t < knots[i + 1]) { k = i; break; }
            }
            // de Boor
            struct Pt3 { double x, y, w; };
            std::vector<Pt3> d(deg + 1);
            for (int j = 0; j <= deg; ++j) {
                int idx = k - deg + j;
                d[j] = {m_data.controlPoints[idx].x, m_data.controlPoints[idx].y, 1.0};
            }
            for (int r = 1; r <= deg; ++r) {
                for (int j = deg; j >= r; --j) {
                    int idx = k - deg + j;
                    double denom = knots[idx + deg - r + 1] - knots[idx];
                    double alpha = (denom < 1e-12) ? 0.0 : (t - knots[idx]) / denom;
                    d[j] = {(1-alpha)*d[j-1].x + alpha*d[j].x,
                             (1-alpha)*d[j-1].y + alpha*d[j].y, 1.0};
                }
            }
            return QPointF(d[deg].x, d[deg].y);
        };

        for (int s = 1; s <= nPts; ++s) {
            double t = (s == nPts) ? tMax - 1e-10
                                   : tMin + (tMax - tMin) * double(s) / double(nPts);
            m_path.lineTo(evalAt(t));
        }
        // 끝점을 정확히 마지막 제어점으로 고정
        m_path.lineTo(QPointF(m_data.controlPoints[n-1].x, m_data.controlPoints[n-1].y));
    } else {
        // 노트 벡터 없으면 Catmull-Rom 근사 (폴백)
        int segments = m_data.isClosed ? n : n - 1;
        for (int i = 0; i < segments; ++i) {
            QPointF p1(m_data.controlPoints[i % n].x, m_data.controlPoints[i % n].y);
            QPointF p2(m_data.controlPoints[(i+1) % n].x, m_data.controlPoints[(i+1) % n].y);
            QPointF p0 = m_data.isClosed
                ? QPointF(m_data.controlPoints[(i-1+n)%n].x, m_data.controlPoints[(i-1+n)%n].y)
                : (i == 0 ? p1 : QPointF(m_data.controlPoints[i-1].x, m_data.controlPoints[i-1].y));
            QPointF p3 = m_data.isClosed
                ? QPointF(m_data.controlPoints[(i+2)%n].x, m_data.controlPoints[(i+2)%n].y)
                : (i+2 >= n ? p2 : QPointF(m_data.controlPoints[i+2].x, m_data.controlPoints[i+2].y));
            m_path.cubicTo(p1 + (p2-p0)/6.0, p2 - (p3-p1)/6.0, p2);
        }
    }

    if (m_data.isClosed)
        m_path.closeSubpath();
}
