"""
Pedal Trinity - scena Blender (Cycles) per generare:
  * build/background.png   -> "foto" frontale fissa dei tre pedali (senza pomelli)
  * build/sheet/f_XXX.png  -> fotogrammi 3D di pomelli, cursori e levetta
  * build/positions.json   -> posizioni proiettate (pixel) per l'interfaccia C++

Uso:
  blender -b --factory-startup -P tools/art/render_scene.py -- [--preview] [--only bg|sheet]
Poi:  python3 tools/art/assemble.py
"""
import bmesh
import bpy
import json
import math
import os
import sys

from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import layout as L  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
PREVIEW = "--preview" in argv
ONLY = argv[argv.index("--only") + 1] if "--only" in argv else "all"
BUILD = os.path.join(HERE, "build")
SHEET_DIR = os.path.join(BUILD, "sheet")
os.makedirs(SHEET_DIR, exist_ok=True)

N_FRAMES = 64
TILT = math.radians(L.TILT_DEG)
COS, SIN = math.cos(TILT), math.sin(TILT)

# --------------------------------------------------------------------------
# Scena di base
# --------------------------------------------------------------------------
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)
scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.use_denoising = True
try:
    scene.cycles.denoiser = "OPENIMAGEDENOISE"
except Exception:
    pass
scene.cycles.use_adaptive_sampling = True
scene.cycles.adaptive_threshold = 0.015
scene.cycles.max_bounces = 6
scene.cycles.diffuse_bounces = 2
scene.cycles.glossy_bounces = 3
scene.cycles.transmission_bounces = 3
scene.cycles.transparent_max_bounces = 4
scene.render.use_persistent_data = True
scene.view_settings.view_transform = "AgX"
try:
    scene.view_settings.look = "AgX - Medium High Contrast"
except Exception:
    pass
scene.view_settings.exposure = 0.0
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_depth = "8"
scene.unit_settings.system = "METRIC"

MAIN = bpy.data.collections.new("main")
SHEET = bpy.data.collections.new("sheet")
scene.collection.children.link(MAIN)
scene.collection.children.link(SHEET)


def link(obj, coll):
    coll.objects.link(obj)
    return obj


# --------------------------------------------------------------------------
# Materiali
# --------------------------------------------------------------------------
def new_mat(name):
    m = bpy.data.materials.new(name)
    try:
        m.use_nodes = True
    except Exception:
        pass
    return m


def bsdf_of(m):
    return next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")


def set_in(node, name, value):
    if name in node.inputs:
        node.inputs[name].default_value = value


def principled(name, color, metallic=0.0, rough=0.4, coat=0.0, spec=0.5, aniso=0.0):
    m = new_mat(name)
    b = bsdf_of(m)
    set_in(b, "Base Color", (*color, 1.0))
    set_in(b, "Metallic", metallic)
    set_in(b, "Roughness", rough)
    set_in(b, "Coat Weight", coat)
    set_in(b, "Coat Roughness", 0.06)
    set_in(b, "Specular IOR Level", spec)
    set_in(b, "Anisotropic", aniso)
    return m


def srgb(c):
    def lin(v):
        v = v / 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return tuple(lin(v) for v in c)


def projected_texture_mat(name, image_file, x0, x1, y0, y1, metallic=0.0, rough=0.35,
                          coat=0.6, flake=0.0, aniso=0.0):
    """Materiale con immagine proiettata dall'alto in coordinate oggetto."""
    m = principled(name, (0.5, 0.5, 0.5), metallic, rough, coat, aniso=aniso)
    nt = m.node_tree
    b = bsdf_of(m)
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1.0 / (x1 - x0), 1.0 / (y1 - y0), 1.0)
    mp.inputs["Location"].default_value = (-x0 / (x1 - x0), -y0 / (y1 - y0), 0.0)
    im = nt.nodes.new("ShaderNodeTexImage")
    im.image = bpy.data.images.load(os.path.join(BUILD, image_file))
    im.extension = "EXTEND"
    im.interpolation = "Cubic"
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    nt.links.new(mp.outputs["Vector"], im.inputs["Vector"])
    nt.links.new(im.outputs["Color"], b.inputs["Base Color"])
    if flake > 0:
        nz = nt.nodes.new("ShaderNodeTexNoise")
        nz.inputs["Scale"].default_value = 9000.0
        bp = nt.nodes.new("ShaderNodeBump")
        bp.inputs["Strength"].default_value = flake
        bp.inputs["Distance"].default_value = 0.00002
        nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
        nt.links.new(nz.outputs["Fac"], bp.inputs["Height"])
        nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return m


