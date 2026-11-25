#include "model3dexport.h"
#include "buildingmodel3d.h"
#include "mechmodel3d.h"
#include "model3dmesh.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace {

const char *kBlenderKey = "blender/exePath";

// Blender 가져오기 스크립트. %1 = OBJ 경로(슬래시), %2 = 기계 부품 원 둘레 분할 수
const char *kBlenderScript = R"PY(# AutoDraft 3D -> Blender 가져오기 스크립트 (자동 생성)
import bpy, sys, math
from mathutils import Vector

OBJ = r"%1"
RADIAL_STEPS = int("%2")   # 기계 부품 원 둘레 분할 수 (간섭 검사의 다각형 오차 계산용)
RENDER = None
argv = sys.argv
if "--" in argv:
    rest = argv[argv.index("--") + 1:]
    if len(rest) >= 2 and rest[0] == "--render":
        RENDER = rest[1]

# 기본 장면(큐브·카메라·조명) 비우기
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)

bpy.ops.wm.obj_import(filepath=OBJ)
scene = bpy.context.scene
scene["autodraft_radial_steps"] = RADIAL_STEPS
meshes = [o for o in scene.objects if o.type == 'MESH']

# 가져오기 축 변환(회전)을 메시에 적용 → 객체 회전 0, 치수 = 실제 가로·세로·높이
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:
    o.select_set(True)
if meshes:
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)

# 메시 좌표 = 도면 X·Y·Z. 세우기를 켠 도면이면 물체만 X 90° 회전 (로컬 축 = 도면 축, 회전값을 0 으로 하면 눕는다)
STAND_UP = False
with open(OBJ, encoding="utf-8", errors="ignore") as _fp:
    for _i, _ln in enumerate(_fp):
        if _ln.startswith("# AUTODRAFT_STANDUP"):
            STAND_UP = True
        if _i > 10:
            break
if STAND_UP:
    for o in meshes:
        o.rotation_mode = 'XYZ'
        o.rotation_euler = (math.radians(90.0), 0.0, 0.0)
bpy.context.view_layer.update()
scene["autodraft_stand_up"] = STAND_UP

# 유리: 반투명
for m in bpy.data.materials:
    if m.name.startswith("Glass"):
        if m.node_tree is None:
            m.use_nodes = True
        bsdf = m.node_tree.nodes.get("Principled BSDF") if m.node_tree else None
        if bsdf:
            bsdf.inputs["Base Color"].default_value = (0.55, 0.75, 0.95, 1.0)
            bsdf.inputs["Alpha"].default_value = 0.3
        for attr, val in (("surface_render_method", "BLENDED"), ("blend_method", "BLEND")):
            if hasattr(m, attr):
                try:
                    setattr(m, attr, val)
                except Exception:
                    pass

# 기계 부품: 금속 재질
for m in bpy.data.materials:
    if m.name.startswith("Steel") or m.name.startswith("Thread"):
        if m.node_tree is None:
            m.use_nodes = True
        b = m.node_tree.nodes.get("Principled BSDF") if m.node_tree else None
        if b:
            b.inputs["Metallic"].default_value = 1.0
            b.inputs["Roughness"].default_value = 0.35 if m.name.startswith("Steel") else 0.6

# 키홈·구멍·나사골: "<부품>_키홈커터" / "<부품>_구멍커터" / "<부품>_나사커터" → <부품> 에서 빼기(DIFFERENCE)
#   (나사산이 무거우면 부품의 "나사커터" 수정자 표시를 끄면 원래 원통으로 돌아간다)
# 돌출 합치기: "<부품>_합침N" → <부품> 에 더하기(UNION). 파내기 "<부품>_파내기N" → 빼기. 도구 객체는 숨김
import re
_TOOL_RE = re.compile(r"^(.*)_(키홈커터\d*|구멍커터\d*|나사커터\d*|합침\d*|파내기\d*)$")
def _cut_suffix(name):
    m = _TOOL_RE.match(name)
    return ("_" + m.group(2)) if m else None
for c in [o for o in meshes if _cut_suffix(o.name)]:
    suf = _cut_suffix(c.name)
    target = bpy.data.objects.get(c.name[:-len(suf)])
    if target is not None:
        mod = target.modifiers.new(suf.strip("_"), 'BOOLEAN')
        mod.operation = 'UNION' if suf.startswith("_합침") else 'DIFFERENCE'
        mod.object = c
        # Manifold 해법: 닫힌 입력 → 닫힌 결과 (곡면이 거의 겹치는 합치기에서 EXACT 는 틈이 남음)
        for _solver in ('MANIFOLD', 'EXACT'):
            try:
                mod.solver = _solver
                break
            except Exception:
                pass
    c.hide_set(True)
    c.hide_render = True
# 합치기 도구(받침대 등 "_합침N")는 숨겨도 본체의 일부 → 범위(바닥판·카메라)에는 넣는다
_union_parts = [o for o in meshes if (_cut_suffix(o.name) or "").startswith("_합침")]
meshes = [o for o in meshes if not _cut_suffix(o.name)]

# 범위
mn = Vector((1e9, 1e9, 1e9))
mx = Vector((-1e9, -1e9, -1e9))
for o in meshes + _union_parts:
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        mn = Vector((min(mn.x, w.x), min(mn.y, w.y), min(mn.z, w.z)))
        mx = Vector((max(mx.x, w.x), max(mx.y, w.y), max(mx.z, w.z)))
if not meshes:
    mn = Vector((-1, -1, 0))
    mx = Vector((1, 1, 1))
center = (mn + mx) / 2
size = max((mx - mn).length, 0.01)   # 기계 부품은 수 cm 단위

# 바닥판
bpy.ops.mesh.primitive_plane_add(size=size * 1.6, location=(center.x, center.y, mn.z - 0.002))
bpy.context.active_object.name = "바닥"

# 태양광
sun = bpy.data.lights.new("태양", type='SUN')
sun.energy = 3.0
sun_obj = bpy.data.objects.new("태양", sun)
scene.collection.objects.link(sun_obj)
sun_obj.rotation_euler = (math.radians(50), 0.0, math.radians(30))

# 카메라 (모델 중심을 바라봄)
target = bpy.data.objects.new("카메라목표", None)
scene.collection.objects.link(target)
target.location = center
cam = bpy.data.cameras.new("카메라")
cam.clip_start = size * 0.01
cam.clip_end = size * 100.0
cam_obj = bpy.data.objects.new("카메라", cam)
scene.collection.objects.link(cam_obj)
d = size * 1.35
cam_obj.location = (center.x + d * 0.55, center.y - d * 0.85, center.z + d * 0.75)
track = cam_obj.constraints.new('TRACK_TO')
track.target = target
scene.camera = cam_obj

print("AUTODRAFT_IMPORT objects=%d" % len(meshes))
for o in sorted(meshes, key=lambda x: x.name):
    print("AUTODRAFT_OBJ %s verts=%d faces=%d dims=%.3f,%.3f,%.3f" % (
        o.name, len(o.data.vertices), len(o.data.polygons), o.dimensions.x, o.dimensions.y, o.dimensions.z))
print("AUTODRAFT_BOUNDS min=%.3f,%.3f,%.3f max=%.3f,%.3f,%.3f" % (mn.x, mn.y, mn.z, mx.x, mx.y, mx.z))

if RENDER:
    scene.render.engine = 'BLENDER_WORKBENCH'
    try:
        scene.display.shading.color_type = 'MATERIAL'
    except Exception:
        pass
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 800
    scene.render.filepath = RENDER
    bpy.ops.render.render(write_still=True)
else:
    def _frame():
        for win in bpy.context.window_manager.windows:
            for area in win.screen.areas:
                if area.type != 'VIEW_3D':
                    continue
                for region in area.regions:
                    if region.type == 'WINDOW':
                        with bpy.context.temp_override(window=win, area=area, region=region):
                            bpy.ops.view3d.view_all()
                        break
        return None
    bpy.app.timers.register(_frame, first_interval=1.0)

