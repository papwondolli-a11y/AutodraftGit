#ifndef MULTILINEDATA_H
#define MULTILINEDATA_H

// QGraphicsItem::setData() / data() 키 — 다중선 그룹 식별용
// TwoLineTool 생성 시 태깅, MultilineCornerTool 에서 읽기

static constexpr int ML_DATA_GROUP_ID = 200; // QString UUID (같은 그리기 = 같은 ID)
static constexpr int ML_DATA_ROLE     = 201; // "outer" | "mortar" | "space" | "center"
static constexpr int ML_DATA_SPACING  = 202; // double: outer mortar ~ outer mortar 전체 폭

#endif // MULTILINEDATA_H