def bumped(m, scale, strength, wave=False, distance=0.0001):
    nt = m.node_tree
    b = bsdf_of(m)
    tc = nt.nodes.new("ShaderNodeTexCoord")
    if wave:
        tx = nt.nodes.new("ShaderNodeTexWave")
        tx.wave_type = "BANDS"
        tx.bands_direction = "Y"
        tx.inputs["Scale"].default_value = scale
        out = tx.outputs["Fac"]
    else:
        tx = nt.nodes.new("ShaderNodeTexNoise")
        tx.inputs["Scale"].default_value = scale
        tx.inputs["Detail"].default_value = 6.0
        out = tx.outputs["Fac"]
    bp = nt.nodes.new("ShaderNodeBump")
    bp.inputs["Strength"].default_value = strength
    bp.inputs["Distance"].default_value = distance
    nt.links.new(tc.outputs["Object"], tx.inputs["Vector"])
    nt.links.new(out, bp.inputs["Height"])
    nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return m


MAT_KNOB = principled("knob_black", (0.012, 0.012, 0.013), 0.0, 0.33, 0.0, spec=0.6)
MAT_WHITE = principled("pointer_white", (0.85, 0.85, 0.82), 0.0, 0.4)
MAT_CHROME = principled("chrome", (0.9, 0.9, 0.92), 1.0, 0.12)
MAT_NICKEL = principled("nickel", (0.75, 0.74, 0.72), 1.0, 0.28)
MAT_RUBBER = bumped(principled("rubber", (0.018, 0.018, 0.018), 0.0, 0.75, spec=0.3),
                    420.0, 0.35, wave=True, distance=0.0004)
MAT_FOOT = bumped(principled("footswitch", (0.02, 0.02, 0.021), 0.0, 0.55),
                  2500.0, 0.25)
MAT_LED_OFF = principled("led_off", (0.25, 0.01, 0.01), 0.0, 0.08, spec=0.9)
set_in(bsdf_of(MAT_LED_OFF), "Transmission Weight", 0.4)
MAT_BOARD = bumped(principled("board", (0.020, 0.020, 0.022), 0.0, 0.92, spec=0.25),
                   2600.0, 0.55, distance=0.0006)
MAT_SLOT = principled("slot", (0.003, 0.003, 0.003), 0.0, 0.9)
MAT_ALU_DARK = principled("alu_dark", (0.05, 0.05, 0.055), 0.6, 0.35)


def paint_color(rgb, metallic=0.25, rough=0.32):
    return principled("paint_%d_%d_%d" % rgb, srgb(rgb), metallic, rough, 0.8)


# --------------------------------------------------------------------------
# Geometria
# --------------------------------------------------------------------------
def mesh_obj(name, bm, coll, mats, location=(0, 0, 0), smooth=True):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = smooth
    for m in mats:
        me.materials.append(m)
    ob = bpy.data.objects.new(name, me)
    ob.location = location
    return link(ob, coll)


def rounded_rect_pts(w, d, r, seg=10):
    pts = []
    r = min(r, w / 2 - 1e-5, d / 2 - 1e-5)
    corners = [(w / 2 - r, d / 2 - r, 0), (-w / 2 + r, d / 2 - r, 90),
               (-w / 2 + r, -d / 2 + r, 180), (w / 2 - r, -d / 2 + r, 270)]
    for cx, cy, a0 in corners:
        for i in range(seg + 1):
            a = math.radians(a0 + 90.0 * i / seg)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def rounded_box(name, w, d, h, corner, edge, coll, mat, location=(0, 0, 0), z0=0.0,
                seg=10, bevel_seg=5):
    bm = bmesh.new()
    pts = rounded_rect_pts(w, d, corner, seg)
    bot = [bm.verts.new((x, y, z0)) for x, y in pts]
    top = [bm.verts.new((x, y, z0 + h)) for x, y in pts]
    n = len(pts)
    bm.faces.new(list(reversed(bot)))
    bm.faces.new(top)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new([bot[i], bot[j], top[j], top[i]])
    ob = mesh_obj(name, bm, coll, [mat], location)
    if edge > 0:
        bv = ob.modifiers.new("bevel", "BEVEL")
        bv.width = edge
        bv.segments = bevel_seg
        bv.limit_method = "ANGLE"
        bv.angle_limit = math.radians(50)
        bv.harden_normals = True
    ob.modifiers.new("wn", "WEIGHTED_NORMAL").keep_sharp = True
    return ob


