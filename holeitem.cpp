#include "holeitem.h"
#include "gripitem.h"
#include "holecalloutlink.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QPen>
#include <QStyleOptionGraphicsItem>

namespace {
// ISO 6410 관례: 나사산(호칭지름) 표시원은 완전한 원이 아니라 3/4(270도) 오픈호로 그림.
// 개구부(90도 갭)의 중심을 7시 방향에 둠.
// 주의: 이 씬의 뷰가 Y축을 반전해서 그리기 때문에(CAD 관례상 Y+ = 위쪽),
// Qt 각도 문서상의 "3시=0도, 반시계=+" 기준을 그대로 대입하면 상하가 뒤집혀
// 7시가 아닌 11시에 개구부가 생긴다 — 실측으로 확인된 상하반전을 보정하기 위해
// 목표각(7시=240도)의 수직미러값(120도)을 기준으로 시작각을 역산함.
constexpr double kThreadArcStartDeg = 165.0; // (7시 목표각 240도의 수직미러 120도) + 45도
constexpr double kThreadArcSpanDeg = 270.0;

// 판재 모서리/대칭선에 걸친 구멍을 절반만 표시할 때 쓰는 반원 시작각.
// Qt 각도 관례(3시=0도, 반시계=+)로 -90~90(오른쪽), 90~270(왼쪽) 180도 구간.
// 좌/우 판별이라 Y축 반전(위 나사산 주석 참고)의 영향을 받지 않는다.
void addCircleOrHalf(QPainterPath &path, const QPointF &c, double r, HoleRenderMode mode) {
    if (r <= 0.0) return;
    if (mode == HoleRenderMode::Full) {
        path.addEllipse(c, r, r);
        return;
    }
    QRectF rect(c.x() - r, c.y() - r, r * 2, r * 2);
    double startAngle = (mode == HoleRenderMode::Right) ? -90.0 : 90.0;
    path.arcMoveTo(rect, startAngle);
    path.arcTo(rect, startAngle, 180.0);
}
}

HoleItem::HoleItem(const MyHole &data, QGraphicsItem *parent)
    : CadItem(parent), m_data(data) {
    m_pen = QPen(Qt::black, 0);
    m_grips.append(new GripItem(0, this)); // Center
    m_grips.append(new GripItem(1, this)); // 반지름 핸들
    updateGrips();
    setGripsVisible(false);
}

HoleItem::~HoleItem() {}

// 기본구멍/탭/카운터보어/카운터싱크 원은 모두 각자 지정된 레이어의 색상 + 라인타입을
// 그대로 따른다(하드코딩된 점선/실선 없음). HoleSectionTool::sectionPenForLayer 와 동일하게
// 씬의 레이어 스타일 캐시에서 대시 패턴을 읽고, Qt가 짝수 길이 패턴만 그리므로 홀수면
// 마지막 값을 한 번 더 붙여 보정한다. 패턴이 없으면(Continuous 등) 실선.
QPen HoleItem::resolvePen(const QPen &basePen, const QString &layerOverride) const {
    if (layerOverride.isEmpty()) return basePen;
    MyGraphicsScene *myScene = qobject_cast<MyGraphicsScene *>(scene());
    if (!myScene) return basePen;
    QPen p = basePen;
    const LayerStyle style = myScene->getLayerStyle(layerOverride);
    p.setColor(style.color);

    QVector<qreal> dashPattern = style.dashPattern;
    if (dashPattern.size() % 2 != 0)
        dashPattern.append(dashPattern.last());
    if (!dashPattern.isEmpty()) {
        p.setStyle(Qt::CustomDashLine);
        p.setDashPattern(dashPattern);
    } else {
        p.setStyle(Qt::SolidLine);
    }
    return p;
}

double HoleItem::maxRadius() const {
    return m_data.outerRadius();
}

