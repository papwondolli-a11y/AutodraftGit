#ifndef PLATEVIEW_H
#define PLATEVIEW_H

#include "extrudefeature.h"
#include "holefeature.h"
#include "projectionview.h"

#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>

class MyGraphicsScene;

// ─────────────────────────────────────────────────────────────────────────────
// PlateView — 단독 돌출(판·블록, MechExtrude Standalone)의 투상도와 구멍
//
//   기준(투상 정렬): a0 = 판 외곽 경계 사각의 가운데, 각도 0 (u = +X, n = +Y). ref = 경계 사각 왼쪽 위(이동 추적).
//   뷰 = 바깥 법선 n 쪽에서 판을 봄 (좌 −X, 우 +X, 평면 +Y, 보조 투상도 = 고른 면의 법선, 기울여 정렬).
//     판 점 P·두께 Z → c + u·((P − a0)·u) − n·Z  (u = n 반시계 90°). 정면과 투상선이 면에 수직으로 맞음.
//   외형 = s 범위 × 두께. 꼭짓점(꺾인 곳·곡선 윤곽)마다 선 — 보는 쪽 가장 앞이면 실선, 가려지면 숨은선.
//
//   구멍 (PlateHole):
//     · 정면(판 공간 안)에 놓은 구멍R·구멍P → 앞면(Z = z1)에서 −Z
//     · 우측면도 / 좌측면도 / 평면도 안에 놓은 구멍R·구멍P → 그 면(오른쪽 / 왼쪽 / 윗면)에서 판 안쪽으로
//       (입구 = 그 방향으로 처음 만나는 판 경계, 두께 = 그 재료 구간)
//     · 정면에 구멍 단면 도구로 그린 구멍(방향이 도면 안) → 옆면 구멍, Z = 두께 가운데
//   다른 뷰: 보는 방향과 나란한 구멍 = 원(그 면에서 들어가면 실선), 수직이면 숨은선 두 줄. 정면에는 옆면 구멍을 숨은선으로.
// ─────────────────────────────────────────────────────────────────────────────
class PlateView {
public:
    struct FaceView {
        QString viewId;
        int kind = 0;            // ProjectionView::Left / Right / Top / 보조(faceKind)
        QPointF center;          // 뷰 기준점 (도면)
        QPointF n;               // 판에서 뷰 쪽 바깥 법선 (보는 방향 = −n)
        QPointF u;               // 뷰 가로 방향 (n 을 반시계 90°)
        QPolygonF poly;          // 뷰가 차지하는 (기울 수 있는) 사각 — 이 안의 구멍R·구멍P = 그 면의 구멍
    };

    static bool isPlate(MyGraphicsScene *scene, const QString &uuid);
    // 판 단면(도면 좌표 고리 + Z 범위)과 투상 기준
    static bool frame(MyGraphicsScene *scene, const QString &uuid, ExtrudeInfo &ei, QPointF &a0, QPointF &ref);
    // 판의 측면·평면 투상도 기록 (정면 기록 제외)
    static QList<FaceView> views(MyGraphicsScene *scene, const QString &uuid);
    // 판의 모든 구멍
    static QList<PlateHole> holes(MyGraphicsScene *scene, const QString &uuid);
    // 투상도 선 (kind = Left/Right/Top/Front). viewId = 그리는 뷰 (그 뷰에 놓은 구멍은 다시 그리지 않음)
    //   bosses = 이 판에 붙은 돌기 (돌기 모서리 + 판 선이 돌기에 가려지면 숨은선)
    static ProjDrawing build(const ExtrudeInfo &ei, int kind, const QPointF &a0, double param, bool showHidden,
                             const QList<PlateHole> &holes, const QString &viewId = QString(),
                             const QList<ExtrudeInfo> &bosses = QList<ExtrudeInfo>());
    // 점이 든 판의 측면도·보조 투상도 (돌기를 그릴 뷰 찾기)
    static bool viewAt(MyGraphicsScene *scene, const QPointF &pos, QString &plateUuid, FaceView &view);
    // 판 돌기 단면: 뷰 안 공간(도면) → info.rings (s, Z)·a0·angleDeg·x0~x1 (판 표면 가장 깊은 곳 ~ 가장 앞 + depth)
    static bool bossSection(MyGraphicsScene *scene, const QString &plateUuid, const QString &viewId,
                            const QPainterPath &region, double depth, bool merge, ExtrudeInfo &info);
    // 판에 붙은 돌기·파내기들 (해석된 것만, cut = 파내기, plateFront = 정면 파내기)
    static QList<ExtrudeInfo> bosses(MyGraphicsScene *scene, const QString &plateUuid);
    // 점이 판 공간 안인가 (투상도 만들기에서 부품 고르기)
    //   outerOnly = 공간 안 닫힌 모양(섬)도 판으로 봄 (정면 파내기 스케치 고르기)
    //   여러 판이 겹치면 가장 앞(윗면 Z 가 가장 큰) 판
    static QString plateAt(MyGraphicsScene *scene, const QPointF &pos, bool outerOnly = false);
    // 보조 투상도용 면 고르기: pos 에 가장 가까운 판 경계 변 → 재료 바깥쪽 법선 각도(도). edge = 그 변 (미리보기용)
    //   maxDist > 0 이면 그보다 멀면 실패
    static bool faceAt(MyGraphicsScene *scene, const QString &uuid, const QPointF &pos, double &normalDeg,
                       QLineF *edge = nullptr, double maxDist = 0.0);
};

#endif // PLATEVIEW_H