def cylinder(name, r, h, coll, mat, location=(0, 0, 0), seg=48, edge=0.0, z0=0.0,
             rotation=(0, 0, 0), r_top=None):
    bm = bmesh.new()
    rt = r if r_top is None else r_top
    bot = [bm.verts.new((r * math.cos(2 * math.pi * i / seg), r * math.sin(2 * math.pi * i / seg), z0))
           for i in range(seg)]
    top = [bm.verts.new((rt * math.cos(2 * math.pi * i / seg), rt * math.sin(2 * math.pi * i / seg), z0 + h))
           for i in range(seg)]
    bm.faces.new(list(reversed(bot)))
    bm.faces.new(top)
    for i in range(seg):
        j = (i + 1) % seg
        bm.faces.new([bot[i], bot[j], top[j], top[i]])
    ob = mesh_obj(name, bm, coll, [mat], location)
    ob.rotation_euler = rotation
    if edge > 0:
        bv = ob.modifiers.new("bevel", "BEVEL")
        bv.width = edge
        bv.segments = 3
        bv.limit_method = "ANGLE"
        bv.harden_normals = True
    ob.modifiers.new("wn", "WEIGHTED_NORMAL").keep_sharp = True
    return ob


def hex_nut(name, r, h, coll, location):
    return cylinder(name, r, h, coll, MAT_NICKEL, location, seg=6, edge=0.0002)


def lathe(name, rings, coll, rib=None, nseg=192, pointer=None, side_line=None, hole=None):
    """Solido di rotazione: rings = [(z, raggio, rigato, spigolo_vivo)] dal basso in alto.
    Se hole = (raggio, profondita') la sommita' e' un anello con foro centrale."""
    bm = bmesh.new()
    loops = []
    for z, rad, ribbed, _ in rings:
        ring = []
        for j in range(nseg):
            a = 2 * math.pi * j / nseg
            rr = rad * (rib(a) if (ribbed and rib) else 1.0)
            ring.append(bm.verts.new((rr * math.sin(a), rr * math.cos(a), z)))
        loops.append(ring)
    bm.faces.new(list(reversed(loops[0])))
    for k in range(len(loops) - 1):
        a, b = loops[k], loops[k + 1]
        for j in range(nseg):
            jj = (j + 1) % nseg
            bm.faces.new([a[j], a[jj], b[jj], b[j]])
    ztop = rings[-1][0]
    if hole:
        hr, hd = hole
        hl = [bm.verts.new((hr * math.sin(2 * math.pi * j / nseg), hr * math.cos(2 * math.pi * j / nseg), ztop))
              for j in range(nseg)]
        hb = [bm.verts.new((hr * math.sin(2 * math.pi * j / nseg), hr * math.cos(2 * math.pi * j / nseg), ztop - hd))
              for j in range(nseg)]
        last = loops[-1]
        for j in range(nseg):
            jj = (j + 1) % nseg
            bm.faces.new([last[j], last[jj], hl[jj], hl[j]])
            bm.faces.new([hl[j], hl[jj], hb[jj], hb[j]])
        bm.faces.new(hb)
        loops.append(hl)
    else:
        c = bm.verts.new((0, 0, ztop))
        last = loops[-1]
        for j in range(nseg):
            bm.faces.new([last[j], last[(j + 1) % nseg], c])
    bm.edges.ensure_lookup_table()
    # spigoli vivi sugli anelli marcati
    sharp_z = {round(z, 7) for z, _, _, s in rings if s}
    for e in bm.edges:
        z0, z1 = e.verts[0].co.z, e.verts[1].co.z
        if abs(z0 - z1) < 1e-9 and round(z0, 7) in sharp_z:
            e.smooth = False
    if hole:
        for e in bm.edges:
            if all(abs(v.co.z - ztop) < 1e-9 for v in e.verts):
                e.smooth = False
    mats = [MAT_KNOB, MAT_WHITE]
    if pointer:
        r0, r1, wdt, zt = pointer
        box_line(bm, (-wdt / 2, r0, zt), (wdt / 2, r1, zt + 0.00012), 1)
    if side_line:
        rr, z0, z1, wdt = side_line
        box_line(bm, (-wdt / 2, rr - 0.00005, z0), (wdt / 2, rr + 0.00025, z1), 1)
    ob = mesh_obj(name, bm, coll, mats)
    return ob