# ── Claude 연결 통로 ────────────────────────────────────────────────────────
#   127.0.0.1 전용 TCP + 실행마다 새 토큰. 정보 파일: %TEMP%/AutoDraft3D/blender_bridge.json
#   요청 한 줄 JSON {"token":..., "code":...} → bpy 코드 실행 → 응답 한 줄 JSON {"ok", "output", "error"}
#   0.1초 타이머로 확인하므로 Blender 화면을 멈추지 않는다. 창 없는(background) 실행에서는 켜지 않는다.
import socket, json, secrets, io, contextlib, traceback, os, tempfile

_bridge = {"srv": None, "token": "", "clients": [], "ns": {"bpy": bpy, "__name__": "__autodraft__"}}

def view3d_override():
    """3D 뷰 컨텍스트 (bpy.ops 를 3D 뷰에서 실행할 때: with bpy.context.temp_override(**view3d_override()):)"""
    for win in bpy.context.window_manager.windows:
        for area in win.screen.areas:
            if area.type == 'VIEW_3D':
                for region in area.regions:
                    if region.type == 'WINDOW':
                        return {"window": win, "screen": win.screen, "area": area, "region": region}
    return {}

def view3d_space():
    o = view3d_override()
    return o["area"].spaces.active if o else None

def screenshot(path):
    """Blender 3D 뷰 화면을 PNG 로 저장"""
    o = view3d_override()
    with bpy.context.temp_override(**o):
        bpy.ops.screen.screenshot_area(filepath=path)
    return path

_bridge["ns"].update(view3d_override=view3d_override, view3d_space=view3d_space, screenshot=screenshot)

def _bridge_handle(line):
    try:
        req = json.loads(line.decode("utf-8"))
    except Exception:
        return {"ok": False, "error": "잘못된 JSON"}
    if req.get("token") != _bridge["token"]:
        return {"ok": False, "error": "토큰이 맞지 않습니다"}
    buf = io.StringIO()
    try:
        with contextlib.redirect_stdout(buf):
            exec(compile(req.get("code", ""), "<claude>", "exec"), _bridge["ns"])
        return {"ok": True, "output": buf.getvalue()}
    except Exception:
        return {"ok": False, "output": buf.getvalue(), "error": traceback.format_exc()}

def _bridge_drop(item):
    try:
        item[0].close()
    except Exception:
        pass
    if item in _bridge["clients"]:
        _bridge["clients"].remove(item)

def _bridge_poll():
    srv = _bridge["srv"]
    try:
        while True:
            conn, _addr = srv.accept()
            conn.setblocking(False)
            _bridge["clients"].append([conn, b""])
    except (BlockingIOError, OSError):
        pass
    for item in list(_bridge["clients"]):
        conn = item[0]
        try:
            data = conn.recv(1 << 20)
            if not data:
                _bridge_drop(item)
                continue
            item[1] += data
        except BlockingIOError:
            pass
        except OSError:
            _bridge_drop(item)
            continue
        while b"\n" in item[1]:
            line, item[1] = item[1].split(b"\n", 1)
            reply = _bridge_handle(line)
            try:
                conn.setblocking(True)
                conn.sendall((json.dumps(reply, ensure_ascii=False) + "\n").encode("utf-8"))
                conn.setblocking(False)
            except OSError:
                _bridge_drop(item)
                break
    return 0.1

def _bridge_start():
    for port in range(8766, 8776):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            srv.bind(("127.0.0.1", port))
        except OSError:
            srv.close()
            continue
        srv.listen(4)
        srv.setblocking(False)
        _bridge["srv"] = srv
        _bridge["token"] = secrets.token_hex(16)
        info_dir = os.path.join(tempfile.gettempdir(), "AutoDraft3D")
        os.makedirs(info_dir, exist_ok=True)
        with open(os.path.join(info_dir, "blender_bridge.json"), "w", encoding="utf-8") as f:
            json.dump({"port": port, "token": _bridge["token"], "pid": os.getpid(), "obj": OBJ}, f)
        bpy.app.timers.register(_bridge_poll, first_interval=0.5, persistent=True)
        print("AUTODRAFT_BRIDGE port=%d" % port)
        return
    print("AUTODRAFT_BRIDGE 사용할 수 있는 포트가 없습니다")

if not bpy.app.background:
    _bridge_start()
)PY";

// 간섭 검사 (가져오기 스크립트 뒤에 붙는다, QString::arg 를 거치지 않음)
//   autodraft_interference(tol_mm=0.0, near_mm=1.0, mark=True)
//   · 보이는 메시 객체끼리(바닥·키홈커터·결과 제외) 현재 위치(matrix_world)로 검사 — Blender 에서 옮긴 조립 상태 그대로
//   · 경계상자 → BVH 삼각형 겹침 → Boolean 교집합(EXACT) 체적·두께(2V/면적)
//   · 두께 ≤ 다각형 오차 r·(1-cos(π/n)) + tol_mm → "접촉", 넘으면 "간섭"(빨간 객체), 안 겹치면 near_mm 안의 "틈새"
//   · 끼워맞춤: <이름>_fits.json 원통면(같은 축·호칭 지름)으로 공차 계산 → 억지(주황)·중간(보라)·헐거움(파랑) 띠
//     (빨강 = 실제 겹침 "간섭" 전용)
//   · 간섭 부품은 객체 재질 슬롯(link=OBJECT)으로 반투명 표시 → 빨간 겹침이 부품 안에 보임. 결과 지우기로 원래 재질 복원
//   · N 패널 "AutoDraft" 탭 [간섭 검사] 버튼, 브리지 이름공간·bpy.app.driver_namespace 에도 등록
const char *kInterferenceScript = R"PY(
# ── 간섭 검사 ──────────────────────────────────────────────────────────────
import bmesh
from mathutils.bvhtree import BVHTree

CLASH_COLL = "간섭 결과"

# ── 끼워맞춤 (공차) ── AutoDraft 가 OBJ 옆에 쓴 <이름>_fits.json 의 원통면으로 판정
#   같은 축·같은 호칭 지름의 축 면 ↔ 구멍 면이 겹치는 길이가 있으면
#   틈새 최소 = 구멍 아래 허용차 - 축 위 허용차, 최대 = 구멍 위 - 축 아래 (지름 기준 mm)
#   최소 ≥ 0 → 헐거움(파랑) · 최대 ≤ 0 → 억지(주황) · 그 사이 → 중간(보라)
#   빨강은 실제 모양이 겹치는 "간섭" 전용 (억지 끼워맞춤과 구별)
FIT_COLORS = {"억지": (1.0, 0.5, 0.0, 1.0), "중간": (0.62, 0.2, 0.95, 1.0), "헐거움": (0.1, 0.4, 1.0, 1.0)}
FIT_ICONS = {"억지": 'COLORSET_02_VEC', "중간": 'COLORSET_06_VEC', "헐거움": 'COLORSET_04_VEC'}
COLORED = ("간섭", "억지", "중간", "헐거움")

def _ad_load_fits():
    path = (OBJ[:-4] if OBJ.lower().endswith(".obj") else OBJ) + "_fits.json"
    try:
        with open(path, encoding="utf-8") as fp:
            parts = json.load(fp).get("parts", {})
    except (OSError, ValueError):
        return 0
    n = 0
    for name, feats in parts.items():
        o = bpy.data.objects.get(name) or bpy.data.objects.get(name.replace(" ", "_"))
        if o is not None:
            o["autodraft_fits"] = json.dumps(feats, ensure_ascii=False)
            n += 1
    return n

def _ad_fit_feats(o):
    raw = o.get("autodraft_fits")
    if not raw:
        return []
    M = o.matrix_world
    R = M.to_3x3()
    out = []
    for f in json.loads(raw):
        g = dict(f)
        g["P"] = M @ Vector(f["o"])
        g["D"] = (R @ Vector(f["d"])).normalized()
        out.append(g)
    return out

def _ad_fit_material(kind):
    name = "끼워맞춤_" + kind
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
    # 이미 있는 재질도 색을 다시 맞춘다 (예전 색으로 저장된 .blend 를 다시 검사할 때)
    m.diffuse_color = FIT_COLORS[kind]
    if m.node_tree is None:
        m.use_nodes = True
    b = m.node_tree.nodes.get("Principled BSDF") if m.node_tree else None
    if b is not None:
        b.inputs["Base Color"].default_value = FIT_COLORS[kind]
    return m

