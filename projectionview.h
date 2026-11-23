#ifndef PROJECTIONVIEW_H
#define PROJECTIONVIEW_H

#include "holefeature.h"
#include "revolveprofile.h"

#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QSet>
#include <QString>

class MyGraphicsScene;
class QGraphicsItem;
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

    // 보조 투상도 (판의 면 하나를 골라 그 면을 정면으로 봄, 기울여 정렬) — kind 한 정수에 바깥 법선 각도를 담는다
    //   kind = FaceKindBase + round(법선각° × 1000)  (0 ≤ 각 < 360, 도면 좌표 반시계)
    //   param = 판 기준점 a0 에서 법선 방향으로 뷰 기준점까지 거리 → 기록·다시 그리기·부품 이동 추적은 다른 뷰와 같은 경로
    static const int FaceKindBase = 1000000;
    static int faceKind(double normalDeg);
    static bool isFaceKind(int kind);
    static double faceAngle(int kind);   // 도

    // 회전체 보조 투상도 (회전체에 붙은 돌출(받침대 등)의 면을 골라 그 면을 정면으로 봄)
    //   kind = RevFaceKindBase + round(φ° × 1000). φ = 그 면의 바깥 법선 방향 = 부품 로컬 (y, z) 평면의 각도
    //   (y = 정면도 위쪽, z = 정면도 앞). 회전체는 축 둘레 어느 방향에서 봐도 외형이 같으므로 평면도처럼
    //   정면도와 축이 나란하게 놓고(param = 축과 수직 거리), 뷰 위쪽 = e = (0, sin φ, −cos φ) 방향.
    //   돌출·구멍만 방향에 따라 달라진다. 이 뷰에 놓은 구멍R·구멍P = 그 면에서 법선 반대로 돌출을 뚫는 구멍
    static const int RevFaceKindBase = 2000000;
    static int revFaceKind(double phiDeg);
    static bool isRevFaceKind(int kind);
    static double revFaceAngle(int kind);   // φ (도)
    // 회전체 측면도에서 pos 에 가장 가까운 붙은 돌출(파내기 제외)의 단면 변 → 바깥 법선 φ. edge = 그 변 (도면, 미리보기용)
    static bool revFaceAt(MyGraphicsScene *scene, const QString &partUuid, const QPointF &pos, double &phiDeg,
                          QLineF *edge = nullptr, double maxDist = 0.0);
    // 회전체 보조 투상도에 놓은 구멍 → 붙은 돌출(받침대)을 뚫는 구멍 (부품 로컬 좌표)
    struct AuxHole {
        MyHole hole;
        QString groupId;
        QString viewId;          // 구멍을 놓은 보조 투상도 (그 뷰에는 다시 그리지 않음)
        QString extrudeUuid;     // 뚫리는 돌출
        double x = 0.0;          // 축 방향 위치
        QPointF entry;           // 입구 (y, z)
        QPointF dir;             // 파고드는 방향 (y, z) 단위 = −법선
        double thickness = 0.0;  // 입구에서 그 방향 돌출 재료 두께 (관통 깊이)
        const QGraphicsItem *item = nullptr;   // 구멍 객체 (HoleItem) — 투상도 숨기기에서 함께 숨김
    };
    static QList<AuxHole> auxHolesOf(MyGraphicsScene *scene, const QString &hostUuid);

    // 투상 기준 (회전체·축·베어링 = 축 시작점·각도, 판(단독 돌출) = 외곽 가운데·0°). ref = 이동 추적 기준점
    static bool partFrame(MyGraphicsScene *scene, const QString &uuid, QPointF &a0, double &angleDeg, QString &name,
                          QPointF *ref = nullptr);
    // 부품 종류에 맞는 투상도 선 (미리보기·결과 보고용)
    static ProjDrawing buildPart(MyGraphicsScene *scene, const QString &uuid, int kind, double param, bool showHidden);

    // 축 방향 단위벡터 u, 축 시작점 a0. 놓을 점 p → 투상 정렬된 뷰 기준
    //   측면: 중심 = 축선 위 (p 를 축선에 투영),  외형: 축 시작점을 축과 수직으로 p 까지 옮긴 점
    //   param = 측면이면 축 방향 거리 t, 외형이면 수직 거리 s (레지스트리에 저장해 부품을 옮겨도 정렬 유지)
    static double paramFor(int kind, const QPointF &a0, double angleDeg, const QPointF &p);
    // param → 도면의 뷰 기준점 (측면 = 원 중심, 평면 = 뷰 축선 위 점)
    static QPointF anchorFor(int kind, const QPointF &a0, double angleDeg, double param);
    //   showHidden = false 면 숨은선(점선)을 그리지 않는다 (기본)
    //   secHoles = 정면에 구멍 단면 도구로 그린 구멍 → 측면도(축 방향 = 원, 반경 방향 = 숨은선)·평면도(축 방향 = 숨은선, 반경 = 원)
    static ProjDrawing build(const QList<RevolveLoop> &loops, int kind, const QPointF &a0, double angleDeg,
                             double param, bool showHidden = false,
                             const QList<HoleFeature> &holes = QList<HoleFeature>(),
                             const QList<SectionHole> &secHoles = QList<SectionHole>());
    static ProjDrawing buildAll(const QList<RevolveLoop> &loops, int kind, const QPointF &a0, double angleDeg,
                                double param, const QList<HoleFeature> &holes,
                                const QList<SectionHole> &secHoles = QList<SectionHole>());   // 숨은선 포함

    // 부품의 구멍: 좌·우측면도 안에 그린 HoleItem (그리기 탭 구멍R·구멍P) — 저장하지 않고 매번 찾음
    static QList<HoleFeature> holesOf(MyGraphicsScene *scene, const QString &partUuid);
    struct SideViewInfo {
        QString viewId;
        int kind = Left;
        QPointF center;
        QPointF u;
        QPointF n;
        double rMax = 0.0;
        bool concealed = false;   // 투상도 숨기기로 숨긴 뷰 (구멍 연결은 유지)
    };
    static QList<SideViewInfo> sideViews(MyGraphicsScene *scene, const QString &partUuid);
    // 정면(원본 단면)에 구멍 단면 — 그리기 탭 "구멍 단면"과 같은 생성기·같은 태그 (절단면에 걸린 구멍만)
    static bool drawFrontHoles(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                               const QString &viewId, const QList<RevolveLoop> &loops, const QPointF &a0,
                               double angleDeg);
    // 반단면만 그린 회전체: 중심선 반대쪽에 대칭 단면(외형선 + 원래 해칭과 같은 해칭)을 정면 뷰 선으로 생성
    //   props "Mirror" = true 일 때. 원래 반쪽을 고치면 다시 그릴 때 따라감
    static void setMirror(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid, bool mirror);
    //   holeArea = 정면 구멍 단면 영역 → 미러 쪽 해칭에서 뺀다
    static void drawFrontMirror(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                                const QString &viewId, const QPointF &a0, double angleDeg,
                                const QPainterPath &holeArea = QPainterPath());

    // 정면 단면에 그릴 구멍 (절단면 z = 0 에 걸리고 그려진 쪽에 있는 구멍): 입구 중심·안쪽 방향·재료 두께
    struct FrontHole {
        HoleFeature hf;
        QPointF top;
        QPointF dir;
        double thickness = 0.0;
    };
    static QList<FrontHole> frontHoles(MyGraphicsScene *scene, const QString &partUuid,
                                       const QList<RevolveLoop> &loops, const QPointF &a0, double angleDeg);
    // 정면 단면 위에 구멍 단면 도구로 직접 그린 구멍 (뷰 태그 없는 구멍 단면 선 — 축 방향·반경 방향)
    static QList<SectionHole> sectionHolesOf(MyGraphicsScene *scene, const QString &partUuid,
                                             const QList<RevolveLoop> &loops, const QPointF &a0, double angleDeg);
    // 정면 구멍 단면이 차지하는 영역 (해칭에서 뺄 곳 — 탭은 드릴 지름까지). 측면 연동 구멍 + 직접 그린 구멍 단면
    static QPainterPath frontHoleArea(MyGraphicsScene *scene, const QString &partUuid,
                                      const QList<RevolveLoop> &loops, const QPointF &a0, double angleDeg);
    // 해치 방식 부품의 해칭을 "선 영역 − holeArea" 로 다시 만든다 (무늬·축척·각도·레이어·부품 태그 유지).
    //   holeArea 가 비면 꽉 찬 해칭으로 되돌림. 모양이 같으면 건드리지 않음.
    static void updatePartHatch(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                                const QPainterPath &holeArea);
    // 보조 투상도 구멍(AuxHole)을 다른 뷰에 숨은선·원으로 그림 (drawView 안에서 호출)
    static void drawAuxHoles(IAppContext *ctx, MyGraphicsScene *scene, const QString &hostUuid, const QString &viewId,
                             int kind, double param, bool showHidden, const QPointF &a0, double angleDeg);
    // 회전체 보조 투상도: 붙은 돌출에 가려지는 회전체 외형선을 숨은선으로 옮김 (showHidden 아니면 지움)
    static void occludeByExtrudes(MyGraphicsScene *scene, const QString &hostUuid, int kind, double param,
                                  const QList<RevolveLoop> &loops, const QPointF &a0, double angleDeg, ProjDrawing &d);
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

    // 지우기(ERASE·Delete·MCP delete_group) 뒤 연동 (같은 undo 매크로 안에서 부를 것):
    //   · 지운 객체에 있던 뷰 태그(touchedViews) 중 선이 하나도 안 남은 측면·평면·보조 뷰 = 사용자가 뷰를 지운 것 → 부품 기록에서도 뺌
    //   · 돌기·파내기가 붙은 부품과 뷰 기록이 바뀐 부품은 투상도를 다시 그림 (스케치를 지우면 다른 뷰의 파낸·돌기 선도 사라지게)
    //   반환 = 다시 그린 부품 수
    static int syncAfterErase(IAppContext *ctx, MyGraphicsScene *scene, const QSet<QString> &touchedViews);
    // 지울 객체들의 뷰 태그 (syncAfterErase 용)
    static QSet<QString> viewIdsOf(const QList<QGraphicsItem *> &items);

    // 모든 부품의 뷰를 지우고 다시 그린다. 반환 = 다시 그린 뷰 수
    static int regenerateAll(IAppContext *ctx, MyGraphicsScene *scene);

    // ── 투상도 숨기기 / 보이기 (메뉴 "3D 기계용 > 투상도 숨기기·보이기") ──
    //   뷰 기록에 "concealed": true → 다시 그릴 때 그 뷰의 선·원은 그리지 않는다. 기록은 남으므로
    //   측면도에 놓은 구멍은 계속 부품에 연결된다(정면 구멍 단면·3D 구멍 유지). 그 구멍(HoleItem)은 화면에서만 숨김.
    //   정면(Front, 구멍 단면) 기록은 숨길 수 없다.
    struct ConcealedView {
        QString partUuid;
        QString viewId;
        QString label;   // "부품 이름 — 좌측면도"
    };
    // 클릭한 투상도 선·원 → 부품·뷰 (정면 기록이면 false)
    static bool viewOfItem(MyGraphicsScene *scene, const QGraphicsItem *item, QString &partUuid, QString &viewId);
    // 숨기기(on = true)·보이기(false). undo 매크로 안에서 부를 것 (기록 변경 + 다시 그리기)
    static bool setViewConcealed(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                                 const QString &viewId, bool on);
    // 숨긴 투상도 목록
    static QList<ConcealedView> concealedViews(MyGraphicsScene *scene);
    // 숨긴 측면도 안 구멍(HoleItem)의 보이기 상태를 기록에 맞춤 (도면 불러온 뒤·숨기기/보이기·실행 취소 때)
    static void applyConcealedHoles(MyGraphicsScene *scene);
};

#endif // PROJECTIONVIEW_H
