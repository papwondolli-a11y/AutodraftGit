#ifndef PROJECTIONVIEW_H
#define PROJECTIONVIEW_H

#include "holefeature.h"
#include "revolveprofile.h"

#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QString>

class MyGraphicsScene;
class IAppContext;

// ─────────────────────────────────────────────────────────────────────────────
// ProjectionView — 3D 데이터(회전 단면)가 있는 부품의 투상도 (메뉴 "3D 기계용 > 투상도 만들기")
//
//   좌측면 / 우측면: 축 방향으로 본 동심원. 단면 꼭짓점(각진 곳·반지름 극값)마다 원 하나,
//     보는 쪽에서 그 높이(r)로 나아가는 시선이 재료 속을 지나면 숨은선.
//   평면(외형): 축과 수직 방향에서 본 외형. 각 x 에서 가장 바깥 반지름(R(x)) 위에 있는 선은 실선, 그 안쪽은 숨은선.
//   생성된 선·원에는 MECH_VIEW_ID(뷰 UUID) 를 붙이고, 부품 레지스트리 props "Views" 에 기록 → [투상도 다시 그리기]
// ─────────────────────────────────────────────────────────────────────────────

struct ProjCircle {
    QPointF c;
    double r = 0.0;
};

struct ProjDrawing {
    QList<QLineF> visible;
    QList<QLineF> hidden;
    QList<QLineF> center;
    QList<ProjCircle> visibleCircles;
    QList<ProjCircle> hiddenCircles;

    bool isEmpty() const;
    QPainterPath path() const;   // 미리보기용
};

class ProjectionView {
public:
    // Front = 정면(원본 단면) 위에 덧그리는 구멍 단면 — 측면도에 구멍이 생기면 자동으로 기록
    enum Kind { Left = 0, Right = 1, Top = 2, Front = 3 };
    static QString kindName(int kind);

    // 축 방향 단위벡터 u, 축 시작점 a0. 놓을 점 p → 투상 정렬된 뷰 기준
    //   측면: 중심 = 축선 위 (p 를 축선에 투영),  외형: 축 시작점을 축과 수직으로 p 까지 옮긴 점
    //   param = 측면이면 축 방향 거리 t, 외형이면 수직 거리 s (레지스트리에 저장해 부품을 옮겨도 정렬 유지)
    static double paramFor(int kind, const QPointF &a0, double angleDeg, const QPointF &p);
    // param → 도면의 뷰 기준점 (측면 = 원 중심, 평면 = 뷰 축선 위 점)
    static QPointF anchorFor(int kind, const QPointF &a0, double angleDeg, double param);
    //   showHidden = false 면 숨은선(점선)을 그리지 않는다 (기본)
    static ProjDrawing build(const QList<RevolveLoop> &loops, int kind, const QPointF &a0, double angleDeg,
                             double param, bool showHidden = false,
                             const QList<HoleFeature> &holes = QList<HoleFeature>());
    static ProjDrawing buildAll(const QList<RevolveLoop> &loops, int kind, const QPointF &a0, double angleDeg,
                                double param, const QList<HoleFeature> &holes);   // 숨은선 포함

    // 부품의 구멍: 좌·우측면도 안에 그린 HoleItem (그리기 탭 구멍R·구멍P) — 저장하지 않고 매번 찾음
    static QList<HoleFeature> holesOf(MyGraphicsScene *scene, const QString &partUuid);
    struct SideViewInfo {
        QString viewId;
        int kind = Left;
        QPointF center;
        QPointF u;
        QPointF n;
        double rMax = 0.0;
    };
    static QList<SideViewInfo> sideViews(MyGraphicsScene *scene, const QString &partUuid);
    // 정면(원본 단면)에 구멍 단면 — 그리기 탭 "구멍 단면"과 같은 생성기·같은 태그 (절단면에 걸린 구멍만)
    static bool drawFrontHoles(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                               const QString &viewId, const QList<RevolveLoop> &loops, const QPointF &a0,
                               double angleDeg);
    // 반단면만 그린 회전체: 중심선 반대쪽에 대칭 단면(외형선 + 원래 해칭과 같은 해칭)을 정면 뷰 선으로 생성
    //   props "Mirror" = true 일 때. 원래 반쪽을 고치면 다시 그릴 때 따라감
    static void setMirror(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid, bool mirror);
    static void drawFrontMirror(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                                const QString &viewId, const QPointF &a0, double angleDeg);
    // 호스트 회전체에 붙은 측면도 돌출을 이 뷰에 그림 (drawView 안에서 호출)
    static void drawExtrudes(IAppContext *ctx, MyGraphicsScene *scene, const QString &hostUuid, const QString &viewId,
                             int kind, double param, bool showHidden, const QList<RevolveLoop> &loops,
                             const QPointF &a0, double angleDeg);
    // 구멍R·구멍P 로 구멍을 만든 뒤 호출: 투상도가 있는 회전체마다 다시 그림 (같은 undo 매크로 안)
    static int syncHoleLinks(IAppContext *ctx, MyGraphicsScene *scene);
    // 부품에 Front(구멍선) 뷰가 없으면 기록 추가
    static void ensureFrontView(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid);


    // 부품 하나의 뷰만 다시 그리기 (구멍 추가·수정 뒤)
    static int regeneratePart(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid);

    // 부품 UUID 의 뷰 하나를 도면에 그린다 (undo 매크로 안에서 부를 것). viewId 태그.
    static bool drawView(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid, const QString &viewId,
                         int kind, double param, bool showHidden, QString *error);

    // 새 뷰 기록 추가 (UpdateRegistryCommand)
    //   기록 = 뷰 기준점(도면 절대좌표) + 그때의 부품 기준점 → 다시 그릴 때 부품이 옮겨진 만큼만 따라감
    //   (회전축을 다시 지정해 축 시작점이 바뀌어도 뷰는 제자리)
    static void addViewRecord(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                              const QString &viewId, int kind, double param, bool showHidden);

    // 점을 클릭한 부품 (태그된 도형, 또는 영역 방식 회전체의 공간 안)
    static QString partAt(MyGraphicsScene *scene, const QPointF &pos);

    // 모든 부품의 뷰를 지우고 다시 그린다. 반환 = 다시 그린 뷰 수
    static int regenerateAll(IAppContext *ctx, MyGraphicsScene *scene);
};

#endif // PROJECTIONVIEW_H
