#ifndef REVOLVEPROFILE_H
#define REVOLVEPROFILE_H

#include <QList>
#include <QMap>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QStringList>

class QGraphicsItem;
class QUndoStack;
class MyGraphicsScene;

// ─────────────────────────────────────────────────────────────────────────────
// RevolveProfile — 해칭 단면 + 중심선(회전축) → 회전체 단면 (메뉴 "3D 기계용 > 단면 회전체 만들기")
//
//   축 좌표: x = 회전축 방향(축 시작점 a0 에서 a1 쪽 +), r = 축에서 떨어진 거리(≥ 0).
//   해칭이 축 위·아래 양쪽에 있으면(전단면) 넓은 쪽 한쪽만 쓴다(대칭 가정).
//   단면 고리: 바깥 고리는 반시계, 안쪽 구멍 고리는 시계 방향으로 맞춰 돌려준다.
//   원통면: 축과 나란한 단면 변 → 재료가 바깥쪽(r 큰 쪽)이면 "구멍", 안쪽이면 "바깥" 면.
// ─────────────────────────────────────────────────────────────────────────────

struct RevolveLoop {
    QPolygonF pts;      // (x, r), 닫힘점 중복 없음
    bool hole = false;  // 단면 안의 빈 구멍(회전하면 내부 공동)
};

struct RevolveCylinder {
    bool hole = false;  // true = 구멍(안쪽 면), false = 바깥 면
    double diameter = 0.0;
    double x0 = 0.0;
    double x1 = 0.0;
    QString key() const;                      // "H:37" / "S:60" (끼워맞춤 기호 저장 키)
    static QString keyFor(bool hole, double diameter);
};

class RevolveProfile {
public:
    // 단면 쪽: 0 = 자동(넓은 쪽), 1 = 축 위쪽(축 방향의 왼쪽), -1 = 아래쪽
    enum Side { SideAuto = 0, SideUpper = 1, SideLower = -1 };

    // 회전축 방향 정리: 왼→오른쪽(수직이면 아래→위)으로 → "위쪽" = 도면에서 위(수직축이면 왼쪽)
    static void normalizeAxis(QPointF &a0, QPointF &a1);

    // 해칭이 축 위·아래 어느 쪽에 있는지, 양쪽이면 대칭인지
    static void sideInfo(const QList<QPainterPath> &hatchPaths, const QPointF &a0, const QPointF &a1,
                         bool &hasUpper, bool &hasLower, bool &symmetric);

    // 해칭 경로들(도면 좌표) → 축 좌표 단면 고리. 실패하면 빈 목록
    static QList<RevolveLoop> sectionLoops(const QList<QPainterPath> &hatchPaths, const QPointF &a0,
                                           const QPointF &a1, int side = SideAuto);

    // 단면 고리에서 원통면 찾기 (같은 종류·지름·이어진 구간은 합침)
    static QList<RevolveCylinder> cylinders(const QList<RevolveLoop> &loops);

    // 같은 종류·지름 원통면을 합친 목록 (설정창 표시용: 길이 합)
    static QList<RevolveCylinder> mergedByKey(const QList<RevolveCylinder> &cyls);

    // 끼워맞춤 기호 맵 <-> JSON 문자열 (레지스트리 props "Fits")
    static QString fitsToJson(const QMap<QString, QString> &fits);
    static QMap<QString, QString> fitsFromJson(const QString &json);

    // 해칭들을 회전체 부품으로 등록 (도구·MCP 공용). 해칭마다 같은 UUID 태그.
    //   반환 = UUID (실패하면 빈 문자열, error 에 이유)
    //   existingUuid 가 있으면 새로 만들지 않고 그 회전체를 고친다(태그 다시 붙이기 + 축·끼워맞춤·쪽 갱신, Undo 지원)
    static QString registerRevolve(MyGraphicsScene *scene, QUndoStack *undo, const QList<QGraphicsItem *> &hatches,
                                   const QPointF &a0, const QPointF &a1, const QString &namePrefix,
                                   const QMap<QString, QString> &fits, int side, const QString &existingUuid,
                                   QString *error);

    // 고를 해칭·축에 해당하는 기존 회전체: 고른 해칭에 이미 태그가 있거나,
    //   같은 회전축 위에서 기존 회전체 해칭과 영역이 겹치면 그 UUID (없으면 빈 문자열)
    static QString findExisting(MyGraphicsScene *scene, const QList<QGraphicsItem *> &hatches, const QPointF &a0,
                                const QPointF &a1, const QRectF &extraBox = QRectF());

    // 같은 UUID 로 태그된 해칭들의 현재 경로와 기준점(경계 사각 왼쪽 아래)
    static bool taggedHatches(MyGraphicsScene *scene, const QString &uuid, QList<QPainterPath> &paths,
                              QPointF &ref);

    // ── 영역(Area) 방식: 해칭 없이 해치 명령처럼 닫힌 공간을 고른다 ──
    //   box = 경계 후보 객체를 고른 사각, picks = 공간 안쪽 점(비면 "복합모양" = 사각 안 전체 모양)
    //   저장해 두고 3D 를 만들 때마다 다시 찾는다 → 선을 고치면 3D 도 따라감 (못 찾으면 저장된 모양 사용)
    //   includeViewLines = 투상도로 생성된 선·원도 경계로 씀 (측면도에 그린 돌출 단면은 투상 원이 경계의 일부)
    //   excludeHoleSections = 구멍 단면 선(HOLE_SECTION_GROUP_ID)도 경계에서 뺌 (해치 방식 부품의 선 영역용)
    //   excludeHoles = 구멍R·구멍P 객체(HoleItem)도 경계에서 뺌 (단독 돌출: 구멍은 구멍 커터로 따로 판다)
    //   excludeViewIds = 이 뷰 UUID(MECH_VIEW_ID)의 선은 경계에서 뺌 (판 정면에 그린 자기 숨은선이 공간을 쪼개지 않게)
    static QPainterPath areaRegion(MyGraphicsScene *scene, const QRectF &box, const QList<QPointF> &picks,
                                   bool includeViewLines = false, bool excludeHoleSections = false,
                                   bool excludeHoles = false, const QStringList &excludeViewIds = QStringList());
    // 해치 방식 부품의 단면 = 해칭이 덮고 있는 "선" 영역 (해칭 안쪽 점을 둘러싼 닫힌 공간).
    //   해칭은 표시용이 되어 구멍 자리를 빼도 3D 단면은 그대로. 선으로 못 찾거나 해칭과 크게 다르면 해칭 모양 그대로.
    static QPainterPath hatchLineRegion(MyGraphicsScene *scene, const QPainterPath &hatchPath);
    static QString registerRevolveArea(MyGraphicsScene *scene, QUndoStack *undo, const QRectF &box,
                                       const QList<QPointF> &picks, const QPainterPath &region, const QPointF &a0,
                                       const QPointF &a1, const QString &namePrefix,
                                       const QMap<QString, QString> &fits, int side, const QString &existingUuid,
                                       QString *error);

    // 회전체 부품의 현재 단면 경로 (해칭 방식 = 태그된 해칭, 영역 방식 = 다시 찾은 공간)와 기준점
    static bool partPaths(MyGraphicsScene *scene, const QString &uuid, QList<QPainterPath> &paths, QPointF &ref);
};

#endif // REVOLVEPROFILE_H
