#!/usr/bin/env python
"""AutoDraft MCP 브리지 (표준 라이브러리만 사용, 설치 불필요)

  Claude (MCP, stdio)  <-->  이 스크립트  <-->  AutoDraft (Windows 명명 파이프 \\\\.\\pipe\\AutoDraftAI)

사용:
  MCP 서버로 실행 (Claude 가 호출):   python autodraft_mcp.py
  터미널에서 도구 직접 시험:          python autodraft_mcp.py call ping
                                      python autodraft_mcp.py call create_line '{"p1":[0,0],"p2":[1000,500]}'

AutoDraft 가 먼저 실행되어 있어야 한다 (시작 시 명령창에 "AI bridge server started" 표시).
"""
import base64
import json
import os
import sys
import time

PIPE_PATH = r"\\.\pipe\AutoDraftAI"
SERVER_NAME = "autodraft"
SERVER_VERSION = "0.9.0"

INSTRUCTIONS_FULL = """AutoDraft(CAD) 조작 도구 사용 규칙.

[기본] 좌표·치수는 mm, X는 동쪽(오른쪽), Y는 북쪽(위쪽)이 +. 원점은 어디든 괜찮다.
[순서] ping 으로 연결 확인 → 사용자가 파일을 지정했으면 open_drawing(그 도면의 레이어·축척을 따른다, 축척 설정 불필요), 아니면 new_drawing → set_drawing_scale(보통 60) → 그리기 → 매 단계 뒤 screenshot 으로 결과를 눈으로 확인 → 잘못되면 undo.
[검증] 도구가 돌려준 값(paramsUsed, room.interiorRect, core.*)으로 면적·위치를 직접 계산해 확인하고, 숫자만 믿지 말고 screenshot 으로 모양도 본다. 면적 요구(예: 32평형)가 있으면 합계를 계산해 보고하라.
[방] create_room 은 내부 유효 치수(dimensions:"interior")로 쓰는 것이 편하다. 이웃한 방은 벽 두께(기본 200mm)만큼 띄워 배치하고, 이미 그려진 이웃 벽을 공유하는 변은 walls 에서 뺀다(뺀 쪽 벽 끝은 이웃 벽 면에서 멈춘다). 문·창은 그 변의 벽이 이미 그려진 뒤에 낸다(다른 방이 그린 벽이면 그 방을 먼저 만든다).
[코어] create_core 는 계단실/홀(칸막이 없는 한 공간)+엘리베이터를 한 번에 만든다. 응답의 core(stairRoomInterior, hallInterior, shaftInterior, outerBounds)를 읽어 이웃 방의 위치를 맞춘다(코어 벽선과 방 벽선을 한 줄로 이어 붙이면 깔끔하다).
[가구·위생기구] 가구·싱크대·화장실·수목은 블록 라이브러리에 있다. list_blocks(category)로 이름·크기·미리보기 이미지를 보고 무엇인지 확인한 뒤 insert_block(layer:"FUR")으로 넣는다. 응답의 bboxFromInsertPoint 로 벽·모서리에 붙일 위치를 계산하고(삽입점은 블록 원점), 삽입 후 screenshot 으로 문 열림 호·창·실명 글자와 겹치지 않는지 확인한다(실명은 create_room 의 nameAt 으로 옮길 수 있다).
[치수] 치수는 기본적으로 **벽 중심선에서 중심선까지의 거리**로 넣는다. build_plan 이면 autoDimensions(기본 basis:"centerline", sides:["S","E"])로 자동 생성하고, create_dimension 을 직접 쓸 때는 점을 벽 중심선 좌표(응답의 room.centerlineRect 의 가장자리, core.outerBounds 를 벽 두께/2 만큼 안으로 줄인 값)로 준다. 치수선은 건물 바깥 면에서 600~1000mm 띄우고 전체 치수는 한 칸 더 바깥(overallAt)에 둔다. 사용자가 요청할 때만 내부 면 기준(basis:"interior")을 쓴다. 원·호의 지름/반지름 치수는 create_circle_dimension 을 쓴다(선형 치수로 지름을 대신하지 않는다).
[재생성·수정] 방 위치·크기를 바꾸라는 요청은 build_plan 의 스펙을 고쳐 같은 group 으로 다시 호출하는 것이 가장 안전하다(이웃 방·문·창·가구·치수가 함께 다시 계산됨). 현재 스펙은 get_plan(group)으로 읽는다. 개별 도구로 만든 것은 set_group 으로 이름표를 붙여 두면 delete_group 으로 지울 수 있다. 스펙을 바꾼 뒤에는 면적 변화를 다시 계산해 보고하고 screenshot 으로 겹침을 확인한다.
[다중선 코너] multiline_lcorner(L), multiline_tcorner(T), multiline_ccorner(C 십자)는 이미 그려진 두 다중선을 클릭 두 점(clickA/clickB)으로 코너 처리한다. 클릭 점은 각 다중선 위에서 코너 가까이 잡는다.
[벽 접합 규칙] 벽(다중선)이 만나는 모양에 따라 처리한다. T자(한 벽이 다른 벽을 만나 끝남)는 "합쳐진 T"로 처리한다: 줄기 벽의 벽선은 통과 벽의 가까운 면 선에서 끝나고, 통과 벽의 가까운 면 선은 줄기 벽 폭만큼 끊기며(바깥 쪽 선은 연속), 줄기 벽의 중심선은 통과 벽 중심선까지 이어진다 — 벽선끼리 겹치거나 침범하면 안 된다. 통과 벽은 줄기와 별개의 끊기지 않는 직선 다중선 하나로 그리고(여러 벽을 한 경로/그룹으로 묶지 않는다), 줄기는 통과 벽 중심선까지 겹쳐 그린 뒤 multiline_tcorner(clickA=통과 벽 위의 점, clickB=줄기 벽의 자를 끝 쪽 점)로 처리한다. L자(두 벽이 꺾여 만남)는 multiline_lcorner 를 쓰거나, 처음부터 create_multiline 의 points 한 경로로 이어 그려 코너를 처리한다(바깥 벽선↔바깥, 안쪽↔안쪽). 승강로 입구쪽 벽(문 앞 개구부 양쪽 벽조각)이 대표적이며, 왼쪽은 L, 승강로 반대쪽 벽에 닿는 쪽은 T 이다. 그린 뒤 screenshot 으로 접합부를 확대해 벽선 겹침·틈이 없는지 확인한다.
[평면 벽 구성 절차] 방이 여러 개인 평면(칸막이가 있는 아파트·사무실 등)은 위 벽 접합 규칙을 반드시 지킨다. build_plan/create_room 이 방마다 사각형 벽을 따로 그리면 칸막이가 두 겹으로 겹치고 접합이 규칙에서 벗어나므로, 이웃한 방끼리 벽을 공유하는 평면은 이 방식으로 벽을 그리지 않는다. 절차: (1) 코어(create_core)만 build_plan 으로 먼저 만든다. (2) 외벽은 create_multiline 한 경로로 그려 L 코너를 처리하고, 복도 등 통과 벽은 끊기지 않는 직선 하나로 그린다. (3) 칸막이(줄기)는 통과 벽의 가까운 면 선에서 끝나게 그리고 T 접합한다. multiline_tcorner 결과는 반드시 확대 screenshot 으로 확인한다 — 가까운 면이 줄기 폭만큼 끊기고 먼 쪽 면이 연속이어야 하는데, 먼 쪽 면이 끊기거나 줄기가 연장되지 않으면 그 T 는 undo 하고, 벽체선/중심선 레이어에 create_line 으로 면 선을 직접 그린다(통과 벽 가까운 면은 줄기 폭 두 조각, 먼 면은 연속, 줄기 벽선은 가까운 면에서 끝, 중심선은 통과 벽 중심선까지). (4) 그다음 방은 별도 group 의 build_plan 으로 walls:[] 를 주어 벽 없이 문·창·실명·치수만 만든다(문·창이 이미 그려진 벽선을 트림한다). 벽을 그릴 때 set_group 으로 이름표를 붙이고, 끝나면 set_group("") 으로 해제한다. 칸막이 없이 이어지는 공간(예: 거실+주방)은 사이 벽을 아예 그리지 않는다. 코어 외벽에 닿는 접합은 코어가 만든 선이라 끊기지 않는다 — 필요하면 해당 선을 edit_entity/create_line 으로 줄기 폭만큼 끊는다.
[기본 배치] 사용자가 방 배치를 따로 지정하지 않고 32평형 평면도를 그려달라고 하면 build_default_layout 한 번으로 기본 배치(32-test.cdb 를 좌우 반전한 배치: 코어 왼쪽 위·현관=코어 홀 아래·거실 아래 중앙·안방 오른쪽 아래·침실2 왼쪽 아래·침실3/욕실2/드레스룸 오른쪽 위·욕실1 현관 옆·주방&식당 코어 옆으로 거실과 오픈)를 그린다. 벽 접합 규칙은 이미 반영되어 있다. 전체 위치는 origin 으로 옮긴다(도면틀 A2 중앙: [2300,-1100]). 배치를 바꾸라는 요청은 layouts/default_32.json 의 방 사각형·벽 선을 고친 값으로 같은 규칙(위 [평면 벽 구성 절차])에 따라 다시 그린다. 그린 뒤 screenshot 으로 확인하고 면적(㎡·평)을 보고한다.
[개별 도구] create_multiline(벽), create_door, create_window, create_stair, create_elevator 는 세부 조정용. 벽(다중선)을 먼저 그린 뒤 문·창을 내야 개구부가 트림된다.
[도어 종류] create_door(와 방의 doors 항목)의 kind 로 평면도어 탭의 11종을 고른다: single 외여닫이, single113, double 쌍여닫이(width1·width2 로 문1·문2 폭), double114, biswing 양방여닫이, doublebiswing 양방쌍여닫이, revolving 회전문(width=문 크기, wings=날개 수), simple 계열 4종(문틀 없는 단순문). 모든 종류가 같은 hinge/toward/side 입력을 쓴다.
[창 종류] create_window(와 방의 windows 항목)의 kind 와 sashes 로 평면 창문 탭의 7종을 고른다: sliding(기본) 2/3/4미서기(double=이중창), top 2/3/4상미서기, front 전면창.
[치수선 탭 도구] 치수선 탭의 모든 도구를 쓸 수 있다.
  · 전용 도구: create_dimension(수평/수직/정렬·연속·전체 치수), create_circle_dimension(원·호 지름/반지름), merge_dimensions(치수병합), split_dimension(치수분리), create_gdt(기하공차), create_datum(데이텀), edit_gdt(기하편집), edit_entity(치수문자 편집 등 속성 변경), dimension_style(치수 스타일).
  · 나머지는 run_dimension_tool(tool, steps)로 도구를 직접 구동한다. steps 는 {"click":[x,y]}, {"click":[x,y],"button":"right"}, {"pick":객체id}(그 객체를 클릭), {"command":"문자"}(명령창 입력, ""는 엔터) 의 목록이다. 객체 id 는 get_entities 나 create_* 응답의 id. 호출 1회 = 실행 취소 1단계 이상(연속 치수는 개수만큼)이며, 응답의 createdCount/removedCount 와 screenshot 으로 확인한다.
  · 도구별 steps (모두 시험됨; 점은 mm):
    dimlinear_hv 수평/수직: click P1, click P2, click 치수선 위치 (방향은 치수선 위치로 정해진다).
    linearDim 정렬: click P1, click P2, click 치수선 위치.
    radDim 원·호: pick 원/호, click 치수선 위치 (원=지름, 호=반지름이 기본, pick 뒤 command "r"/"d" 로 바꿈). 단순 지름·반지름은 create_circle_dimension 이 간편.
    angDim 각도(P): click 꼭짓점, click 끝점1, click 끝점2, click 문자 위치.
    dimangular 각도(E): pick 선1, pick 선2, click 치수선 위치.
    dimbaseline 기준점: click 기준점, click 다음 점(1개 이상), 오른쪽 click(점 지정 끝), click 치수선 위치.
    dimcontinue 연결치수: click 첫 점, click 다음 점들, 오른쪽 click, click 치수선 위치 (끝에 command "y" 를 주면 전체 치수도 추가).
    autocontinuousdim 연속치수: click 시작점, click 끝점(이 가상선이 선 2개 이상과 교차해야 함), click 치수선 위치 → 교차한 선 사이마다 연속 치수.
    halfdim 대칭치수: click P1, click 중심점, click 치수선 위치.
    leader 지시선: click 화살표 시작점, click 다음 점(들), command "글자", command "" (끝).
    dimlea 치수지시선: pick 치수, click 지시선 끝점 (기존 치수가 바뀐다).
    dimMoveText 치수이동: pick 선형 치수, click 새 문자 위치, command ""(문자 유지) 또는 command "새 문자".
    areadim 영역치수: command ""(Ø 기호 붙임, "n" = 없음), click P1, click P2 (단차 있는 외형선을 감싸는 사각형의 대각 두 점), click 치수선 위치.
    holecallout 구멍 콜아웃: pick 구멍 객체(구멍 도구로 만든 HoleItem, 여러 개 가능), 오른쪽 click(선택 끝), click 지시선 시작점, click 끝점(문자 위치). 구멍 객체가 없으면 동작하지 않는다.
  · 치수병합·분리는 반드시 merge_dimensions(ids, mode merge|total), split_dimension(id, points) 를 쓴다(선형 치수만).
[박스글자] 글자쓰기 탭의 박스글자는 create_box_text 로 만든다(글자 + 감싸는 박스, textId 로 기존 글자에 박스만도 가능). 글자 높이는 textHeight(도면 mm), 레이어는 textLayer/boxLayer.
[해칭] 그리기 탭의 해칭은 create_hatch(inside 점 또는 box, pattern/scale/angle/layer)로 만든다. 패턴 이름은 list_hatch_patterns. 단면·마감 표현은 닫힌 경계를 먼저 그린 뒤 그 안쪽 점을 inside 로 준다.
[레이어] 새 레이어는 create_layer(name, color, weight, linetype)로 만든다. 현재 레이어는 get_layers 로 확인한다.
[한계] 해치 편집(경계 수정·패턴 변경)은 아직 도구가 없다 — 바꾸려면 undo 후 다시 만든다. 없는 기능을 한 것처럼 말하지 말고, 만들지 못한 항목은 보고하라."""

