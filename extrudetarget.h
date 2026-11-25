#ifndef EXTRUDETARGET_H
#define EXTRUDETARGET_H

#include "extrudefeature.h"

#include <QColor>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QStringList>

class MyGraphicsScene;
class QGraphicsItem;
class IAppContext;

// ─────────────────────────────────────────────────────────────────────────────
// ExtrudeTarget — 돌출·파내기 설정창의 "대상" 하나 (어느 부품·어느 뷰에 붙는가)
//   방향은 대상에 따라 정해진다: 회전체 측면도 = 그 회전축, 판의 측면·보조 뷰 = 그 면의 법선, 없음 = 도면에 수직(Z)
// ─────────────────────────────────────────────────────────────────────────────
struct ExtrudeTarget {
    enum Type { None = 0, RevolveSide = 1, PlateFace = 2, PlateFront = 3 };
    Type type = None;
    QString host;             // 부품 UUID (없음이면 빔)
    QString hostName;
    QString viewId;
    int viewKind = 0;
    QString viewName;         // 좌측면도 · 우측면도 · 보조 투상도 · 정면
    bool concealed = false;   // 숨긴 측면도
    bool away = false;        // 고른 공간이 이 측면도와 떨어져 있음 (막지 않고 알려만 줌)
    QPainterPath outline;     // 대상 강조 (도면 좌표)
    double startZ = 0.0;      // 없음(새 부품): 시작 Z 자동값
    QString startNote;

    QString label() const;    // 목록 글자
};

// 설정창의 현재 값 (미리보기 계산용)
struct ExtrudeValues {
    double offset = 0.0;
    double depth = 10.0;
    bool symmetric = false;
    bool through = false;
    bool cut = false;
};

class ExtrudeTargets {
public:
    // 돌출 대상 후보 — 첫 번째가 자동 판단값 (판의 뷰 → 가까운 회전체 측면도 → 없음)
    //   probe = 공간 안 대표 점
    static QList<ExtrudeTarget> forExtrude(MyGraphicsScene *scene, const QPainterPath &region, const QPointF &probe);
    // 파내기 대상 후보 (없음 제외) — 판의 뷰 → 판 정면 → 회전체 측면도. 비면 파낼 부품이 없음
    static QList<ExtrudeTarget> forCut(MyGraphicsScene *scene, const QPainterPath &region, const QPointF &probe);

    // 방향 설명 (서식 있는 글자 — 축 색) 과 축 색
    static QString directionText(MyGraphicsScene *scene, const ExtrudeTarget &t, bool cut, QColor *axisColor = nullptr);

    // 미리보기 도형(대상 주황 · 돌출 범위 파란 점선 · 방향 화살표)을 씬에 추가. 저장·실행 취소와 무관한 임시 객체
    static QList<QGraphicsItem *> addPreview(MyGraphicsScene *scene, const ExtrudeTarget &t, const QPainterPath &region,
                                             const ExtrudeValues &v);
    static void removePreview(MyGraphicsScene *scene, QList<QGraphicsItem *> &items);

    // 같은 공간을 "다른 대상"으로 돌출(파내기)한 기존 기록들
    static QStringList otherRecords(MyGraphicsScene *scene, const QPainterPath &region, const ExtrudeTarget &t, bool cut);
    // 기록 설명 "'받침대' (회전체-1 — 좌측면도)"
    static QString recordLabel(MyGraphicsScene *scene, const QString &uuid);
    // 기록 지우기 — 투상도 선·붙은 돌기·파내기까지, 붙어 있던 부품의 투상도는 다시 그림 (undo 매크로 안에서)
    static void removeRecord(IAppContext *ctx, MyGraphicsScene *scene, const QString &uuid);
};

#endif // EXTRUDETARGET_H