def _ad_fit_band(name, P, D, r, t0, t1, kind, coll):
    # 맞닿는 원통면을 보이게 하는 색 띠 (반지름 방향 두께는 과장 표시)
    w = max(0.0002, r * 0.03)
    u = D.orthogonal().normalized()
    v = D.cross(u).normalized()
    n = 96
    verts = []
    for t in (t0, t1):
        for rr in (r - w, r + w):
            for k in range(n):
                a = 2.0 * math.pi * k / n
                verts.append(tuple(P + D * t + (u * math.cos(a) + v * math.sin(a)) * rr))
    def ix(ti, ri, k):
        return (ti * 2 + ri) * n + (k % n)
    faces = []
    for k in range(n):
        faces.append((ix(0, 1, k), ix(0, 1, k + 1), ix(1, 1, k + 1), ix(1, 1, k)))
        faces.append((ix(0, 0, k), ix(1, 0, k), ix(1, 0, k + 1), ix(0, 0, k + 1)))
        faces.append((ix(0, 0, k), ix(0, 0, k + 1), ix(0, 1, k + 1), ix(0, 1, k)))
        faces.append((ix(1, 0, k), ix(1, 1, k), ix(1, 1, k + 1), ix(1, 0, k + 1)))
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    me.materials.append(_ad_fit_material(kind))
    ob = bpy.data.objects.new(name, me)
    ob["autodraft_clash"] = True
    coll.objects.link(ob)
    return ob

def _ad_fit_check(A, B, coll, mark):
    out = []
    for fa in A["fits"]:
        for fb in B["fits"]:
            if fa["hole"] == fb["hole"]:
                continue
            if fa["hole"]:
                sh, ho, so, hobj = fb, fa, B["obj"], A["obj"]
            else:
                sh, ho, so, hobj = fa, fb, A["obj"], B["obj"]
            if not (sh["tol"] or ho["tol"]):
                continue                                   # 둘 다 공차 없음 → 모양 검사(접촉)에 맡김
            if abs(sh["r"] - ho["r"]) > 1e-6:              # 호칭 지름이 다름 (0.001 mm)
                continue
            if abs(sh["D"].dot(ho["D"])) < 0.99999:        # 축 방향이 다름
                continue
            vv = ho["P"] - sh["P"]
            t = vv.dot(sh["D"])
            if (vv - sh["D"] * t).length > 2e-5:           # 같은 축선이 아님 (0.02 mm)
                continue
            sgn = 1.0 if sh["D"].dot(ho["D"]) > 0.0 else -1.0
            h0, h1 = sorted((t + sgn * ho["x0"], t + sgn * ho["x1"]))
            lo = max(sh["x0"], h0)
            hi = min(sh["x1"], h1)
            if hi - lo < 1e-5:                             # 겹치는 길이 0.01 mm 미만
                continue
            cmax = ho["up"] - sh["lo"]
            cmin = ho["lo"] - sh["up"]
            kind = "헐거움" if cmin >= 0.0 else ("억지" if cmax <= 0.0 else "중간")
            r = {"a": so.name, "b": hobj.name, "kind": kind,
                 "fit": "%s ↔ %s" % (sh["label"], ho["label"]),
                 "clearance_min_mm": round(cmin, 4), "clearance_max_mm": round(cmax, 4),
                 "length_mm": round((hi - lo) * 1000.0, 3)}
            if mark:
                _ad_fit_band("끼워맞춤_%s_%s" % (so.name, hobj.name), sh["P"], sh["D"], sh["r"], lo, hi, kind, coll)
            out.append(r)
    return out

def _ad_parts():
    out = []
    for o in bpy.context.scene.objects:
        if o.type != 'MESH' or not o.visible_get():
            continue
        if o.name == "바닥" or _cut_suffix(o.name) or o.get("autodraft_clash"):
            continue
        out.append(o)
    return out

def _ad_world_bm(o, dg):
    ev = o.evaluated_get(dg)
    me = ev.to_mesh()
    bm = bmesh.new()
    bm.from_mesh(me)
    ev.to_mesh_clear()
    bm.transform(o.matrix_world)
    bm.normal_update()
    return bm

def _ad_radius_m(o):
    # 회전체 반지름 추정: 세 치수의 가운데 값 / 2 (축 138x32x32 → 16, 베어링 9x37x37 → 18.5)
    d = sorted([o.dimensions.x, o.dimensions.y, o.dimensions.z])
    return d[1] / 2.0

def _ad_facet_mm(o, steps):
    return _ad_radius_m(o) * 1000.0 * (1.0 - math.cos(math.pi / max(3, steps)))

def _ad_ghost_material():
    m = bpy.data.materials.get("간섭_반투명")
    if m is None:
        m = bpy.data.materials.new("간섭_반투명")
        m.diffuse_color = (0.75, 0.78, 0.82, 0.18)   # 솔리드 보기(Workbench)는 이 색·알파를 쓴다
        for attr, val in (("surface_render_method", "BLENDED"), ("blend_method", "BLEND")):
            if hasattr(m, attr):
                try:
                    setattr(m, attr, val)
                except Exception:
                    pass
    return m

def _ad_ghost(o):
    # 객체 쪽 재질 슬롯(link='OBJECT')으로만 덮어씌운다 → 메시의 원래 재질은 그대로, 지우기에서 복원
    if o.get("autodraft_ghost"):
        return
    g = _ad_ghost_material()
    for slot in o.material_slots:
        slot.link = 'OBJECT'
        slot.material = g
    if not o.material_slots:
        o.data.materials.append(None)
        o.material_slots[0].link = 'OBJECT'
        o.material_slots[0].material = g
        o["autodraft_ghost_added_slot"] = True
    o["autodraft_ghost"] = True

def _ad_unghost_all():
    for o in bpy.data.objects:
        if not o.get("autodraft_ghost"):
            continue
        for slot in o.material_slots:
            if slot.link == 'OBJECT':
                slot.material = None
                slot.link = 'DATA'
        if o.get("autodraft_ghost_added_slot") and o.type == 'MESH' and len(o.data.materials) > 0:
            o.data.materials.pop(index=len(o.data.materials) - 1)
            del o["autodraft_ghost_added_slot"]
        del o["autodraft_ghost"]

def autodraft_clear_interference():
    _ad_unghost_all()
    coll = bpy.data.collections.get(CLASH_COLL)
    if coll is None:
        return
    for o in list(coll.objects):
        me = o.data
        bpy.data.objects.remove(o, do_unlink=True)
        if me is not None and me.users == 0:
            bpy.data.meshes.remove(me)
    bpy.data.collections.remove(coll)

def _ad_clash_material():
    m = bpy.data.materials.get("간섭")
    if m is None:
        m = bpy.data.materials.new("간섭")
        m.diffuse_color = (1.0, 0.05, 0.05, 1.0)
        if m.node_tree is None:
            m.use_nodes = True
        b = m.node_tree.nodes.get("Principled BSDF")
        if b is None:
            b = m.node_tree.nodes.new("ShaderNodeBsdfPrincipled")
            out = m.node_tree.nodes.get("Material Output") or m.node_tree.nodes.new("ShaderNodeOutputMaterial")
            m.node_tree.links.new(b.outputs[0], out.inputs[0])
        b.inputs["Base Color"].default_value = (1.0, 0.05, 0.05, 1.0)
        try:
            b.inputs["Emission Color"].default_value = (1.0, 0.0, 0.0, 1.0)
            b.inputs["Emission Strength"].default_value = 0.6
        except Exception:
            pass
    return m

def _ad_intersect(a, bm_a, b, keep, coll):
    me = bpy.data.meshes.new("간섭_%s_%s" % (a.name, b.name))
    bm_a.to_mesh(me)
    tmp = bpy.data.objects.new(me.name, me)
    tmp["autodraft_clash"] = True
    coll.objects.link(tmp)
    mod = tmp.modifiers.new("교집합", 'BOOLEAN')
    mod.operation = 'INTERSECT'
    mod.object = b
    try:
        mod.solver = 'EXACT'
    except Exception:
        pass
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    ev = tmp.evaluated_get(dg)
    rme = ev.to_mesh()
    bm = bmesh.new()
    bm.from_mesh(rme)
    ev.to_mesh_clear()
    vol = abs(bm.calc_volume(signed=True)) if bm.faces else 0.0
    area = sum(f.calc_area() for f in bm.faces)
    if keep and bm.faces:
        tmp.modifiers.remove(mod)
        bm.to_mesh(me)
        me.materials.clear()
        me.materials.append(_ad_clash_material())
    else:
        keep = False
    bm.free()
    if not keep:
        bpy.data.objects.remove(tmp, do_unlink=True)
        bpy.data.meshes.remove(me)
        tmp = None
    return vol, area, tmp