POINT = {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2,
         "description": "[x, y] 도면 좌표 (mm)"}
LAYER = {"type": "string",
         "description": "레이어 이름. 생략하면 현재 레이어. 없는 이름이면 오류와 함께 존재하는 레이어 목록을 돌려줌"}

TOOLS = [
    {
        "name": "ping",
        "description": "AutoDraft 연결 확인. 열려 있는 도면 유무와 객체 수를 돌려준다. 작업 시작 전에 먼저 호출.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "get_layers",
        "description": "현재 도면의 레이어 목록(이름, 색, 선두께, 선종류, 켜짐/잠금, 현재 여부).",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "get_entities",
        "description": ("도면 객체를 조회한다(읽기 전용). 각 객체에 id, type, layer, props(좌표 등), "
                        "bbox[x,y,w,h]가 있다. 결과는 limit(기본 200, 최대 1000)까지만 돌려주므로 "
                        "layer/type 으로 좁히거나 offset 으로 페이지를 넘긴다. 그린 뒤 결과를 검증할 때 사용."),
        "inputSchema": {"type": "object", "properties": {
            "layer": {"type": "string", "description": "이 레이어의 객체만"},
            "group": {"type": "string", "description": "이 그룹 이름표(build_plan/set_group)의 객체만"},
            "type": {"type": "string", "description": "객체 종류 (예: Line, Circle, Text)"},
            "limit": {"type": "integer", "minimum": 1, "maximum": 1000},
            "offset": {"type": "integer", "minimum": 0}}},
    },
    {
        "name": "create_line",
        "description": "선 한 개를 추가한다. 호출 1회 = 실행 취소 1단계. 색·선두께·선종류는 레이어 설정을 따른다.",
        "inputSchema": {"type": "object", "properties": {"p1": POINT, "p2": POINT, "layer": LAYER},
                        "required": ["p1", "p2"]},
    },
    {
        "name": "create_circle",
        "description": "원 한 개를 추가한다. 호출 1회 = 실행 취소 1단계.",
        "inputSchema": {"type": "object", "properties": {
            "center": POINT, "radius": {"type": "number", "exclusiveMinimum": 0, "description": "반지름 (mm)"},
            "layer": LAYER}, "required": ["center", "radius"]},
    },
    {
        "name": "create_multiline",
        "description": ("다중선(2/4/6-Line, 벽 같은 평행선 묶음)을 기존 AutoDraft 다중선 도구로 그린다. "
                        "points 는 중심선 경로의 꼭짓점 목록(2점 이상, 꺾이면 코너 처리됨). 설정창 없이 도면에 "
                        "저장된 Setup 값(간격·레이어·중심선)을 쓰며 spacing 으로 이번 호출에만 선 사이 간격을 바꿀 수 있다. "
                        "호출 1회 = 실행 취소 1단계. 응답의 spacingUsed 로 실제 간격을 확인."),
        "inputSchema": {"type": "object", "properties": {
            "points": {"type": "array", "items": POINT, "minItems": 2,
                       "description": "중심선 꼭짓점 [[x,y],...] (mm). 예: [[0,0],[100,0]]"},
            "lines": {"type": "integer", "enum": [2, 4, 6], "description": "선 개수 (기본 2)"},
            "spacing": {"type": "number", "exclusiveMinimum": 0,
                        "description": "두 바깥선 사이 간격(벽 두께, mm). 생략하면 도면 기본값"}},
            "required": ["points"]},
    },
    {
        "name": "multiline_lcorner",
        "description": ("다중선 탭의 L-코너. 서로 만나는 두 다중선(create_multiline 으로 만든 것)을 코너로 "
                        "연결한다. clickA/clickB 는 각 다중선 위(또는 근처)를 코너 가까이에서 클릭하는 점이며, "
                        "클릭한 쪽 끝이 교점에서 잘려 L 자로 맞물린다(두 선이 서로 연장되어 만난다). "
                        "서로 다른 다중선 그룹이어야 한다. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "clickA": {**POINT, "description": "첫 번째 다중선을 코너 근처에서 클릭하는 점 [x,y] (mm)"},
            "clickB": {**POINT, "description": "두 번째 다중선을 코너 근처에서 클릭하는 점 [x,y] (mm)"}},
            "required": ["clickA", "clickB"]},
    },
    {
        "name": "multiline_tcorner",
        "description": ("다중선 탭의 레이캐스트 T-코너(합쳐진 T). clickA = 그대로 통과하는 다중선(교차선, 끊기지 않는 직선 하나), "
                        "clickB = 끝이 잘려 교차선에 T 자로 붙는 다중선(줄기선, 자를 끝 쪽 근처 클릭). 줄기 선은 교차선의 가까운 면 선에서 "
                        "트림되고, 교차선의 가까운 면 선은 줄기 폭만큼 분할(제거)된다(먼 쪽 선은 연속). 서로 다른 다중선 그룹이어야 한다. "
                        "호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "clickA": {**POINT, "description": "교차선(통과하는 다중선) 위의 점 [x,y] (mm)"},
            "clickB": {**POINT, "description": "줄기선(끝이 잘리는 다중선)의 자를 끝 근처 점 [x,y] (mm)"}},
            "required": ["clickA", "clickB"]},
    },
    {
        "name": "multiline_ccorner",
        "description": ("다중선 탭의 C(십자)-코너. 서로 교차(+ 형태)하는 두 다중선을 교점에서 분할해 "
                        "십자로 정리한다(같은 역할의 선끼리 교점에서 잘려 조각이 새로 생길 수 있다). "
                        "clickA/clickB 는 각 다중선 위(또는 근처)를 교차 근처에서 클릭하는 점. "
                        "서로 다른 다중선 그룹이어야 한다. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "clickA": {**POINT, "description": "첫 번째 다중선을 교차 근처에서 클릭하는 점 [x,y] (mm)"},
            "clickB": {**POINT, "description": "두 번째 다중선을 교차 근처에서 클릭하는 점 [x,y] (mm)"}},
            "required": ["clickA", "clickB"]},
    },
    {
        "name": "create_stair",
        "description": ("계단평면(UP/DN 표시, 디딤판, 계단참, 미끄럼방지선)을 기존 AutoDraft 계단 도구로 그린다. "
                        "p1(UP)·p2(DN)는 계단참 쪽 벽선의 두 모서리, toward 는 계단이 뻗어 나갈 쪽에 있는 한 점이다 "
                        "(p1-p2 선에서 떨어진 점). 설정창 없이 도면에 저장된 기본값을 쓰고, 아래 값을 주면 이번 호출에만 덮어쓴다. "
                        "up/down 이 둘 다 1 이상이면 꺾임(U자) 계단, 한쪽이 0 이면 직진 계단. "
                        "응답의 paramsUsed 로 실제 사용된 값을 확인. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "p1": POINT, "p2": POINT, "toward": POINT,
            "up": {"type": "integer", "minimum": 0, "description": "UP 계단 수"},
            "down": {"type": "integer", "minimum": 0, "description": "DN 계단 수"},
            "tread": {"type": "number", "exclusiveMinimum": 0, "description": "디딤판 폭 (mm)"},
            "treadLen": {"type": "number", "minimum": 0, "description": "디딤판 길이 (mm), 0 = 자동(꺾임 계단)"},
            "land": {"type": "number", "minimum": 0, "description": "계단참 길이 (mm)"},
            "type": {"type": "integer", "enum": [1, 2], "description": "1 = 기본, 2 = 계단참에도 중간계단"},
            "midSteps": {"type": "integer", "minimum": 0, "description": "type 2 의 중간계단 수"},
            "numbering": {"type": "boolean", "description": "계단 번호 표시"},
            "nonslip": {"type": "boolean", "description": "미끄럼방지선"},
            "layer": LAYER, "nonslipLayer": {"type": "string", "description": "미끄럼방지선 레이어"}},
            "required": ["p1", "p2", "toward"]},
    },
    {
        "name": "create_elevator",
        "description": ("엘리베이터 평면(승강로 벽선·카·문·균형추)을 기존 AutoDraft 엘리베이터 도구로 그린다. "
                        "p1 은 승강로 문 쪽 모서리, p2 는 대각 모서리(x, y 모두 달라야 함). p1 이 있는 쪽 벽에 문이 생긴다. "
                        "persons(인승)를 주면 내장 규격 데이터에서 카 폭·깊이·문 크기를 가져오고, carWidth/carDepth/doorSize 로 "
                        "직접 지정할 수도 있다. 승강로가 작아 카가 들어가지 않으면 이유와 함께 오류를 돌려주고 그리지 않는다. "
                        "응답의 paramsUsed 로 실제 사용된 값을 확인. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "p1": POINT, "p2": POINT,
            "persons": {"type": "integer", "minimum": 1, "description": "인승 (규격 데이터에 있는 값, 예: 8, 9, 10 ...)"},
            "maker": {"type": "string", "description": "제조사/제품 이름 일부. 생략하면 기본 규격"},
            "carWidth": {"type": "number", "exclusiveMinimum": 0, "description": "카 폭 (mm)"},
            "carDepth": {"type": "number", "exclusiveMinimum": 0, "description": "카 깊이 (mm)"},
            "doorSize": {"type": "number", "exclusiveMinimum": 0, "description": "문 크기 (mm)"},
            "drawWalls": {"type": "boolean", "description": "승강로 벽선(W1) 그리기"},
            "drawDims": {"type": "boolean", "description": "치수선 그리기"}},
            "required": ["p1", "p2"]},
    },
    {
        "name": "create_door",
        "description": ("평면 도어(문틀, 문짝, 열림 호)를 기존 AutoDraft '평면도어' 탭의 도어 도구로 그린다. 종류는 kind 로 고른다: "
                        "single 외여닫이(기본), single113 외여닫이-113(30도 열린 문짝), double 쌍여닫이, double114 쌍여닫이-114, "
                        "biswing 양방여닫이(경첩이 벽 중심, 180도 호), doublebiswing 양방쌍여닫이, revolving 회전문, "
                        "simple/simpledouble/simplebiswing/simpledoublebiswing 단순문(문틀·프레임 없이 문짝선+호만). "
                        "hinge 는 벽면 위 경첩 쪽 시작점, "
                        "toward 는 벽을 따라 문이 뻗는 방향의 점, side 는 벽 두께와 문이 열리는 쪽(진행 방향 기준 left/right). "
                        "벽이 이미 다중선으로 그려져 있으면 도구가 개구부 트림과 마감선까지 처리한다(벽 먼저 그릴 것). "
                        "설정창 없이 도면 기본값을 쓰고 width 로 문 폭만 이번 호출에 덮어쓸 수 있다. "
                        "응답의 paramsUsed 로 실제 값을 확인. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "hinge": POINT, "toward": POINT,
            "side": {"type": "string", "enum": ["left", "right"],
                     "description": "진행 방향(hinge→toward) 기준 벽 두께와 열림이 향하는 쪽. 기본 left"},
            "wallThickness": {"type": "number", "minimum": 10,
                              "description": "벽 두께 (mm). 생략하면 다중선 기본 간격"},
            "width": {"type": "number", "minimum": 100,
                      "description": "문 전체 폭 (mm). 생략하면 도면 기본값. 쌍여닫이류는 문1=문2=폭/2 로 나뉜다(회전문은 문 크기, 기본 2400)"},
            "kind": {"type": "string",
                     "enum": ["single", "single113", "double", "double114", "biswing", "doublebiswing", "revolving",
                              "simple", "simpledouble", "simplebiswing", "simpledoublebiswing"],
                     "description": "평면도어 탭의 도어 종류 (기본 single)"},
            "width1": {"type": "number", "minimum": 100,
                       "description": "문1(hinge 쪽) 폭 (mm). double, double114, doublebiswing 에서만. width 와 함께 쓰지 말 것"},
            "width2": {"type": "number", "minimum": 100,
                       "description": "문2 폭 (mm). double, double114, doublebiswing 에서만. width 와 함께 쓰지 말 것"},
            "wings": {"type": "integer", "enum": [3, 4], "description": "회전문(revolving) 날개 수 (기본 도면 설정)"},
            "style": {"type": "integer", "enum": [0, 1],
                      "description": "kind 대신 쓰는 옛 방식. 1 = single→single113, double→double114"}},
            "required": ["hinge", "toward"]},
    },
    {
        "name": "create_window",
        "description": ("평면 창문(미서기창)을 기존 AutoDraft 창문 도구로 그린다. 입력은 create_door 와 같다: hinge 는 벽면 위 시작점, "
                        "toward 는 벽을 따라 진행하는 방향의 점, side 는 벽 두께 쪽. 벽(다중선)을 먼저 그려 두면 개구부 트림까지 처리된다. "
                        "평면 창문 탭의 7종을 kind 와 sashes 로 고른다: kind=sliding(기본) 2/3/4미서기, kind=top 2/3/4상미서기(상단부만), "
                        "kind=front 전면창. sashes 로 2/3/4짝, double 로 단창/이중창(sliding 만). 설정창 없이 도면 기본값을 쓰고 width 만 덮어쓸 수 있다. "
                        "응답의 paramsUsed 로 실제 값을 확인. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "hinge": POINT, "toward": POINT,
            "side": {"type": "string", "enum": ["left", "right"],
                     "description": "진행 방향(hinge→toward) 기준 벽 두께와 열림이 향하는 쪽. 기본 left"},
            "wallThickness": {"type": "number", "minimum": 10,
                              "description": "벽 두께 (mm). 생략하면 다중선 기본 간격"},
            "width": {"type": "number", "minimum": 100, "description": "창 폭 (mm). 생략하면 도면 기본값"},
            "kind": {"type": "string", "enum": ["sliding", "top", "front"],
                     "description": "sliding = 미서기(기본), top = 상미서기(상단부만), front = 전면창(2짝 기준)"},
            "sashes": {"type": "integer", "enum": [2, 3, 4], "description": "창짝 수 (기본 2). front 에서는 지정하지 않는다"},
            "double": {"type": "boolean", "description": "true = 이중창, false = 단창. 생략하면 도면 기본값. kind=sliding 에서만"}},
            "required": ["hinge", "toward"]},
    },
    {
        "name": "screenshot",
        "description": ("도면을 PNG 이미지로 저장하고 파일 경로를 돌려준다(Y 위쪽이 +, 어두운 배경). 결과를 눈으로 확인할 때 사용 — "
                        "돌려준 path 의 이미지를 읽어 모양을 점검한다. region [x,y,폭,높이] 로 영역을 지정하고, 생략하면 도면 전체."),
        "inputSchema": {"type": "object", "properties": {
            "region": {"type": "array", "items": {"type": "number"}, "minItems": 4, "maxItems": 4,
                       "description": "보고 싶은 영역 [x, y, 폭, 높이] (mm). 왼쪽 아래 기준"},
            "widthPx": {"type": "integer", "minimum": 200, "maximum": 3000, "description": "이미지 가로 픽셀 (기본 1400)"},
            "caption": {"type": "string", "description": "이미지에 넣을 설명 자막 (영상·보고용, 선택)"},
            "captionPos": {"type": "string", "enum": ["bottom", "top"], "description": "자막 위치 (기본 bottom)"}}},
    },
    {
        "name": "create_room",
        "description": ("사각형 방 하나를 만든다: 벽(2-Line 다중선, 모서리 처리됨) → 문·창(개구부 트림) → 실명 문자. 호출 1회 = 실행 취소 1단계. "
                        "origin 은 사각형의 남서쪽 모서리, width 는 동서, depth 는 남북 길이(mm). dimensions 가 'centerline'(기본)이면 벽 중심선 "
                        "기준, 'interior'면 내부 유효 치수 기준. 이웃 방과 벽을 공유하면 walls 로 이 방이 직접 그릴 변만 지정한다(S남,E동,N북,W서). "
                        "doors/windows 의 side 는 S/E/N/W, offset 은 그 변의 서쪽(또는 남쪽) 모서리 중심선에서 개구부 시작까지의 거리(mm). "
                        "다른 방이 그린 벽에 문을 내려면 그 방을 먼저 만들 것. 응답의 room.interiorRect 로 내부 실제 크기, warnings 로 실패한 개구부를 확인."),
        "inputSchema": {"type": "object", "properties": {
            "origin": POINT,
            "width": {"type": "number", "minimum": 500, "description": "동서 방향 길이 (mm)"},
            "depth": {"type": "number", "minimum": 500, "description": "남북 방향 길이 (mm)"},
            "dimensions": {"type": "string", "enum": ["centerline", "interior"], "description": "치수 기준 (기본 centerline)"},
            "wallThickness": {"type": "number", "minimum": 10, "description": "벽 두께 (mm). 생략하면 다중선 기본 간격"},
            "walls": {"type": "array", "items": {"type": "string", "enum": ["S", "E", "N", "W"]},
                      "description": "이 방이 그릴 변. 생략하면 4면 모두"},
            "name": {"type": "string", "description": "실명 문자 (예: 거실). 기본은 방 중앙에 표시"},
            "nameAt": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2,
                       "description": "실명 문자 위치 [x, y] (mm). 가구와 겹치지 않게 옮길 때 사용"},
            "textHeight": {"type": "number", "exclusiveMinimum": 0, "description": "실명 글자 높이 (mm, 기본 200)"},
            "doors": {"type": "array", "items": {"type": "object", "properties": {
                "side": {"type": "string", "enum": ["S", "E", "N", "W"]},
                "offset": {"type": "number", "minimum": 0},
                "width": {"type": "number", "minimum": 100, "description": "문 폭 (기본 900)"},
                "swing": {"type": "string", "enum": ["in", "out"], "description": "열리는 쪽 (방 안/밖, 기본 in)"},
                "hingeAt": {"type": "string", "enum": ["start", "end"], "description": "경첩 위치 (기본 start = 서/남쪽 끝)"},
                "kind": {"type": "string",
                         "enum": ["single", "single113", "double", "double114", "biswing", "doublebiswing", "revolving",
                                  "simple", "simpledouble", "simplebiswing", "simpledoublebiswing"],
                         "description": "도어 종류 (기본 single). create_door 설명 참고"},
                "width1": {"type": "number", "minimum": 100, "description": "문1 폭 (쌍여닫이류만)"},
                "width2": {"type": "number", "minimum": 100, "description": "문2 폭 (쌍여닫이류만)"},
                "wings": {"type": "integer", "enum": [3, 4], "description": "회전문 날개 수"},
                "style": {"type": "integer", "enum": [0, 1]}}, "required": ["side", "offset"]}},
            "windows": {"type": "array", "items": {"type": "object", "properties": {
                "side": {"type": "string", "enum": ["S", "E", "N", "W"]},
                "offset": {"type": "number", "minimum": 0},
                "width": {"type": "number", "minimum": 100, "description": "창 폭 (기본 1200)"},
                "kind": {"type": "string", "enum": ["sliding", "top", "front"],
                         "description": "창 종류 (기본 sliding 미서기). create_window 설명 참고"},
                "sashes": {"type": "integer", "enum": [2, 3, 4]},
                "double": {"type": "boolean"}}, "required": ["side", "offset"]}}},
            "required": ["origin", "width", "depth"]},
    },
    {
        "name": "set_drawing_scale",
        "description": ("도면 축척을 분모로 설정한다(60 = 1:60). 계단 UP/DN 문자 같은 축척 비례 요소의 크기가 이 값에 따라 정해지므로 "
                        "새 도면에서 계단·치수를 그리기 전에 설정할 것. 현재 값은 ping 의 drawingScale 로 확인."),
        "inputSchema": {"type": "object", "properties": {
            "scale": {"type": "number", "minimum": 1, "maximum": 10000, "description": "축척 분모 (예: 1/60 도면 → 60)"}},
            "required": ["scale"]},
    },
    {
        "name": "create_core",
        "description": ("건물 코어(계단실/홀 + 엘리베이터)를 한 번에 만든다. 서쪽에 U자 계단, 동쪽 북쪽에 승강로(문이 홀 쪽), "
                        "나머지는 칸막이벽 없이 계단실과 홀이 한 공간이다(실명은 \"계단실/홀\" 하나, UP/DN 글자 아래). "
                        "바깥 둘레와 승강로 벽(2-Line)은 L 코너·T 접합 규칙으로 그려지고(승강로 입구 벽은 왼쪽 L, 오른쪽 T), 홀 출입문, 계단, 엘리베이터, 실명까지 생성하고 "
                        "호출 1회 = 실행 취소 1단계. origin 은 계단실 내부의 남서쪽 모서리. 모든 치수는 내부 유효 치수. "
                        "계단은 기본 디딤판 291·계단참 700(UP/DN 연결선이 북쪽 벽에서 약 400 떨어짐)이고, 승강로는 인승 규격(persons)에서 정해지며 안 들어가면 자동으로 키운다. "
                        "응답의 core 로 각 공간의 내부 사각형(stairRoomInterior, hallInterior, shaftInterior)과 바깥 경계(outerBounds)를 확인하고 "
                        "warnings 로 실패한 부분을 확인. 도면 축척(set_drawing_scale)을 먼저 설정할 것."),
        "inputSchema": {"type": "object", "properties": {
            "origin": POINT,
            "persons": {"type": "integer", "minimum": 1, "description": "엘리베이터 인승 (기본 8)"},
            "maker": {"type": "string", "description": "엘리베이터 제조사/제품 이름 일부 (생략 시 기본)"},
            "wallThickness": {"type": "number", "minimum": 10, "description": "벽 두께 (mm). 생략하면 다중선 기본 간격"},
            "stairWidth": {"type": "number", "minimum": 1500, "description": "계단실 내부 폭 (mm, 기본 2400)"},
            "up": {"type": "integer", "minimum": 1, "description": "UP 계단 수"},
            "down": {"type": "integer", "minimum": 1, "description": "DN 계단 수"},
            "tread": {"type": "number", "exclusiveMinimum": 0, "description": "디딤판 폭 (mm, 기본 291)"},
            "land": {"type": "number", "minimum": 0, "description": "계단참 길이 (mm, 기본 700)"},
            "hallDepth": {"type": "number", "minimum": 1500, "description": "홀 내부 깊이 최소값 (mm, 기본 2400). 계단실이 더 깊으면 홀이 늘어남"},
            "hallDoor": {"type": "string", "enum": ["E", "S", "none"], "description": "홀 출입문 위치 (기본 E = 동쪽 벽)"},
            "doorWidth": {"type": "number", "minimum": 600, "description": "문 폭 (mm, 기본 900)"},
            "stairName": {"type": "string", "description": "계단실/홀 실명 (기본 \"계단실/홀\", 빈 문자열이면 생략)"},
            "hallName": {"type": "string", "description": "홀 실명을 따로 넣을 때만 (기본 생략)"},
            "textHeight": {"type": "number", "exclusiveMinimum": 0, "description": "실명 글자 높이 (mm, 기본 200)"},
            "drawDims": {"type": "boolean", "description": "엘리베이터 치수선"}},
            "required": ["origin"]},
    },
    {
        "name": "list_blocks",
        "description": ("블록 라이브러리(가구·싱크대·화장실·수목 등)를 조회한다. category 를 생략하면 분류 목록(furniture, sink, toilet, tree, elevator, etc)과 "
                        "개수를 돌려주고, category 를 주면 그 분류의 블록 이름·크기(sizeMm)·삽입점 기준 범위와 **미리보기 모음 이미지**를 함께 돌려준다. "
                        "파일 이름(ft1, tp04 등)만으로는 무엇인지 알 수 없으므로 이미지의 이름·크기로 확인한 뒤 insert_block 으로 삽입한다. 크기는 선 두께가 포함된 근사값."),
        "inputSchema": {"type": "object", "properties": {
            "category": {"type": "string", "description": "분류 이름 (예: furniture). 생략하면 분류 목록"}}},
    },
    {
        "name": "insert_block",
        "description": ("블록 라이브러리의 블록 하나를 도면에 삽입한다. at 은 삽입점(블록 원점), rotation 은 도(반시계, 기본 0), scale 은 배율(기본 1). "
                        "layer 를 지정하면 그 레이어에 넣고(없으면 만든다. 가구는 FUR 권장), 생략하면 블록 원래 레이어를 쓴다. "
                        "블록의 어느 쪽이 앞인지·원점 위치는 list_blocks 의 이미지와 bboxFromInsertPoint 로 확인하고, 삽입 후 응답의 placedBounds 와 screenshot 으로 위치를 점검한다. "
                        "속성 입력창은 띄우지 않고 기본값을 쓴다. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "category": {"type": "string", "description": "분류 (예: furniture, sink, toilet)"},
            "name": {"type": "string", "description": "블록 이름 (list_blocks 의 name, 예: ft1)"},
            "at": POINT,
            "rotation": {"type": "number", "description": "회전 각도 (도, 반시계). 기본 0"},
            "scale": {"type": "number", "exclusiveMinimum": 0, "maximum": 100, "description": "배율. 기본 1"},
            "layer": {"type": "string", "description": "삽입 레이어 (없으면 생성). 예: FUR"}},
            "required": ["category", "name", "at"]},
    },
    {
        "name": "create_dimension",
        "description": ("선형 치수와 연속 치수를 만든다. points 에 측정할 점들을 주면(2점 이상) 이웃한 점끼리 치수가 만들어지고 같은 치수선 위에 놓인다 "
                        "(3점 이상이면 연속 치수). orientation: horizontal = 가로 길이를 재며 lineAt 은 치수선의 y 좌표, vertical = 세로 길이를 재며 lineAt 은 치수선의 x 좌표, "
                        "aligned = 두 점과 평행하게 offset(수직 거리, 부호로 쪽 선택), auto = 점 배열로 판단. overallAt 을 주면 처음~끝 점의 전체 치수를 그 좌표의 치수선에 "
                        "한 줄 더 만든다(보통 개별 치수선 바깥쪽). 레이어는 layer > 도면 치수 스타일 > DIM(없으면 생성). "
                        "치수 문자 크기는 도면 치수 스타일(축척에 비례)을 따르므로 set_drawing_scale 이후에 만들고 screenshot 으로 확인할 것. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "points": {"type": "array", "items": POINT, "minItems": 2,
                       "description": "측정 점들 [[x,y],...] (mm). 기본은 벽 중심선 좌표(중심선↔중심선 거리). 예: 방 중심선 사각형의 모서리"},
            "orientation": {"type": "string", "enum": ["auto", "horizontal", "vertical", "aligned"], "description": "기본 auto"},
            "lineAt": {"type": "number", "description": "horizontal 이면 치수선의 y, vertical 이면 치수선의 x (mm)"},
            "offset": {"type": "number", "description": "aligned 의 치수선 수직 거리 (mm, 기본 600)"},
            "overallAt": {"type": "number", "description": "전체 치수선의 y(horizontal) 또는 x(vertical). 3점 이상일 때만"},
            "layer": {"type": "string", "description": "치수 레이어 (기본 DIM)"}},
            "required": ["points"]},
    },
    {
        "name": "create_circle_dimension",
        "description": ("원 또는 호의 지름(Ø)·반지름(R) 치수를 만든다. 대상은 circleId(create_circle 응답의 entity.id 또는 get_entities 의 id, 원/호) "
                        "또는 center+radius(원으로 취급)로 준다. kind: diameter(지름, 원의 기본) | radius(반지름, 호의 기본). "
                        "치수선 위치는 at([x,y])로 직접 정하거나, 생략하면 angle(도, 기본 45)과 inside(기본 false = 원 바깥으로 빼낸 지시선, true = 원 안쪽)로 정한다. "
                        "바깥 지시선 길이는 gap(mm, 기본 반지름의 절반, 최소 500). 레이어는 layer > 도면 치수 스타일 > DIM. "
                        "다른 객체·글자와 겹치면 angle 을 바꿔 다시 만들고, screenshot 으로 확인할 것. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "circleId": {"type": "integer", "description": "원/호 객체 id"},
            "center": POINT,
            "radius": {"type": "number", "exclusiveMinimum": 0, "description": "center 와 함께 줄 때 반지름 (mm)"},
            "kind": {"type": "string", "enum": ["diameter", "radius"], "description": "지름/반지름 (원 기본 diameter, 호 기본 radius)"},
            "at": POINT,
            "angle": {"type": "number", "description": "치수선 방향 각도 (도, 동쪽 0 · 반시계, 기본 45)"},
            "inside": {"type": "boolean", "description": "true 면 원 안쪽에 표시 (기본 false)"},
            "gap": {"type": "number", "exclusiveMinimum": 0, "description": "바깥 지시선 길이 (mm)"},
            "layer": {"type": "string", "description": "치수 레이어 (기본 도면 치수 스타일, 없으면 DIM)"}}},
    },
    {
        "name": "run_dimension_tool",
        "description": ("치수선 탭의 도구를 기존 AutoDraft 도구 그대로 구동한다(클릭 순서를 주입). tool 은 dimlinear_hv(수평/수직), linearDim(정렬), radDim(원·호), "
                        "angDim(각도 P), dimangular(각도 E), dimbaseline(기준점), dimcontinue(연결치수), autocontinuousdim(연속치수), halfdim(대칭치수), leader(지시선), "
                        "holecallout(구멍 콜아웃), areadim(영역치수), dimlea(치수지시선), dimMoveText(치수이동), dimMerge, dimSplit. 도구별 steps 순서는 사용 규칙의 [치수선 탭 도구]를 따른다. "
                        "steps 항목: click [x,y](왼쪽 클릭, button:\"right\" 이면 오른쪽), pick 객체id(그 객체 위를 클릭 — 도구의 객체 선택 단계), move [x,y], command 문자(명령창 입력, \"\"는 엔터). "
                        "스냅은 꺼지므로 준 좌표가 그대로 쓰인다. 응답: 만든 객체(entities), 지워진 객체(removedIds), undoSteps. 아무것도 안 바뀌면 오류로 알려준다."),
        "inputSchema": {"type": "object", "properties": {
            "tool": {"type": "string", "enum": ["dimlinear_hv", "linearDim", "radDim", "angDim", "dimangular", "dimbaseline", "dimcontinue", "autocontinuousdim",
                                                "halfdim", "leader", "holecallout", "areadim", "dimlea", "dimMoveText", "dimMerge", "dimSplit"]},
            "steps": {"type": "array", "minItems": 1, "items": {"type": "object", "properties": {
                "click": POINT, "move": POINT,
                "pick": {"type": "integer", "description": "클릭할 객체 id"},
                "command": {"type": "string", "description": "명령창에 입력할 문자 (\"\"는 엔터)"},
                "button": {"type": "string", "enum": ["left", "right"], "description": "click/pick 의 마우스 버튼 (기본 left)"}}}}},
            "required": ["tool", "steps"]},
    },
    {
        "name": "merge_dimensions",
        "description": ("치수병합. mode merge(기본): 같은 방향의 선형 치수 2개를 바깥 두 점 기준의 하나로 합친다(원래 두 치수와 그 사이에 낀 같은 높이 치수는 삭제). "
                        "mode total: 선형 치수 2개 이상을 감싸는 전체(토탈) 치수를 한 줄 바깥에 추가하고 기존 치수는 유지(겹치는 보조선은 숨김). ids 는 선형 치수의 객체 id. "
                        "응답의 removedIds/entities 로 결과를 확인. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "ids": {"type": "array", "items": {"type": "integer"}, "minItems": 2, "description": "병합할 선형 치수 id (merge 는 정확히 2개)"},
            "mode": {"type": "string", "enum": ["merge", "total"], "description": "merge = 2개를 1개로, total = 전체 치수 추가 (기본 merge)"}},
            "required": ["ids"]},
    },
    {
        "name": "split_dimension",
        "description": ("치수분리. 선형 치수 하나를 points 위치에서 여러 개로 나눈다(보조선이 그 점에 놓임). points 는 치수 범위 안쪽(양 끝 제외)의 대상물 위 점 [x,y] 이며 "
                        "치수 방향 좌표만 달라도 된다. keepOriginal true 면 원래 치수(토탈)를 남기고 분리된 치수를 한 칸 안쪽에 추가, 기본은 원래 치수를 삭제. "
                        "응답의 removedIds/entities 로 확인. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "id": {"type": "integer", "description": "분리할 선형 치수 id"},
            "points": {"type": "array", "items": POINT, "minItems": 1, "description": "나눌 위치 [[x,y],...]"},
            "keepOriginal": {"type": "boolean", "description": "원래 치수 보존 (기본 false)"}},
            "required": ["id", "points"]},
    },
    {
        "name": "create_gdt",
        "description": ("기하공차(GD&T) 프레임을 창 없이 만든다. symbol: Straightness 직진도, Flatness 평면도, Circularity 진원도, Cylindricity 원통도, LineProfile 선의 윤곽도, "
                        "SurfaceProfile 면의 윤곽도, Parallelism 평행도, Perpendicularity 직각도, Angularity 경사도, Position 위치도, Concentricity 동축도, Symmetry 대칭도, "
                        "CircularRunout 원주 흔들림, TotalRunout 전체 흔들림(또는 0~13). tolerance 는 공차값 문자(예 \"0.05\"), diameter true 면 Ø 접두, datums 는 최대 3개(A,B,C). "
                        "위치는 지시선 없이 프레임만 at([x,y], 프레임 왼쪽 중앙)이거나, 지시선 꼭짓점 points(2점 이상, 마지막 점에 프레임). 레이어는 layer > 치수 스타일 > DIM. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "symbol": {"type": "string", "description": "기하 특성 이름 (예: Position)"},
            "tolerance": {"type": "string", "description": "공차값 (예: 0.05)"},
            "diameter": {"type": "boolean", "description": "Ø 접두어"},
            "datums": {"type": "array", "items": {"type": "string"}, "maxItems": 3, "description": "데이텀 참조 (A, B, C)"},
            "textHeight": {"type": "number", "exclusiveMinimum": 0, "description": "문자 높이 (기본 치수 스타일 문자 크기)"},
            "at": POINT,
            "points": {"type": "array", "items": POINT, "minItems": 2, "description": "지시선 꼭짓점 (마지막 점에 프레임)"},
            "layer": {"type": "string", "description": "레이어 (기본 치수 스타일 > DIM)"}},
            "required": ["symbol"]},
    },
    {
        "name": "create_datum",
        "description": ("데이텀 기호(삼각형 + 문자 박스)를 창 없이 만든다. start 는 삼각형이 붙는 점, end 는 문자 박스 중심, text 는 데이텀 문자(A, B ...). 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "text": {"type": "string", "description": "데이텀 문자 (예: A)"},
            "start": POINT, "end": POINT,
            "textHeight": {"type": "number", "exclusiveMinimum": 0},
            "layer": {"type": "string"}},
            "required": ["text", "start", "end"]},
    },
    {
        "name": "edit_gdt",
        "description": ("기하공차 프레임 또는 데이텀을 편집한다(기하편집 도구의 창 없는 버전). 바꿀 항목만 주면 나머지(위치·지시선·레이어)는 유지하고 객체를 교체한다. "
                        "프레임: symbol, tolerance, diameter, datums, textHeight. 데이텀: text, textHeight. 응답의 entity.id 는 새 id. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "id": {"type": "integer", "description": "기하공차/데이텀 객체 id"},
            "symbol": {"type": "string"}, "tolerance": {"type": "string"}, "diameter": {"type": "boolean"},
            "datums": {"type": "array", "items": {"type": "string"}, "maxItems": 3},
            "text": {"type": "string", "description": "데이텀 문자"},
            "textHeight": {"type": "number", "exclusiveMinimum": 0}},
            "required": ["id"]},
    },
    {
        "name": "edit_entity",
        "description": ("객체 속성을 바꾼다(치수편집 도구의 창 없는 버전). properties 에 get_entities 의 props 이름과 새 값을 준다. 치수 문자·공차: "
                        "Content(문자 덮어쓰기, \"\"이면 자동 측정값), Prefix, Suffix, TolType(0 없음 1 대칭 2 편차 3 한계 4 끼워맞춤), TolUpper, TolLower, TolPrecision, "
                        "FitCode(예 H7), Precision 등. 없는 속성이면 사용 가능한 목록을 알려준다. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "id": {"type": "integer", "description": "객체 id"},
            "properties": {"type": "object", "description": "바꿀 속성 {이름: 값}"}},
            "required": ["id", "properties"]},
    },
    {
        "name": "dimension_style",
        "description": ("치수 스타일(치수설정 창의 값)을 조회하거나 바꾼다. style 을 생략하면 현재 값을 돌려주고, 주면 바꿀 항목만 반영한다(기존 치수에도 적용). "
                        "항목: arrowSize, arrowStyle(0~5 또는 filled/open/filledCircle/openCircle/doubleCircle/hollow), textSize, toleranceSize, textGap, extOffset, extExtend, "
                        "precision, tolPrecision, suppressTrailingZeros, suppressTrailingZerosTol, useThousandSep, baselineSpacing, dimScale, dimLineLayer, dimTextLayer, fontFamily. "
                        "크기 값은 도면 축척 1:1 기준 mm 이고 실제 크기는 축척에 비례한다. 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "style": {"type": "object", "description": "바꿀 항목 {이름: 값}. 생략하면 조회"}}},
    },
    {
        "name": "create_box_text",
        "description": ("박스글자(글자쓰기 탭): 글자를 쓰고 그 글자를 감싸는 사각 박스(닫힌 폴리선)를 함께 만든다. 새 글자는 text + at(글자 시작점, 왼쪽 아래 기준) 로, "
                        "이미 있는 글자에 박스만 만들려면 textId(get_entities type:\"Text\" 의 id)를 준다. textHeight 는 도면상 글자 높이(mm), 생략하면 현재 글꼴 크기 × 도면 축척. "
                        "font/bold/italic/rotation 은 선택. 레이어는 textLayer(글자)·boxLayer(박스) > 박스글자 도구의 [Setup] 설정 > 앱 글자 레이어/글자와 같은 레이어 순서로 정해지며, "
                        "textId 와 textLayer 를 함께 주면 그 글자를 그 레이어로 옮긴다. 박스 크기는 도구와 같은 계산(글자 폭·높이 + 위아래 여유)이라 이후 글자를 편집하면 박스가 따라간다. "
                        "응답의 boxCorners 로 박스 좌표를 알 수 있다(다른 도형·치수 배치에 쓰기). 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "text": {"type": "string", "description": "글자 내용 (새 글자일 때)"},
            "at": POINT,
            "textId": {"type": "integer", "description": "박스만 만들 기존 글자 객체 id"},
            "textHeight": {"type": "number", "exclusiveMinimum": 0, "description": "도면상 글자 높이 (mm)"},
            "font": {"type": "string", "description": "글꼴 이름 (기본 현재 글꼴)"},
            "bold": {"type": "boolean"}, "italic": {"type": "boolean"},
            "rotation": {"type": "number", "description": "글자 회전 각도 (도, 반시계)"},
            "textLayer": {"type": "string", "description": "글자 레이어 (도면에 있는 이름)"},
            "boxLayer": {"type": "string", "description": "박스 레이어 (도면에 있는 이름)"}}},
    },
    {
        "name": "create_hatch",
        "description": ("해칭(그리기 탭): 닫힌 영역을 패턴으로 채운다. inside([x,y]) 에 영역 안쪽 점을 주면 그 점을 둘러싼 닫힌 경계(선·도형으로 이루어진 영역)를 자동으로 찾아 채우고(Select 모드), "
                        "box([x,y,폭,높이]) 를 주면 그 사각 안의 객체로 경계를 만들어 채운다(Box 모드). 둘 중 하나만. inside 는 점 주변 ±searchRadius(기본 15000mm) 안의 객체를 경계 후보로 쓰며 "
                        "region([x,y,폭,높이])으로 후보 범위를 직접 정할 수도 있다. pattern 은 list_hatch_patterns 의 이름(기본 ANSI31, SOLID = 꽉 채움), scale 은 패턴 간격 배율(도면 축척이 크면 큰 값), "
                        "angle 은 패턴 회전(도). 생략하면 해칭 도구의 마지막 설정을 쓴다. layer 는 해치 레이어(기본 도구 설정, 없으면 현재 레이어). "
                        "경계가 끊겨 있으면 오류로 알려준다. 만든 뒤 screenshot 으로 패턴이 보이는지 확인하고 안 보이거나 너무 촘촘하면 scale 을 바꿔 다시 만든다(undo 후). 호출 1회 = 실행 취소 1단계."),
        "inputSchema": {"type": "object", "properties": {
            "inside": POINT,
            "box": {"type": "array", "items": {"type": "number"}, "minItems": 4, "maxItems": 4, "description": "[x, y, 폭, 높이] 사각 영역"},
            "region": {"type": "array", "items": {"type": "number"}, "minItems": 4, "maxItems": 4, "description": "inside 경계 후보를 찾을 범위 [x, y, 폭, 높이]"},
            "searchRadius": {"type": "number", "exclusiveMinimum": 0, "description": "inside 주변 후보 범위 반경 (mm, 기본 15000)"},
            "pattern": {"type": "string", "description": "패턴 이름 (예: ANSI31, SOLID)"},
            "scale": {"type": "number", "exclusiveMinimum": 0, "description": "패턴 간격 배율"},
            "angle": {"type": "number", "description": "패턴 회전 각도 (도)"},
            "layer": {"type": "string", "description": "해치 레이어"}}},
    },
    {
        "name": "list_hatch_patterns",
        "description": "해칭에 쓸 수 있는 패턴 이름(과 설명) 목록을 돌려준다. SOLID 는 꽉 채움.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "create_layer",
        "description": ("레이어 도크의 [만들기]: 새 레이어를 만든다. name(이름, 필수·중복 불가), color(\"#rrggbb\" 또는 red/yellow/cyan 같은 색 이름, 기본 흰색), "
                        "weight(선두께 mm, 도면 선두께 목록의 값만, 기본 0.25), linetype(등록된 선종류 이름, 기본 Continuous; 없는 이름이면 등록된 목록을 알려줌), "
                        "makeCurrent true 면 만든 레이어를 현재 레이어로 설정. 만든 뒤 create_* 도구의 layer 인자로 이름을 쓰면 된다. "
                        "이미 있는 이름이면 오류와 함께 그 레이어의 설정을 알려준다. 레이어 추가는 실행 취소(undo) 대상이 아니다."),
        "inputSchema": {"type": "object", "properties": {
            "name": {"type": "string", "description": "레이어 이름"},
            "color": {"type": "string", "description": "색: #rrggbb 또는 색 이름 (기본 #ffffff)"},
            "weight": {"type": "number", "exclusiveMinimum": 0, "description": "선두께 mm (목록의 값, 기본 0.25)"},
            "linetype": {"type": "string", "description": "선종류 이름 (기본 Continuous)"},
            "makeCurrent": {"type": "boolean", "description": "만든 레이어를 현재 레이어로 (기본 false)"}},
            "required": ["name"]},
    },
    {
        "name": "open_drawing",
        "description": ("기존 AutoDraft 도면(.cdb) 또는 템플릿(.cdt)을 열어 그 도면에서 작업한다. 사용자가 작업할 파일을 지정했으면 new_drawing 대신 이것을 쓴다. "
                        "이미 열려 있으면 그 창을 활성화한다. 응답으로 기존 객체 수, 축척, 레이어 목록, 치수 레이어를 돌려주므로 "
                        "그 도면의 표준(레이어 이름·축척)에 맞춰 작업한다. 저장 도구는 없다 — 저장(덮어쓰기)은 사용자가 직접 한다."),
        "inputSchema": {"type": "object", "properties": {
            "path": {"type": "string", "description": "도면 파일 경로 (예: C:/Autodraft/data/ai-test.cdb)"}},
            "required": ["path"]},
    },
    {
        "name": "new_drawing",
        "description": "새 빈 도면 창을 연다. ping 에서 hasActiveDrawing 이 false 일 때 사용.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "undo",
        "description": "마지막 작업 1단계를 되돌린다. 잘못 그렸을 때 사용.",
        "inputSchema": {"type": "object", "properties": {}},
    },
]



