#ifndef DETAILVIEWTOOL_H
#define DETAILVIEWTOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QList>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QString>
#include <vector>

class CadItem;
class MyLine;
class MyArc;
class MyCircle;
class MyPolyline;

// ─────────────────────────────────────────────────────────────────────────
// DetailViewTool — 상세도(Detail View) 생성 툴
//
// 기존 도면의 일부 영역을 원형 경계로 지정한 뒤, 그 경계 "안쪽" 부분만
// 잘라내어(hard-clip) 다른 축척으로 확대(또는 축소)한 상세도를 별도 위치에
// 다시 그린다. 경계 바깥으로 삐져나간 부분은 버린다(trim).
//
//   1) 상세도로 만들 영역의 중심점 지정
//   2) 영역 반지름 지정 (클릭 또는 숫자 입력)
//   3) 축척 배율 입력 (예: 2 → 2:1, 0.5 → 1:2. "2:1"/"1:2" 형식도 허용)
//   4) 상세도를 배치할 위치 지정
//
// 결과물
//   · 원본 위치 : 참조 경계원 + 라벨("A")            → DetailMarker 레이어
//   · 배치 위치 : 경계 안쪽만 클리핑된 개별 엔티티 + 상세도 경계원 + 캡션.
//                 하나의 블록으로 묶지 않고 각 엔티티를 독립 CadItem 으로
//                 생성하므로 개별 선택·그립 편집이 가능하다.
//                 논리적 묶음은 "DETAIL-A" 레이어로만 구분한다.
//
// 클리핑 동작
//   · Line/Arc/Polyline : 경계원과의 교차점에서 잘라 안쪽 구간만 재생성
//   · Circle            : 경계에 걸치면 안쪽 호로, 완전히 들어오면 원 그대로
//   · Hatch             : 경계원과 path.intersected() 후 재생성
//   · Spline            : 근사 폴리라인으로 클리핑
//   · Text/Dimension/Leader/GDT/표면기호/Hole : 대표점이 경계 안이면 통째 포함,
//                         아니면 제외(부분 클리핑 불가)
//
// v1 제약 : 원형 경계만 지원. 원본 변경과의 연관(associative) 갱신 없음.
//           복제된 치수의 측정값은 배율이 곱해진 채로 표시된다.
// ─────────────────────────────────────────────────────────────────────────
class DetailViewTool : public AbstractTool {
    Q_OBJECT
public:
    enum State { WaitCenter, WaitRadius, WaitScale, WaitPlace };

    explicit DetailViewTool(QObject *parent = nullptr);
    ~DetailViewTool();

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    bool processCommand(const QString &command) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

private:
    void showCenterPrompt();
    void showRadiusPrompt();
    void showScalePrompt();
    void showPlacePrompt();

    void updateRadiusGhost(const QPointF &edgePos);
    void updatePlaceGhost(const QPointF &placePos);

    QPointF snappedPoint(const QPointF &pos, const QPointF &base) const;

    // 경계원에 (일부라도) 걸치는 최상위 CadItem 수집
    QList<CadItem *> collectItemsInCircle(const QPointF &center,
                                          double radius) const;

    // ── 좌표 변환: 원본 좌표 p → 상세도 좌표 ──────────────────────
    // (p - center) * scale + place
    QPointF mapPoint(const MyVector &p) const;
    QPainterPath mapPath(const QPainterPath &src) const;
    bool insideBoundary(const MyVector &p) const;

    // ── 엔티티 클리핑 (결과는 상세도 좌표계의 새 아이템으로 out 에 추가) ──
    void emitClippedItem(CadItem *src, const QPen &pen,
                         QList<QGraphicsItem *> &out) const;
    void clipLineToBoundary(const MyLine &line, const QPen &pen,
                            QList<QGraphicsItem *> &out) const;
    void clipArcToBoundary(const MyArc &arc, const QPen &pen,
                           QList<QGraphicsItem *> &out) const;
    void clipCircleToBoundary(const MyCircle &circle, const QPen &pen,
                              QList<QGraphicsItem *> &out) const;
    void clipPolylineToBoundary(const MyPolyline &pline, const QPen &pen,
                                QList<QGraphicsItem *> &out) const;
    void clipPointRunToBoundary(const std::vector<MyVector> &pts, bool closed,
                                const QPen &pen,
                                QList<QGraphicsItem *> &out) const;
    // Text/Leader 등 분할 불가 엔티티: 대표점이 경계 안이면 통째 복제
    void cloneAtomicIfInside(CadItem *src, QList<QGraphicsItem *> &out) const;
    // 치수선: 경계원에 "완전히" 포함될 때만 재생성. 걸치면 무시.
    // 표시 숫자는 원본 본문값을 그대로 고정(배율로 커진 측정값을 쓰지 않음).
    void emitDimensionIfContained(CadItem *src,
                                  QList<QGraphicsItem *> &out) const;

    // 라벨/레이어 헬퍼
    QString nextDetailLabel() const;
    void ensureDetailLayer(const QString &layerName);

    static QString formatScaleRatio(double factor);
    // "2:1" / "1:2" 등 → 배율(double). 파싱 실패 시 fallback 반환.
    static double parseScaleRatio(const QString &text, double fallback);

    // "S" 옵션: Setup 다이얼로그 열기
    void openSetupDialog();

    // Setup 설정 영구화 (QSettings 그룹 "DetailViewTool")
    static void loadSettings(class MyGraphicsScene *scene);
    static void saveSettings(class MyGraphicsScene *scene);

    void buildDetailView(const QPointF &placePos);
    // 원본 위치 참조 마커 생성 (지시선/경계선 모드). layerName = Setup 설정 레이어.
    void buildSourceMarker(const QString &label, double labelSize,
                           const QString &layerName,
                           QList<QGraphicsItem *> &out) const;
    void cancelDrawing();

    State   m_state;
    QPointF m_center;     // 경계 중심 (원본 좌표계)
    double  m_radius;     // 경계 반지름 (원본 좌표계)
    double  m_scale;      // 축척 배율 (상세도 / 원본)
    QPointF m_place;      // 상세도 배치 기준점
    QPointF m_currentPos; // 현재 마우스 위치

    // ── Setup 설정 (세션 + QSettings 유지) ──────────────────────
    static QString s_detailLayer;     // 빈 문자열 = DETAIL-<라벨> 자동 레이어
    static double  s_labelTextSize;   // <=0 = 도면 설정값(치수 스타일 textSize)
    static bool    s_labelOnLeader;   // 지시선 끝에 라벨
    static bool    s_labelOnBoundary; // 경계선 선상에 라벨(그 자리 절단)
    static QString s_scaleText;       // "2:1" 등
};

#endif // DETAILVIEWTOOL_H