def _ad_gap_m(bm_a, bvh_b, bm_b, bvh_a, limit):
    best = limit
    for bm, bvh in ((bm_a, bvh_b), (bm_b, bvh_a)):
        for v in bm.verts:
            hit = bvh.find_nearest(v.co, best)
            if hit[0] is not None and hit[3] < best:
                best = hit[3]
    return best

def autodraft_interference(tol_mm=0.0, near_mm=1.0, mark=True, quiet=False, ghost=True):
    """보이는 부품끼리 간섭·접촉·틈새를 검사한다. 결과 dict 목록을 돌려주고, 간섭은 '간섭 결과' 컬렉션에 빨간 객체로 남긴다."""
    scene = bpy.context.scene
    steps = int(scene.get("autodraft_radial_steps", 64))
    autodraft_clear_interference()
    coll = bpy.data.collections.new(CLASH_COLL)
    scene.collection.children.link(coll)
    dg = bpy.context.evaluated_depsgraph_get()
    parts = _ad_parts()
    data = []
    for o in parts:
        bm = _ad_world_bm(o, dg)
        if not bm.verts:
            bm.free()
            continue
        xs = [v.co.x for v in bm.verts]
        ys = [v.co.y for v in bm.verts]
        zs = [v.co.z for v in bm.verts]
        data.append({"obj": o, "bm": bm, "bvh": BVHTree.FromBMesh(bm), "fits": _ad_fit_feats(o),
                     "mn": Vector((min(xs), min(ys), min(zs))), "mx": Vector((max(xs), max(ys), max(zs))),
                     "facet": _ad_facet_mm(o, steps)})
    near = near_mm / 1000.0
    results = []
    for i in range(len(data)):
        for j in range(i + 1, len(data)):
            A, B = data[i], data[j]
            sep = max(A["mn"].x - B["mx"].x, B["mn"].x - A["mx"].x,
                      A["mn"].y - B["mx"].y, B["mn"].y - A["mx"].y,
                      A["mn"].z - B["mx"].z, B["mn"].z - A["mx"].z)
            if sep > near:
                continue
            fits = _ad_fit_check(A, B, coll, mark) if (A["fits"] and B["fits"]) else []
            results.extend(fits)
            allow = max(A["facet"], B["facet"]) + tol_mm + 1e-4
            pairs = A["bvh"].overlap(B["bvh"]) if sep <= 1e-9 else []
            r = {"a": A["obj"].name, "b": B["obj"].name, "allow_mm": round(allow, 4)}
            if pairs:
                vol, area, obj = _ad_intersect(A["obj"], A["bm"], B["obj"], mark, coll)
                depth = (2.0 * vol / area * 1000.0) if area > 0.0 else 0.0
                r["volume_mm3"] = round(vol * 1e9, 4)
                r["depth_mm"] = round(depth, 4)
                if vol * 1e9 < 1e-3 or depth <= allow:
                    r["kind"] = "접촉"
                    if obj is not None:
                        me = obj.data
                        bpy.data.objects.remove(obj, do_unlink=True)
                        bpy.data.meshes.remove(me)
                else:
                    r["kind"] = "간섭"
            else:
                gap = _ad_gap_m(A["bm"], B["bvh"], B["bm"], A["bvh"], near)
                if gap >= near:
                    continue
                r["gap_mm"] = round(gap * 1000.0, 4)
                r["kind"] = "접촉" if gap * 1000.0 <= allow else "틈새"
            if fits and r["kind"] in ("접촉", "틈새"):
                continue                                   # 끼워맞춤 판정이 대신한다
            results.append(r)
    for d in data:
        d["bm"].free()
    if mark and ghost:   # 간섭 난 부품만 반투명 → 빨간 겹침이 부품 안 어디에 있는지 보이게
        for x in results:
            if x["kind"] in COLORED:
                for n in (x["a"], x["b"]):
                    o = bpy.data.objects.get(n)
                    if o is not None:
                        _ad_ghost(o)
    if not coll.objects:
        bpy.data.collections.remove(coll)
    order = {"간섭": 0, "억지": 1, "중간": 2, "헐거움": 3, "접촉": 4, "틈새": 5}
    results.sort(key=lambda x: (order.get(x["kind"], 9), x["a"], x["b"]))
    bpy.app.driver_namespace["autodraft_clash_last"] = results
    if not quiet:
        print("AUTODRAFT_CLASH_SUMMARY parts=%d steps=%d %s" % (
            len(data), steps, " ".join("%s=%d" % (k, sum(1 for x in results if x["kind"] == k))
                                       for k in ("간섭", "억지", "중간", "헐거움", "접촉", "틈새"))))
        for x in results:
            print("AUTODRAFT_CLASH", json.dumps(x, ensure_ascii=False))
    return results

def _ad_result_lines(x):
    head = "%s  %s ↔ %s" % (x["kind"], x["a"], x["b"])
    if "fit" in x:
        cmin, cmax = x["clearance_min_mm"], x["clearance_max_mm"]
        if x["kind"] == "억지":
            val = "죔새 %.3f ~ %.3f mm" % (-cmax, -cmin)
        elif x["kind"] == "헐거움":
            val = "틈새 %.3f ~ %.3f mm" % (cmin, cmax)
        else:
            val = "틈새 최대 %.3f / 죔새 최대 %.3f mm" % (cmax, -cmin)
        return head, "    " + x["fit"], "    " + val
    if x["kind"] == "간섭":
        return head, "    %.3f mm³ · 두께 약 %.3f mm" % (x["volume_mm3"], x["depth_mm"])
    if "gap_mm" in x:
        return head, "    틈 %.3f mm" % x["gap_mm"]
    return head, "    다각형 오차(%.4f mm) 이내" % x["allow_mm"]

class AUTODRAFT_OT_interference(bpy.types.Operator):
    bl_idname = "autodraft.interference"
    bl_label = "간섭 검사"
    bl_description = "보이는 부품끼리 겹침(간섭)·맞닿음(접촉)·가까운 틈을 검사하고 간섭 부분을 빨갛게 표시합니다"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        sc = context.scene
        res = autodraft_interference(tol_mm=sc.autodraft_clash_tol, near_mm=sc.autodraft_clash_near,
                                     ghost=sc.autodraft_clash_ghost)
        cnt = {}
        for x in res:
            cnt[x["kind"]] = cnt.get(x["kind"], 0) + 1
        text = ", ".join("%s %d" % (k, cnt[k]) for k in ("간섭", "억지", "중간", "헐거움", "접촉", "틈새") if k in cnt)
        self.report({'WARNING'} if cnt.get("간섭") else {'INFO'}, text or "겹치거나 가까운 부품 없음")
        return {'FINISHED'}

class AUTODRAFT_OT_clear_interference(bpy.types.Operator):
    bl_idname = "autodraft.clear_interference"
    bl_label = "결과 지우기"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        autodraft_clear_interference()
        bpy.app.driver_namespace["autodraft_clash_last"] = []
        return {'FINISHED'}