def _tool_props(name):
    return next(t for t in TOOLS if t["name"] == name)["inputSchema"]["properties"]


_FURNITURE = {"type": "object", "properties": {
    "category": {"type": "string", "description": "분류 (furniture, sink, toilet, tree …)"},
    "name": {"type": "string", "description": "블록 이름 (list_blocks 의 name)"},
    "left": {"type": "number", "description": "블록 왼쪽 가장자리 x (스펙 좌표)"},
    "right": {"type": "number", "description": "블록 오른쪽 가장자리 x"},
    "cx": {"type": "number", "description": "블록 중심 x"},
    "bottom": {"type": "number", "description": "블록 아래 가장자리 y"},
    "top": {"type": "number", "description": "블록 위 가장자리 y"},
    "cy": {"type": "number", "description": "블록 중심 y"},
    "rotation": {"type": "integer", "enum": [0, 90, 180, 270], "description": "회전(도, 반시계). 기본 0"},
    "scale": {"type": "number", "exclusiveMinimum": 0, "description": "배율 (기본 1)"},
    "layer": {"type": "string", "description": "레이어 (기본 FUR)"}},
    "required": ["category", "name"]}

TOOLS.extend([
    {
        "name": "build_plan",
        "description": ("레이아웃 스펙(JSON) 하나로 평면 전체(코어·방·문·창·가구·치수)를 한 번에 만든다. 같은 group 이름으로 다시 호출하면 이전에 만든 그 그룹의 객체를 "
                        "지우고 새로 만든다(replace 기본 true) — 방 위치·크기를 바꿀 때는 스펙을 고쳐 다시 호출하면 되고, 도면틀·직접 그린 객체는 건드리지 않는다. "
                        "호출 1회 = 실행 취소 1단계(이전 그룹 삭제 포함). 모든 좌표는 spec.origin 기준 상대 좌표(mm). "
                        "만드는 순서: core → rooms(배열 순서대로, 앞 방이 그린 벽을 뒤 방이 공유하므로 walls 로 공유 변을 빼고, 문·창은 그 벽이 그려진 뒤에) → furniture → dimensions. "
                        "응답의 steps 로 단계별 성공 여부·경고를, areas 로 방별 내부 면적·전체 면적(㎡, 평)을 확인하고 screenshot 으로 모양을 점검한다. "
                        "마지막 스펙은 get_plan(group)으로 다시 읽을 수 있다(AutoDraft 를 다시 켜면 사라짐)."),
        "inputSchema": {"type": "object", "properties": {
            "group": {"type": "string", "description": "그룹 이름 (기본 plan). 재생성할 때 같은 이름을 쓴다"},
            "replace": {"type": "boolean", "description": "같은 그룹의 이전 객체를 먼저 지울지 (기본 true)"},
            "origin": POINT,
            "wallThickness": {"type": "number", "minimum": 10, "description": "벽 두께 기본값 (mm)"},
            "furnitureLayer": {"type": "string", "description": "가구 기본 레이어 (기본 FUR)"},
            "core": {"type": "object", "description": "create_core 의 인자 그대로 (origin 은 spec 기준 상대)", "properties": _tool_props("create_core")},
            "rooms": {"type": "array", "description": "create_room 의 인자 그대로의 목록 (dimensions 기본 interior)",
                      "items": {"type": "object", "properties": _tool_props("create_room"), "required": ["origin", "width", "depth"]}},
            "furniture": {"type": "array", "items": _FURNITURE,
                          "description": "가구·위생기구 배치. 가로는 left/right/cx, 세로는 bottom/top/cy 중 하나씩. 크기·원점은 list_blocks 로 확인"},
            "autoDimensions": {"type": "object", "description": ("방·코어 사각형의 가장자리로 연속 치수 + 전체 치수를 자동 생성. 기본은 벽 중심선 ↔ 중심선 거리. "
                               "basis(centerline|interior), sides(['S','E','N','W'], 기본 S·E), gap(벽 바깥 면에서 첫 치수선까지, 기본 700), "
                               "overallGap(첫 치수선에서 전체 치수선까지, 기본 600), layer"),
                               "properties": {"basis": {"type": "string", "enum": ["centerline", "interior"]},
                                              "sides": {"type": "array", "items": {"type": "string", "enum": ["S", "E", "N", "W"]}},
                                              "gap": {"type": "number"}, "overallGap": {"type": "number"}, "layer": {"type": "string"}}},
            "dimensions": {"type": "array", "description": "create_dimension 의 인자 그대로의 목록",
                           "items": {"type": "object", "properties": _tool_props("create_dimension"), "required": ["points"]}}}},
    },
    {
        "name": "set_group",
        "description": ("이후 create_* / insert_block 으로 만드는 객체에 그룹 이름표를 자동으로 붙인다(빈 이름이면 해제). build_plan 을 쓰지 않고 개별 도구로 만든 것도 "
                        "delete_group 으로 한꺼번에 지울 수 있게 한다."),
        "inputSchema": {"type": "object", "properties": {"name": {"type": "string", "description": "그룹 이름. 빈 문자열이면 자동 이름표 해제"}},
                        "required": ["name"]},
    },
    {
        "name": "delete_group",
        "description": ("그룹 이름표가 붙은 객체만 모두 지운다(도면틀·이름표 없는 객체는 그대로). 호출 1회 = 실행 취소 1단계. 이름표가 없는 그룹이면 오류. "
                        "문·창이 이름표 없는 기존 벽을 트림한 조각은 그 그룹으로 표시되므로, 사용자가 직접 그린 벽과 섞은 경우 주의."),
        "inputSchema": {"type": "object", "properties": {"name": {"type": "string", "description": "지울 그룹 이름 (get_plan 으로 확인)"}},
                        "required": ["name"]},
    },
    {
        "name": "get_plan",
        "description": ("그룹 목록(이름·객체 수·저장된 스펙 유무)을 돌려준다. group 을 주면 build_plan 으로 만든 마지막 스펙(JSON)을 돌려주므로, "
                        "스펙을 수정해 build_plan 을 다시 호출할 때 시작점으로 쓴다."),
        "inputSchema": {"type": "object", "properties": {"group": {"type": "string", "description": "생략하면 그룹 목록"}}},
    },
])


