#ifndef EXTRUDEFEATURE_H
#define EXTRUDEFEATURE_H

#include <QList>
#include <QMap>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>

class MyGraphicsScene;
class QUndoStack;

// ─────────────────────────────────────────────────────────────────────────────
// ExtrudeFeature — 측면도에 그린 닫힌 공간을 축 방향으로 돌출 (메뉴 "3D 기계용 > 돌출", 명령 ext3d)
//
//   단면 회전체(호스트)의 좌·우측면도 안(또는 걸쳐서)에 그린 공간을 해치 명령처럼 고른다(투상 원도 경계).
//   단면 = 호스트 로컬 (y, z) 평면 다각형 (좌측면도 오른쪽 = +z, 우측면도 오른쪽 = −z, 위 = +y).
//   돌출 범위(축 방향 x): 그 측면도 쪽 호스트 끝면에서 offset 만큼 안쪽부터 depth 만큼.
//   merge = 호스트와 한 덩어리 (Blender Union), 아니면 새 부품.
//   레지스트리 type "MechExtrude": Name, Host, ViewId, ViewKind, Box, Picks, Region, Offset, Depth, Merge.
//   공간은 3D·투상도를 만들 때마다 다시 찾는다(선을 고치면 따라감, 못 찾으면 등록 때 모양).
// ─────────────────────────────────────────────────────────────────────────────
struct ExtrudeInfo {
    QString uuid;
    QString name;
    QString host;            // 호스트 회전체 UUID
    QString viewId;          // 그린 측면도
    int viewKind = 0;        // ProjectionView::Left / Right
    bool merge = false;
    double offset = 0.0;
    double depth = 10.0;
    QList<QPolygonF> rings;  // 로컬 (y, z) — 바깥 반시계 / 구멍 시계
    double x0 = 0.0;         // 호스트 로컬 축 방향 범위
    double x1 = 0.0;
    QPointF a0;              // 호스트 축 시작점(도면)
    double angleDeg = 0.0;
};

class ExtrudeFeature {
public:
    // 현재 도면 기준으로 단면·범위 계산. 실패하면 false
    static bool resolve(MyGraphicsScene *scene, const QString &uuid, ExtrudeInfo &info);
    // 호스트에 붙은 돌출 UUID 들
    static QList<QString> attachedTo(MyGraphicsScene *scene, const QString &hostUuid);

    // 고른 공간(도면 좌표) 가운데에 가장 가까운 측면도 → 호스트·뷰. 없으면 false
    static bool findHostView(MyGraphicsScene *scene, const QPainterPath &region, QString &host, QString &viewId,
                             int &viewKind);

    static QString registerExtrude(MyGraphicsScene *scene, QUndoStack *undo, const QString &host,
                                   const QString &viewId, int viewKind, const QRectF &box,
                                   const QList<QPointF> &picks, const QPainterPath &region, const QString &name,
                                   double offset, double depth, bool merge, QString *error);
};

#endif // EXTRUDEFEATURE_H