def box_line(bm, a, b, mat_index):
    x0, y0, z0 = a
    x1, y1, z1 = b
    v = [bm.verts.new(p) for p in [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                                   (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]]
    for f in [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]:
        face = bm.faces.new([v[i] for i in f])
        face.material_index = mat_index
        face.smooth = False


def ts_rib(a):
    s = 0.5 - 0.5 * math.cos(24 * a)
    return 1.0 - 0.07 * (s ** 3)


def boss_rib(a):
    s = 0.5 - 0.5 * math.cos(40 * a)
    return 1.0 - 0.045 * (s ** 2)


def make_knob(kind, coll, name):
    t = L.KNOB_TYPES[kind]
    r, h = t["r"], t["h"]
    if kind.startswith("ts"):
        rings = [(0.0, r * 1.13, False, False), (0.0016, r * 1.13, False, True),
                 (0.0019, r * 1.10, False, True), (0.0019, r, True, True),
                 (h * 0.86, r, True, True), (h * 0.93, r * 0.95, False, False),
                 (h, r * 0.86, False, True)]
        return lathe(name, rings, coll, ts_rib, pointer=(r * 0.12, r * 0.80, 0.0010, h))
    if kind == "boss":
        rings = [(0.0, r, True, False), (h * 0.88, r, True, True),
                 (h * 0.95, r * 0.95, False, False), (h, r * 0.88, False, True)]
        return lathe(name, rings, coll, boss_rib, pointer=(r * 0.10, r * 0.82, 0.0010, h),
                     side_line=(r, 0.0006, h * 0.86, 0.0009))
    if kind == "boss_outer":
        rings = [(0.0, r, True, False), (h * 0.84, r, True, True),
                 (h * 0.93, r * 0.95, False, False), (h, r * 0.90, False, True)]
        return lathe(name, rings, coll, boss_rib, hole=(0.0042, 0.002),
                     pointer=(0.0050, r * 0.88, 0.0010, h),
                     side_line=(r, 0.0006, h * 0.82, 0.0009))
    if kind == "boss_inner":
        rings = [(0.0, r, True, False), (h * 0.85, r, True, True),
                 (h * 0.93, r * 0.94, False, False), (h, r * 0.86, False, True)]
        return lathe(name, rings, coll, boss_rib, pointer=(r * 0.10, r * 0.80, 0.0009, h))
    raise ValueError(kind)


def make_slider_cap(coll, name):
    c = L.SLIDER_CAP
    ob = rounded_box(name, c["w"], c["d"], c["h"], 0.0009, 0.0006, coll, MAT_KNOB, bevel_seg=3)
    line = rounded_box(name + "_line", c["w"] * 0.86, 0.0007, 0.00012, 0.0002, 0.0, coll, MAT_WHITE,
                       z0=c["h"])
    line.parent = ob
    stem = rounded_box(name + "_stem", 0.0012, 0.0030, 0.002, 0.0002, 0.0, coll, MAT_NICKEL, z0=-0.0015)
    stem.parent = ob
    return ob


def make_toggle_lever(coll, name):
    root = bpy.data.objects.new(name, None)
    link(root, coll)
    lev = cylinder(name + "_bat", 0.00135, 0.0120, coll, MAT_CHROME, seg=32, r_top=0.0007)
    lev.parent = root
    tip = cylinder(name + "_tip", 0.0011, 0.0014, coll, MAT_CHROME, seg=32, z0=0.0117, edge=0.0005)
    tip.parent = root
    return root


# --------------------------------------------------------------------------
# Pedali
# --------------------------------------------------------------------------
def add_led(coll, x, y, z, r):
    cylinder("led_bezel", r * 1.45, 0.0012, coll, MAT_CHROME, (x, y, z), seg=40, edge=0.0003)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=r)
    for v in bm.verts:
        v.co.z = max(v.co.z, 0.0) * 0.8
    ob = mesh_obj("led_dome", bm, coll, [MAT_LED_OFF], (x, y, z + 0.0010))
    return ob


