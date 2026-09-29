"""
Pedal Trinity - render Blender (Cycles) di ogni pedale del catalogo.

Per ogni modello:  build/pedals/<id>.png   immagine del pedale (senza pomelli: sono filmstrip)
                   build/pedals/<id>.json  coordinate proiettate (px logici @1x) di comandi, LED, pedale
Stesse luci, materiali, camera inclinata e scala delle filmstrip (render_scene.py).

Uso: blender -b --factory-startup -P tools/art/render_pedals.py -- [--samples 64] [--force] [id ...]
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
import catalog  # noqa: E402
import pedal_layout as PL  # noqa: E402
import pedal_geometry as PG  # noqa: E402
import layout as L  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
SAMPLES = int(argv[argv.index("--samples") + 1]) if "--samples" in argv else 64
FORCE = "--force" in argv
REAL = "--real" in argv            # repliche REAL MOD (build/real/, serigrafie in build/cat_real/)
ONLY = [a for a in argv if not a.startswith("--") and not a.isdigit()]
BUILD = os.path.join(HERE, "build")
TEX = os.path.join(BUILD, "cat_real" if REAL else "cat")
OUT = os.path.join(BUILD, "real" if REAL else "pedals")
os.makedirs(OUT, exist_ok=True)

TILT = math.radians(L.TILT_DEG)
COS, SIN = math.cos(TILT), math.sin(TILT)
PPM = L.UI_WIDTH * L.RENDER_SCALE / (L.VIEW_X_MAX - L.VIEW_X_MIN)   # uguale alle filmstrip (2x)
RS = L.RENDER_SCALE

# ------------------------------------------------------------------ scena
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)
scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.use_denoising = True
scene.cycles.use_adaptive_sampling = True
scene.cycles.adaptive_threshold = 0.02
scene.cycles.max_bounces = 6
scene.cycles.diffuse_bounces = 2
scene.cycles.glossy_bounces = 3
scene.render.use_persistent_data = False
scene.view_settings.view_transform = "AgX"
try:
    scene.view_settings.look = "AgX - Medium High Contrast"
except Exception:
    pass
scene.view_settings.exposure = -0.75      # vernici vive senza bruciare i piani illuminati
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
# sfondo trasparente: il pedale (con la sua ombra) si appoggia su qualsiasi pedana del tema scelto
scene.render.film_transparent = True

STATIC = bpy.data.collections.new("static")
PEDAL = bpy.data.collections.new("pedal")
scene.collection.children.link(STATIC)
scene.collection.children.link(PEDAL)


def link(o, coll):
    coll.objects.link(o)
    return o


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
    set_in(b, "Base Color", (*color, 1.0)); set_in(b, "Metallic", metallic); set_in(b, "Roughness", rough)
    set_in(b, "Coat Weight", coat); set_in(b, "Coat Roughness", 0.18); set_in(b, "Specular IOR Level", spec)
    set_in(b, "Anisotropic", aniso)
    return m


def srgb(c):
    def lin(v):
        v = v / 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return tuple(lin(v) for v in c)


def bumped(m, scale, strength, wave=False, distance=0.0001):
    nt = m.node_tree; b = bsdf_of(m)
    tc = nt.nodes.new("ShaderNodeTexCoord")
    if wave:
        tx = nt.nodes.new("ShaderNodeTexWave"); tx.wave_type = "BANDS"; tx.bands_direction = "Y"
    else:
        tx = nt.nodes.new("ShaderNodeTexNoise"); tx.inputs["Detail"].default_value = 6.0
    tx.inputs["Scale"].default_value = scale
    bp = nt.nodes.new("ShaderNodeBump")
    bp.inputs["Strength"].default_value = strength; bp.inputs["Distance"].default_value = distance
    nt.links.new(tc.outputs["Object"], tx.inputs["Vector"]); nt.links.new(tx.outputs["Fac"], bp.inputs["Height"])
    nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return m


def textured(name, image_path, x0, x1, y0, y1, metallic, rough, coat, flake=0.0):
    m = principled(name, (0.5, 0.5, 0.5), metallic, rough, coat)
    nt = m.node_tree; b = bsdf_of(m)
    tc = nt.nodes.new("ShaderNodeTexCoord"); mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1.0 / (x1 - x0), 1.0 / (y1 - y0), 1.0)
    mp.inputs["Location"].default_value = (-x0 / (x1 - x0), -y0 / (y1 - y0), 0.0)
    im = nt.nodes.new("ShaderNodeTexImage")
    im.image = bpy.data.images.load(image_path, check_existing=False)
    im.extension = "EXTEND"; im.interpolation = "Cubic"
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"]); nt.links.new(mp.outputs["Vector"], im.inputs["Vector"])
    nt.links.new(im.outputs["Color"], b.inputs["Base Color"])
    if flake > 0:
        nz = nt.nodes.new("ShaderNodeTexNoise"); nz.inputs["Scale"].default_value = 9000.0
        bp = nt.nodes.new("ShaderNodeBump"); bp.inputs["Strength"].default_value = flake; bp.inputs["Distance"].default_value = 0.00002
        nt.links.new(tc.outputs["Object"], nz.inputs["Vector"]); nt.links.new(nz.outputs["Fac"], bp.inputs["Height"])
        nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return m


MAT_CHROME = principled("chrome", (0.9, 0.9, 0.92), 1.0, 0.12)
MAT_NICKEL = principled("nickel", (0.75, 0.74, 0.72), 1.0, 0.28)
MAT_RUBBER = bumped(principled("rubber", (0.018, 0.018, 0.018), 0.0, 0.75, spec=0.3), 420.0, 0.35, wave=True, distance=0.0004)
MAT_FOOT = bumped(principled("footswitch", (0.02, 0.02, 0.021), 0.0, 0.55), 2500.0, 0.25)
MAT_LED_OFF = principled("led_off", (0.25, 0.01, 0.01), 0.0, 0.08, spec=0.9)
set_in(bsdf_of(MAT_LED_OFF), "Transmission Weight", 0.4)
MAT_BOARD = bumped(principled("board", (0.020, 0.020, 0.022), 0.0, 0.92, spec=0.25), 2600.0, 0.55, distance=0.0006)


def mesh_obj(name, bm, coll, mats, location=(0, 0, 0), smooth=True):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    for p in me.polygons: p.use_smooth = smooth
    for m in mats: me.materials.append(m)
    ob = bpy.data.objects.new(name, me); ob.location = location
    return link(ob, coll)


def rounded_rect_pts(w, d, r, seg=10):
    pts = []; r = min(r, w / 2 - 1e-5, d / 2 - 1e-5)
    for cx, cy, a0 in [(w / 2 - r, d / 2 - r, 0), (-w / 2 + r, d / 2 - r, 90), (-w / 2 + r, -d / 2 + r, 180), (w / 2 - r, -d / 2 + r, 270)]:
        for i in range(seg + 1):
            a = math.radians(a0 + 90.0 * i / seg)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def rounded_box(name, w, d, h, corner, edge, coll, mat, location=(0, 0, 0), z0=0.0, bevel_seg=5):
    bm = bmesh.new(); pts = rounded_rect_pts(w, d, corner)
    bot = [bm.verts.new((x, y, z0)) for x, y in pts]; top = [bm.verts.new((x, y, z0 + h)) for x, y in pts]
    bm.faces.new(list(reversed(bot))); bm.faces.new(top)
    for i in range(len(pts)):
        j = (i + 1) % len(pts); bm.faces.new([bot[i], bot[j], top[j], top[i]])
    ob = mesh_obj(name, bm, coll, [mat], location)
    if edge > 0:
        bv = ob.modifiers.new("bevel", "BEVEL"); bv.width = edge; bv.segments = bevel_seg
        bv.limit_method = "ANGLE"; bv.angle_limit = math.radians(50); bv.harden_normals = True
    ob.modifiers.new("wn", "WEIGHTED_NORMAL").keep_sharp = True
    return ob


def cylinder(name, r, h, coll, mat, location=(0, 0, 0), seg=48, edge=0.0, rotation=(0, 0, 0)):
    bm = bmesh.new()
    bot = [bm.verts.new((r * math.cos(2 * math.pi * i / seg), r * math.sin(2 * math.pi * i / seg), 0)) for i in range(seg)]
    top = [bm.verts.new((r * math.cos(2 * math.pi * i / seg), r * math.sin(2 * math.pi * i / seg), h)) for i in range(seg)]
    bm.faces.new(list(reversed(bot))); bm.faces.new(top)
    for i in range(seg):
        j = (i + 1) % seg; bm.faces.new([bot[i], bot[j], top[j], top[i]])
    ob = mesh_obj(name, bm, coll, [mat], location); ob.rotation_euler = rotation
    if edge > 0:
        bv = ob.modifiers.new("bevel", "BEVEL"); bv.width = edge; bv.segments = 3; bv.limit_method = "ANGLE"; bv.harden_normals = True
    ob.modifiers.new("wn", "WEIGHTED_NORMAL").keep_sharp = True
    return ob


def add_led(x, y, z, r):
    cylinder("led_bezel", r * 1.45, 0.0012, PEDAL, MAT_CHROME, (x, y, z), seg=40, edge=0.0003)
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=r)
    for v in bm.verts: v.co.z = max(v.co.z, 0.0) * 0.8
    mesh_obj("led_dome", bm, PEDAL, [MAT_LED_OFF], (x, y, z + 0.0010))


def add_jacks(m):
    """Prese jack sui fianchi: A (e B per i pedali stereo) su entrambi i lati; posizioni in pedal_geometry."""
    W, pos = PG.jack_layout(m)
    for p in pos:
        if p is None:
            continue
        y, z = p
        for s in (-1, 1):
            cylinder("jack_nut", 0.0055, 0.0030, PEDAL, MAT_NICKEL, (s * (W / 2 - 0.0005), y, z), seg=6, rotation=(0, s * math.pi / 2, 0), edge=0.0003)
            cylinder("jack_ring", 0.0046, 0.0042, PEDAL, MAT_CHROME, (s * (W / 2 - 0.0005), y, z), seg=40, rotation=(0, s * math.pi / 2, 0), edge=0.0003)


def build_boss(m, lay):
    py0, ty1 = lay["panel_y0"], lay["tread_y1"]
    body = principled("body_" + m["id"], srgb(m["colour"]), 0.15, 0.38, 0.3)
    rounded_box("base", PL.W, PL.D, 0.030, 0.007, 0.003, PEDAL, body)
    pd = PL.PANEL_Y1 - py0
    pm = textured("panel_" + m["id"], os.path.join(TEX, m["id"] + "_panel.png"), -PL.W / 2, PL.W / 2, -pd / 2, pd / 2, 0.1, 0.5, 0.15)
    rounded_box("panelblk", PL.W, pd, PL.PANEL_H, min(0.007, pd / 2 - 0.001), 0.003, PEDAL, pm, (0, py0 + pd / 2, 0))
    length = math.hypot(ty1 - PL.TREAD_Y0, PL.TREAD_H_BACK - PL.TREAD_H_FRONT)
    tm = textured("tread_" + m["id"], os.path.join(TEX, m["id"] + "_tread.png"), -PL.W / 2, PL.W / 2, -length / 2, length / 2, 0.15, 0.38, 0.3, 0.2)
    thick = 0.014
    ang = math.atan2(PL.TREAD_H_BACK - PL.TREAD_H_FRONT, ty1 - PL.TREAD_Y0)
    tr = rounded_box("treadle", PL.W - 0.0006, length, thick, 0.007, 0.0039, PEDAL, tm, z0=-thick)
    tr.location = (0, (PL.TREAD_Y0 + ty1) / 2, (PL.TREAD_H_FRONT + PL.TREAD_H_BACK) / 2)
    tr.rotation_euler = (ang, 0, 0)
    r_back = min(-0.0215, ty1 - 0.020)
    ry0 = -length / 2 + (-0.0615 - PL.TREAD_Y0) / (ty1 - PL.TREAD_Y0) * length
    ry1 = -length / 2 + (r_back - PL.TREAD_Y0) / (ty1 - PL.TREAD_Y0) * length
    rb = rounded_box("rubber", PL.W - 0.008, ry1 - ry0, 0.0022, 0.004, 0.0008, PEDAL, MAT_RUBBER, (0, (ry0 + ry1) / 2, 0))
    rb.parent = tr
    add_jacks(m)
    lx, ly, lz, lr = lay["led"]
    if lay.get("led_on_tread"):
        lz = PL.tread_top_z(ly, ty1)
    add_led(lx, ly, lz, lr)
    for k, c in enumerate(m["controls"]):
        if c["kind"] == "toggle":
            p = lay["controls"][k]
            cylinder("toggle_nut", 0.0026, 0.0016, PEDAL, MAT_NICKEL, (p["x"], p["y"], PL.PANEL_H), seg=6, edge=0.0002)
            cylinder("toggle_collar", 0.0017, 0.0048, PEDAL, MAT_CHROME, (p["x"], p["y"], PL.PANEL_H), seg=32, edge=0.0003)


def build_ts(m, lay):
    mat = textured("ts_" + m["id"], os.path.join(TEX, m["id"] + "_top.png"), -PL.TS_W / 2, PL.TS_W / 2, -PL.TS_D / 2, PL.TS_D / 2, 0.35, 0.30, 0.9, 0.25)
    rounded_box("ts_body", PL.TS_W, PL.TS_D, PL.TS_H, 0.011, 0.004, PEDAL, mat)
    fs = dict(x=0.0, y=-0.035, w=0.050, d=0.038, h=0.009)
    rounded_box("fs_frame", fs["w"] + 0.004, fs["d"] + 0.004, 0.0016, 0.004, 0.0006, PEDAL, MAT_NICKEL, (fs["x"], fs["y"], PL.TS_H))
    rounded_box("fs", fs["w"], fs["d"], fs["h"], 0.0035, 0.0018, PEDAL, MAT_FOOT, (fs["x"], fs["y"], PL.TS_H))
    lx, ly, lz, lr = lay["led"]
    add_led(lx, ly, lz, lr)
    add_jacks(m)


def build_nam(m, lay):
    """Contenitore grande NAM-A1A2: piano piatto serigrafato, footswitch metallici A/B con LED, prese A/B."""
    W, D, H = PL.NAM_W, PL.NAM_D, PL.NAM_H
    mat = textured("nam_" + m["id"], os.path.join(TEX, m["id"] + "_top.png"), -W / 2, W / 2, -D / 2, D / 2, 0.15, 0.36, 0.35, 0.2)
    rounded_box("nam_body", W, D, H, 0.008, 0.004, PEDAL, mat)
    for k, c in enumerate(m["controls"]):
        if c["kind"] != "toggle":
            continue
        p = lay["controls"][k]
        cylinder("fs_nut", p["r"] * 1.30, 0.0030, PEDAL, MAT_NICKEL, (p["x"], p["y"], H), seg=6, edge=0.0004)
        cylinder("fs_cap", p["r"], 0.0085, PEDAL, MAT_CHROME, (p["x"], p["y"], H + 0.0030), seg=64, edge=0.0016)
        lx, ly, _ = p["led"]
        add_led(lx, ly, H, 0.0024)
    add_jacks(m)


# ------------------------------------------------------------------ repliche REAL MOD
MAT_PAD = bumped(principled("pad_rubber", (0.015, 0.015, 0.016), 0.0, 0.8, spec=0.3), 1800.0, 0.3, distance=0.0003)
MAT_SCREEN = principled("screen", (0.004, 0.006, 0.008), 0.0, 0.06, coat=1.0, spec=0.9)
MAT_BLACKMETAL = principled("black_metal", (0.025, 0.025, 0.027), 0.6, 0.35)
MAT_KNOB = principled("deco_knob", (0.018, 0.018, 0.019), 0.0, 0.45, coat=0.2)
MAT_POINTER = principled("pointer", (0.9, 0.9, 0.88), 0.0, 0.4)


def add_knob_base(x, y, z):
    """Dado del perno dei pomelli (i pomelli sono filmstrip, disegnati dall'interfaccia)."""
    cylinder("knob_nut", 0.0032, 0.0012, PEDAL, MAT_NICKEL, (x, y, z), seg=6, edge=0.0002)


def add_toggles(r, lay, z):
    for k, c in enumerate(r["controls"]):
        if c["kind"] == "toggle":
            p = lay["controls"][k]
            cylinder("toggle_nut", 0.0026, 0.0016, PEDAL, MAT_NICKEL, (p["x"], p["y"], z), seg=6, edge=0.0002)
            cylinder("toggle_collar", 0.0017, 0.0048, PEDAL, MAT_CHROME, (p["x"], p["y"], z), seg=32, edge=0.0003)


def add_side_jacks(W, pos):
    for p in pos:
        if p is None:
            continue
        y, z = p
        for sgn in (-1, 1):
            cylinder("jack_nut", 0.0055, 0.0030, PEDAL, MAT_NICKEL, (sgn * (W / 2 - 0.0005), y, z), seg=6, rotation=(0, sgn * math.pi / 2, 0), edge=0.0003)
            cylinder("jack_ring", 0.0046, 0.0042, PEDAL, MAT_CHROME, (sgn * (W / 2 - 0.0005), y, z), seg=40, rotation=(0, sgn * math.pi / 2, 0), edge=0.0003)


def build_real_box(r, lay):
    """Twin, serie 200/500, unita' da pavimento e da tavolo: scocca metallica con piano serigrafato,
    footswitch in gomma (o pedali basculanti sui Twin), display, pulsanti, LED e prese."""
    b = lay["body"]
    W, D, H = b["W"], b["D"], b["H"]
    body = principled("body_" + r["id"], srgb(r["colour"]), 0.35, 0.34, 0.25)
    rounded_box("chassis", W, D, H - 0.003, 0.006, 0.0025, PEDAL, body)
    top = textured("top_" + r["id"], os.path.join(TEX, r["id"] + "_top.png"), -W / 2, W / 2, -D / 2, D / 2, 0.2, 0.40, 0.25, 0.15)
    rounded_box("topplate", W - 0.001, D - 0.001, 0.003, 0.006, 0.0012, PEDAL, top, (0, 0, H - 0.003))
    for part in lay["parts"]:
        t = part["type"]
        if t == "footswitch":
            if part.get("kind") == "treadle_pad":
                # pedale basculante come i compatti: piastra verniciata + gomma
                w, d = part["w"], part["d"]
                pl = rounded_box("fs_plate", w, d, 0.010, 0.005, 0.003, PEDAL, body, (part["x"], part["y"], H - 0.001))
                pl.rotation_euler = (math.radians(4.0), 0, 0)
                rounded_box("fs_rubber", w - 0.008, d * 0.72, 0.0022, 0.004, 0.0008, PEDAL, MAT_RUBBER,
                            (part["x"], part["y"] - d * 0.08, H + 0.0095))
            else:
                w, d = part["w"], part["d"]
                rounded_box("fs_base", w + 0.003, d + 0.003, 0.0025, 0.004, 0.0008, PEDAL, MAT_BLACKMETAL, (part["x"], part["y"], H - 0.0005))
                rounded_box("fs_pad", w, d, 0.0075, 0.0035, 0.0022, PEDAL, MAT_PAD, (part["x"], part["y"], H + 0.0015))
        elif t == "display":
            x0, y0, x1, y1 = part["x0"], part["y0"], part["x1"], part["y1"]
            rounded_box("screen", x1 - x0, y1 - y0, 0.0006, 0.0008, 0.0002, PEDAL, MAT_SCREEN, ((x0 + x1) / 2, (y0 + y1) / 2, H))
        elif t == "button":
            cylinder("btn_ring", part["r"] * 1.25, 0.0008, PEDAL, MAT_BLACKMETAL, (part["x"], part["y"], H), seg=40)
            cylinder("btn", part["r"], 0.0030, PEDAL, MAT_FOOT, (part["x"], part["y"], H + 0.0006), seg=40, edge=0.0008)
        elif t == "led":
            add_led(part["x"], part["y"], H, 0.0018)
        elif t == "knob":
            # pomello reale senza comando corrispondente: modellato (fisso)
            add_knob_base(part["x"], part["y"], H)
            cylinder("deco_knob", part["r"], 0.0140, PEDAL, MAT_KNOB, (part["x"], part["y"], H + 0.001), seg=48, edge=0.0012)
            rounded_box("deco_ptr", 0.0012, part["r"] * 0.9, 0.0004, 0.0003, 0.0, PEDAL, MAT_POINTER,
                        (part["x"], part["y"] + part["r"] * 0.45, H + 0.0151))
        elif t == "switch":
            rounded_box("sw_slot", 0.009, 0.0032, 0.0006, 0.0008, 0.0, PEDAL, MAT_BLACKMETAL, (part["x"], part["y"], H))
            rounded_box("sw_cap", 0.0035, 0.0026, 0.0030, 0.0006, 0.0004, PEDAL, MAT_FOOT, (part["x"] - 0.0022, part["y"], H + 0.0005))
        elif t == "slider":
            rounded_box("sl_cap", 0.0060, 0.0030, 0.0070, 0.0008, 0.0006, PEDAL, MAT_FOOT, (part["x"], (part["y0"] + part["y1"]) / 2, H))
    for k, c in enumerate(r["controls"]):
        p = lay["controls"][k]
        if p["strip"] in ("boss", "boss_outer"):
            add_knob_base(p["x"], p["y"], H)
        elif p["strip"] == "button":
            cylinder("btn_ring", p["r"] * 1.25, 0.0008, PEDAL, MAT_BLACKMETAL, (p["x"], p["y"], H), seg=40)
    add_toggles(r, lay, H)
    lx, ly, lz, lr = lay["led"]
    add_led(lx, ly, lz, lr)
    add_side_jacks(W, lay["jacks"][1])
    # piedini
    for sx in (-1, 1):
        for sy in (-1, 1):
            cylinder("foot", 0.006, 0.002, PEDAL, MAT_RUBBER, (sx * (W / 2 - 0.012), sy * (D / 2 - 0.012), -0.002), seg=24)


def build_real_treadle(r, lay):
    """Pedali di volume/espressione, wah e rocker: basamento, bilanciere con gomma, targhetta, blocco comandi."""
    b = lay["body"]
    W, D = b["W"], b["D"]
    t = lay["treadle"]
    base_h = t["base_h"]
    body = principled("body_" + r["id"], srgb(r["colour"]), 0.35, 0.36, 0.2)
    rounded_box("base", W, D, base_h, 0.010, 0.003, PEDAL, body)
    blk = lay["back_block"]
    bd = blk["y1"] - blk["y0"]
    bt = textured("back_" + r["id"], os.path.join(TEX, r["id"] + "_back.png"), -W / 2, W / 2, -bd / 2, bd / 2, 0.2, 0.4, 0.2)
    rounded_box("backblock", W, bd, blk["z"] - base_h, 0.006, 0.002, PEDAL, bt, (0, (blk["y0"] + blk["y1"]) / 2, base_h))
    # bilanciere: piastra verniciata inclinata, gomma e targhetta sulla punta
    length = math.hypot(t["y1"] - t["y0"], t["z1"] - t["z0"])
    ang = math.atan2(t["z1"] - t["z0"], t["y1"] - t["y0"])
    tr = rounded_box("treadle", t["w"], length, 0.012, 0.008, 0.003, PEDAL, body, z0=-0.012)
    tr.location = (0, (t["y0"] + t["y1"]) / 2, (t["z0"] + t["z1"]) / 2)
    tr.rotation_euler = (ang, 0, 0)
    rb = rounded_box("rubber", t["w"] - 0.010, length * 0.74, 0.0024, 0.005, 0.001, PEDAL, MAT_RUBBER, (0, -length * 0.08, 0))
    rb.parent = tr
    pl = textured("plate_" + r["id"], os.path.join(TEX, r["id"] + "_plate.png"), -(W - 0.020) / 2, (W - 0.020) / 2, -0.014, 0.014, 0.5, 0.3, 0.3)
    pp = rounded_box("plate", W - 0.020, 0.028, 0.0012, 0.003, 0.0005, PEDAL, pl, (0, length / 2 - 0.022, 0))
    pp.parent = tr
    # fianchi del bilanciere (cerniera)
    for sgn in (-1, 1):
        cylinder("hinge", 0.006, 0.004, PEDAL, MAT_BLACKMETAL, (sgn * (W / 2 - 0.002), t["y0"] + 0.02, base_h + 0.004), seg=24,
                 rotation=(0, math.pi / 2, 0))
    for k, c in enumerate(r["controls"]):
        p = lay["controls"][k]
        if p["strip"] == "boss":
            add_knob_base(p["x"], p["y"], p["z"])
    add_toggles(r, lay, blk["z"])
    lx, ly, lz, lr = lay["led"]
    add_led(lx, ly, lz, lr)
    add_side_jacks(W, lay["jacks"][1])


# board e luci (come nella scena originale)
bm = bmesh.new(); bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=1.2 if REAL else 0.3)
_board = mesh_obj("board", bm, STATIC, [MAT_BOARD], smooth=False)
_board.is_shadow_catcher = True          # invisibile, ma raccoglie l'ombra del pedale (canale alfa)