TOOLS.append({
    "name": "build_default_layout",
    "description": ("32평형 기본 배치(layouts/default_32.json)를 한 번에 그린다. 코어(왼쪽 위, 홀 문 남쪽)·현관(코어 홀 아래)·거실(아래 중앙)·안방(오른쪽 아래)·"
                    "침실2(왼쪽 아래)·침실3/욕실2/드레스룸(오른쪽 위)·욕실1·주방&식당(코어 옆, 거실과 오픈)을 벽 접합 규칙(합쳐진 T·L)대로 그리고 문·창·치수까지 만든다. "
                    "기존 apt32·aptwalls·aptrooms 그룹은 먼저 지운다. origin 으로 전체를 평행이동한다(기본 [0,0]; 도면틀 A2 중앙에 놓으려면 [2300,-1100]). "
                    "사용자가 배치를 따로 말하지 않고 '평면도를 그려줘'만 했을 때의 기본 배치이다. 그린 뒤 screenshot 으로 확인하고, 방 크기·위치를 바꾸라는 요청은 "
                    "이 json 의 방 사각형·벽 선을 고쳐 다시 그린다."),
    "inputSchema": {"type": "object", "properties": {
        "origin": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2, "description": "[dx, dy] 평행이동 (mm). 기본 [0,0]"}}},
})

