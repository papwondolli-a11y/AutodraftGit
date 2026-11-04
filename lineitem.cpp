#include "lineitem.h"
#include "gripitem.h"
#include "mygraphicsscene.h"
#include "appcontext.h"     // IAppContext 구현체 (싱글톤 getInstance() 사용 목적)
#include "iappcontext.h"
#include "snapmanager.h"
#include "abstracttool.h"
#include "multilinedata.h"
#include "hairlinepen.h"

#include <QPainter>
#include <QPainterPathStroker>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <cmath>
#include <QMenu>

LineItem::LineItem(const MyVector &start, const MyVector &end,QGraphicsItem *parent)
    : LineItem(MyLine(start, end), parent) {}

CadItem *LineItem::clone() const {
    auto *item = new LineItem(m_data);
    item->setLayerName(layerName());
    item->setPen(pen());
    // ML 태그 복사 (group ID 는 복사 측에서 재매핑)
    const QString gid = data(ML_DATA_GROUP_ID).toString();
    if (!gid.isEmpty()) {
        item->setData(ML_DATA_GROUP_ID, gid);
        item->setData(ML_DATA_ROLE,     data(ML_DATA_ROLE));
        const double sp = data(ML_DATA_SPACING).toDouble();
        if (sp > 0.0) item->setData(ML_DATA_SPACING, sp);
    }
    return item;
}

LineItem::LineItem(const MyLine &line, QGraphicsItem *parent)
    : CadItem(parent), m_data(line) {
    // 그립 생성 (0:Start, 1:End, 2:Mid)
    m_startGrip = new GripItem(0, this);
    m_endGrip = new GripItem(1, this);
    m_midGrip = new GripItem(2, this);

    updateGrips();
    setGripsVisible(false);
}

QRectF LineItem::boundingRect() const {
    QPointF p1 = m_data.start.toPointF();
    QPointF p2 = m_data.end.toPointF();
//    double margin = 3.0;
    return QRectF(p1, p2).normalized() ; //.adjusted(-margin, -margin, margin, margin);
}

void LineItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                     QWidget *widget) {
    Q_UNUSED(widget);

    QPen currentPen = m_pen;
    currentPen.setCosmetic(true);

    MyGraphicsScene *myScene = qobject_cast<MyGraphicsScene *>(scene());
    bool showWeight = myScene ? myScene->showLineWeight() : false;

    if (!showWeight) {
        HairlinePen::apply(currentPen, painter->transform());
    } else {
        currentPen.setCosmetic(false);
        qreal scale = qAbs(painter->transform().m11());
        if (scale > 0.0 && currentPen.widthF() * scale < 1.0) {
            currentPen.setWidthF(1.0 / scale);
        }
    }

    // [정밀도] OpenGL 뷰포트는 정점/행렬을 float 로 처리하므로 원점에서 먼 큰 좌표를
    // 그대로 그리면 선이 흔들려 거칠게 보인다. 시작점을 원점으로 옮긴(double 행렬에
    // 합쳐지는) 작은 로컬 좌표로 그린다.
    // 또한 GL 래스터라이저는 선의 진행 방향에 따라 결과가 달라질 수 있어(같은 선이
    // 그린 방향에 따라 울퉁불퉁해 보임) 항상 오른쪽 끝 → 왼쪽 끝 순서로 그린다.
    QPointF origin = m_data.start.toPointF();
    QPointF last   = m_data.end.toPointF();
    if (origin.x() < last.x() || (origin.x() == last.x() && origin.y() < last.y()))
        qSwap(origin, last);
    const QPointF localEnd = last - origin;
    painter->save();
    painter->translate(origin);

    //  프린터 출력 시에는 선택 하이라이트를 그리지 않음
    if ((option->state & QStyle::State_Selected) &&
        painter->device()->devType() == QInternal::Widget) {
        painter->save();

        QPen glowPen(QColor(0, 255, 255, 80));
        glowPen.setCosmetic(true);
        glowPen.setWidthF(HairlinePen::glowWidth(currentPen, painter->transform()));
        painter->setPen(glowPen);
        painter->drawLine(QPointF(0, 0), localEnd);

        QPen dashPen(Qt::white);
        dashPen.setCosmetic(true);
        dashPen.setWidthF(1.0);
        dashPen.setStyle(Qt::DashLine);
        painter->setPen(dashPen);
        painter->drawLine(QPointF(0, 0), localEnd);

        painter->restore();
    }

    painter->setPen(currentPen);
    painter->drawLine(QPointF(0, 0), localEnd);
    painter->restore();
}

QPainterPath LineItem::geometryPath() const {
    QPainterPath path;
    path.moveTo(m_data.start.toPointF());
    path.lineTo(m_data.end.toPointF());
    return path;
}

