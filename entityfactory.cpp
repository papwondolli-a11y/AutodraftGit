#include "entityfactory.h"
#include "arcitem.h"
#include "caditem.h"
#include "circleitem.h"
#include "gdtitem.h"
#include "holeitem.h"
#include "surfacefinishitem.h"
#include "lineitem.h"
#include "polylineitem.h"
#include "textitem.h"
#include "hatchitem.h"
#include "splineitem.h"
#include "ellipseitem.h"

// [수정] 단일 dimensionitem.h 대신 다형성 치수선 헤더들을 포함합니다.
#include "lineardimensionitem.h"
#include "radialdimensionitem.h"
#include "angulardimensionitem.h"
#include "halfdimensionitem.h"
#include "datumitem.h"
#include "blockitem.h"
#include "leaderitem.h"

#include <QDebug>
#include <QFont>
#include <QGraphicsTextItem>

EntityFactory::EntityFactory(QObject *parent) : QObject(parent) {}

QGraphicsItem *EntityFactory::createLine(const QLineF &line, const QPen &pen) {
    LineItem *item = new LineItem(MyVector(line.p1()), MyVector(line.p2()));
    item->setPen(pen);
    initializeItem(item);
    return item;
}

QGraphicsItem *EntityFactory::createCircle(const QPointF &center, double radius,
                                           const QPen &pen) {
    CircleItem *item = new CircleItem(MyVector(center), radius);
    item->setPen(pen);
    initializeItem(item);
    return item;
}

QGraphicsItem *EntityFactory::createArc(const QPointF &center, double radius,
                                        double startAngle, double spanAngle,
                                        const QPen &pen) {
    MyArc arcData;
    arcData.center = MyVector(center);
    arcData.radius = radius;
    arcData.startAngle = startAngle;
    arcData.spanAngle = spanAngle;

    ArcItem *item = new ArcItem(arcData);
    item->setPen(pen);
    initializeItem(item);
    return item;
}

QGraphicsItem *EntityFactory::createArc(const QPointF &center,
                                        const QPointF &start,
                                        const QPointF &end, const QPen &pen) {
    ArcItem *item = new ArcItem(MyVector(center), MyVector(start), MyVector(end));
    item->setPen(pen);
    initializeItem(item);
    return item;
}

QGraphicsItem *EntityFactory::createHatch(const QPainterPath &path,
                                         const QString& patternName,
                                         double scale, double angle, const QPen &pen) {
    HatchItem *item = new HatchItem(path, patternName, scale, angle);
    item->setPen(pen);
    initializeItem(item);
    return item;
}

QGraphicsItem *EntityFactory::createHatch(const QPainterPath &path,
                                         Qt::BrushStyle style, const QPen &pen) {
    HatchItem *item = new HatchItem(path, style);
    item->setPen(pen);
    initializeItem(item);
    return item;
}

QGraphicsItem *EntityFactory::createText(const QString &text,
                                         const QPointF &pos, const QFont &font,
                                         const QColor &color, double rotation,
                                         int alignH) {
    MyText data(MyVector(pos), text, font.family(), font.pointSizeF(), rotation, alignH);
    data.bold = font.bold();
    data.italic = font.italic();
    data.underline = font.underline();
    TextItem *item = new TextItem(data);

    QPen pen;
    pen.setColor(color);
    item->setPen(pen);

    initializeItem(item);
    return item;
}

QGraphicsItem *
EntityFactory::createPolyline(const std::vector<MyVector> &vertices,
                              bool closed, const QPen &pen,
                              const std::vector<double> &bulges) {
    MyPolyline poly;
    poly.vertices = vertices;
    poly.bulges = bulges;
    poly.isClosed = closed;
    PolylineItem *item = new PolylineItem(poly);
    item->setPen(pen);
    initializeItem(item);
    return item;
}

QGraphicsItem *
EntityFactory::createSpline(const std::vector<MyVector> &controlPoints,
                            int degree, bool closed, const QPen &pen) {
    SplineItem *item = new SplineItem(controlPoints, degree, closed);
    item->setPen(pen);
    initializeItem(item);
    return item;
}

QGraphicsItem *EntityFactory::createSpline(const MySpline &data, const QPen &pen) {
    SplineItem *item = new SplineItem(data);
    item->setPen(pen);
    initializeItem(item);
    return item;
}

QGraphicsItem *EntityFactory::createEllipse(const MyEllipse &data, const QPen &pen) {
    EllipseItem *item = new EllipseItem(data);
    item->setPen(pen);
    initializeItem(item);
    return item;
}

// [핵심 수정] 전달받은 MyDimension의 타입에 맞는 전용 자식 클래스를 생성합니다.
QGraphicsItem *EntityFactory::createDimension(const MyDimension &data,
                                              const QPen &pen) {
    CadItem *item = nullptr;

    if (data.dimensionType == DimensionType::Linear) {
        item = new LinearDimensionItem(data);
    } else if (data.dimensionType == DimensionType::Angular) {
        item = new AngularDimensionItem(data);
    } else if (data.dimensionType == DimensionType::Radial || data.dimensionType == DimensionType::Diametric) {
        item = new RadialDimensionItem(data);
    } else if (data.dimensionType == DimensionType::HalfDimension) {
        item = new HalfDimensionItem(data);
    } else {
        // 기본값 폴백 (Fallback)
        item = new LinearDimensionItem(data);
    }

    if (item) {
        item->setPen(pen);
        initializeItem(item);
    }

    return item;
}

void EntityFactory::initializeItem(QGraphicsItem *item) {
    if (!item)
        return;

    // [#12 수정] 매 생성마다 qDebug() 출력 제거 (대량 로드 시 성능/로그 노이즈 해소)
    // 중앙 집중 관리: 모든 객체에 공통 플래그 설정
    item->setFlag(QGraphicsItem::ItemIsSelectable, true);
}

// [GDT 추가] 기하공차 프레임 생성
QGraphicsItem *EntityFactory::createGdt(const GdtData &data, const QPen &pen) {
    GdtItem *item = new GdtItem(data);
    item->setPen(pen);
    initializeItem(item);
    return item;
}
QGraphicsItem *EntityFactory::createSurfaceFinish(const SurfaceFinishData &data, const QPen &pen) {
    SurfaceFinishItem *item = new SurfaceFinishItem(data);
    item->setPen(pen);
    initializeItem(item);
    return item;
}
QGraphicsItem *EntityFactory::createHole(const MyHole &data, const QPen &pen) {
    HoleItem *item = new HoleItem(data);
    item->setPen(pen);
    initializeItem(item);
    return item;
}
QGraphicsItem *EntityFactory::createDatum(const DatumData &data, const QPen &pen) {
    DatumItem *item = new DatumItem(data);
    item->setPen(pen);
    initializeItem(item);
    return item;
}

QGraphicsItem *EntityFactory::createLeader(const MyLeader &data, const QPen &pen) {
    LeaderItem *item = new LeaderItem(data);
    item->setPen(pen);
    initializeItem(item);
    return item;
}

QGraphicsItem *
EntityFactory::createBlock(const QList<CadItem *> &items,
                           const MyVector &insertionPoint, const QString &name,
                           double scaleX, double scaleY, double rotation) {
    BlockItem *item = new BlockItem(items, insertionPoint, name, scaleX, scaleY, rotation);
    initializeItem(item);
    return item;
}