LAYOUT_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "layouts", "default_32.json")


def _find_core_line(entities, match, dx, dy):
    x1, y1, x2, y2 = match[0] + dx, match[1] + dy, match[2] + dx, match[3] + dy
    for e in entities:
        p = e.get("props", {})
        if (abs(p.get("StartX", 1e9) - x1) < 1 and abs(p.get("StartY", 1e9) - y1) < 1
                and abs(p.get("EndX", 1e9) - x2) < 1 and abs(p.get("EndY", 1e9) - y2) < 1):
            return e["id"]
    return None


def build_default_layout(arguments):
    with open(LAYOUT_FILE, encoding="utf-8") as f:
        spec = json.load(f)
    origin = arguments.get("origin") or [0, 0]
    dx, dy = float(origin[0]), float(origin[1])
    groups = spec["groups"]
    log = []

    def call(tool, args=None, optional=False):
        response = CONNECTION.call(tool, args or {})
        if not response.get("ok") and not optional:
            raise RuntimeError("%s 실패: %s" % (tool, response.get("error")))
        log.append(tool)
        return response

    try:
        for g in (groups["rooms"], groups["walls"], groups["core"]):
            call("delete_group", {"name": g}, optional=True)
        core_resp = call("build_plan", {"group": groups["core"], "origin": [dx, dy], "wallThickness": spec["wallThickness"], "core": spec["core"]})
        entities = call("get_entities", {"group": groups["core"], "layer": "벽체선", "type": "Line", "limit": 300}).get("entities", [])
        call("set_group", {"name": groups["core"]})
        for edit in spec["coreEdits"]:
            line_id = _find_core_line(entities, edit["matchLine"], dx, dy)
            if line_id is None:
                raise RuntimeError("코어 선을 찾지 못했습니다: %s" % edit["matchLine"])
            call("edit_entity", {"id": line_id, "properties": {
                k: v + (dx if k.endswith("X") else dy) for k, v in edit["set"].items()}})
            for a in edit["add"]:
                call("create_line", {"p1": [a[0] + dx, a[1] + dy], "p2": [a[2] + dx, a[3] + dy], "layer": "벽체선"})
        call("set_group", {"name": groups["walls"]})
        for a in spec["wallLines"]:
            call("create_line", {"p1": [a[0] + dx, a[1] + dy], "p2": [a[2] + dx, a[3] + dy], "layer": "벽체선"})
        for a in spec["centerLines"]:
            call("create_line", {"p1": [a[0] + dx, a[1] + dy], "p2": [a[2] + dx, a[3] + dy], "layer": "중심선"})
        call("set_group", {"name": ""})
        rooms_resp = call("build_plan", {"group": groups["rooms"], "origin": [dx, dy], "wallThickness": spec["wallThickness"],
                                         "rooms": spec["rooms"], "dimensions": spec["dimensions"]})
    except (RuntimeError, ConnectionError) as exc:
        try:
            CONNECTION.call("set_group", {"name": ""})
        except ConnectionError:
            pass
        return {"ok": False, "error": str(exc), "calls": len(log)}
    return {"ok": True, "layout": spec["name"], "origin": [dx, dy], "calls": len(log),
            "areas": rooms_resp.get("areas"), "core": core_resp.get("steps", [{}])[0].get("core"),
            "note": "문·창이 트림한 벽 조각은 aptrooms 그룹에 속한다. 다시 그리려면 이 도구를 다시 호출한다(세 그룹을 먼저 지움)."}