QPainterPath LineItem::shape() const {
    if (m_isShapeValid)
        return m_cachedShape;

    QPainterPath path = geometryPath();
    QPainterPathStroker stroker;
    stroker.setWidth(1.0);
    m_cachedShape = stroker.createStroke(path);
    m_isShapeValid = true;
    return m_cachedShape;
}

QList<SnapPoint> LineItem::getSnapPoints() const {
    MyVector mid = (m_data.start + m_data.end) * 0.5;
    return {{m_data.start, SnapType::Endpoint},
            {m_data.end, SnapType::Endpoint},
            {mid, SnapType::Midpoint}};
}

QList<QPointF> LineItem::getGripPoints() const {
    return {m_data.start.toPointF(), m_data.end.toPointF(),
            ((m_data.start + m_data.end) * 0.5).toPointF()};
}

// =========================================================
// 직교 모드(F8) 공통 기능 지원용 기준점 제공 함수
// =========================================================
bool LineItem::getOrthoBasePoint(int index, MyVector &outBasePoint) const {
    if (index == 0) {
        outBasePoint = m_data.end;
        return true;
    } else if (index == 1) {
        outBasePoint = m_data.start;
        return true;
    } else if (index == 2) {
        outBasePoint = (m_data.start + m_data.end) * 0.5;
        return true;
    }
    return false;
}

void LineItem::applyGripMove(int index, const MyVector &pos) {
    prepareGeometryChange();

    if (index == 0) {
        m_data.start = pos;
    } else if (index == 1) {
        m_data.end = pos;
    } else if (index == 2) {
        MyVector currentMid = (m_data.start + m_data.end) * 0.5;
        MyVector delta = pos - currentMid;
        m_data.start = m_data.start + delta;
        m_data.end = m_data.end + delta;
    }
    m_isShapeValid = false;
    updateGrips();
    update();
}

QPainterPath LineItem::getGhostShape(int index, const MyVector &pos) const {
    QPainterPath path;
    MyVector s = m_data.start;
    MyVector e = m_data.end;

    if (index == 0)
        s = pos;
    else if (index == 1)
        e = pos;
    else if (index == 2) {
        MyVector mid = (s + e) * 0.5;
        MyVector delta = pos - mid;
        s = s + delta;
        e = e + delta;
    }

    path.moveTo(s.toPointF());
    path.lineTo(e.toPointF());
    return path;
}

double LineItem::distanceTo(const MyVector &pos) const {
    MyVector ab = m_data.end - m_data.start;
    MyVector ap = pos - m_data.start;
    double lenSq = ab.x * ab.x + ab.y * ab.y;

    if (lenSq < 1e-9) {
        return m_data.start.distanceTo(pos);
    }

    double t = (ap.x * ab.x + ap.y * ab.y) / lenSq;

    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;

    MyVector closestPoint = m_data.start + (ab * t);
    return pos.distanceTo(closestPoint);
}

void LineItem::updateGrips() {
    if (!m_startGrip)
        return;

    QList<SnapPoint> sp = getSnapPoints();

    m_startGrip->setPos(sp[0].pos.toPointF());
    m_endGrip->setPos(sp[1].pos.toPointF());
    m_midGrip->setPos(sp[2].pos.toPointF());
}

void LineItem::setGripsVisible(bool visible) {
    if (m_startGrip)
        m_startGrip->setVisible(visible);
    if (m_endGrip)
        m_endGrip->setVisible(visible);
    if (m_midGrip)
        m_midGrip->setVisible(visible);
}

void LineItem::setLine(const MyLine &line) {
    prepareGeometryChange();
    m_data = line;
    m_isShapeValid = false;
    updateGrips();
    update();
}

QVariant LineItem::itemChange(GraphicsItemChange change,
                              const QVariant &value) {
    if (change == QGraphicsItem::ItemSelectedChange) {
        if (!value.toBool()) {
            setGripsVisible(false);
        }
    }
    return CadItem::itemChange(change, value);
}

SnapPoint LineItem::getNearestSnap(const MyVector &pos) const {
    MyVector pt = m_data.getNearestPoint(pos);
    return SnapPoint(pt, SnapType::Nearest);
}

void LineItem::move(const MyVector &delta) {
    prepareGeometryChange();
    m_data.start = m_data.start + delta;
    m_data.end = m_data.end + delta;
    m_isShapeValid = false;
    updateGrips();
    update();
}

void LineItem::mirror(const MyVector &p1, const MyVector &p2) {
    prepareGeometryChange();
    m_data.start = MyMath::reflectPoint(m_data.start, p1, p2);
    m_data.end = MyMath::reflectPoint(m_data.end, p1, p2);
    m_isShapeValid = false;
    updateGrips();
    update();
}

