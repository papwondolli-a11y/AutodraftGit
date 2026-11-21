#ifndef MECHPARTDATA_H
#define MECHPARTDATA_H

// QGraphicsItem::setData() 키 — 기계 부품(파라메트릭 3D) 식별용
//   기계 Tools 가 부품을 배치할 때 외형 폴리라인(닫힘)에 SceneSymbolRegistry UUID 를 붙인다.
//   레지스트리 항목: type "MechShaft" …, props "Params"(JSON) + 배치 기준(refX/refY).
//   .cdb 저장 시 Polyline 의 "mechPartId" 로 보존된다.
static constexpr int MECH_DATA_PART_ID = 240; // QString UUID
// 투상도(3D 기계용 > 투상도 만들기)로 생성된 선·원: 뷰 UUID (.cdb "mechViewId")
static constexpr int MECH_VIEW_ID = 241;
// 투상도에 그린 돌출(ExtrudeFeature) 선: 돌출 부품 UUID (깊이 이동에서 클릭으로 고르기)
static constexpr int MECH_EXTRUDE_ID = 242;

#endif // MECHPARTDATA_H