class AutoDraftConnection:
    """명명 파이프로 AutoDraft 와 줄 단위 JSON 을 주고받는다."""

    def __init__(self):
        self._pipe = None
        self._next_id = 1

    def _open(self):
        last_error = None
        for _ in range(3):
            try:
                self._pipe = open(PIPE_PATH, "r+b", buffering=0)
                return
            except OSError as exc:
                last_error = exc
                time.sleep(0.3)
        raise ConnectionError(
            "AutoDraft 에 연결할 수 없습니다. AutoDraft 를 실행하고 도면을 연 뒤 다시 시도하세요. "
            "(%s)" % last_error)

    def call(self, tool, args=None):
        if self._pipe is None:
            self._open()
        request_id = self._next_id
        self._next_id += 1
        payload = json.dumps({"id": request_id, "tool": tool, "args": args or {}}, ensure_ascii=False)
        try:
            self._pipe.write(payload.encode("utf-8") + b"\n")
            buffer = b""
            while b"\n" not in buffer:
                chunk = self._pipe.read(65536)
                if not chunk:
                    raise ConnectionError("AutoDraft 연결이 끊겼습니다.")
                buffer += chunk
        except OSError:
            self._pipe = None
            raise ConnectionError("AutoDraft 연결이 끊겼습니다. AutoDraft 가 실행 중인지 확인하세요.")
        return json.loads(buffer.split(b"\n", 1)[0].decode("utf-8"))