class AUTODRAFT_PT_interference(bpy.types.Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "AutoDraft"
    bl_label = "간섭 검사"

    def draw(self, context):
        sc = context.scene
        col = self.layout.column()
        col.label(text="원 분할 %d" % int(sc.get("autodraft_radial_steps", 64)))
        col.prop(sc, "autodraft_clash_tol")
        col.prop(sc, "autodraft_clash_near")
        col.prop(sc, "autodraft_clash_ghost")
        row = col.row(align=True)
        row.operator("autodraft.interference", icon='MOD_BOOLEAN')
        row.operator("autodraft.clear_interference", text="", icon='X')
        res = bpy.app.driver_namespace.get("autodraft_clash_last")
        if res is None:
            return
        box = col.box()
        if not res:
            box.label(text="겹치거나 가까운 부품 없음", icon='CHECKMARK')
        for x in res:
            icon = FIT_ICONS.get(x["kind"]) or ('ERROR' if x["kind"] == "간섭" else
                                                ('LINKED' if x["kind"] == "접촉" else 'DOT'))
            lines = _ad_result_lines(x)
            box.label(text=lines[0], icon=icon)
            for line in lines[1:]:
                box.label(text=line)

# ── 회전 간섭 검사 ─────────────────────────────────────────────────────────
#   선택한 부품(+ 합침·커터 객체)을 축선 둘레로 각도별로 돌려 다른 부품과 겹치는 각도 구간을 찾는다.
#   축선 = 활성(또는 선택한) 부품의 끼워맞춤 원통면(_fits.json) 축. 모든 각도에서 닿는 부품 = "항상 맞닿음"(축·끼워진 면).
#   애니메이션: 축선 위 Empty "회전축" 에 부품을 붙이고 delta 회전을 키프레임 → 재생하면 닿는 프레임에서 빨갛게.
from mathutils import Matrix
SWEEP_EMPTY = "회전축"

def _ad_base_name(o):
    suf = _cut_suffix(o.name)
    return o.name[:-len(suf)] if suf else o.name

def _ad_group_of(names):
    out = []
    for o in bpy.data.objects:
        if o.type != 'MESH':
            continue
        b = _ad_base_name(o)
        if b in names and o not in out:
            out.append(o)
    return out

def _ad_axis_of(names):
    for n in names:
        o = bpy.data.objects.get(n)
        if o is None:
            continue
        f = _ad_fit_feats(o)
        if f:
            return f[0]["P"], f[0]["D"]
    return None, None

def _ad_red(o, on):
    # 객체 재질 슬롯(link=OBJECT)으로 빨강 덮기 / 되돌리기 (메시 재질은 그대로)
    if on:
        if o.get("autodraft_sweep_red"):
            return
        m = _ad_clash_material()
        if not o.material_slots:
            o.data.materials.append(None)
            o["autodraft_sweep_added_slot"] = True
        for slot in o.material_slots:
            slot.link = 'OBJECT'
            slot.material = m
        o["autodraft_sweep_red"] = True
    elif o.get("autodraft_sweep_red"):
        for slot in o.material_slots:
            if slot.link == 'OBJECT':
                slot.material = None
                slot.link = 'DATA'
        if o.get("autodraft_sweep_added_slot") and len(o.data.materials) > 0:
            o.data.materials.pop(index=len(o.data.materials) - 1)
            del o["autodraft_sweep_added_slot"]
        del o["autodraft_sweep_red"]

def _ad_fcurves(action):
    # Blender 4.4 이전: action.fcurves / 5.x 층 구조: layers → strips → channelbags → fcurves
    fcs = list(getattr(action, "fcurves", None) or [])
    for layer in getattr(action, "layers", []):
        for strip in layer.strips:
            for cb in getattr(strip, "channelbags", []):
                fcs.extend(cb.fcurves)
    return fcs

def _ad_sweep_frame(scene, depsgraph=None):
    info = bpy.app.driver_namespace.get("autodraft_sweep_anim")
    if not info:
        return
    ang = info["a0"] + (scene.frame_current - scene.frame_start) * info["step"]
    hit_any = False
    for name, ranges in info["ranges"].items():
        o = bpy.data.objects.get(name)
        if o is None:
            continue
        hit = any(r[0] - 1e-6 <= ang <= r[1] + 1e-6 for r in ranges)
        hit_any = hit_any or hit
        _ad_red(o, hit)
    for name in info["movers"]:
        o = bpy.data.objects.get(name)
        if o is not None and o.visible_get():
            _ad_red(o, hit_any)

def autodraft_sweep_clear():
    for h in list(bpy.app.handlers.frame_change_post):
        if getattr(h, "__name__", "") == "_ad_sweep_frame":
            bpy.app.handlers.frame_change_post.remove(h)
    info = bpy.app.driver_namespace.get("autodraft_sweep_anim")
    for nm in (SWEEP_EMPTY + "_회전", SWEEP_EMPTY):
        e = bpy.data.objects.get(nm)
        if e is None:
            continue
        for o in list(e.children):
            mw = o.matrix_world.copy()
            o.parent = None
            o.matrix_world = mw
        bpy.data.objects.remove(e, do_unlink=True)
    if info:
        for name, mw in info.get("rest", {}).items():
            o = bpy.data.objects.get(name)
            if o is not None:
                o.matrix_world = Matrix(mw)
    for o in bpy.data.objects:
        if o.get("autodraft_sweep_red"):
            _ad_red(o, False)
    bpy.app.driver_namespace["autodraft_sweep_anim"] = None

def autodraft_rotation_sweep(names, a0=0.0, a1=360.0, step=2.0, anim=True, quiet=False):
    """names 부품(+합침·커터)을 축선 둘레로 a0~a1 도를 step 간격으로 돌리며 다른 부품과 겹치는 각도 구간을 찾는다."""
    autodraft_sweep_clear()
    names = [n for n in names if bpy.data.objects.get(n) is not None]
    if not names:
        return {"error": "돌릴 부품을 고르세요"}
    P, D = _ad_axis_of(names)
    if P is None:
        return {"error": "회전축을 찾지 못했습니다 (회전체·축·베어링 같이 원통면이 있는 부품을 함께 고르세요)"}
    group = _ad_group_of(names)
    others = [o for o in _ad_parts() if o not in group]
    dg = bpy.context.evaluated_depsgraph_get()
    trees = []
    for o in others:
        bm = _ad_world_bm(o, dg)
        if bm.verts:
            trees.append((o, BVHTree.FromBMesh(bm)))
        bm.free()
    M0 = {o.name: o.matrix_world.copy() for o in group}
    movers = [o for o in group if o.visible_get()]
    step = max(0.1, float(step))
    n = max(1, int(round((a1 - a0) / step)))
    hits = {o.name: [] for o, t in trees}
    angles = []
    for i in range(n + 1):
        ang = a0 + i * step
        angles.append(ang)
        R = Matrix.Translation(P) @ Matrix.Rotation(math.radians(ang), 4, D) @ Matrix.Translation(-P)
        for o in group:
            o.matrix_world = R @ M0[o.name]
        bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get()
        trees_m = []
        for o in movers:
            bm = _ad_world_bm(o, dg)
            if bm.verts:
                trees_m.append(BVHTree.FromBMesh(bm))
            bm.free()
        for o, t in trees:
            if any(tm.overlap(t) for tm in trees_m):
                hits[o.name].append(ang)
    for o in group:
        o.matrix_world = M0[o.name]
    bpy.context.view_layer.update()
    results = []
    ranges = {}
    for name, angs in hits.items():
        if not angs:
            continue
        if len(angs) == len(angles):
            results.append({"other": name, "always": True, "ranges": [[a0, a1]]})
            continue
        rs = []
        for a in angs:
            if rs and a - rs[-1][1] <= step + 1e-6:
                rs[-1][1] = a
            else:
                rs.append([a, a])
        ranges[name] = rs
        results.append({"other": name, "always": False, "ranges": rs})
    out = {"parts": [o.name for o in movers], "axis_point_mm": [round(v * 1000.0, 3) for v in P],
           "axis_dir": [round(v, 4) for v in D], "from": a0, "to": a1, "step": step, "results": results}
    bpy.app.driver_namespace["autodraft_sweep_last"] = out
    if anim:
        scene = bpy.context.scene
        e = bpy.data.objects.new(SWEEP_EMPTY, None)
        e.empty_display_type = 'SINGLE_ARROW'
        e.empty_display_size = max(0.01, max(d for d in movers[0].dimensions) * 0.6) if movers else 0.05
        scene.collection.objects.link(e)
        # 바깥 Empty = 축선 위 · 로컬 Z 가 축 방향 / 안쪽 Empty = 그 자식, 로컬 Z 둘레로만 회전 (= 축선 둘레)
        e.matrix_world = Matrix.Translation(P) @ D.to_track_quat('Z', 'Y').to_matrix().to_4x4()
        spin = bpy.data.objects.new(SWEEP_EMPTY + "_회전", None)
        spin.empty_display_type = 'ARROWS'
        spin.empty_display_size = e.empty_display_size * 0.5
        scene.collection.objects.link(spin)
        spin.parent = e
        spin.matrix_parent_inverse = Matrix.Identity(4)
        spin.rotation_mode = 'XYZ'
        spin.location = (0.0, 0.0, 0.0)
        spin.rotation_euler = (0.0, 0.0, 0.0)
        bpy.context.view_layer.update()
        for o in group:
            mw = o.matrix_world.copy()
            o.parent = spin
            o.matrix_parent_inverse = spin.matrix_world.inverted()
            o.matrix_world = mw
        # 프레임 = 각도 간격이 되게 선형 보간 (Blender 5 의 층 구조 액션도 되도록 새 키프레임 기본값을 잠시 바꿈)
        prefs = bpy.context.preferences.edit
        old_interp = prefs.keyframe_new_interpolation_type
        prefs.keyframe_new_interpolation_type = 'LINEAR'
        try:
            spin.rotation_euler = (0.0, 0.0, math.radians(a0))
            spin.keyframe_insert(data_path="rotation_euler", index=2, frame=1)
            spin.rotation_euler = (0.0, 0.0, math.radians(a0 + n * step))
            spin.keyframe_insert(data_path="rotation_euler", index=2, frame=1 + n)
        finally:
            prefs.keyframe_new_interpolation_type = old_interp
        act = spin.animation_data.action if spin.animation_data else None
        if act is not None:
            for fc in _ad_fcurves(act):
                for kp in fc.keyframe_points:
                    kp.interpolation = 'LINEAR'
                fc.update()
        scene.frame_start = 1
        scene.frame_end = 1 + n
        bpy.app.driver_namespace["autodraft_sweep_anim"] = {
            "a0": a0, "step": step, "ranges": ranges, "movers": [o.name for o in movers],
            "rest": {k: [list(r) for r in v] for k, v in M0.items()}}
        bpy.app.handlers.frame_change_post.append(_ad_sweep_frame)
        scene.frame_set(1)
    if not quiet:
        print("AUTODRAFT_SWEEP", json.dumps(out, ensure_ascii=False))
    return out

class AUTODRAFT_OT_rotation_sweep(bpy.types.Operator):
    bl_idname = "autodraft.rotation_sweep"
    bl_label = "회전 간섭 검사"
    bl_description = ("선택한 부품(활성 부품의 원통면 축 둘레)을 각도별로 돌려 다른 부품과 닿는 각도 구간을 찾고, "
                      "애니메이션으로 만들어 닿는 구간에서 빨갛게 표시합니다")
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        sc = context.scene
        sel = [o for o in context.selected_objects if o.type == 'MESH']
        if context.active_object is not None and context.active_object.type == 'MESH':
            sel = [context.active_object] + [o for o in sel if o is not context.active_object]
        names = []
        for o in sel:
            b = _ad_base_name(o)
            if b not in names:
                names.append(b)
        res = autodraft_rotation_sweep(names, sc.autodraft_sweep_from, sc.autodraft_sweep_to,
                                       sc.autodraft_sweep_step, sc.autodraft_sweep_anim)
        if "error" in res:
            self.report({'ERROR'}, res["error"])
            return {'CANCELLED'}
        hit = [r for r in res["results"] if not r["always"]]
        if hit:
            self.report({'WARNING'}, ", ".join("%s %s" % (r["other"], " ".join("%g~%g°" % (a, b) for a, b in r["ranges"]))
                                               for r in hit))
        else:
            self.report({'INFO'}, "돌리는 동안 닿는 부품 없음")
        return {'FINISHED'}

class AUTODRAFT_OT_rotation_sweep_clear(bpy.types.Operator):
    bl_idname = "autodraft.rotation_sweep_clear"
    bl_label = "되돌리기"
    bl_description = "회전 애니메이션·빨간 표시를 지우고 부품을 원래 자리로"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        autodraft_sweep_clear()
        bpy.app.driver_namespace["autodraft_sweep_last"] = None
        return {'FINISHED'}

class AUTODRAFT_PT_rotation_sweep(bpy.types.Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "AutoDraft"
    bl_label = "회전 간섭 검사"

    def draw(self, context):
        sc = context.scene
        col = self.layout.column()
        col.label(text="돌릴 부품 선택 (축선 = 활성 부품 원통면)")
        row = col.row(align=True)
        row.prop(sc, "autodraft_sweep_from")
        row.prop(sc, "autodraft_sweep_to")
        col.prop(sc, "autodraft_sweep_step")
        col.prop(sc, "autodraft_sweep_anim")
        row = col.row(align=True)
        row.operator("autodraft.rotation_sweep", icon='FILE_REFRESH')
        row.operator("autodraft.rotation_sweep_clear", text="", icon='X')
        res = bpy.app.driver_namespace.get("autodraft_sweep_last")
        if not res:
            return
        box = col.box()
        box.label(text="돌린 부품: " + ", ".join(res["parts"]))
        hit = [r for r in res["results"] if not r["always"]]
        if not hit:
            box.label(text="돌리는 동안 닿는 부품 없음", icon='CHECKMARK')
        for r in hit:
            box.label(text=r["other"], icon='ERROR')
            box.label(text="    " + ", ".join("%g° ~ %g°" % (a, b) for a, b in r["ranges"]))
        for r in res["results"]:
            if r["always"]:
                box.label(text="항상 맞닿음: " + r["other"], icon='LINKED')
        if hit and sc.autodraft_sweep_anim:
            box.label(text="재생(스페이스): 닿는 구간에서 빨강", icon='PLAY')


def _ad_register():
    bpy.types.Scene.autodraft_clash_tol = bpy.props.FloatProperty(
        name="허용 (mm)", description="다각형 오차에 더해 접촉으로 볼 겹침 두께", default=0.0, min=0.0, precision=3)
    bpy.types.Scene.autodraft_clash_near = bpy.props.FloatProperty(
        name="틈 표시 (mm)", description="이 거리 안의 틈을 '틈새'로 보고", default=1.0, min=0.0, precision=2)
    bpy.types.Scene.autodraft_clash_ghost = bpy.props.BoolProperty(
        name="간섭 부품 반투명", description="간섭 난 부품을 반투명하게 해 빨간 겹침 부분이 보이게 합니다 (결과 지우기로 복원)",
        default=True)
    bpy.types.Scene.autodraft_sweep_from = bpy.props.FloatProperty(name="시작 (°)", default=0.0)
    bpy.types.Scene.autodraft_sweep_to = bpy.props.FloatProperty(name="끝 (°)", default=360.0)
    bpy.types.Scene.autodraft_sweep_step = bpy.props.FloatProperty(
        name="간격 (°)", description="몇 도마다 검사할지 (작을수록 정확·느림)", default=2.0, min=0.1, max=90.0)
    bpy.types.Scene.autodraft_sweep_anim = bpy.props.BoolProperty(
        name="애니메이션 만들기", description="회전축 Empty 키프레임 + 닿는 프레임 빨강 (되돌리기로 지움)", default=True)
    for c in (AUTODRAFT_OT_interference, AUTODRAFT_OT_clear_interference, AUTODRAFT_PT_interference,
              AUTODRAFT_OT_rotation_sweep, AUTODRAFT_OT_rotation_sweep_clear, AUTODRAFT_PT_rotation_sweep):
        try:
            bpy.utils.unregister_class(c)
        except Exception:
            pass
        bpy.utils.register_class(c)

_ad_register()
print("AUTODRAFT_FITS parts=%d" % _ad_load_fits())
bpy.app.driver_namespace["autodraft_interference"] = autodraft_interference
bpy.app.driver_namespace["autodraft_clear_interference"] = autodraft_clear_interference
bpy.app.driver_namespace["autodraft_rotation_sweep"] = autodraft_rotation_sweep
_bridge["ns"].update(autodraft_interference=autodraft_interference,
                     autodraft_clear_interference=autodraft_clear_interference,
                     autodraft_rotation_sweep=autodraft_rotation_sweep,
                     autodraft_sweep_clear=autodraft_sweep_clear)
)PY";

struct ObjWriter {
    QTextStream *out = nullptr;
    int nextIndex = 1;   // OBJ 꼭짓점 번호는 1부터
    double cx = 0.0;
    double cy = 0.0;
    double baseZ = 0.0;
    //   좌표는 언제나 도면 X·Y·Z (원점만 옮김). 세우기는 Blender 에서 물체 회전 X 90° 로 한다 (가져오기 스크립트)
    //   세우기면 원점 = (X 가운데, Y 최솟값, Z 가운데) → 원점 둘레로 X 90° 돌리면 Y 최솟값이 바닥(Z = 0)

    QHash<QString, int> shared; // 객체 안에서 같은 좌표 꼭짓점 공유 (Blender 에서 붙은 메시)

    void beginObject() { shared.clear(); }

    // 도면 (x, y, z mm, Z 위) → OBJ (m, Y 위): (x, z, -y)
    //   (X, Y, Z) = Blender 로컬 좌표 = 도면 축
    int vertex(double x, double y, double z)
    {
        double X, Y, Z;
        blender(x, y, z, X, Y, Z);
        // 같은 점은 같은 글자로: −0.000000 / 0.000000 이 갈리면 이음매가 떨어져 열린 메시가 되고,
        //   Blender Manifold 불리언(커터 빼기·합치기)이 빈 결과를 낸다 (세운 축·회전체는 축이 0 근처라 자주 생김)
        const QString key = coord(X) + QLatin1Char(' ') + coord(Z) + QLatin1Char(' ') + coord(-Y);
        QHash<QString, int>::const_iterator it = shared.constFind(key);
        if (it != shared.constEnd())
            return it.value();
        *out << "v " << key << '\n';
        shared.insert(key, nextIndex);
        return nextIndex++;
    }

    // 좌표 글자 (μm 까지). 0 으로 반올림되는 값은 부호를 떼어 "0.000000" 하나로
    static QString coord(double v)
    {
        double r = std::round(v * 1e6) / 1e6;
        if (r == 0.0)
            r = 0.0;                                    // −0.0 → +0.0
        return QString::number(r, 'f', 6);
    }

    // 도면 점 (mm) → Blender 좌표 (m, Z 위)
    void blender(double x, double y, double z, double &X, double &Y, double &Z) const
    {
        X = (x - cx) / 1000.0;
        Y = (y - cy) / 1000.0;
        Z = (z - baseZ) / 1000.0;
    }
    void face3(int a, int b, int c)
    {
        if (a == b || b == c || a == c)
            return; // 축 끝 등 면적 0 삼각형
        *out << "f " << a << ' ' << b << ' ' << c << '\n';
    }
    void face4(int a, int b, int c, int d)
    {
        *out << "f " << a << ' ' << b << ' ' << c << ' ' << d << '\n';
    }

    void cap(const QList<QPolygonF> &rings, double z, bool up)
    {
        const QVector<QPointF> tris = Model3DMesh::triangulate(rings);
        for (int i = 0; i + 2 < tris.size(); i += 3) {
            const int a = vertex(tris.at(i).x(), tris.at(i).y(), z);
            const int b = vertex(tris.at(i + 1).x(), tris.at(i + 1).y(), z);
            const int c = vertex(tris.at(i + 2).x(), tris.at(i + 2).y(), z);
            if (up)
                face3(a, b, c);
            else
                face3(a, c, b);
        }
    }

    void solid(const Model3DSolid &s)
    {
        if (s.z1 - s.z0 < 1e-6)
            return;
        cap(s.rings, s.z1, true);
        cap(s.rings, s.z0, false);
        for (int r = 0; r < s.rings.size(); ++r) {
            const QPolygonF &ring = s.rings.at(r);
            for (int k = 0; k < ring.size(); ++k) {
                const QPointF p = ring.at(k);
                const QPointF q = ring.at((k + 1) % ring.size());
                if (std::hypot(q.x() - p.x(), q.y() - p.y()) < 1e-6)
                    continue;
                // 바깥 링(CCW)·구멍 링(CW) 모두 p→q 오른쪽이 덩어리 바깥 → (p0, q0, q1, p1)
                const int a0 = vertex(p.x(), p.y(), s.z0);
                const int b0 = vertex(q.x(), q.y(), s.z0);
                const int b1 = vertex(q.x(), q.y(), s.z1);
                const int a1 = vertex(p.x(), p.y(), s.z1);
                face4(a0, b0, b1, a1);
            }
        }
    }
};

QString objName(const QString &kind, const QString &layer)
{
    QString n = kind + QLatin1Char('_') + layer;
    n.replace(QLatin1Char(' '), QLatin1Char('_'));
    return n;
}

} // namespace