def add_jacks(coll, cx, W, y, z):
    for s in (-1, 1):
        cylinder("jack_nut", 0.0055, 0.0030, coll, MAT_NICKEL, (cx + s * (W / 2 - 0.0005), y, z),
                 seg=6, rotation=(0, s * math.pi / 2, 0), edge=0.0003)
        cylinder("jack_ring", 0.0046, 0.0042, coll, MAT_CHROME, (cx + s * (W / 2 - 0.0005), y, z),
                 seg=40, rotation=(0, s * math.pi / 2, 0), edge=0.0003)


def build_ts(coll):
    p = L.TS
    cx = p["cx"]
    mat = projected_texture_mat("ed9_top", "tex_ed9_top.png", -p["W"] / 2, p["W"] / 2,
                                -p["D"] / 2, p["D"] / 2, metallic=0.35, rough=0.30, coat=0.9,
                                flake=0.25)
    rounded_box("ed9_body", p["W"], p["D"], p["H"], p["corner"], p["edge"], coll, mat, (cx, 0, 0))
    fs = p["footswitch"]
    rounded_box("ed9_fs_frame", fs["w"] + 0.004, fs["d"] + 0.004, 0.0016, 0.004, 0.0006, coll,
                MAT_NICKEL, (cx + fs["x"], fs["y"], p["H"]))
    rounded_box("ed9_fs", fs["w"], fs["d"], fs["h"], 0.0035, 0.0018, coll, MAT_FOOT,
                (cx + fs["x"], fs["y"], p["H"]))
    led = p["led"]
    add_led(coll, cx + led["x"], led["y"], p["H"], led["r"])
    add_jacks(coll, cx, p["W"], 0.028, 0.024)


def build_boss(coll, p, panel_tex, tread_tex, metallic, knob_like=True):
    cx = p["cx"]
    body_mat = paint_color(p["paint"], metallic=metallic, rough=0.34)
    W, D = p["W"], p["D"]
    rounded_box(p["id"] + "_base", W, D, p["base_h"], p["corner"], p["edge"], coll, body_mat, (cx, 0, 0))
    # blocco pannello comandi
    py0 = p["panel_y0"]
    pd = D / 2 - py0
    pm = projected_texture_mat(p["id"] + "_panel", panel_tex, -W / 2, W / 2, -pd / 2, pd / 2,
                               metallic=0.15, rough=0.45, coat=0.3)
    rounded_box(p["id"] + "_panelblk", W, pd, p["panel_h"], min(p["corner"], pd / 2 - 0.001),
                p["edge"], coll, pm, (cx, py0 + pd / 2, 0))
    # treadle (pedale inclinato)
    length = math.hypot(p["tread_y1"] - p["tread_y0"], p["tread_h_back"] - p["tread_h_front"])
    tm = projected_texture_mat(p["id"] + "_tread", tread_tex, -W / 2, W / 2, -length / 2, length / 2,
                               metallic=metallic, rough=0.30, coat=0.9, flake=0.2)
    thick = 0.014
    ang = L.tread_angle()
    ymid = (p["tread_y0"] + p["tread_y1"]) / 2
    zmid = (p["tread_h_front"] + p["tread_h_back"]) / 2
    tr = rounded_box(p["id"] + "_treadle", W - 0.0006, length, thick, p["corner"], p["edge"] * 1.3,
                     coll, tm, z0=-thick)
    tr.location = (cx, ymid, zmid)
    tr.rotation_euler = (ang, 0, 0)
    # gomma antiscivolo sul treadle (coordinate locali)
    ry0 = -length / 2 + (p["rubber_y0"] - p["tread_y0"]) / (p["tread_y1"] - p["tread_y0"]) * length
    ry1 = -length / 2 + (p["rubber_y1"] - p["tread_y0"]) / (p["tread_y1"] - p["tread_y0"]) * length
    rb = rounded_box(p["id"] + "_rubber", W - 0.008, ry1 - ry0, 0.0022, 0.004, 0.0008, coll, MAT_RUBBER,
                     ((0, (ry0 + ry1) / 2, 0)))
    rb.parent = tr
    add_jacks(coll, cx, W, 0.042, 0.022)
    return tr


def build_mt(coll):
    p = L.MT
    build_boss(coll, p, "tex_mc2_panel.png", "tex_mc2_tread.png", 0.15)
    cx, top = p["cx"], p["panel_h"]
    add_led(coll, cx + p["led"]["x"], p["led"]["y"], top, p["led"]["r"])
    t = p["toggle"]
    hex_nut("toggle_nut", 0.0026, 0.0016, coll, (cx + t["x"], t["y"], top))
    cylinder("toggle_collar", 0.0017, 0.0048, coll, MAT_CHROME, (cx + t["x"], t["y"], top), seg=32,
             edge=0.0003)


