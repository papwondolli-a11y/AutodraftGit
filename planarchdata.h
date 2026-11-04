#ifndef PLANARCHDATA_H
#define PLANARCHDATA_H

// QGraphicsItem::setData() / data() 키 — 평면도어·창문 그룹 식별용
// multilinedata.h (200~202) 와 충돌 방지: 210번 사용
static constexpr int PD_DATA_GROUP_ID      = 210; // QString: 씬 심볼 레지스트리 UUID 키
static constexpr int PD_DATA_CLOSING_LINE  = 211; // QString: 마감선 소속 UUID (addWallClosingLines 결과물)

#endif // PLANARCHDATA_H