QPainterPath HoleItem::buildPath(const MyVector &center) const {
    QPainterPath path;
    QPointF c = center.toPointF();
    double r = m_data.diameter / 2.0;

    // 1. 기본 구멍 원 (탭드릴 지름) — renderMode가 Right/Left면 그 반원만
    addCircleOrHalf(path, c, r, m_data.renderMode);

    // 2. 탭(나사) 호칭지름(major diameter) 원 — 기본구멍 원 바깥쪽.
    // Full일 때만 ISO 6410 3/4 오픈호를 쓰고, 반원 모드에서는 다른 원들과 같은
    // 단순 반원으로 통일한다(3/4 오픈호와 반원 클리핑을 동시에 표현하면 과도하게 복잡함).
    if (m_data.isThreaded) {
        double majorR = m_data.threadMajorRadius();
        if (m_data.renderMode == HoleRenderMode::Full) {
            QRectF rect(c.x() - majorR, c.y() - majorR, majorR * 2, majorR * 2);
            path.arcMoveTo(rect, kThreadArcStartDeg);
            path.arcTo(rect, kThreadArcStartDeg, kThreadArcSpanDeg);
        } else {
            addCircleOrHalf(path, c, majorR, m_data.renderMode);
        }
    }

    // 3. 카운터보어
    if (m_data.hasCounterbore && m_data.cbDiameter > 0) {
        addCircleOrHalf(path, c, m_data.cbDiameter / 2.0, m_data.renderMode);
    }

    // 4. 카운터싱크
    if (m_data.hasCountersink && m_data.csDiameter > 0) {
        addCircleOrHalf(path, c, m_data.csDiameter / 2.0, m_data.renderMode);
    }

    return path;
}

QRectF HoleItem::boundingRect() const {
    QPointF c = m_data.position.toPointF();
    double r = maxRadius();
    double margin = 3.0;
    return QRectF(c.x() - r, c.y() - r, r * 2, r * 2)
        .adjusted(-margin, -margin, margin, margin);
}

void HoleItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                     QWidget *widget) {
    Q_UNUSED(widget);

    QPen currentPen = m_pen;
    MyGraphicsScene *myScene = qobject_cast<MyGraphicsScene *>(scene());
    bool showWeight = myScene ? myScene->showLineWeight() : false;

    if (!showWeight) {
        currentPen.setCosmetic(true);
        currentPen.setWidthF(1.0);
    } else {
        currentPen.setCosmetic(false);
        qreal s = qAbs(painter->transform().m11());
        if (s > 0.0 && currentPen.widthF() * s < 1.0) {
            currentPen.setWidthF(1.0 / s);
        }
    }

    if ((option->state & QStyle::State_Selected) &&
        painter->device()->devType() == QInternal::Widget) {
        painter->save();
        QPen glowPen(QColor(0, 255, 255, 80));
        glowPen.setCosmetic(true);
        glowPen.setWidthF(currentPen.widthF() + 4.0);
        painter->setPen(glowPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(buildPath(m_data.position));

        QPen dashPen(Qt::white);
        dashPen.setCosmetic(true);
        dashPen.setWidthF(1.0);
        dashPen.setStyle(Qt::DashLine);
        painter->setPen(dashPen);
        painter->drawPath(buildPath(m_data.position));
        painter->restore();
    }

    QPointF c = m_data.position.toPointF();
    painter->setBrush(Qt::NoBrush);

    // 기본 구멍 원
    QPen basePen = resolvePen(currentPen, m_data.baseLayer);
    painter->setPen(basePen);
    double r = m_data.diameter / 2.0;
    drawCircleOrHalf(painter, c, r);

    // 탭 호칭지름(major diameter) 원 - 기본구멍 원 바깥쪽.
    // Full일 때만 ISO 6410 3/4 오픈호(7시 방향 갭), 반원 모드에서는 단순 반원으로 통일.
    if (m_data.isThreaded) {
        painter->setPen(resolvePen(currentPen, m_data.threadLayer));
        double majorR = m_data.threadMajorRadius();
        if (m_data.renderMode == HoleRenderMode::Full) {
            QRectF rect(c.x() - majorR, c.y() - majorR, majorR * 2, majorR * 2);
            painter->drawArc(rect, qRound(kThreadArcStartDeg * 16), qRound(kThreadArcSpanDeg * 16));
        } else {
            drawCircleOrHalf(painter, c, majorR);
        }
    }

    // 카운터보어 — 카운터보어 레이어에 설정된 라인타입을 따른다.
    if (m_data.hasCounterbore && m_data.cbDiameter > 0) {
        painter->setPen(resolvePen(currentPen, m_data.cbLayer));
        double cbR = m_data.cbDiameter / 2.0;
        drawCircleOrHalf(painter, c, cbR);
    }

    // 카운터싱크 — 카운터싱크 레이어에 설정된 라인타입(대시 패턴)을 그대로 따른다.
    if (m_data.hasCountersink && m_data.csDiameter > 0) {
        painter->setPen(resolvePen(currentPen, m_data.csLayer));
        double csR = m_data.csDiameter / 2.0;
        drawCircleOrHalf(painter, c, csR);
    }
}

void HoleItem::drawCircleOrHalf(QPainter *painter, const QPointF &c, double r) const {
    if (r <= 0.0) return;
    if (m_data.renderMode == HoleRenderMode::Full) {
        painter->drawEllipse(c, r, r);
        return;
    }
    QRectF rect(c.x() - r, c.y() - r, r * 2, r * 2);
    double startAngle = (m_data.renderMode == HoleRenderMode::Right) ? -90.0 : 90.0;
    painter->drawArc(rect, qRound(startAngle * 16), qRound(180.0 * 16));
}

QPainterPath HoleItem::geometryPath() const {
    return buildPath(m_data.position);
}

QPainterPath HoleItem::shape() const {
    if (m_isShapeValid)
        return m_cachedShape;

    QPainterPathStroker stroker;
    stroker.setWidth(4.0);
    m_cachedShape = stroker.createStroke(geometryPath());
    m_isShapeValid = true;
    return m_cachedShape;
}

QList<SnapPoint> HoleItem::getSnapPoints() const {
    MyVector c = m_data.position;
    double r = m_data.diameter / 2.0;
    return {
        {c, SnapType::Center},
        {c + MyVector(r, 0, 0), SnapType::Quadrant},
        {c + MyVector(0, -r, 0), SnapType::Quadrant},
        {c + MyVector(-r, 0, 0), SnapType::Quadrant},
        {c + MyVector(0, r, 0), SnapType::Quadrant}
    };
}

SnapPoint HoleItem::getNearestSnap(const MyVector &pos) const {
    return SnapPoint(m_data.getNearestPoint(pos), SnapType::Nearest);
}

QList<QPointF> HoleItem::getGripPoints() const { return {}; }

void HoleItem::applyGripMove(int index, const MyVector &pos) {
    prepareGeometryChange();
    if (index == 0) {
        m_data.position = pos;
    } else {
        m_data.diameter = std::max(0.001, m_data.position.distanceTo(pos) * 2.0);
    }
    m_isShapeValid = false;
    updateGrips();
    update();
    if (index != 0) HoleCalloutLink::refreshCalloutsForHole(this); // 지름 그립
}

QPainterPath HoleItem::getGhostShape(int index, const MyVector &pos) const {
    MyHole tmp = m_data;
    if (index == 0) {
        tmp.position = pos;
    } else {
        tmp.diameter = std::max(0.001, m_data.position.distanceTo(pos) * 2.0);
    }
    return buildPath(tmp.position);
}

double HoleItem::distanceTo(const MyVector &pos) const {
    double distToCenter = m_data.position.distanceTo(pos);
    double best = std::abs(distToCenter - m_data.diameter / 2.0);
    if (m_data.isThreaded)
        best = std::min(best, std::abs(distToCenter - m_data.threadMajorRadius()));
    if (m_data.hasCounterbore && m_data.cbDiameter > 0)
        best = std::min(best, std::abs(distToCenter - m_data.cbDiameter / 2.0));
    if (m_data.hasCountersink && m_data.csDiameter > 0)
        best = std::min(best, std::abs(distToCenter - m_data.csDiameter / 2.0));
    return best;
}

void HoleItem::updateGrips() {
    if (m_grips.size() < 2) return;
    m_grips[0]->setPos(m_data.position.toPointF());
    MyVector rightPt = m_data.position + MyVector(m_data.diameter / 2.0, 0, 0);
    m_grips[1]->setPos(rightPt.toPointF());
}

void HoleItem::setGripsVisible(bool visible) {
    for (auto grip : std::as_const(m_grips))
        grip->setVisible(visible);
}

void HoleItem::setHoleData(const MyHole &data) {
    prepareGeometryChange();
    m_data = data;
    m_isShapeValid = false;
    updateGrips();
    update();
    HoleCalloutLink::refreshCalloutsForHole(this);
}

QVariant HoleItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == QGraphicsItem::ItemSelectedChange) {
        if (!value.toBool()) setGripsVisible(false);
    }
    return CadItem::itemChange(change, value);
}