def add_sun(name, direction, strength, angle_deg):
    ld = bpy.data.lights.new(name, "SUN"); ld.energy = strength; ld.angle = math.radians(angle_deg)
    ob = bpy.data.objects.new(name, ld)
    ob.rotation_euler = Vector(direction).normalized().to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(ob)


add_sun("key", (0.42, -0.30, -0.86), 4.2, 9.0)
add_sun("fill", (-0.65, 0.20, -0.72), 0.9, 35.0)
add_sun("rim", (0.05, -0.85, -0.45), 0.8, 20.0)
world = bpy.data.worlds.new("studio"); scene.world = world
try:
    world.use_nodes = True
except Exception:
    pass
wn = world.node_tree; bg = next(n for n in wn.nodes if n.type == "BACKGROUND")
tc = wn.nodes.new("ShaderNodeTexCoord"); sep = wn.nodes.new("ShaderNodeSeparateXYZ"); ramp = wn.nodes.new("ShaderNodeValToRGB")
ramp.color_ramp.elements[0].position = 0.45; ramp.color_ramp.elements[0].color = (0.004, 0.004, 0.005, 1)
ramp.color_ramp.elements[1].position = 0.98; ramp.color_ramp.elements[1].color = (0.55, 0.56, 0.60, 1)
wn.links.new(tc.outputs["Generated"], sep.inputs["Vector"]); wn.links.new(sep.outputs["Z"], ramp.inputs["Fac"])
wn.links.new(ramp.outputs["Color"], bg.inputs["Color"]); bg.inputs["Strength"].default_value = 0.9