bool Model3DExport::writeObj(const Model3D &model, const QString &objPath, QString *error)
{
    if (model.solids.isEmpty() && model.meshes.isEmpty()) {
        if (error)
            *error = QStringLiteral("내보낼 3D 형상이 없습니다.");
        return false;
    }

    // 평면 중심·최저 높이
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300, minZ = 1e300, maxZ = -1e300;
    for (int i = 0; i < model.solids.size(); ++i) {
        const Model3DSolid &s = model.solids.at(i);
        minZ = std::min(minZ, s.z0);
        for (int r = 0; r < s.rings.size(); ++r) {
            for (int k = 0; k < s.rings.at(r).size(); ++k) {
                const QPointF p = s.rings.at(r).at(k);
                minX = std::min(minX, p.x());
                maxX = std::max(maxX, p.x());
                minY = std::min(minY, p.y());
                maxY = std::max(maxY, p.y());
            }
        }
    }
    for (int i = 0; i < model.meshes.size(); ++i) {
        const Model3DMeshPart &part = model.meshes.at(i);
        if (!part.cutterFor.isEmpty())
            continue;
        for (int g = 0; g < part.groups.size(); ++g) {
            const QVector<QVector3D> &t = part.groups.at(g).tris;
            for (int k = 0; k < t.size(); ++k) {
                minX = std::min(minX, double(t.at(k).x()));
                maxX = std::max(maxX, double(t.at(k).x()));
                minY = std::min(minY, double(t.at(k).y()));
                maxY = std::max(maxY, double(t.at(k).y()));
                minZ = std::min(minZ, double(t.at(k).z()));
                maxZ = std::max(maxZ, double(t.at(k).z()));
            }
        }
    }

    QDir().mkpath(QFileInfo(objPath).absolutePath());
    const QString mtlPath = QFileInfo(objPath).absolutePath() + QLatin1Char('/') +
                            QFileInfo(objPath).completeBaseName() + QStringLiteral(".mtl");

    QFile mtl(mtlPath);
    if (!mtl.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("MTL 파일을 쓸 수 없습니다: %1").arg(mtlPath);
        return false;
    }
    {
        QTextStream m(&mtl);
        m << "# AutoDraft 3D materials\n"
          << "newmtl Wall\nKd 0.87 0.85 0.82\nKa 0 0 0\nKs 0.05 0.05 0.05\nd 1.0\nillum 2\n\n"
          << "newmtl Glass\nKd 0.55 0.75 0.95\nKa 0 0 0\nKs 0.5 0.5 0.5\nd 0.3\nillum 4\n\n"
          << "newmtl Steel\nKd 0.73 0.75 0.78\nKa 0 0 0\nKs 0.6 0.6 0.6\nNs 200\nd 1.0\nillum 2\n\n"
          << "newmtl Thread\nKd 0.47 0.49 0.53\nKa 0 0 0\nKs 0.4 0.4 0.4\nNs 100\nd 1.0\nillum 2\n\n"
          << "newmtl Cutter\nKd 0.9 0.2 0.2\nKa 0 0 0\nd 1.0\nillum 1\n";
    }
    mtl.close();

    QFile f(objPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("OBJ 파일을 쓸 수 없습니다: %1").arg(objPath);
        return false;
    }
    QTextStream out(&f);
    out << "# AutoDraft 3D export (단위 m, Y 위쪽)\n";
    if (model.standUp)
        out << "# AUTODRAFT_STANDUP\n";                 // 가져오기 스크립트가 읽어 물체를 X 90° 회전
    out << "mtllib " << QFileInfo(mtlPath).fileName() << "\n";

    ObjWriter w;
    w.out = &out;
    w.cx = (minX + maxX) / 2.0;
    w.cy = (minY + maxY) / 2.0;
    w.baseZ = minZ;
    if (model.standUp) {
        w.cy = minY;
        w.baseZ = (maxZ > minZ) ? (minZ + maxZ) / 2.0 : minZ;
    }

    // 객체별로 묶기: 종류 + 레이어
    QMap<QString, QList<int>> groups;
    for (int i = 0; i < model.solids.size(); ++i) {
        const Model3DSolid &s = model.solids.at(i);
        const QString kind = (s.kind == Model3DSolid::Wall) ? QStringLiteral("벽") : QStringLiteral("개구부벽");
        groups[objName(kind, s.layer)].append(i);
    }
    for (QMap<QString, QList<int>>::const_iterator it = groups.constBegin(); it != groups.constEnd(); ++it) {
        out << "o " << it.key() << "\nusemtl Wall\n";
        w.beginObject();
        const QList<int> &idx = it.value();
        for (int i = 0; i < idx.size(); ++i)
            w.solid(model.solids.at(idx.at(i)));
    }

    // 창 유리판
    bool glassStarted = false;
    for (int i = 0; i < model.openings.size(); ++i) {
        QPointF a, b;
        double z0 = 0.0, z1 = 0.0;
        if (!Model3DMesh::glassPane(model.openings.at(i), a, b, z0, z1))
            continue;
        if (!glassStarted) {
            out << "o 창유리\nusemtl Glass\n";
            w.beginObject();
            glassStarted = true;
        }
        const int v0 = w.vertex(a.x(), a.y(), z0);
        const int v1 = w.vertex(b.x(), b.y(), z0);
        const int v2 = w.vertex(b.x(), b.y(), z1);
        const int v3 = w.vertex(a.x(), a.y(), z1);
        w.face4(v0, v1, v2, v3);
    }

    // 메시 부품 (기계용). 한 부품 = 한 객체(재질은 usemtl 로 구분, 꼭짓점 공유 → 닫힌 입체).
    //   키홈 표시면(Keyway)은 Blender 에서 절삭체 불리언으로 실제 홈을 파므로 내보내지 않는다.
    for (int i = 0; i < model.meshes.size(); ++i) {
        const Model3DMeshPart &part = model.meshes.at(i);
        QString name = part.name;
        name.replace(QLatin1Char(' '), QLatin1Char('_'));
        out << "o " << name << "\n";
        w.beginObject();
        for (int g = 0; g < part.groups.size(); ++g) {
            const Model3DMeshGroup &grp = part.groups.at(g);
            if (grp.material == QLatin1String("Keyway"))
                continue;
            out << "usemtl " << grp.material << "\n";
            for (int k = 0; k + 2 < grp.tris.size(); k += 3) {
                const int a = w.vertex(grp.tris.at(k).x(), grp.tris.at(k).y(), grp.tris.at(k).z());
                const int b = w.vertex(grp.tris.at(k + 1).x(), grp.tris.at(k + 1).y(), grp.tris.at(k + 1).z());
                const int c = w.vertex(grp.tris.at(k + 2).x(), grp.tris.at(k + 2).y(), grp.tris.at(k + 2).z());
                w.face3(a, b, c);
            }
        }
    }
    out.flush();
    f.close();

    // 끼워맞춤 원통면 → <이름>_fits.json (Blender 좌표: m, Z 위, 가져온 직후 객체 좌표 = 월드 좌표)
    const QString fitsPath = QFileInfo(objPath).absolutePath() + QLatin1Char('/') +
                             QFileInfo(objPath).completeBaseName() + QStringLiteral("_fits.json");
    QJsonObject partsJson;
    for (int i = 0; i < model.meshes.size(); ++i) {
        const Model3DMeshPart &part = model.meshes.at(i);
        if (part.fits.isEmpty())
            continue;
        QString name = part.name;
        name.replace(QLatin1Char(' '), QLatin1Char('_'));
        QJsonArray arr;
        for (int k = 0; k < part.fits.size(); ++k) {
            const Model3DFitFeature &ft = part.fits.at(k);
            QJsonObject o;
            // 꼭짓점과 같은 로컬 좌표 (스크립트가 물체의 월드 변환을 곱해 씀)
            double bx = 0.0, by = 0.0, bz = 0.0;
            w.blender(ft.ox, ft.oy, ft.oz, bx, by, bz);
            o.insert(QStringLiteral("o"), QJsonArray{bx, by, bz});
            o.insert(QStringLiteral("d"), QJsonArray{ft.dx, ft.dy, ft.dz});
            o.insert(QStringLiteral("x0"), ft.x0 / 1000.0);
            o.insert(QStringLiteral("x1"), ft.x1 / 1000.0);
            o.insert(QStringLiteral("r"), ft.radius / 1000.0);
            o.insert(QStringLiteral("hole"), ft.hole);
            o.insert(QStringLiteral("tol"), ft.hasTol);
            o.insert(QStringLiteral("up"), ft.upper);
            o.insert(QStringLiteral("lo"), ft.lower);
            o.insert(QStringLiteral("label"), ft.label);
            arr.append(o);
        }
        partsJson.insert(name, arr);
    }
    if (partsJson.isEmpty()) {
        QFile::remove(fitsPath);
    } else {
        QFile fj(fitsPath);
        if (fj.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QJsonObject root;
            root.insert(QStringLiteral("units"), QStringLiteral("m (o, x0, x1, r) / mm (up, lo = 지름 허용차)"));
            root.insert(QStringLiteral("parts"), partsJson);
            fj.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            fj.close();
        }
    }
    return true;
}

