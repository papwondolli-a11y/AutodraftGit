#ifndef DETAILREGIONDATA_H
#define DETAILREGIONDATA_H

// 상세도(Detail View) 경계원에 QGraphicsItem::setData()로 태깅하는 키.
// DetailViewTool이 배치한 경계원(CircleItem)에 이 두 값을 심어두면,
// 그 원 안에서 새로 생성되는 치수는 상세도 축척의 역수(1/scale)를
// measureScale로 적용받아 "원본 실측값"을 표시한다.
//   ML_DATA_* (200번대), PD_DATA_* (210), HOLE_DATA_* (220번대)와 겹치지 않게 230번대 사용.
static constexpr int DETAIL_REGION_LABEL = 230; // QString: 상세도 라벨 ("A", "B" ...)
static constexpr int DETAIL_REGION_SCALE = 231; // double : 상세도 축척 배율 (상세도/원본)
static constexpr int DETAIL_REGION_ROLE  = 232; // int   : 1=상세도 내용, 2=원본 참조마커
static constexpr int DETAIL_REGION_SCALE_CAPTION = 233; // int(1) : "축척 N:1" 캡션 텍스트 표식

static constexpr int DETAIL_ROLE_CONTENT = 1;
static constexpr int DETAIL_ROLE_MARKER  = 2;

#endif // DETAILREGIONDATA_H
