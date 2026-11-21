#ifndef MECHPARTTAG_H
#define MECHPARTTAG_H

#include <QMap>
#include <QPointF>
#include <QString>
#include <QVariant>

class MyGraphicsScene;
class QGraphicsItem;
class QUndoStack;

// ─────────────────────────────────────────────────────────────────────────────
// MechPartTag — 기계 Tools 가 부품을 배치할 때 "3D 기계용" 파라미터를 남기는 공용 도우미
//
//   1) 부품 도형 중 하나(기준 도형: 닫힌 폴리라인 또는 선)를 고른다.
//   2) registerPart() → SceneSymbolRegistry 에 type·이름·props + ox/oy/angleDeg/refX/refY 저장 (Undo 지원)
//   3) 기준 도형에 MECH_DATA_PART_ID(UUID) 를 붙인다 (.cdb 에 mechPartId 로 보존)
//   MechModel3D 는 기준 도형의 현재 위치(refPoint)와 저장된 refX/refY 의 차이만큼 부품을 옮겨 3D 를 만든다.
// ─────────────────────────────────────────────────────────────────────────────
class MechPartTag {
public:
    // 기준 도형의 기준점: 폴리라인 = 꼭짓점 최소 X·Y, 선 = 시작점, 해칭 = 경계 사각 최소 X·Y. 그 밖에는 false.
    static bool refPoint(const QGraphicsItem *item, QPointF &out);

    // 부품 등록 후 anchor 에 태그를 붙이고 UUID 를 돌려준다. undo 가 있으면 RegisterSymbolCommand 로 넣는다
    //   (호출부의 beginMacro~endMacro 안에서 부르면 배치와 함께 한 번에 취소된다).
    static QString registerPart(MyGraphicsScene *scene, QUndoStack *undo, QGraphicsItem *anchor,
                                const QString &type, const QString &namePrefix,
                                QMap<QString, QVariant> props, const QPointF &origin, double angleDeg,
                                const QPointF *refOverride = nullptr);   // 여러 도형이 한 부품일 때 기준점 직접 지정
};

#endif // MECHPARTTAG_H