QPainterPath LineItem::getMovedPath(const MyVector &delta) const {
    QPainterPath path;
    MyVector s = m_data.start + delta;
    MyVector e = m_data.end + delta;
    path.moveTo(s.toPointF());
    path.lineTo(e.toPointF());
    return path;
}

QMap<QString, QVariant> LineItem::getProperties() const {
    QMap<QString, QVariant> props;
    props["Type"] = "Line";
    props["Layer"] = layerName();
    props["StartX"] = m_data.start.x;
    props["StartY"] = m_data.start.y;
    props["EndX"] = m_data.end.x;
    props["EndY"] = m_data.end.y;
    props["Length"] = m_data.length();

    // 다중선 그룹 소속 선 → 몰타르 포함 전체 폭 노출
    if (!data(ML_DATA_GROUP_ID).toString().isEmpty()) {
        const QVariant w = data(ML_DATA_SPACING);
        if (w.isValid() && w.toDouble() > 0.0)
            props["MLWidth"] = w.toDouble();
    }

    return props;
}

bool LineItem::setProperty(const QString &key, const QVariant &value) {
    if (key == "Layer") {
        setLayerName(value.toString());
        return true;
    }

    prepareGeometryChange();
    if (key == "StartX")
        m_data.start.x = value.toDouble();
    else if (key == "StartY")
        m_data.start.y = value.toDouble();
    else if (key == "EndX")
        m_data.end.x = value.toDouble();
    else if (key == "EndY")
        m_data.end.y = value.toDouble();
    else
        return false;

    m_isShapeValid = false;
    updateGrips();
    update();
    return true;
}

void LineItem::updateEndpoint(const QPointF &newPos, bool isFirstEnd) {
    prepareGeometryChange();
    if (isFirstEnd) {
        m_data.start = MyVector(newPos);
    } else {
        m_data.end = MyVector(newPos);
    }
    m_isShapeValid = false;
    updateGrips();
    update();
}

QPainterPath LineItem::getMirroredPath(const MyVector &p1,
                                       const MyVector &p2) const {
    MyVector mStart = MyMath::reflectPoint(m_data.start, p1, p2);
    MyVector mEnd = MyMath::reflectPoint(m_data.end, p1, p2);

    QPainterPath path;
    path.moveTo(mStart.toPointF());
    path.lineTo(mEnd.toPointF());
    return path;
}

void LineItem::rotate(const MyVector &base, double angle) {
    prepareGeometryChange();
    m_data.start = MyMath::rotatePoint(m_data.start, base, angle);
    m_data.end = MyMath::rotatePoint(m_data.end, base, angle);
    m_isShapeValid = false;
    updateGrips();
    update();
}

QPainterPath LineItem::getRotatedPath(const MyVector &base,
                                      double angle) const {
    MyVector rStart = MyMath::rotatePoint(m_data.start, base, angle);
    MyVector rEnd = MyMath::rotatePoint(m_data.end, base, angle);

    QPainterPath path;
    path.moveTo(rStart.toPointF());
    path.lineTo(rEnd.toPointF());
    return path;
}

void LineItem::scale(const MyVector &base, double scaleFactor) {
    prepareGeometryChange();
    m_data.start = MyMath::scalePoint(m_data.start, base, scaleFactor);
    m_data.end = MyMath::scalePoint(m_data.end, base, scaleFactor);
    m_isShapeValid = false;
    updateGrips();
    update();
}

QPainterPath LineItem::getScaledPath(const MyVector &base,
                                     double scaleFactor) const {
    MyVector sStart = MyMath::scalePoint(m_data.start, base, scaleFactor);
    MyVector sEnd = MyMath::scalePoint(m_data.end, base, scaleFactor);

    QPainterPath path;
    path.moveTo(sStart.toPointF());
    path.lineTo(sEnd.toPointF());
    return path;
}

void LineItem::stretch(const QList<int> &indices, const MyVector &delta) {
    prepareGeometryChange();
    if (indices.contains(0)) {
        m_data.start = m_data.start + delta;
    }
    if (indices.contains(1)) {
        m_data.end = m_data.end + delta;
    }
    m_isShapeValid = false;
    updateGrips();
    update();
}

QPainterPath LineItem::getStretchedPath(const QList<int> &indices,
                                        const MyVector &delta) const {
    MyVector s = m_data.start;
    MyVector e = m_data.end;
    bool sChanged = indices.contains(0);
    bool eChanged = indices.contains(1);

    if (sChanged)
        s = m_data.start + delta;
    if (eChanged)
        e = m_data.end + delta;

    QPainterPath path;
    if (sChanged || eChanged) {
        path.moveTo(s.toPointF());
        path.lineTo(e.toPointF());
    }
    return path;
}