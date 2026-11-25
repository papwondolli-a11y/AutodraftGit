#ifndef EXTRUDEFEATURE_H
#define EXTRUDEFEATURE_H

#include <QList>
#include <QMap>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSet>
#include <QString>

class MyGraphicsScene;
class IAppContext;
class QGraphicsItem;
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
//   단독 돌출 (2026-11-22): 호스트 없이 도면의 닫힌 공간(사각형·원·폴리라인·복합)을 도면에 수직(Z, 보는 쪽 +)으로 돌출.
//   props Standalone = true, Offset = 시작 Z, Depth, Symmetric(가운데 대칭). rings = 도면 좌표 (X, Y), x0~x1 = Z 범위.
//   공간 안의 닫힌 모양(섬)은 관통 구멍, 공간 안의 구멍R·구멍P(HoleItem)는 Z 방향 구멍(앞면 = 보는 쪽 면에서 파고듦).
//   판 돌기 (2026-11-23): 판의 측면도·보조 투상도 안에 그린 공간을 그 뷰의 바깥 법선(보는 쪽)으로 돌출.
//   props PlateBoss = true, Host = 판, ViewId = 그린 뷰, Depth, Merge. 로컬 = 판 기준점 a0, 각도 = 법선 각,
//   x = 법선 방향(판 표면 → 바깥), (y, z) = (뷰 가로 s, 두께 Z). 시작 = 그 단면 아래 판 표면의 가장 깊은 곳, 끝 = 가장 앞 + Depth.
//   파내기 (2026-11-23, props Cut / Through / PlateFront): 같은 공간 고르기로 재료를 뺀다.
//     판 측면·보조 뷰 = 그 면에서 안쪽으로 (x = 표면 가장 깊은 곳 − Depth ~ 가장 앞, 관통 = 반대쪽 면까지),
//     판 정면 = 앞면 Z = z1 에서 −Z 로 Depth (관통 = z0 까지), 회전체 측면도 = 돌출과 같은 범위의 홈.
struct ExtrudeInfo {
    QString uuid;
    QString name;
    bool standalone = false; // 단독 돌출 (호스트 없음)
    bool plateBoss = false;  // 판의 측면도·보조 투상도 안에 그린 공간 → 그 면에 붙는 돌기 (host = 판, viewId = 그린 뷰)
    bool plateFront = false; // 판 정면(판 공간 안)에 그린 공간 → 앞면에서 −Z 로 파내기 (rings = 도면 X, Y)
    bool cut = false;        // 파내기 (메뉴 "파내기", cut3d) — 3D 는 커터로 빼고, 투상도는 파낸 모서리
    bool through = false;    // 파내기: 관통 (판 반대쪽 면까지)
    bool symmetric = false;  // 단독: 시작 Z 를 가운데로 대칭
    QString host;            // 호스트 회전체 UUID (판 돌기면 판 UUID)
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

    // 기록이 지금 도면에서 나타내는 공간 (도면 좌표, 그린 자리 그대로). 못 찾으면 빈 경로
    static QPainterPath currentArea(MyGraphicsScene *scene, const QString &uuid);

    // 도형 이동(MOVE)에 돌출·파내기 기록이 따라가기
    //   이동 전: 스케치(바깥 윤곽 선)가 모두 옮겨질 기록들 → 이동 후: 기록(사각·고른 점·모양)을 그만큼 옮기고 투상도 갱신
    //   값 = 이동 전 측면도 중심 (회전체에 붙은 돌출은 측면도가 함께 옮겨진 만큼을 뺀다)
    static QMap<QString, QPointF> sketchesMovedBy(MyGraphicsScene *scene, const QSet<QGraphicsItem *> &moved);
    static void followMove(IAppContext *ctx, MyGraphicsScene *scene, const QMap<QString, QPointF> &records,
                           const QPointF &delta);
    // 뷰를 숨기기 직전: 그 뷰에 그린 돌출·파내기의 기록을 지금 모양으로 맞춤 (숨긴 동안은 기록 모양을 쓰므로)
    static void freezeRegions(MyGraphicsScene *scene, QUndoStack *undo, const QString &host, const QString &viewId);
    // 숨긴 뷰에 그린 스케치 선들 (숨김 대상) — 판 자체의 윤곽·정면 파내기에도 쓰는 선은 뺌
    static QSet<QGraphicsItem *> concealedSketchItems(MyGraphicsScene *scene);

    // 고른 공간(도면 좌표) 가운데에 가장 가까운 측면도 → 호스트·뷰. 없으면 false
    static bool findHostView(MyGraphicsScene *scene, const QPainterPath &region, QString &host, QString &viewId,
                             int &viewKind);

    static QString registerExtrude(MyGraphicsScene *scene, QUndoStack *undo, const QString &host,
                                   const QString &viewId, int viewKind, const QRectF &box,
                                   const QList<QPointF> &picks, const QPainterPath &region, const QString &name,
                                   double offset, double depth, bool merge, QString *error, bool cut = false);
    // 판 돌기 등록 (plate = 판 UUID, viewId = 공간을 그린 판의 측면도·보조 투상도)
    static QString registerPlateBoss(MyGraphicsScene *scene, QUndoStack *undo, const QString &plate,
                                     const QString &viewId, int viewKind, const QRectF &box, const QList<QPointF> &picks,
                                     const QPainterPath &region, const QString &name, double depth, bool merge,
                                     QString *error, bool cut = false, bool through = false);
    // 판 정면 파내기 등록 (판 공간 안에 그린 닫힌 모양 → 앞면에서 −Z 로 depth, through = 관통)
    static QString registerPlateFrontCut(MyGraphicsScene *scene, QUndoStack *undo, const QString &plate,
                                         const QRectF &box, const QList<QPointF> &picks, const QPainterPath &region,
                                         const QString &name, double depth, bool through, QString *error);
    // 단독 돌출 등록 (zStart = 시작 Z, symmetric = 가운데 대칭)
    // 새 단독 돌출과 같은 공간을 찾는 기존 단독 돌출 기록을 지움 (지운 판 자리에 다시 그렸을 때 겹침 방지)
    static void removeSameStandalone(MyGraphicsScene *scene, QUndoStack *undo, const QPainterPath &region);
    static QString registerStandalone(MyGraphicsScene *scene, QUndoStack *undo, const QRectF &box,
                                      const QList<QPointF> &picks, const QPainterPath &region, const QString &name,
                                      double zStart, double depth, bool symmetric, QString *error);
};

#endif // EXTRUDEFEATURE_H
