#ifndef BOUNDARYFINDER_H
#define BOUNDARYFINDER_H

#include "polylineitem.h"
#include <QObject>
#include <QPointF>
#include <QList>
#include <QPainterPath>

class CadItem;
class MyGraphicsScene;

class BoundaryFinder {
public:
    /**
     * @brief 특정 좌표를 포함하는 가장 작은 닫힌 객체를 찾습니다.
     * @param items 검색 대상 객체 리스트 (Crossing으로 선택된 리스트 등)
     * @param point 마우스 입력 좌표
     * @return 찾은 객체의 포인터 (없으면 nullptr)
     */
    static void extracted(bool &isClosed, PolylineItem *&pi);
    static CadItem *findInnermostBoundary(const QList<CadItem *> &items,
                                          const QPointF &point);

private:
    static double calculatePathArea(const QPainterPath &path);
};

#endif // BOUNDARYFINDER_H
