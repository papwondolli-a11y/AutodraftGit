#ifndef ENTITYFACTORY_H
#define ENTITYFACTORY_H

#include <QLineF>
#include <QObject>
#include <QPen>
#include <QPointF>
#include <QString>
#include <vector>

#include "mydimension.h"
#include "mygdt.h"
#include "myhole.h"
#include "mysurfacefinish.h"
#include "myvector.h"
#include "datumitem.h"
#include "myleader.h"

class QGraphicsItem;
class MySpline;
class MyEllipse;
class CadItem;

class EntityFactory : public QObject {
    Q_OBJECT
public:
    explicit EntityFactory(QObject *parent = nullptr);

    // 선 생성
    QGraphicsItem *createLine(const QLineF &line, const QPen &pen);

    // 원 생성
    QGraphicsItem *createCircle(const QPointF &center, double radius,
                                const QPen &pen);

    // 아크 생성 (중심점, 반지름, 시작각도, 스팬각도)
    QGraphicsItem *createArc(const QPointF &center, double radius,
                             double startAngle, double spanAngle,
                             const QPen &pen);

    // 아크 생성 (중심점, 시작점, 끝점)
    QGraphicsItem *createArc(const QPointF &center, const QPointF &start,
                             const QPointF &end, const QPen &pen);
    QGraphicsItem *createHatch(const QPainterPath &path, const QString& patternName,
                               double scale, double angle, const QPen &pen);
    QGraphicsItem *createHatch(const QPainterPath &path, Qt::BrushStyle style,
                               const QPen &pen);
    // 텍스트 생성
    QGraphicsItem *createText(const QString &text, const QPointF &pos,
                              const QFont &font, const QColor &color,
                              double rotation = 0.0, int alignH = 0);

    // 폴리라인 생성
    QGraphicsItem *createPolyline(const std::vector<MyVector> &vertices,
                                  bool closed, const QPen &pen,
                                  const std::vector<double> &bulges = {});

    // 스플라인 생성
    QGraphicsItem *createSpline(const std::vector<MyVector> &controlPoints,
                                int degree, bool closed, const QPen &pen);
    QGraphicsItem *createSpline(const MySpline &data, const QPen &pen);

    // 타원/타원호 생성
    QGraphicsItem *createEllipse(const MyEllipse &data, const QPen &pen);

    // 치수선 생성
    QGraphicsItem *createDimension(const MyDimension &data, const QPen &pen);

    // [GDT 추가] 기하공차 프레임 생성
    QGraphicsItem *createGdt(const GdtData &data, const QPen &pen);

    // 표면 거칠기 기호 생성
    QGraphicsItem *createSurfaceFinish(const SurfaceFinishData &data, const QPen &pen);

    // 구멍(드릴/탭/카운터보어/카운터싱크) 생성
    QGraphicsItem *createHole(const MyHole &data, const QPen &pen);

    //데이텀 생성 팩토리
    QGraphicsItem *createDatum(const DatumData &data, const QPen &pen);

    // 지시선 생성
    QGraphicsItem *createLeader(const MyLeader &data, const QPen &pen);

    // 블록 생성
    QGraphicsItem *createBlock(const QList<CadItem *> &items,
                               const MyVector &insertionPoint, const QString &name,
                               double scaleX, double scaleY, double rotation);

private:
    void initializeItem(QGraphicsItem *item);
};

#endif // ENTITYFACTORY_H