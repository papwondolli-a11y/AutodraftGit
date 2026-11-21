import bmesh

# ── 벽돌 재질 (Blender 내장 Brick Texture, 190x57 mm + 줄눈 10 mm) ──
mat = bpy.data.materials.get("Brick") or bpy.data.materials.new("Brick")
if mat.node_tree is None:
    mat.use_nodes = True
nodes = mat.node_tree.nodes
links = mat.node_tree.links
nodes.clear()
out = nodes.new("ShaderNodeOutputMaterial")
bsdf = nodes.new("ShaderNodeBsdfPrincipled")
brick = nodes.new("ShaderNodeTexBrick")
coord = nodes.new("ShaderNodeTexCoord")
bump = nodes.new("ShaderNodeBump")
out.location, bsdf.location, bump.location = (600, 0), (300, 0), (100, -250)
brick.location, coord.location = (-150, 0), (-400, 0)

brick.offset = 0.5
brick.inputs["Scale"].default_value = 1.0
brick.inputs["Brick Width"].default_value = 0.200   # 190 + 10 mm (m 단위 UV)
brick.inputs["Row Height"].default_value = 0.067    # 57 + 10 mm
brick.inputs["Mortar Size"].default_value = 0.010
brick.inputs["Color1"].default_value = (0.50, 0.20, 0.12, 1.0)
brick.inputs["Color2"].default_value = (0.38, 0.14, 0.08, 1.0)
brick.inputs["Mortar"].default_value = (0.78, 0.76, 0.72, 1.0)
bsdf.inputs["Roughness"].default_value = 0.85
bump.inputs["Strength"].default_value = 0.4

links.new(coord.outputs["UV"], brick.inputs["Vector"])
links.new(brick.outputs["Color"], bsdf.inputs["Base Color"])
links.new(brick.outputs["Fac"], bump.inputs["Height"])
links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

# ── 벽 객체에 UV(실제 m 단위, 면 방향별 투영) + 재질 ──
done = []
for o in bpy.data.objects:
    if o.type != 'MESH' or not (o.name.startswith("벽_") or o.name.startswith("개구부벽_")):
        continue
    me = o.data
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv.verify()
    mw = o.matrix_world
    for f in bm.faces:
        n = f.normal
        for loop in f.loops:
            p = mw @ loop.vert.co
            if abs(n.z) > 0.7:          # 윗면·아랫면
                loop[uv].uv = (p.x, p.y)
            elif abs(n.x) > abs(n.y):   # X 방향을 보는 벽면 → (Y, Z)
                loop[uv].uv = (p.y, p.z)
            else:                       # Y 방향을 보는 벽면 → (X, Z)
                loop[uv].uv = (p.x, p.z)
    bm.to_mesh(me)
    bm.free()
    me.materials.clear()
    me.materials.append(mat)
    done.append(o.name)

sp = view3d_space()
if sp:
    sp.shading.type = 'MATERIAL'
print("brick applied:", done)