def build_eq(coll):
    p = L.EQ
    tr = build_boss(coll, p, "tex_gq7_panel.png", "tex_gq7_tread.png", 0.55)
    cx = p["cx"]
    led = p["led"]
    z = L.tread_top_z(led["y"])
    add_led(coll, cx + led["x"], led["y"], z, led["r"])


def build_board_and_plate(coll):
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=0.6)
    mesh_obj("board", bm, coll, [MAT_BOARD], (0, 0, 0), smooth=False)
    pl = L.PLATE
    pm = projected_texture_mat("plate", "tex_plate.png", -pl["w"] / 2, pl["w"] / 2, -pl["d"] / 2,
                               pl["d"] / 2, metallic=1.0, rough=0.30, coat=0.0, aniso=0.6)
    rounded_box("plate", pl["w"], pl["d"], pl["h"], 0.0015, 0.0007, coll, pm, (pl["x"], pl["y"], 0))
    ib = L.INFO_BUTTON
    cylinder("info_btn", ib["r"], 0.0032, coll, MAT_CHROME, (ib["x"], ib["y"], pl["h"]), seg=64,
             edge=0.0009)
    cu = bpy.data.curves.new("info_i", "FONT")
    cu.body = "i"
    cu.align_x = "CENTER"
    cu.align_y = "CENTER"
    cu.size = 0.0075
    cu.extrude = 0.00015
    try:
        cu.font = bpy.data.fonts.load("/usr/share/fonts/truetype/lato/Lato-Black.ttf")
    except Exception:
        pass
    ti = bpy.data.objects.new("info_i", cu)
    ti.data.materials.append(MAT_ALU_DARK)
    ti.location = (ib["x"], ib["y"], pl["h"] + 0.0032 + 0.0001)
    link(ti, coll)


# --------------------------------------------------------------------------
# Luci e mondo (luci "sun": identiche per sfondo e fotogrammi)
# --------------------------------------------------------------------------
def add_sun(name, direction, strength, angle_deg):
    ld = bpy.data.lights.new(name, "SUN")
    ld.energy = strength
    ld.angle = math.radians(angle_deg)
    ob = bpy.data.objects.new(name, ld)
    ob.rotation_euler = Vector(direction).normalized().to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(ob)


add_sun("key", (0.42, -0.30, -0.86), 4.2, 9.0)
add_sun("fill", (-0.65, 0.20, -0.72), 0.9, 35.0)
add_sun("rim", (0.05, -0.85, -0.45), 0.8, 20.0)

world = bpy.data.worlds.new("studio")
scene.world = world
try:
    world.use_nodes = True
except Exception:
    pass
wn = world.node_tree
bg = next(n for n in wn.nodes if n.type == "BACKGROUND")
tc = wn.nodes.new("ShaderNodeTexCoord")
sep = wn.nodes.new("ShaderNodeSeparateXYZ")
ramp = wn.nodes.new("ShaderNodeValToRGB")
ramp.color_ramp.elements[0].position = 0.45
ramp.color_ramp.elements[0].color = (0.004, 0.004, 0.005, 1)
ramp.color_ramp.elements[1].position = 0.98
ramp.color_ramp.elements[1].color = (0.55, 0.56, 0.60, 1)
wn.links.new(tc.outputs["Generated"], sep.inputs["Vector"])
wn.links.new(sep.outputs["Z"], ramp.inputs["Fac"])
wn.links.new(ramp.outputs["Color"], bg.inputs["Color"])
bg.inputs["Strength"].default_value = 0.9


# --------------------------------------------------------------------------
# Camera
# --------------------------------------------------------------------------
PPM = L.UI_WIDTH * L.RENDER_SCALE / (L.VIEW_X_MAX - L.VIEW_X_MIN)   # pixel/metro nel render


def make_camera(name, center_world, width_m, res_x, res_y):
    cd = bpy.data.cameras.new(name)
    cd.type = "ORTHO"
    cd.ortho_scale = width_m
    cd.clip_start = 0.001
    cd.clip_end = 10.0
    cd.sensor_fit = "HORIZONTAL"
    cam = bpy.data.objects.new(name, cd)
    direction = Vector((0.0, SIN, -COS))
    cam.location = Vector(center_world) - direction * 1.0
    cam.rotation_euler = (TILT, 0.0, 0.0)
    scene.collection.objects.link(cam)
    return cam