bool Model3DExport::writeBlenderScript(const QString &objPath, QString *scriptPath, QString *error)
{
    const QFileInfo fi(objPath);
    const QString path = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral("_blender.py");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Blender 스크립트를 쓸 수 없습니다: %1").arg(path);
        return false;
    }
    QTextStream out(&f);
    out << QString::fromUtf8(kBlenderScript)
               .arg(QDir::fromNativeSeparators(fi.absoluteFilePath()), QString::number(MechModel3D::radialSteps()));
    out << QString::fromUtf8(kInterferenceScript);
    out.flush();
    f.close();
    if (scriptPath)
        *scriptPath = path;
    return true;
}

QString Model3DExport::findBlenderExe()
{
    const QString saved = QSettings().value(QString::fromLatin1(kBlenderKey)).toString();
    if (!saved.isEmpty() && QFileInfo::exists(saved))
        return saved;

    QStringList roots;
    roots << QStringLiteral("C:/Program Files/Blender Foundation")
          << QStringLiteral("C:/Program Files (x86)/Blender Foundation");
    for (int r = 0; r < roots.size(); ++r) {
        QDir dir(roots.at(r));
        if (!dir.exists())
            continue;
        QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        // 이름 역순 = 대체로 최신 버전 먼저 ("Blender 5.3" > "Blender 4.2")
        for (int i = subs.size() - 1; i >= 0; --i) {
            const QString exe = dir.absoluteFilePath(subs.at(i)) + QStringLiteral("/blender.exe");
            if (QFileInfo::exists(exe))
                return exe;
        }
    }
    return QString();
}