CONNECTION = AutoDraftConnection()


# ─────────────────────────────────────────────────────────────────────────────
# 실험용 도구 프로필 (도구 사용 유무 비교 실험)
#   ai_bridge/tool_profile.txt 의 첫 단어로 Claude 에게 보이는 도구를 정한다 (요청마다 다시 읽음 → 앱 재시작 불필요).
#     basic   : 선·원 + 조회 + 도면 열기 + undo        (조건 A)
#     basic_s : basic + screenshot                      (조건 A+S)
#     full    : 모든 도구                               (조건 B, 파일이 없을 때의 기본값)
#   이 파일이 있으면 "실험 모드": 어떤 조건에서든 같은 중립 안내문만 쓴다(안내문이 조건을 흐리지 않게).
#   실험을 끝내려면 파일을 지우고 Claude 앱을 재시작한다.
# ─────────────────────────────────────────────────────────────────────────────
PROFILE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tool_profile.txt")
BASIC_TOOLS = ["ping", "get_layers", "get_entities", "create_line", "create_circle", "new_drawing", "open_drawing", "undo"]
PROFILES = {"basic": BASIC_TOOLS, "basic_s": BASIC_TOOLS + ["screenshot"], "full": None}

INSTRUCTIONS_NEUTRAL = """AutoDraft(CAD) 조작 도구. 좌표·치수는 mm, X는 동쪽(오른쪽), Y는 북쪽(위쪽)이 +.
사용할 수 있는 도구는 지금 목록에 있는 것뿐이다(목록에 없는 도구는 호출할 수 없다). 각 도구의 설명과 응답(paramsUsed, warnings 등)을 따른다.
결과를 확인할 수 있는 도구(get_entities, screenshot)가 있으면 활용하고, 잘못되면 undo 로 되돌린다."""


