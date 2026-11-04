#ifndef HOLEDATA_H
#define HOLEDATA_H

// QGraphicsItem::setData() 키 — Hole과 HoleTool이 함께 생성하는 중심선(HoleItem의 도형이
// 아니라 전용 레이어의 독립 LineItem 2개)을 같은 세션 안에서 연결해, 지름/나사규격 등이
// 바뀔 때 중심선 길이도 같이 갱신할 수 있게 한다.
// 200번대는 multilinedata.h/planarchdata.h가 이미 사용 중이라 220 사용.
static constexpr int HOLE_DATA_GROUP_ID = 220; // QString UUID (같은 구멍 = 같은 ID)

// HoleSectionTool이 만드는 단면(HoleItem과 실시간 연동되지 않는 독립 LineItem들)을 원본
// 구멍과 연결하는 태그. HOLE_DATA_GROUP_ID(중심선용)와는 값이 같아도 역할이 다르므로
// 별도 role로 분리해 중심선 동기화 로직이 단면 선을 잘못 건드리지 않게 한다.
// 배치 정보(topCenter/direction/materialThickness)도 함께 저장해 두어야, 구멍 속성이
// 바뀌었을 때 같은 위치·방향으로 단면을 다시 생성(재계산)할 수 있다.
static constexpr int HOLE_SECTION_GROUP_ID = 221;             // QString UUID (원본 구멍의 HOLE_DATA_GROUP_ID)
static constexpr int HOLE_SECTION_TOP_X = 222;                 // double
static constexpr int HOLE_SECTION_TOP_Y = 223;                 // double
static constexpr int HOLE_SECTION_DIR_X = 224;                 // double
static constexpr int HOLE_SECTION_DIR_Y = 225;                 // double
static constexpr int HOLE_SECTION_MATERIAL_THICKNESS = 226;    // double

#endif // HOLEDATA_H