# camera: stessa inclinazione e scala delle filmstrip
import pedal_geometry as _PG  # noqa: E402
X0, X1, RES_X, RES_Y, VC = _PG.X0, _PG.X1, _PG.RES_X, _PG.RES_Y, _PG.VC
cd = bpy.data.cameras.new("cam"); cd.type = "ORTHO"; cd.ortho_scale = X1 - X0; cd.sensor_fit = "HORIZONTAL"
cd.clip_start = 0.001; cd.clip_end = 10
cam = bpy.data.objects.new("cam", cd)
direction = Vector((0.0, SIN, -COS))
cam.location = Vector((0.0, VC / COS, 0.0)) - direction * 1.0
cam.rotation_euler = (TILT, 0, 0)
scene.collection.objects.link(cam)
scene.camera = cam
scene.render.resolution_x = RES_X; scene.render.resolution_y = RES_Y; scene.render.resolution_percentage = 100
scene.cycles.samples = SAMPLES


import pedal_geometry as PG  # noqa: E402


def export(m, lay):
    with open(os.path.join(OUT, m["id"] + ".json"), "w") as f:
        json.dump(PG.geometry(m, lay), f)


if REAL:
    import real_catalog  # noqa: E402
    import real_layout  # noqa: E402
    models = real_catalog.real_models(strict=False)
