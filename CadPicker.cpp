#include "CadPicker.h"
#include "caditem.h"
#include "hatchitem.h"
#include "basedimensionitem.h" // 치수 객체 헤더 추가
#include "leaderitem.h"
#include "mygraphicsscene.h"
#include "gripitem.h"
#include "myvector.h"
#include <QGraphicsItem>
#include <limits>
#include <cmath>

// ──────────────────────────────────────────────────────────────
// 생성자 / 씬 설정
// ──────────────────────────────────────────────────────────────

CadPicker::CadPicker(MyGraphicsScene* scene, QObject* parent)
    : QObject(parent), m_scene(scene)
{
}

void CadPicker::setScene(MyGraphicsScene* scene)
{
    m_scene = scene;
}

// ──────────────────────────────────────────────────────────────
// resolveItem: HatchItem 내부 자식이면 자식 자신을 반환,
//              그 외에는 최상위 CadItem을 반환
// ──────────────────────────────────────────────────────────────
CadItem* CadPicker::resolveItem(QGraphicsItem* item) const
{
    if (!item) return nullptr;

    // 부모 체인에 HatchItem이 있는지 확인
    QGraphicsItem* cur = item->parentItem();
    while (cur) {
        if (dynamic_cast<HatchItem*>(cur)) {
            // HatchItem 내부 자식: item 자체가 CadItem이면 그대로 반환
            if (CadItem* asChild = dynamic_cast<CadItem*>(item)) {
                return asChild;
            }
            // item이 CadItem이 아니면(그래픽 서브항목), 부모 HatchItem 반환
            return dynamic_cast<CadItem*>(cur);
        }
        cur = cur->parentItem();
    }

    // HatchItem 자식이 아닌 경우: 일반 CadItem 최상위로 거슬러 올라감
    QGraphicsItem* topItem = item;
    while (topItem->parentItem() &&
           dynamic_cast<CadItem*>(topItem->parentItem())) {
        topItem = topItem->parentItem();
    }
    return dynamic_cast<CadItem*>(topItem);
}

// ──────────────────────────────────────────────────────────────
// pickPriority: 일반 객체 = 2, 치수·지시선 = 1, HatchItem = 0
// ──────────────────────────────────────────────────────────────
int CadPicker::pickPriority(const CadItem* item) const
{
    if (dynamic_cast<const HatchItem*>(item)) return 0;
    if (dynamic_cast<const BaseDimensionItem*>(item)) return 1;
    // 지시선도 주석 객체 — 치수와 같은 우선순위로 두어 겹친 일반 객체 선택을 가로채지 않게 함
    if (dynamic_cast<const LeaderItem*>(item)) return 1;

    // 그 외 일반 CadItem은 가장 높은 우선순위 부여
    return 2;
}

// ──────────────────────────────────────────────────────────────
// pick: 타입 우선순위 → 동일 우선순위면 거리 기준으로 선택
// ──────────────────────────────────────────────────────────────
CadItem* CadPicker::pick(const QPointF& pos, double tolerance) const
{
    if (!m_scene) return nullptr;

    // 오차 범위 사각형으로 후보 검색 (Z-Order 역순 반환)
    QRectF searchRect(pos.x() - tolerance, pos.y() - tolerance,
                      tolerance * 2, tolerance * 2);
    QList<QGraphicsItem*> candidates =
        m_scene->items(searchRect, Qt::IntersectsItemBoundingRect);

    if (candidates.isEmpty()) return nullptr;

    CadItem* closest  = nullptr;
    double   minDist  = std::numeric_limits<double>::max();
    int      bestPrio = -1;

    for (int i = 0; i < candidates.size(); ++i) {
        QGraphicsItem* item = candidates.at(i);

        // GripItem이거나 비가시 객체는 건너뜀
        if (item->type() == GripItem::Type || !item->isVisible()) continue;

        // 타입 해석: HatchItem 내부 자식이면 자식, 아니면 최상위 CadItem
        CadItem* cadItem = resolveItem(item);
        if (!cadItem) continue;

        double dist = cadItem->distanceTo(MyVector(pos));
        if (dist > tolerance) continue;

        int prio = pickPriority(cadItem);

        if (!closest) {
            // 첫 번째 후보
            closest  = cadItem;
            minDist  = dist;
            bestPrio = prio;
        } else if (prio > bestPrio) {
            // 더 높은 우선순위 타입 → 무조건 교체
            closest  = cadItem;
            minDist  = dist;
            bestPrio = prio;
        } else if (prio == bestPrio && dist < minDist) {
            // 같은 우선순위 → 더 가까운 것 선택
            closest = cadItem;
            minDist = dist;
        }
        // prio < bestPrio 이면 무시
    }

    return closest;
}