from mathutils import Vector
from mathutils.bvhtree import BVHTree

# ── 흰 벽 재질 ──
white = bpy.data.materials.get("WhiteWall") or bpy.data.materials.new("WhiteWall")
if white.node_tree is None:
    white.use_nodes = True
wn = white.node_tree.nodes
wb = wn.get("Principled BSDF")
if wb is None:
    wn.clear()
    wout = wn.new("ShaderNodeOutputMaterial")
    wb = wn.new("ShaderNodeBsdfPrincipled")
    wout.location = (300, 0)
    white.node_tree.links.new(wb.outputs["BSDF"], wout.inputs["Surface"])
wb.inputs["Base Color"].default_value = (0.92, 0.91, 0.89, 1.0)
wb.inputs["Roughness"].default_value = 0.9
white.diffuse_color = (0.92, 0.91, 0.89, 1.0)
brick = bpy.data.materials["Brick"]

walls = [o for o in bpy.data.objects
         if o.type == 'MESH' and (o.name.startswith("벽_") or o.name.startswith("개구부벽_"))]

# ── 모든 벽(유리 제외)을 하나의 광선 검사 트리로 ──
verts, polys = [], []
for o in walls:
    base = len(verts)
    mw = o.matrix_world
    verts.extend(mw @ v.co for v in o.data.vertices)
    polys.extend([base + i for i in p.vertices] for p in o.data.polygons)
tree = BVHTree.FromPolygons(verts, polys)

# ── 면마다: 바깥쪽으로 수평 광선 9개, 하나도 벽에 안 맞으면 바깥면 ──
outer_count, inner_count = 0, 0
for o in walls:
    me = o.data
    me.materials.clear()
    me.materials.append(brick)   # 0 = 벽돌
    me.materials.append(white)   # 1 = 흰색
    mw = o.matrix_world
    for p in me.polygons:
        n = (mw.to_3x3() @ p.normal).normalized()
        if abs(n.z) > 0.5:          # 윗면·아랫면
            p.material_index = 1
            inner_count += 1
            continue
        pts = [mw @ me.vertices[i].co for i in p.vertices]
        zs = [q.z for q in pts]
        z0, z1 = min(zs), max(zs)
        # 면의 수평 방향 끝점 두 개
        horiz = sorted(pts, key=lambda q: (q.x, q.y))
        a, b = horiz[0], horiz[-1]
        # 벽 끝 마구리·개구부 측면(폭 ≈ 벽 두께)은 안쪽 마감으로 본다
        if (Vector((a.x, a.y)) - Vector((b.x, b.y))).length <= 0.25:
            p.material_index = 1
            inner_count += 1
            continue
        hit = False
        for s in (0.15, 0.5, 0.85):
            for t in (0.2, 0.5, 0.8):
                origin = a.lerp(b, s)
                origin = Vector((origin.x, origin.y, z0 + (z1 - z0) * t)) + n * 0.005
                loc, _nrm, _idx, _dist = tree.ray_cast(origin, Vector((n.x, n.y, 0.0)).normalized(), 60.0)
                if loc is not None:
                    hit = True
                    break
            if hit:
                break
        if hit:
            p.material_index = 1
            inner_count += 1
        else:
            p.material_index = 0
            outer_count += 1
    me.update()

print("바깥면(벽돌) %d개, 안쪽·윗면(흰색) %d개" % (outer_count, inner_count))