void HoleItem::move(const MyVector &delta) {
    prepareGeometryChange();
    m_data.position = m_data.position + delta;
    m_isShapeValid = false;
    updateGrips();
    update();
}

void HoleItem::mirror(const MyVector &p1, const MyVector &p2) {
    prepareGeometryChange();
    m_data.position = MyMath::reflectPoint(m_data.position, p1, p2);
    m_isShapeValid = false;
    updateGrips();
    update();
}

QPainterPath HoleItem::getMovedPath(const MyVector &delta) const {
    return buildPath(m_data.position + delta);
}

QPainterPath HoleItem::getMirroredPath(const MyVector &p1, const MyVector &p2) const {
    return buildPath(MyMath::reflectPoint(m_data.position, p1, p2));
}

void HoleItem::rotate(const MyVector &base, double angle) {
    prepareGeometryChange();
    m_data.position = MyMath::rotatePoint(m_data.position, base, angle);
    m_isShapeValid = false;
    updateGrips();
    update();
}

QPainterPath HoleItem::getRotatedPath(const MyVector &base, double angle) const {
    return buildPath(MyMath::rotatePoint(m_data.position, base, angle));
}

void HoleItem::scale(const MyVector &base, double scaleFactor) {
    prepareGeometryChange();
    double f = std::abs(scaleFactor);
    m_data.position = MyMath::scalePoint(m_data.position, base, scaleFactor);
    m_data.diameter *= f;
    if (m_data.hasCounterbore) m_data.cbDiameter *= f;
    if (m_data.hasCountersink) m_data.csDiameter *= f;
    m_isShapeValid = false;
    updateGrips();
    update();
    HoleCalloutLink::refreshCalloutsForHole(this);
}

QPainterPath HoleItem::getScaledPath(const MyVector &base, double scaleFactor) const {
    MyVector c = MyMath::scalePoint(m_data.position, base, scaleFactor);
    return buildPath(c);
}

void HoleItem::stretch(const QList<int> &indices, const MyVector &delta) {
    if (indices.contains(0)) {
        move(delta);
        m_isShapeValid = false;
    }
}

QPainterPath HoleItem::getStretchedPath(const QList<int> &indices, const MyVector &delta) const {
    if (indices.contains(0)) return getMovedPath(delta);
    return QPainterPath();
}

CadItem *HoleItem::clone() const {
    auto *item = new HoleItem(m_data);
    item->setLayerName(layerName());
    item->setPen(pen());
    return item;
}

