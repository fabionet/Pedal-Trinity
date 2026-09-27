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
ONLY = [a for a in argv if not a.startswith("--") and not a.isdigit()]
BUILD = os.path.join(HERE, "build")
TEX = os.path.join(BUILD, "cat")
OUT = os.path.join(BUILD, "pedals")
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


# board e luci (come nella scena originale)
bm = bmesh.new(); bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=0.3)
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


models, errors = catalog.load_all(strict=False)
done = 0
for m in models:
    if ONLY and m["id"] not in ONLY:
        continue
    lay = PL.pedal_layout(m)
    png = os.path.join(OUT, m["id"] + ".png")
    export(m, lay)
    if os.path.exists(png) and not FORCE:
        continue
    for o in list(PEDAL.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    if m["style"] == "ts":
        build_ts(m, lay)
    else:
        build_boss(m, lay)
    scene.render.filepath = png
    bpy.ops.render.render(write_still=True)
    done += 1
    print("RENDER %s (%d)" % (m["id"], done), flush=True)
print("FINE: %d render" % done)