void Model3DExport::setBlenderExe(const QString &path)
{
    QSettings().setValue(QString::fromLatin1(kBlenderKey), path);
}

bool Model3DExport::openInBlender(const QString &objPath, const QString &blenderExe, QString *error)
{
    const QString exe = blenderExe.isEmpty() ? findBlenderExe() : blenderExe;
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        if (error)
            *error = QStringLiteral("blender.exe 를 찾을 수 없습니다.");
        return false;
    }
    QString script;
    if (!writeBlenderScript(objPath, &script, error))
        return false;
    QStringList args;
    args << QStringLiteral("--python") << QDir::toNativeSeparators(script);
    if (!QProcess::startDetached(exe, args, QFileInfo(objPath).absolutePath())) {
        if (error)
            *error = QStringLiteral("Blender 를 실행하지 못했습니다: %1").arg(exe);
        return false;
    }
    return true;
}

QString Model3DExport::tempObjPath(const QString &baseName)
{
    QString name = baseName.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("model");
    // 파일 이름에 못 쓰는 문자 정리
    const QString bad = QStringLiteral("\\/:*?\"<>|[]");
    for (int i = 0; i < bad.size(); ++i)
        name.replace(bad.at(i), QLatin1Char('_'));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/AutoDraft3D");
    return dir + QLatin1Char('/') + name + QStringLiteral(".obj");
}