QMap<QString, QVariant> HoleItem::getProperties() const {
    QMap<QString, QVariant> props;
    props["Type"] = "Hole";
    props["Layer"] = layerName();
    props["CenterX"] = m_data.position.x;
    props["CenterY"] = m_data.position.y;
    props["Diameter"] = m_data.diameter;
    props["ThroughHole"] = m_data.throughHole;
    props["Depth"] = m_data.depth;
    props["IsThreaded"] = m_data.isThreaded;
    props["ThreadSpec"] = m_data.threadSpec;
    props["ThreadClass"] = m_data.threadClass;
    props["ThreadDepth"] = m_data.threadDepth;
    props["ToleranceClass"] = m_data.toleranceClass;
    props["HasCounterbore"] = m_data.hasCounterbore;
    props["CbDiameter"] = m_data.cbDiameter;
    props["CbDepth"] = m_data.cbDepth;
    props["HasCountersink"] = m_data.hasCountersink;
    props["CsDiameter"] = m_data.csDiameter;
    props["CsAngle"] = m_data.csAngle;
    props["SymbolStyle"] = (m_data.symbolStyle == HoleSymbolStyle::Graphic) ? "Graphic" : "Text";
    props["RenderMode"] = (m_data.renderMode == HoleRenderMode::Right) ? "Right"
                          : (m_data.renderMode == HoleRenderMode::Left) ? "Left" : "Full";
    props["BaseLayer"] = m_data.baseLayer;
    props["ThreadLayer"] = m_data.threadLayer;
    props["CbLayer"] = m_data.cbLayer;
    props["CsLayer"] = m_data.csLayer;
    return props;
}

bool HoleItem::setProperty(const QString &key, const QVariant &value) {
    if (key == "Layer") {
        setLayerName(value.toString());
        return true;
    }

    prepareGeometryChange();
    if (key == "CenterX") m_data.position.x = value.toDouble();
    else if (key == "CenterY") m_data.position.y = value.toDouble();
    else if (key == "Diameter") m_data.diameter = value.toDouble();
    else if (key == "ThroughHole") m_data.throughHole = value.toBool();
    else if (key == "Depth") m_data.depth = value.toDouble();
    else if (key == "IsThreaded") m_data.isThreaded = value.toBool();
    else if (key == "ThreadSpec") m_data.threadSpec = value.toString();
    else if (key == "ThreadClass") m_data.threadClass = value.toString();
    else if (key == "ThreadDepth") m_data.threadDepth = value.toDouble();
    else if (key == "ToleranceClass") m_data.toleranceClass = value.toString();
    else if (key == "HasCounterbore") m_data.hasCounterbore = value.toBool();
    else if (key == "CbDiameter") m_data.cbDiameter = value.toDouble();
    else if (key == "CbDepth") m_data.cbDepth = value.toDouble();
    else if (key == "HasCountersink") m_data.hasCountersink = value.toBool();
    else if (key == "CsDiameter") m_data.csDiameter = value.toDouble();
    else if (key == "CsAngle") m_data.csAngle = value.toDouble();
    else if (key == "SymbolStyle") m_data.symbolStyle = (value.toString() == "Graphic") ? HoleSymbolStyle::Graphic : HoleSymbolStyle::Text;
    else if (key == "RenderMode") {
        QString s = value.toString();
        m_data.renderMode = (s == "Right") ? HoleRenderMode::Right
                            : (s == "Left") ? HoleRenderMode::Left : HoleRenderMode::Full;
    }
    else if (key == "BaseLayer") m_data.baseLayer = value.toString();
    else if (key == "ThreadLayer") m_data.threadLayer = value.toString();
    else if (key == "CbLayer") m_data.cbLayer = value.toString();
    else if (key == "CsLayer") m_data.csLayer = value.toString();
    else return false;

    m_isShapeValid = false;
    updateGrips();
    update();
    // 연결된 구멍 콜아웃 텍스트를 새 스펙으로 다시 조립 (undo/redo 때도 이 경로를 탄다)
    HoleCalloutLink::refreshCalloutsForHole(this);
    return true;
}