def project(P, center_x, v_center, res_x, res_y):
    """Proiezione ortografica -> pixel (origine in alto a sinistra)."""
    u = P[0] - center_x
    v = P[1] * COS + P[2] * SIN - v_center
    return (res_x / 2 + u * PPM, res_y / 2 - v * PPM)


MAIN_RES_X = L.UI_WIDTH * L.RENDER_SCALE
MAIN_RES_Y = int(round((L.VIEW_V_MAX - L.VIEW_V_MIN) * PPM))
V_C = (L.VIEW_V_MAX + L.VIEW_V_MIN) / 2
MAIN_CX = (L.VIEW_X_MAX + L.VIEW_X_MIN) / 2


def pmain(P):
    x, y = project(P, MAIN_CX, V_C, MAIN_RES_X, MAIN_RES_Y)
    return [round(x / L.RENDER_SCALE, 3), round(y / L.RENDER_SCALE, 3)]


# --------------------------------------------------------------------------
# Costruzione e render dello sfondo
# --------------------------------------------------------------------------
build_board_and_plate(MAIN)
build_ts(MAIN)
build_mt(MAIN)
build_eq(MAIN)

cam_main = make_camera("cam_main", (MAIN_CX, V_C / COS, 0.0), L.VIEW_X_MAX - L.VIEW_X_MIN,
                       MAIN_RES_X, MAIN_RES_Y)

# ---- posizioni per l'interfaccia (px @1x) ----
pos = dict(ui_w=L.UI_WIDTH, ui_h=MAIN_RES_Y / L.RENDER_SCALE, render_scale=L.RENDER_SCALE,
           ppm_1x=PPM / L.RENDER_SCALE, cos=COS, sin=SIN, knobs=[], sliders=[], leds=[],
           toggles=[], footswitches=[])
ts = L.TS
for k in ts["knobs"]:
    t = L.KNOB_TYPES[k["type"]]
    P = (ts["cx"] + k["x"], k["y"], ts["H"])
    pos["knobs"].append(dict(id=k["id"], type=k["type"], anchor=pmain(P),
                             top=pmain((P[0], P[1], P[2] + t["h"])), r=t["r"]))
mt = L.MT
for k in mt["knobs"]:
    t = L.KNOB_TYPES[k["type"]]
    P = (mt["cx"] + k["x"], k["y"], mt["panel_h"])
    pos["knobs"].append(dict(id=k["id"], type=k["type"], anchor=pmain(P),
                             top=pmain((P[0], P[1], P[2] + t["h"])), r=t["r"]))
    if "inner" in k:
        ti = L.KNOB_TYPES["boss_inner"]
        Pi = (P[0], P[1], P[2] + t["h"])
        pos["knobs"].append(dict(id=k["inner"]["id"], type="boss_inner", anchor=pmain(Pi),
                                 top=pmain((Pi[0], Pi[1], Pi[2] + ti["h"])), r=ti["r"]))
eq = L.EQ
for i, s in enumerate(eq["sliders"]):
    x = eq["cx"] + L.slider_x(i)
    pos["sliders"].append(dict(id=s["id"], a_min=pmain((x, eq["slot_y0"], eq["panel_h"])),
                               a_max=pmain((x, eq["slot_y1"], eq["panel_h"]))))
for p in (ts, mt):
    z = p.get("H", p.get("panel_h"))
    pos["leds"].append(dict(id=p["id"], c=pmain((p["cx"] + p["led"]["x"], p["led"]["y"], z + 0.0022)),
                            r=p["led"]["r"]))
pos["leds"].append(dict(id=eq["id"], c=pmain((eq["cx"] + eq["led"]["x"], eq["led"]["y"],
                                              L.tread_top_z(eq["led"]["y"]) + 0.0022)),
                        r=eq["led"]["r"]))
t = mt["toggle"]
pos["toggles"].append(dict(id=t["id"], anchor=pmain((mt["cx"] + t["x"], t["y"], mt["panel_h"]))))
fs = ts["footswitch"]
pos["footswitches"].append(dict(id=ts["id"], quad=[
    pmain((ts["cx"] + fs["x"] + sx * fs["w"] / 2, fs["y"] + sy * fs["d"] / 2, ts["H"] + fs["h"]))
    for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]))