def current_profile():
    try:
        with open(PROFILE_FILE, encoding="utf-8") as f:
            words = f.read().strip().lower().split()
        name = words[0] if words else "full"
    except OSError:
        return "full"
    return name if name in PROFILES else "full"


def experiment_mode():
    return os.path.exists(PROFILE_FILE)


def active_tools():
    names = PROFILES[current_profile()]
    return TOOLS if names is None else [t for t in TOOLS if t["name"] in names]


def instructions_text():
    if os.environ.get("AUTODRAFT_INSTRUCTIONS", "").lower() == "full":
        return INSTRUCTIONS_FULL
    return INSTRUCTIONS_NEUTRAL if experiment_mode() else INSTRUCTIONS_FULL


def tool_result(response):
    ok = bool(response.get("ok"))
    body = {k: v for k, v in response.items() if k != "id"}
    return {"content": [{"type": "text", "text": json.dumps(body, ensure_ascii=False)}], "isError": not ok}


def handle_call(name, arguments):
    if not any(t["name"] == name for t in TOOLS):
        return tool_result({"ok": False, "error": "알 수 없는 도구: %s" % name})
    if not any(t["name"] == name for t in active_tools()):
        return tool_result({"ok": False, "error": "이 도구는 지금 사용할 수 없습니다(도구 프로필 '%s'). 목록에 있는 도구만 사용하세요." % current_profile()})
    if name == "build_default_layout":
        try:
            return tool_result(build_default_layout(arguments))
        except (OSError, ValueError) as exc:
            return tool_result({"ok": False, "error": "기본 배치 파일을 읽지 못했습니다: %s" % exc})
    try:
        response = CONNECTION.call(name, arguments)
    except ConnectionError as exc:
        return tool_result({"ok": False, "error": str(exc)})
    result = tool_result(response)
    # screenshot 은 이미지를 결과에 직접 실어 보낸다 (데스크톱 앱의 Claude 는 로컬 파일을 열 수 없다)
    image_path = response.get("path") if name == "screenshot" else response.get("sheetPath")
    if response.get("ok") and image_path:
        try:
            with open(image_path, "rb") as f:
                data = base64.b64encode(f.read()).decode("ascii")
            result["content"].append({"type": "image", "data": data, "mimeType": "image/png"})
        except OSError:
            pass
    return result


_last_profile = None


def send(message):
    sys.stdout.write(json.dumps(message, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def serve():
    global _last_profile
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        method = msg.get("method")
        msg_id = msg.get("id")
        # 프로필 파일이 바뀌었으면 도구 목록이 바뀌었음을 알린다
        now_profile = current_profile()
        if _last_profile is not None and now_profile != _last_profile:
            send({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"})
        _last_profile = now_profile
        if method == "initialize":
            requested = (msg.get("params") or {}).get("protocolVersion", "2024-11-05")
            send({"jsonrpc": "2.0", "id": msg_id, "result": {
                "protocolVersion": requested,
                "capabilities": {"tools": {"listChanged": True}},
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                "instructions": instructions_text()}})
        elif method == "tools/list":
            send({"jsonrpc": "2.0", "id": msg_id, "result": {"tools": active_tools()}})
        elif method == "tools/call":
            params = msg.get("params") or {}
            send({"jsonrpc": "2.0", "id": msg_id,
                  "result": handle_call(params.get("name"), params.get("arguments") or {})})
        elif method == "ping":
            send({"jsonrpc": "2.0", "id": msg_id, "result": {}})
        elif msg_id is not None:  # 알 수 없는 요청
            send({"jsonrpc": "2.0", "id": msg_id,
                  "error": {"code": -32601, "message": "Method not found: %s" % method}})
        # id 없는 알림(notifications/initialized 등)은 응답하지 않는다


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "call":
        sys.stdout.reconfigure(encoding="utf-8")
        arguments = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
        try:
            print(json.dumps(CONNECTION.call(sys.argv[2], arguments), ensure_ascii=False, indent=2))
        except ConnectionError as exc:
            print("ERROR:", exc)
            sys.exit(1)
        return
    serve()


if __name__ == "__main__":
    main()
