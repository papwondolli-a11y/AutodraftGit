#ifndef SPACEFINISH_H
#define SPACEFINISH_H

#include "myvector.h"
#include <QList>
#include <QPen>
#include <QPointF>
#include <QString>
#include <QVariantMap>

class IAppContext;
class LineItem;
class MyGraphicsScene;
class QUndoStack;

// ─────────────────────────────────────────────────────────────────────────────
// SpaceFinish — 공간벽 마감형태 (평면도어·평면창문 공용)
//
// 개구부 P1-P2 가상선(좌측) 과 개구부 너비 W 만큼 평행이동한 가상선(우측) 각각에서
// ML_DATA_ROLE "space" 선을 P1 에서 가까운 순으로 수집한다.
//   space 선이 2개 미만 → 조건 무시 (swtype1 과 동일)
//   swtype1(0) : 개구부 너비만큼만 끊음 (현행)
//   swtype2(1) : 1번째 space 선을 바깥(좌 -X / 우 +X)으로 EXT 만큼 더 끊고, 끝점에서 2번째 space 선까지 수직선
//   swtype3(2) : 2번째 space 선을 더 끊고, 끝점에서 1번째 space 선까지 수직선
//   swtype4(3) : 두 선 모두 더 끊고 양쪽 끝점을 연결
// 연결선은 더 끊어낸(기준) space 선과 같은 레이어/펜을 쓰며 개구부 마감선(PD_DATA_CLOSING_LINE)으로 태깅된다.
//
// 선택값(type)은 모든 도어/창문 툴이 공유한다 (도면 단위 ToolDefaults "SpaceFinish").
// ─────────────────────────────────────────────────────────────────────────────
class SpaceFinish {
public:
    struct Plan {
        QList<LineItem *> extItems;      // EXT 만큼 더 끊을 space 선
        double            ext          = 0.0;
        bool              hasConnector = false;
        MyVector          connA;
        MyVector          connB;
        double            lyA          = 0.0;   // 연결선 양 끝점의 로컬 Y (속성창 편집 시 재생성용)
        double            lyB          = 0.0;
        QString           layer;
        QPen              pen;
    };

    static double extLength();            // 더 끊어낼 길이 (mm)

    static int  type();                   // 0..3
    static void setType(int t);
    // ns: ToolDefaults 네임스페이스 (기본 "SpaceFinish" 공용, 문-113/114 는 전용 네임스페이스 사용)
    static void load(MyGraphicsScene *scene, const QString &ns = QStringLiteral("SpaceFinish"));
    static void save(MyGraphicsScene *scene, const QString &ns = QStringLiteral("SpaceFinish"));

    // 좌/우 계획 동시 산출. wallGroupId 가 비었거나 type()==0 이면 빈 계획.
    // (ox,oy,c,s2) = 개구부 로컬 좌표계, W = 개구부 너비
    static void planBoth(MyGraphicsScene *scene, const QString &wallGroupId,
                         const QPointF &p1, const QPointF &p2,
                         double ox, double oy, double c, double s2, double W,
                         Plan &left, Plan &right);

    // 심볼 props 에 저장 (SpaceExtL/R, SpaceConnLayerL/R, SpaceConnLyAL/BL/AR/BR)
    static void storeProps(QVariantMap &props, const Plan &left, const Plan &right);

    // 배치 직후 연결선 생성
    static void addConnectors(MyGraphicsScene *scene,
                              const Plan &left, const Plan &right,
                              const QString &symbolUuid, QUndoStack *stack);

    // 속성창 편집 시 props 로부터 연결선 재생성 (W = 새 개구부 너비)
    static void addConnectorsFromProps(MyGraphicsScene *scene, IAppContext *ctx,
                                       double ox, double oy, double c, double s2, double W,
                                       const QVariantMap &props,
                                       const QString &symbolUuid, QUndoStack *stack);

private:
    static Plan planSide(MyGraphicsScene *scene, int type, const QString &wallGroupId,
                         const QPointF &p1, const QPointF &p2,
                         double ox, double oy, double c, double s2,
                         bool rightSide, double W);
    static int s_type;
};

#endif // SPACEFINISH_H