for p in (mt, eq):
    pos["footswitches"].append(dict(id=p["id"], quad=[
        pmain((p["cx"] + sx * p["W"] / 2, yy, L.tread_top_z(yy)))
        for sx, yy in ((-1, p["tread_y0"]), (1, p["tread_y0"]), (1, p["tread_y1"]), (-1, p["tread_y1"]))]))
ib = L.INFO_BUTTON
pos["info"] = dict(c=pmain((ib["x"], ib["y"], L.PLATE["h"] + 0.0032)), r=ib["r"])


def render_to(path, samples):
    scene.cycles.samples = samples
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


if ONLY in ("all", "bg"):
    scene.camera = cam_main
    scene.render.resolution_x = MAIN_RES_X
    scene.render.resolution_y = MAIN_RES_Y
    scene.render.resolution_percentage = 35 if PREVIEW else 100
    scene.render.film_transparent = False
    scene.render.image_settings.color_mode = "RGB"
    SHEET.hide_render = True
    MAIN.hide_render = False
    render_to(os.path.join(BUILD, "background_preview.png" if PREVIEW else "background.png"),
              48 if PREVIEW else 320)

# --------------------------------------------------------------------------
# Foglio dei fotogrammi (pomelli, cursore, levetta) con shadow catcher
# --------------------------------------------------------------------------
CELL = 0.05
ITEMS = ["ts_big", "ts_small", "boss", "boss_outer", "boss_inner", "slider", "toggle"]
SHEET_W = CELL * len(ITEMS)
SHEET_RES_X = int(round(SHEET_W * PPM))
SHEET_RES_Y = int(round(CELL * PPM))

bm = bmesh.new()
bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=SHEET_W)
catcher = mesh_obj("catcher", bm, SHEET, [principled("catcher", (0.02, 0.02, 0.02), 0.0, 0.9)], smooth=False)
catcher.is_shadow_catcher = True

sheet_objs = {}
anchors = {}
for i, kind in enumerate(ITEMS):
    x = -SHEET_W / 2 + (i + 0.5) * CELL
    z = 0.0
    if kind == "slider":
        ob = make_slider_cap(SHEET, "sheet_slider")
    elif kind == "toggle":
        ob = make_toggle_lever(SHEET, "sheet_toggle")
        z = 0.0048
    else:
        ob = make_knob(kind, SHEET, "sheet_" + kind)
        if kind == "boss_inner":
            z = L.KNOB_TYPES["boss_outer"]["h"]
            under = make_knob("boss_outer", SHEET, "sheet_under_outer")
            under.location = (x, 0, 0)
            under.is_shadow_catcher = True
    ob.location = (x, 0, z)
    sheet_objs[kind] = ob
    base = (x, 0.0, z if kind == "boss_inner" else 0.0)
    ax, ay = project(base, 0.0, 0.0, SHEET_RES_X, SHEET_RES_Y)
    anchors[kind] = dict(cell_x0=int(round(i * CELL * PPM)), cell_x1=int(round((i + 1) * CELL * PPM)),
                         anchor=[ax, ay])

pos["sheet"] = dict(items=ITEMS, frames=N_FRAMES, res=[SHEET_RES_X, SHEET_RES_Y], cells=anchors)
with open(os.path.join(BUILD, "positions.json"), "w") as f:
    json.dump(pos, f, indent=1)
print("positions.json scritto")

if ONLY in ("all", "sheet"):
    cam_sheet = make_camera("cam_sheet", (0.0, 0.0, 0.0), SHEET_W, SHEET_RES_X, SHEET_RES_Y)
    scene.camera = cam_sheet
    scene.render.resolution_x = SHEET_RES_X
    scene.render.resolution_y = SHEET_RES_Y
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.color_mode = "RGBA"
    MAIN.hide_render = True
    SHEET.hide_render = False
    frames = [0, N_FRAMES // 2, N_FRAMES - 1] if PREVIEW else range(N_FRAMES)
    for fr in frames:
        a = math.radians(150.0 - 300.0 * fr / (N_FRAMES - 1))
        for kind in ("ts_big", "ts_small", "boss", "boss_outer", "boss_inner"):
            sheet_objs[kind].rotation_euler = (0, 0, a)
        sheet_objs["toggle"].rotation_euler = (0, math.radians(-24 if fr < N_FRAMES / 2 else 24), 0)
        render_to(os.path.join(SHEET_DIR, "f_%03d.png" % fr), 24 if PREVIEW else 56)
print("FINE")