else:
    models, errors = catalog.load_all(strict=False)
done = 0
for m in models:
    if ONLY and m["id"] not in ONLY:
        continue
    lay = real_layout.real_layout(m) if REAL else PL.pedal_layout(m)
    png = os.path.join(OUT, m["id"] + ".png")
    export(m, lay)
    if os.path.exists(png) and not FORCE:
        continue
    for o in list(PEDAL.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    # inquadratura dello stile (il contenitore grande e' piu' profondo)
    PG.use_frame(m["style"], lay.get("frame"))
    scene.render.resolution_x = PG.RES_X
    scene.render.resolution_y = PG.RES_Y
    # i contenitori grandi si renderizzano a risoluzione ridotta (lato lungo al massimo ~2400 px)
    scene.render.resolution_percentage = min(100, int(2400 * 100 / max(PG.RES_X, PG.RES_Y)))
    cd.ortho_scale = PG.X1 - PG.X0
    cam.location = Vector((0.0, PG.VC / COS, 0.0)) - direction * 1.0
    if REAL and lay["builder"] == "box":
        build_real_box(m, lay)
    elif REAL and lay["builder"] == "treadle":
        build_real_treadle(m, lay)
    elif m["style"] == "ts":
        build_ts(m, lay)
    elif m["style"] == "nam":
        build_nam(m, lay)
    else:
        build_boss(m, lay)
    scene.render.filepath = png
    bpy.ops.render.render(write_still=True)
    done += 1
    print("RENDER %s (%d)" % (m["id"], done), flush=True)
print("FINE: %d render" % done)
