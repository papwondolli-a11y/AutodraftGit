#ifndef HOLESYNC_H
#define HOLESYNC_H

#include "holesectiongenerator.h" // HoleSectionLineRole
#include "myhole.h"
#include <QList>
#include <QString>

class CadItem;
class IAppContext;
class QGraphicsItem;
class QWidget;

// 구멍(HoleItem) 스펙이 바뀐 뒤 따라가야 하는 연결 객체 동기화 모음.
// 원래 PropertyDock 내부 함수였으나 구멍 콜아웃 편집에서도 똑같이 써야 해서 분리했다.
class HoleSync {
public:
    // 연결된 중심선(HOLE_DATA_GROUP_ID로 태깅된 LineItem)을 새 outerRadius()에 맞춰 늘이거나 줄인다.
    static void syncCenterLines(IAppContext *ctx, CadItem *ci);

    // 탭가공된 구멍의 드릴지름이 바뀌면 기존 피치를 유지한 채 ThreadSpec 호칭지름을 다시 계산한다.
    static void syncThreadedMajorDiameter(IAppContext *ctx, CadItem *ci, double newDrillDiameter);

    // 연결된 단면(HOLE_SECTION_GROUP_ID)이 있으면 하이라이트 후 확인창을 띄우고,
    // "예"일 때만 같은 위치/방향으로 최신 스펙의 단면을 다시 만든다.
    static void confirmAndSyncSections(IAppContext *ctx, CadItem *ci, QWidget *parent);

    // 단면 LineItem들을 배치(위치/방향)별로 묶어서 반환. 없으면 빈 리스트.
    static QList<QList<QGraphicsItem *>> findSectionInstances(CadItem *ci);
    static void highlightSections(CadItem *ci);
    // HoleSectionTool::layerNameForRole과 동일한 규칙(역할별 레이어, 비어있으면 fallback).
    static QString layerForSectionRole(const MyHole &hole, HoleSectionLineRole role,
                                       const QString &fallbackLayer);
};

#endif // HOLESYNC_H
