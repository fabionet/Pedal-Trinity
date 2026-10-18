"""
Pedal Trinity - geometria Blender di amplificatori e casse (tappa 6), usata da render_pedals.py:
mobile rivestito (tolex, tweed, vinile a quadretti, moquette), griglie con piping e cornici, pannello dei comandi
incassato (frontale o sul cielo) con le serigrafie di amp_textures.py, comandi fissi modellati (pomelli a gonna, a
calotta dorata, a puntatore, levette, prese, spia a gioiello, interruttori a bilanciere), maniglie, angolari, piedini e
ruote, testata ribaltabile, rack; pedaliera dei comandi davanti alle casse; retro con piastra del telaio, prese,
selettori, fusibili, valvole visibili, trasformatori, magneti degli altoparlanti, pannelli posteriori e feritoie.
Coordinate: quelle di amp_layout.py (ampli in piedi, faccia frontale a y = -D/2). Il retro si renderizza girando
tutto l'ampli di 180 gradi attorno a z.
"""
import math
import os

import bmesh
import bpy
from mathutils import Vector

import amp_layout as AL

G = {}
_MATS = {}


def setup(g):
    """g = globals() di render_pedals (aiuti per mesh e materiali, collezione PEDAL, camera, luci)."""
    G.update(g)
    scene = G["scene"]
    lights = []
    for name, d, e, ang in (("amp_key", (0.38, 0.78, -0.50), 3.6, 12.0), ("amp_fill", (-0.62, 0.70, -0.32), 1.0, 35.0),
                            ("amp_top", (0.10, 0.25, -0.96), 1.6, 20.0), ("amp_rim", (-0.30, -0.70, -0.65), 0.7, 25.0)):
        ld = bpy.data.lights.new(name, "SUN")
        ld.energy = e
        ld.angle = math.radians(ang)
        ob = bpy.data.objects.new(name, ld)
        ob.rotation_euler = Vector(d).normalized().to_track_quat("-Z", "Y").to_euler()
        scene.collection.objects.link(ob)
        ob.hide_render = True
        lights.append(ob)
    # luce puntiforme davanti all'apertura del retro (solo per il retro): illumina valvole e interno del mobile
    lp = bpy.data.lights.new("amp_backfill", "POINT")
    lp.energy = 60.0
    lp.shadow_soft_size = 0.25
    ob = bpy.data.objects.new("amp_backfill", lp)
    scene.collection.objects.link(ob)
    ob.hide_render = True
    lights.append(ob)
    G["amp_lights"] = lights


def use_amp_lights(on):
    for ob in bpy.data.objects:
        if ob.type == "LIGHT":
            if ob.name.startswith("amp_backfill"):
                ob.hide_render = True
            elif ob.name.startswith("amp_"):
                ob.hide_render = not on
            else:
                ob.hide_render = on


# ------------------------------------------------------------------ materiali
def mat(name, col, met=0.0, rough=0.5, coat=0.0, spec=0.5):
    key = (name, tuple(col), met, rough, coat)
    if key not in _MATS:
        _MATS[key] = G["principled"](name, G["srgb"](col), met, rough, coat, spec)
    return _MATS[key]


def _bump(m, kind, scale, strength, dist):
    nt = m.node_tree
    b = G["bsdf_of"](m)
    tc = nt.nodes.new("ShaderNodeTexCoord")
    if kind == "wave":
        tx = nt.nodes.new("ShaderNodeTexWave"); tx.wave_type = "BANDS"; tx.bands_direction = "DIAGONAL"
    elif kind == "voronoi":
        tx = nt.nodes.new("ShaderNodeTexVoronoi")
    else:
        tx = nt.nodes.new("ShaderNodeTexNoise"); tx.inputs["Detail"].default_value = 8.0
    tx.inputs["Scale"].default_value = scale
    bp = nt.nodes.new("ShaderNodeBump")
    bp.inputs["Strength"].default_value = strength
    bp.inputs["Distance"].default_value = dist
    nt.links.new(tc.outputs["Object"], tx.inputs["Vector"])
    nt.links.new(tx.outputs["Distance" if kind == "voronoi" else "Fac"], bp.inputs["Height"])
    nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return tx


def covering_mat(rid, cov):
    col = tuple(cov.get("col") or (24, 24, 26))
    kind = cov.get("kind") or "levant"
    m = G["principled"]("cov_" + rid, G["srgb"](col), 0.0, 0.62, 0.0, 0.4)
    nt = m.node_tree
    b = G["bsdf_of"](m)
    if kind == "tweed":
        # tela di cotone a spina diagonale laccata: bande diagonali in due toni + vernice lucida
        tc = nt.nodes.new("ShaderNodeTexCoord")
        wv = nt.nodes.new("ShaderNodeTexWave"); wv.wave_type = "BANDS"; wv.bands_direction = "DIAGONAL"
        wv.inputs["Scale"].default_value = 260.0
        wv.inputs["Distortion"].default_value = 2.0
        rp = nt.nodes.new("ShaderNodeValToRGB")
        rp.color_ramp.elements[0].color = (*G["srgb"]((150, 112, 62)), 1)
        rp.color_ramp.elements[1].color = (*G["srgb"]((214, 182, 118)), 1)
        nt.links.new(tc.outputs["Object"], wv.inputs["Vector"]); nt.links.new(wv.outputs["Fac"], rp.inputs["Fac"])
        nt.links.new(rp.outputs["Color"], b.inputs["Base Color"])
        G["set_in"](b, "Coat Weight", 0.7); G["set_in"](b, "Roughness", 0.45)
        bp = nt.nodes.new("ShaderNodeBump"); bp.inputs["Strength"].default_value = 0.25; bp.inputs["Distance"].default_value = 0.0003
        nt.links.new(wv.outputs["Fac"], bp.inputs["Height"]); nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    elif kind == "check":
        # vinile azzurro a quadretti (B-15 'blue check')
        tc = nt.nodes.new("ShaderNodeTexCoord")
        ck = nt.nodes.new("ShaderNodeTexChecker"); ck.inputs["Scale"].default_value = 260.0
        ck.inputs["Color1"].default_value = (*G["srgb"]((54, 86, 136)), 1)
        ck.inputs["Color2"].default_value = (*G["srgb"]((92, 128, 176)), 1)
        nt.links.new(tc.outputs["Object"], ck.inputs["Vector"]); nt.links.new(ck.outputs["Color"], b.inputs["Base Color"])
        _bump(m, "noise", 2600.0, 0.35, 0.0002)
    elif kind == "carpet":
        G["set_in"](b, "Roughness", 0.95)
        _bump(m, "noise", 4200.0, 0.9, 0.0006)
    elif kind == "metal":
        G["set_in"](b, "Metallic", 0.6); G["set_in"](b, "Roughness", 0.4)
    elif kind in ("bronco", "silver"):
        _bump(m, "voronoi", 900.0, 0.45, 0.0005)
    else:
        _bump(m, "noise", 2200.0, 0.55, 0.0004)
    return m


def tex_mat(name, img, w, h, met, rough, coat, aniso=0.0, y0=None, y1=None, alpha=False, bump=None):
    m = G["textured"](name, img, -w / 2, w / 2, -h / 2 if y0 is None else y0, h / 2 if y1 is None else y1, met, rough, coat)
    b = G["bsdf_of"](m)
    if aniso:
        G["set_in"](b, "Anisotropic", aniso)
    if alpha:
        im = next(n for n in m.node_tree.nodes if n.type == "TEX_IMAGE")
        m.node_tree.links.new(im.outputs["Alpha"], b.inputs["Alpha"])
    if bump:
        _bump(m, bump[0], bump[1], bump[2], bump[3])
    return m


FINISH = {"black": (0.25, 0.42, 0.15, 0.0), "brushed": (0.85, 0.30, 0.0, 0.55), "brushed_gold": (0.85, 0.30, 0.1, 0.55),
          "chrome": (0.92, 0.20, 0.0, 0.0), "plexi": (0.45, 0.22, 1.0, 0.0), "copper": (0.70, 0.34, 0.1, 0.4),
          "orange": (0.10, 0.42, 0.3, 0.0), "lucite": (0.0, 0.12, 1.0, 0.0), "grey": (0.3, 0.45, 0.2, 0.0)}


# ------------------------------------------------------------------ geometria di base
def _par(ob, p):
    ob.parent = p
    return ob


def empty(name, loc=(0, 0, 0), rot=(0, 0, 0), parent=None):
    ob = bpy.data.objects.new(name, None)
    ob.location = loc
    ob.rotation_euler = rot
    G["PEDAL"].objects.link(ob)
    if parent is not None:
        ob.parent = parent
    return ob


def box(name, w, d, h, m, loc, parent, corner=0.0015, edge=0.0008):
    ob = G["rounded_box"](name, w, d, h, corner, edge, G["PEDAL"], m, loc)
    return _par(ob, parent)


def cyl(name, r, h, m, loc, parent, seg=32, edge=0.0, rot=(0, 0, 0)):
    ob = G["cylinder"](name, r, h, G["PEDAL"], m, loc, seg=seg, edge=edge, rotation=rot)
    return _par(ob, parent)


def boolean_cut(target, cutter, cut_mat=None):
    if cut_mat is not None:
        cutter.data.materials.clear()
        cutter.data.materials.append(cut_mat)
        if cut_mat.name not in [m.name for m in target.data.materials if m]:
            target.data.materials.append(cut_mat)
    mod = target.modifiers.new("cut", "BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.object = cutter
    try:
        mod.solver = "EXACT"
        mod.material_mode = "TRANSFER"
    except Exception:
        pass
    # il taglio va prima della smussatura
    try:
        target.modifiers.move(len(target.modifiers) - 1, 0)
    except Exception:
        pass
    if target.parent is not None:
        cutter.parent = target.parent          # i tagli girano con l'ampli (retro)
    cutter.hide_render = True
    cutter.display_type = "WIRE"


def raw_box(name, x0, x1, y0, y1, z0, z1, m=None):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x = x0 if v.co.x < 0 else x1
        v.co.y = y0 if v.co.y < 0 else y1
        v.co.z = z0 if v.co.z < 0 else z1
    ob = G["mesh_obj"](name, bm, G["PEDAL"], [m] if m else [], smooth=False)
    return ob


def slant_box(name, W, D, Dt, H, z0, m, edge):
    """Cassa angolata: meta' alta della faccia inclinata all'indietro (profondita' in cima Dt)."""
    Hb = H * 0.5
    prof = [(-D / 2, z0), (D / 2, z0), (D / 2, z0 + H), (D / 2 - Dt, z0 + H), (-D / 2, z0 + Hb)]
    bm = bmesh.new()
    L = [bm.verts.new((-W / 2, y, z)) for y, z in prof]
    R = [bm.verts.new((W / 2, y, z)) for y, z in prof]
    bm.faces.new(L[::-1]); bm.faces.new(R)
    n = len(prof)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new([L[i], L[j], R[j], R[i]])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob = G["mesh_obj"](name, bm, G["PEDAL"], [m], smooth=False)
    bv = ob.modifiers.new("bevel", "BEVEL"); bv.width = edge; bv.segments = 4
    bv.limit_method = "ANGLE"; bv.angle_limit = math.radians(30); bv.harden_normals = True
    ob.modifiers.new("wn", "WEIGHTED_NORMAL").keep_sharp = True
    return ob


def piping_rect(name, x0, x1, z0, z1, y, r, m, parent, corner=0.006, plane="xz"):
    """Bordino a sezione tonda attorno a un rettangolo (piano xz a profondita' y)."""
    cd = bpy.data.curves.new(name, "CURVE")
    cd.dimensions = "3D"
    cd.bevel_depth = r
    cd.bevel_resolution = 3
    sp = cd.splines.new("POLY")
    pts = []
    c = min(corner, (x1 - x0) / 3, (z1 - z0) / 3)
    for cx, cz, a0 in ((x1 - c, z1 - c, 0), (x0 + c, z1 - c, 90), (x0 + c, z0 + c, 180), (x1 - c, z0 + c, 270)):
        for k in range(7):
            a = math.radians(a0 + 90 * k / 6)
            pts.append((cx + c * math.cos(a), cz + c * math.sin(a)))
    sp.points.add(len(pts) - 1)
    for p, (x, z) in zip(sp.points, pts):
        p.co = (x, y, z, 1.0) if plane == "xz" else (x, z, y, 1.0)
    sp.use_cyclic_u = True
    ob = bpy.data.objects.new(name, cd)
    ob.data.materials.append(m)
    G["PEDAL"].objects.link(ob)
    return _par(ob, parent)


def strap_handle(name, L, rise, w, t, m, cap_m, loc, parent):
    """Maniglia a cinghia sul cielo: nastro piegato ad arco con le calotte d'aggancio."""
    bm = bmesh.new()
    n = 16
    rows = []
    for i in range(n + 1):
        u = i / n
        x = -L / 2 + L * u
        z = rise * math.sin(math.pi * u) ** 0.7
        row = [bm.verts.new((x, sy * w / 2, z + sz * t / 2 + t / 2)) for sy, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        rows.append(row)
    for a, b in zip(rows, rows[1:]):
        for k in range(4):
            bm.faces.new([a[k], a[(k + 1) % 4], b[(k + 1) % 4], b[k]])
    bm.faces.new(rows[0][::-1]); bm.faces.new(rows[-1])
    ob = G["mesh_obj"](name, bm, G["PEDAL"], [m])
    ob.location = loc
    _par(ob, parent)
    for sx in (-1, 1):
        box(name + "_cap", w * 1.15, w * 1.3, t * 2.2, cap_m, (loc[0] + sx * L / 2, loc[1], loc[2]), parent, corner=w * 0.3, edge=t * 0.5)
    return ob


# ------------------------------------------------------------------ comandi fissi
KNOB_COL = {"skirt": (14, 14, 15), "goldcap": (14, 14, 15), "chicken": (230, 222, 196), "pointer": (24, 20, 18),
            "black": (16, 16, 17), "silver": (16, 16, 17)}


def deco_knob(style, x, y, R, parent, angle=0.0, z=0.0):
    k = mat("knob_" + style, KNOB_COL.get(style, (16, 16, 17)), 0.0, 0.35, 0.3)
    white = mat("knob_line", (235, 235, 228), 0.0, 0.4)
    gold = mat("knob_gold", (214, 172, 80), 1.0, 0.25)
    alu = mat("knob_alu", (200, 202, 205), 1.0, 0.25)
    piv = empty("knob_piv", (x, y, z), (0, 0, -angle), parent)
    if style == "skirt":
        cyl("skirt", R * 1.08, 0.0035, k, (0, 0, 0), piv, seg=48, edge=0.0008)
        cyl("cap", R * 0.74, 0.0115, k, (0, 0, 0.0030), piv, seg=48, edge=0.0012)
        box("line", 0.0009, R * 0.55, 0.0004, white, (0, R * 0.42, 0.0146), piv, corner=0.0002, edge=0)
        box("sk_line", 0.0010, R * 0.30, 0.0003, white, (0, R * 0.93, 0.0036), piv, corner=0.0002, edge=0)
    elif style == "goldcap":
        cyl("body", R * 0.86, 0.0120, k, (0, 0, 0), piv, seg=48, edge=0.0010)
        cyl("gold", R * 0.66, 0.0008, gold, (0, 0, 0.0120), piv, seg=48)
        box("line", 0.0009, R * 0.45, 0.0003, mat("knob_dark", (20, 20, 20)), (0, R * 0.36, 0.0129), piv, corner=0.0002, edge=0)
    elif style in ("chicken", "pointer"):
        cyl("base", R * 0.62, 0.0050, k, (0, 0, 0), piv, seg=40, edge=0.0010)
        bm = bmesh.new()
        hw, ln = R * 0.42, R * 1.15
        pts = [(-hw, -R * 0.45), (hw, -R * 0.45), (hw * 0.35, ln), (-hw * 0.35, ln)]
        bot = [bm.verts.new((px, py, 0.0050)) for px, py in pts]
        top = [bm.verts.new((px * 0.9, py * 0.97, 0.0115)) for px, py in pts]
        bm.faces.new(bot[::-1]); bm.faces.new(top)
        for i in range(4):
            j = (i + 1) % 4
            bm.faces.new([bot[i], bot[j], top[j], top[i]])
        ob = G["mesh_obj"]("pointer", bm, G["PEDAL"], [k])
        bv = ob.modifiers.new("bevel", "BEVEL"); bv.width = 0.0012; bv.segments = 3
        _par(ob, piv)
        box("line", 0.0008, R * 0.7, 0.0003, mat("knob_dark", (20, 20, 20)) if style == "chicken" else white,
            (0, R * 0.45, 0.0117), piv, corner=0.0002, edge=0)
    else:
        cyl("body", R * 0.90, 0.0125, k, (0, 0, 0), piv, seg=48, edge=0.0011)
        if style == "silver":
            cyl("alu", R * 0.62, 0.0008, alu, (0, 0, 0.0125), piv, seg=48)
        box("line", 0.0009, R * 0.55, 0.0003, white, (0, R * 0.42, 0.0132), piv, corner=0.0002, edge=0)
    return piv


def deco_toggle(x, y, parent, mini=False, z=0.0):
    sc = 0.7 if mini else 1.0
    nick = G["MAT_NICKEL"]; chrome = G["MAT_CHROME"]
    cyl("tg_nut", 0.0030 * sc, 0.0016, nick, (x, y, z), parent, seg=6, edge=0.0002)
    cyl("tg_collar", 0.0019 * sc, 0.0045, chrome, (x, y, z), parent, seg=24)
    cyl("tg_lever", 0.0010 * sc, 0.0100 * sc, chrome, (x, y, z + 0.004), parent, seg=16, rot=(math.radians(-20), 0, 0))


def deco_rocker(x, y, parent, col=(150, 20, 18), z=0.0):
    box("rk_frame", 0.0110, 0.0150, 0.0020, mat("rk_frame", (12, 12, 12), 0.0, 0.5), (x, y, z), parent, corner=0.0010)
    r = box("rk_paddle", 0.0090, 0.0128, 0.0040, mat("rk_" + str(col), col, 0.0, 0.35, 0.3), (x, y, z + 0.0012), parent, corner=0.0008)
    r.rotation_euler = (math.radians(8), 0, 0)


def deco_jack(x, y, parent, z=0.0):
    cyl("jk_nut", 0.0052, 0.0022, G["MAT_NICKEL"], (x, y, z), parent, seg=6, edge=0.0003)
    cyl("jk_ring", 0.0040, 0.0030, G["MAT_CHROME"], (x, y, z), parent, seg=32, edge=0.0003)
    cyl("jk_hole", 0.0027, 0.0031, mat("hole", (4, 4, 4), 0.0, 0.8), (x, y, z), parent, seg=24)


def deco_lamp(x, y, parent, r=0.0042, col=(170, 14, 10), z=0.0):
    cyl("lamp_bezel", r * 1.35, 0.0030, G["MAT_CHROME"], (x, y, z), parent, seg=12, edge=0.0005)
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=1, radius=r)
    for v in bm.verts:
        v.co.z = max(v.co.z, 0.0) * 0.9
    glass = mat("lamp_glass_" + str(col), col, 0.0, 0.08, 0.0, 0.9)
    G["set_in"](G["bsdf_of"](glass), "Transmission Weight", 0.35)
    ob = G["mesh_obj"]("lamp_dome", bm, G["PEDAL"], [glass], smooth=False)
    ob.location = (x, y, z + 0.0026)
    _par(ob, parent)


def deco_led(x, y, parent, col="red", z=0.0):
    tint = {"red": (150, 10, 8), "green": (10, 120, 30), "blue": (10, 40, 160), "orange": (180, 70, 8)}.get(col, (150, 10, 8))
    cyl("led_bezel", 0.0026, 0.0010, G["MAT_CHROME"], (x, y, z), parent, seg=24)
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=12, radius=0.0018)
    for v in bm.verts:
        v.co.z = max(v.co.z, 0.0) * 0.8
    gl = mat("led_" + col, tint, 0.0, 0.1, 0.0, 0.9)
    ob = G["mesh_obj"]("led_dome", bm, G["PEDAL"], [gl])
    ob.location = (x, y, z + 0.0008)
    _par(ob, parent)


def deco_button(x, y, parent, col=(16, 16, 17), z=0.0):
    cyl("bt_ring", 0.0046, 0.0012, G["MAT_CHROME"], (x, y, z), parent, seg=32, edge=0.0003)
    cyl("bt_cap", 0.0036, 0.0036, mat("bt_" + str(col), col, 0.0, 0.4, 0.3), (x, y, z + 0.0008), parent, seg=32, edge=0.0010)


def deco_fuse(x, y, parent, z=0.0):
    cyl("fuse", 0.0045, 0.0090, mat("fuse_cap", (12, 12, 12), 0.0, 0.5), (x, y, z), parent, seg=20, edge=0.0010)


def deco_slider(x, y, parent, z=0.0):
    box("sl_cap", 0.0062, 0.0042, 0.0065, G["MAT_SLIDER_CAP"], (x, y, z), parent, corner=0.0008, edge=0.0005)


def panel_items(A, items, pv, P, R, real):
    """Comandi fissi del pannello (posizioni senza comando funzionale), prese, spie e interruttori."""
    style = A.get("knob") or "black"
    rockers = A.get("rockers")
    for e in items:
        x, y = P.local(e["u"], e["v"])
        k = e["kind"]
        ang = math.radians(((hash(e.get("key") or e["label"]) % 9) - 4) * 18.0)
        if k in ("knob", "selector"):
            deco_knob(style, x, y, R, pv, ang)
        elif k in ("toggle", "mini"):
            deco_toggle(x, y, pv, mini=(k == "mini"))
        elif k in ("power", "standby", "rocker"):
            if rockers:
                deco_rocker(x, y, pv, (150, 20, 18) if k == "power" else (18, 18, 18))
            else:
                deco_toggle(x, y, pv)
        elif k == "button":
            red = "CH" in (e.get("label") or "").upper() or A.get("red_buttons")
            deco_button(x, y, pv, (150, 18, 16) if red else (16, 16, 17))
        elif k == "jack":
            deco_jack(x, y, pv)
        elif k == "lamp":
            deco_lamp(x, y, pv)
        elif k == "led":
            lab = (e.get("label") or "").upper()
            deco_led(x, y, pv, {"CH1": "green", "CH2": "blue", "CH3": "red"}.get(lab, "red"))
        elif k == "fuse":
            deco_fuse(x, y, pv)
        elif k == "slider":
            deco_slider(x, y, pv)


def functional_bases(r, lay, pv, P):
    """Dadi dei comandi funzionali (le filmstrip sono disegnate dall'interfaccia sopra il render)."""
    for p, c in zip(lay["controls"], r["controls"]):
        if not p.get("amp") or p.get("inner_of"):
            continue
        x, y = P.local(p["u"], p["v"])
        if p["strip"] in ("ts_big", "ts_small", "boss", "boss_outer"):
            cyl("knob_nut", 0.0032, 0.0012, G["MAT_NICKEL"], (x, y, 0.0), pv, seg=6, edge=0.0002)
        elif p["strip"] == "toggle":
            cyl("tg_nut", 0.0030, 0.0016, G["MAT_NICKEL"], (x, y, 0.0), pv, seg=6, edge=0.0002)
            cyl("tg_collar", 0.0019, 0.0045, G["MAT_CHROME"], (x, y, 0.0), pv, seg=24)
        elif p["strip"] == "slider":
            pass


# ------------------------------------------------------------------ costruzione
def build(r, lay, tex, real):
    """Costruisce l'ampli o la cassa; restituisce l'oggetto radice (da girare per il retro)."""
    A = lay["amp"]
    rid = r["id"]
    W, D, H, z0 = lay["W"], lay["D"], lay["H"], lay["z0"]
    s = lay["s"]
    root = empty("amp_root")
    cov = covering_mat(rid, A.get("covering") or {})
    inner = mat("interior", (34, 26, 20), 0.0, 0.85)
    edge = 0.0045 * s / 0.62
    corner = 0.010 * s / 0.62
    kind = A["kind"]
    body = None
    if kind == "flip":
        hh = (A.get("head_h") or 300) * s / 1000.0
        hc = H - hh
        body = box("cab", W, D, hc, cov, (0, 0, z0), root, corner=corner, edge=edge)
        head = box("head", W * 0.97, D * 0.94, hh - 0.002, cov, (0, 0.0, z0 + hc + 0.002), root, corner=corner, edge=edge)
        for sx in (-1, 1):                  # fermi della testata
            box("latch", 0.010, 0.004, 0.022, G["MAT_CHROME"], (sx * W * 0.30, -D * 0.47 - 0.002, z0 + hc - 0.010), root, corner=0.001)
        top_obj = head
    elif A.get("slant"):
        dt = (A.get("depth_top") or A["dims"][2] * 0.72) * s / 1000.0
        body = slant_box("cab", W, D, dt, H, z0, cov, edge)
        _par(body, root)
        top_obj = body
    elif kind == "rack":
        body = box("chassis", W * 0.92, D * 0.96, H * 0.92, mat("rack_body", (40, 40, 42), 0.6, 0.4), (0, D * 0.02, z0 + H * 0.04), root,
                   corner=0.002, edge=0.001)
        top_obj = body
    else:
        body = box("cab", W, D, H, cov, (0, 0, z0), root, corner=corner, edge=edge)
        top_obj = body
    P = lay.get("panel")
    console = A.get("console")
    # ---- pannello dei comandi (incassato) ----
    if P is not None:
        pn = A["panel"]
        met, rough, coat, aniso = FINISH.get(pn.get("finish"), FINISH["black"])
        pimg = os.path.join(tex, rid + "_apanel.png")
        pm = tex_mat("panel_" + rid, pimg, P.w, P.h, met, rough, coat, aniso)
        if P.view == "front":
            pv = empty("panel_piv", P.centre, (math.pi / 2, 0, 0), root)
            if P.rec > 0.0005 and kind != "rack":
                cut = raw_box("panel_cut", P.X0, P.X1, -D / 2 - 0.02, P.yp + 0.0005, P.Z0, P.Z1)
                tgt = top_obj if kind != "flip" else body
                boolean_cut(tgt, cut, mat("well_" + rid, tuple(int(v * 0.6) for v in (A.get("covering") or {}).get("col", (24, 24, 26))), 0.0, 0.7))
        else:
            pv = empty("panel_piv", P.centre, (0, 0, 0), root)
            if P.rec > 0.0005:
                cut = raw_box("panel_cut", P.X0, P.X1, P.Y0, P.Y1, P.zp - 0.0005, z0 + H + 0.03)
                boolean_cut(top_obj, cut, mat("well_" + rid, (20, 20, 22), 0.0, 0.7))
        thick = 0.003 if kind != "rack" else 0.004
        box("panel", P.w - 0.0004, P.h - 0.0004, thick, pm, (0, 0, -thick), pv, corner=0.0015 if kind != "rack" else 0.003, edge=0.0005)
        if not console:
            import pedal_layout as PL
            R = PL.KNOB_R[A.get("strip") or "ts_small"]
            panel_items(A, lay["deco"], pv, P, R, real)
            functional_bases(r, lay, pv, P)
    # ---- zone della faccia: griglie, lamiera mandorlata ----
    yf = -D / 2
    for k, f in enumerate(lay["faces"]):
        X0, X1, Z0, Z1 = f["xz"]
        w, h = X1 - X0, Z1 - Z0
        img = os.path.join(tex, "%s_agrille%d.png" % (rid, k))
        if f["kind"] == "grille":
            gm = tex_mat("grille_%s_%d" % (rid, k), img, w, h, 0.0, 0.88, 0.0, bump=("noise", 6000.0, 0.25, 0.0002))
            if A.get("slant"):
                hb = z0 + H * 0.5
                dt = (A.get("depth_top") or A["dims"][2] * 0.72) * s / 1000.0
                run = D - dt
                fz = (hb - Z0) / h
                lo = h * fz
                pv1 = empty("grille_lo", ((X0 + X1) / 2, yf - 0.0008, Z0 + lo / 2), (math.pi / 2, 0, 0), root)
                gm1 = tex_mat("grille_lo_%s_%d" % (rid, k), img, w, h, 0.0, 0.88, 0.0, y0=-lo / 2, y1=-lo / 2 + h)
                box("grille_lo", w, lo, 0.0016, gm1, (0, 0, -0.0016), pv1, corner=0.001, edge=0.0003)
                up_h = Z1 - hb
                ang = math.atan2(run, H * 0.5)
                Lu = up_h / math.cos(ang)
                cy = yf + run * (up_h / 2) / (H * 0.5)
                pv2 = empty("grille_up", ((X0 + X1) / 2, cy - 0.0008 * math.cos(ang), hb + up_h / 2), (math.pi / 2 - ang, 0, 0), root)
                y0u = Lu / 2 - Lu / (1 - fz)
                gm2 = tex_mat("grille_up_%s_%d" % (rid, k), img, w, h, 0.0, 0.88, 0.0, y0=y0u, y1=Lu / 2)
                box("grille_up", w, Lu, 0.0016, gm2, (0, 0, -0.0016), pv2, corner=0.001, edge=0.0003)
            else:
                pv = empty("grille_piv", ((X0 + X1) / 2, yf - 0.0006, (Z0 + Z1) / 2), (math.pi / 2, 0, 0), root)
                box("grille", w, h, 0.0016, gm, (0, 0, -0.0016), pv, corner=0.0025 if not A.get("round") else 0.03, edge=0.0004)
            if f.get("piping"):
                pm = mat("piping_" + str(f["piping"]), f["piping"], 0.0, 0.35, 0.4)
                rp = 0.0016
                if not A.get("slant"):
                    piping_rect("piping", X0 - rp, X1 + rp, Z0 - rp, Z1 + rp, yf - 0.0012, rp, pm, root,
                                corner=0.004 if not A.get("round") else 0.03)
                    if f.get("double"):
                        piping_rect("piping2", X0 - 4 * rp, X1 + 4 * rp, Z0 - 4 * rp, Z1 + 4 * rp, yf - 0.0012, rp, pm, root)
            if f.get("frame"):
                fc = mat("frame_" + rid, tuple(A.get("frame_col") or (210, 170, 80)) if not isinstance(f["frame"], (list, tuple)) else f["frame"], 0.6, 0.35)
                ft = 0.006 * s / 0.62
                for (fx0, fx1, fz0, fz1) in ((X0 - ft, X1 + ft, Z1, Z1 + ft), (X0 - ft, X1 + ft, Z0 - ft, Z0),
                                             (X0 - ft, X0, Z0, Z1), (X1, X1 + ft, Z0, Z1)):
                    box("frame", fx1 - fx0, 0.0035, fz1 - fz0, fc, ((fx0 + fx1) / 2, yf - 0.0012, fz0), root, corner=0.0005, edge=0.0004)
            if f.get("band"):
                zb = z0 + H * (1 - f["band"] / 100.0)
                pm = mat("piping_" + str(f.get("piping")), f.get("piping") or (200, 165, 85), 0.0, 0.35, 0.4)
                cyl("band", 0.0016, w, pm, (X0, yf - 0.0006, zb), root, seg=12, rot=(0, math.pi / 2, 0))
        elif f["kind"] == "diamond":
            dm = tex_mat("diamond_" + rid, img, w, h, 0.95, 0.22, 0.0, bump=("noise", 3000.0, 0.15, 0.0002))
            pv = empty("diamond_piv", ((X0 + X1) / 2, yf - 0.0008, (Z0 + Z1) / 2), (math.pi / 2, 0, 0), root)
            box("diamond", w - 0.002, h - 0.002, 0.0022, dm, (0, 0, -0.0022), pv, corner=0.002, edge=0.0006)
    if A.get("piping_all"):
        pm = mat("piping_" + str(A["piping_all"]), A["piping_all"], 0.3, 0.3, 0.4)
        piping_rect("piping_all", -W / 2 + 0.006, W / 2 - 0.006, z0 + 0.006, z0 + H - 0.006, yf - 0.0010, 0.0014, pm, root)
    # ---- logo (solo modalita' normale: scritta originale) ----
    if not real:
        lb = AL.logo_box(A, W, H, z0)
        limg = os.path.join(tex, rid + "_alogo.png")
        if lb and os.path.exists(limg):
            lx, lz, lw, lh = lb
            lg = A.get("logo") or {}
            lm = tex_mat("logo_" + rid, limg, lw, lh, 0.7 if not lg.get("plate") else 0.2, 0.28, 0.2, alpha=True)
            pv = empty("logo_piv", (lx, yf - 0.0030, lz), (math.pi / 2, 0, 0), root)
            box("logo", lw, lh, 0.0012, lm, (0, 0, -0.0010), pv, corner=0.0003, edge=0.0)
    # ---- maniglie, angolari, piedini, feritoie ----
    hm = mat("handle", (12, 12, 12), 0.0, 0.5, 0.2)
    ztop = z0 + H
    hd = A.get("handle")
    if hd in ("strap", "leather"):
        strap_handle("handle", min(0.17, W * 0.34) * s / 0.62 / max(0.6, s / 0.62), 0.024 * s / 0.62, 0.020 * s / 0.62,
                     0.005 * s / 0.62, hm if hd == "strap" else mat("leather", (52, 30, 20), 0.0, 0.55, 0.15),
                     G["MAT_CHROME"], (0, -D * 0.22 if A.get("view") == "top" else 0.0, ztop), root)
    elif hd == "towel":
        for sx in (-1, 1):
            cyl("towel_post", 0.004, 0.030, G["MAT_CHROME"], (sx * W * 0.32, -D * 0.38, ztop), root, seg=16)
        cyl("towel_bar", 0.005, W * 0.64, G["MAT_CHROME"], (-W * 0.32, -D * 0.38, ztop + 0.030), root, seg=16, rot=(0, math.pi / 2, 0))
    elif hd == "rack":
        for sx in (-1, 1):
            box("rack_ear", 0.020, 0.004, H, mat("rack_ear", (22, 22, 24), 0.6, 0.4), (sx * (W / 2 + 0.008), -D / 2 + 0.002, z0), root, corner=0.002)
    cn = A.get("corners")
    if cn:
        cm = {"chrome": G["MAT_CHROME"], "brass": mat("brass", (200, 160, 80), 1.0, 0.3), "black": mat("corner_blk", (18, 18, 18), 0.3, 0.45),
              "leather": mat("corner_lth", (16, 16, 16), 0.0, 0.6)}.get(cn, G["MAT_CHROME"])
        c = 0.024 * s / 0.62
        tops = [ztop - c / 2] if kind == "flip" else [z0 - 0.001 + c / 2, ztop - c / 2 + 0.001]
        for zc in tops + ([z0 + (H - (A.get("head_h") or 0) * s / 1000.0) - c / 2] if kind == "flip" else []):
            for sx in (-1, 1):
                for sy in (-1, 1):
                    yy = sy * (D / 2 - c / 2 + 0.0012)
                    if A.get("slant") and sy < 0 and zc > z0 + H * 0.5:
                        dt = (A.get("depth_top") or A["dims"][2] * 0.72) * s / 1000.0
                        yy = D / 2 - dt - c / 2 + 0.0012 + c
                    box("corner", c, c, c, cm, (sx * (W / 2 - c / 2 + 0.0012), yy, zc - c / 2), root, corner=c * 0.35, edge=c * 0.2)
    ft = A.get("feet") or "rubber"
    fm = G["MAT_RUBBER"]
    for sx in (-1, 1):
        for sy in (-1, 1):
            px, py = sx * (W / 2 - 0.03 * s / 0.62), sy * (D / 2 - 0.03 * s / 0.62)
            if ft == "casters":
                cyl("caster_plate", 0.016 * s / 0.62, 0.003, G["MAT_CHROME"], (px, py, z0 - 0.003), root, seg=20)
                cyl("caster_wheel", 0.017 * s / 0.62, 0.012 * s / 0.62, mat("wheel", (20, 20, 20), 0.0, 0.6),
                    (px - 0.006 * s / 0.62, py, 0.017 * s / 0.62), root, seg=28, rot=(0, math.pi / 2, 0))
            elif ft == "skids":
                if sx < 0:
                    box("skid", W - 0.02, 0.022 * s / 0.62, z0, mat("skid", (40, 28, 18), 0.0, 0.7), (0, py, 0), root, corner=0.002)
            elif z0 > 0:
                cyl("foot", 0.010 * s / 0.62, z0, fm, (px, py, 0.0), root, seg=20, edge=0.0008)
    if A.get("vents"):
        vm = mat("vent", (6, 6, 6), 0.0, 0.8)
        for k in range(3):
            box("vent", W * 0.18, 0.012 * s / 0.62, 0.0015, vm, ((k - 1) * W * 0.28, -D * 0.25, ztop - 0.0008), root, corner=0.002)
    if console:
        build_console(r, lay, tex, root)
    build_back(r, lay, tex, root, cov, inner)
    return root


def build_console(r, lay, tex, root):
    cg = lay["console"]
    rid = r["id"]
    x0, x1, y0, y1, zf, zb = cg["x0"], cg["x1"], cg["y0"], cg["y1"], cg["zf"], cg["zb"]
    body = mat("console_body", (30, 30, 33), 0.55, 0.38, 0.2)
    bm = bmesh.new()
    prof = [(y0, 0.0), (y1, 0.0), (y1, zb), (y0, zf)]
    L = [bm.verts.new((x0, y, z)) for y, z in prof]
    R = [bm.verts.new((x1, y, z)) for y, z in prof]
    bm.faces.new(L[::-1]); bm.faces.new(R)
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new([L[i], L[j], R[j], R[i]])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob = G["mesh_obj"]("console", bm, G["PEDAL"], [body], smooth=False)
    bv = ob.modifiers.new("bevel", "BEVEL"); bv.width = 0.0025; bv.segments = 4
    bv.limit_method = "ANGLE"; bv.angle_limit = math.radians(30)
    _par(ob, root)
    ang = math.atan2(zb - zf, y1 - y0)
    slant = math.hypot(y1 - y0, zb - zf)
    span = x1 - x0
    cy, cz = (y0 + y1) / 2, (zf + zb) / 2
    pv = empty("console_piv", (0.0, cy, cz), (ang, 0, 0), root)
    cm = tex_mat("console_" + rid, os.path.join(tex, rid + "_aconsole.png"), span, slant, 0.25, 0.42, 0.15)
    box("console_top", span - 0.004, slant - 0.003, 0.0012, cm, (0, 0, -0.0006), pv, corner=0.002, edge=0.0004)
    for p, c in zip(lay["controls"], r["controls"]):
        if p.get("inner_of"):
            continue
        x = p["u"] * span - span / 2
        y = slant / 2 - p["v"] * slant
        if p["strip"] == "toggle":
            cyl("tg_nut", 0.0030, 0.0016, G["MAT_NICKEL"], (x, y, 0.0006), pv, seg=6, edge=0.0002)
            cyl("tg_collar", 0.0019, 0.0045, G["MAT_CHROME"], (x, y, 0.0006), pv, seg=24)
        else:
            cyl("knob_nut", 0.0032, 0.0012, G["MAT_NICKEL"], (x, y, 0.0006), pv, seg=6, edge=0.0002)
    uf, vf = lay["foot_uv"]
    fx = uf * span - span / 2
    box("fs_pad", 0.019, slant * 0.72, 0.006, G["MAT_PAD"], (fx, slant / 2 - 0.55 * slant, 0.0006), pv, corner=0.002, edge=0.0015)
    lu, lv = lay["led_uv"]
    deco_led(lu * span - span / 2, slant / 2 - lv * slant, pv, z=0.0006)


# ------------------------------------------------------------------ retro
TUBE_DIMS = {"power": (0.016, 0.100), "rect": (0.016, 0.110), "pre": (0.0110, 0.055)}


def tube(x, y, z, cls, down, parent, s):
    r, ln = TUBE_DIMS.get(cls, TUBE_DIMS["pre"])
    r *= s / 0.62 * 0.95
    ln *= s / 0.62 * 0.95
    sgn = -1 if down else 1
    glass = G["MAT_GLASS"]
    cyl("tube_socket", r * 1.25, 0.004, G["MAT_SOCKET"], (x, y, z - (0.004 if down else 0)), parent, seg=24)
    zb = z + sgn * 0.004
    cyl("tube_base", r * 0.95, ln * 0.12, mat("tube_base", (20, 16, 12), 0.0, 0.6), (x, y, zb if not down else zb - ln * 0.12), parent, seg=24)
    zg0 = zb + sgn * ln * 0.12
    zc = zg0 + sgn * ln * 0.88 / 2
    cyl("tube_plate", r * 0.55, ln * 0.55, G["MAT_TUBE_INNER"], (x, y, zc - ln * 0.55 / 2), parent, seg=16)
    cyl("tube_glass", r, ln * 0.88, glass, (x, y, min(zg0, zg0 + sgn * ln * 0.88)), parent, seg=32, edge=r * 0.6)
    cyl("tube_getter", r * 0.85, ln * 0.06, mat("getter", (50, 50, 55), 1.0, 0.2),
        (x, y, (zg0 + sgn * ln * 0.80) - (ln * 0.06 if not down else 0)), parent, seg=24)


def back_items(els, pv, s):
    for e in els:
        x, y, k = e["x"], e["y"], e["kind"]
        if k == "jack":
            deco_jack(x, y, pv, z=0.0015)
        elif k == "jack2":
            deco_jack(x - 0.006, y, pv, z=0.0015); deco_jack(x + 0.006, y, pv, z=0.0015)
        elif k == "iec":
            box("iec", 0.016, 0.012, 0.004, mat("iec", (14, 14, 14), 0.0, 0.5), (x, y, 0.0015), pv, corner=0.002)
        elif k == "fuse":
            deco_fuse(x, y, pv, z=0.0015)
        elif k == "selector":
            deco_knob("pointer", x, y, 0.0060, pv, 0.4, z=0.0015)
        elif k == "toggle":
            deco_toggle(x, y, pv, z=0.0015)
        elif k == "cord":
            cyl("cord_relief", 0.0035, 0.008, mat("cord", (10, 10, 10), 0.0, 0.5), (x, y, 0.0015), pv, seg=16)
            cyl("cord", 0.0022, 0.030, mat("cord", (10, 10, 10), 0.0, 0.5), (x, y, 0.0090), pv, seg=12, rot=(math.radians(70), 0, 0))
        elif k in ("xlr", "round"):
            cyl("xlr_ring", 0.0075, 0.002, G["MAT_CHROME"] if k == "xlr" else mat("speakon", (20, 30, 60), 0.0, 0.4), (x, y, 0.0015), pv, seg=32)
            cyl("xlr_in", 0.0060, 0.0022, mat("hole", (4, 4, 4), 0.0, 0.8), (x, y, 0.0015), pv, seg=32)


def build_back(r, lay, tex, root, cov, inner):
    A = lay["amp"]
    rid = r["id"]
    W, D, H, z0 = lay["W"], lay["D"], lay["H"], lay["z0"]
    s = lay["s"]
    b = lay["back"]
    kind = b["kind"]
    zc = z0 + H / 2
    # pivot del retro: x a destra vista da dietro, y in alto, z verso l'esterno (dietro)
    bp = empty("back_piv", (0, D / 2, zc), (math.pi / 2, 0, math.pi), root)
    px0, px1, py0, py1 = b["plate"]
    pw, ph = px1 - px0, py1 - py0
    if kind == "rack":
        for k in range(9):
            box("heatsink", 0.0025, 0.030, H * 0.70, mat("heatsink", (30, 30, 32), 0.7, 0.35), (W * 0.10 + k * 0.012, -H * 0.35, 0.0), bp, corner=0.0005)
    body = None
    for ob in root.children:
        if ob.name.startswith("cab") or ob.name.startswith("chassis"):
            body = ob
    open_like = kind in ("open", "vent", "cage", "cabopen")
    if open_like and body is not None:
        t = 0.012 * s / 0.62
        top = py0 - 0.004 if kind != "cabopen" else H / 2 - 0.30 * H
        bot = -H / 2 + t if kind in ("open", "cabopen") else max(-H / 2 + t, py0 - 0.62 * H)
        if kind == "cabopen":
            top, bot = H / 2 - 0.22 * H, -H / 2 + 0.22 * H
        depth = D - 0.022 * s / 0.62 if kind in ("open", "cabopen") else D * 0.55
        cut = raw_box("back_cut", -W / 2 + t, W / 2 - t, D / 2 - depth, D / 2 + 0.02, zc + bot, zc + top)
        boolean_cut(body, cut, inner)
        # magneti degli altoparlanti sul baffle
        for (sx, sy, sd) in A.get("speakers") or []:
            xs = (sx / 100.0 - 0.5) * W
            zs = z0 + H * (1 - sy / 100.0)
            if zc + bot < zs < zc + top:
                rr = sd / 100.0 * W / 2
                cyl("basket", rr * 0.92, 0.012 * s / 0.62, mat("basket", (40, 40, 42), 0.6, 0.45), (xs, -D / 2 + 0.022 * s / 0.62, zs),
                    root, seg=40, rot=(-math.pi / 2, 0, 0))
                cyl("magnet", rr * 0.38, 0.045 * s / 0.62, mat("magnet", (70, 70, 74), 0.8, 0.35), (xs, -D / 2 + 0.03 * s / 0.62, zs),
                    root, seg=32, rot=(-math.pi / 2, 0, 0), edge=0.002)
        # valvole che pendono dal telaio (o salgono dal fondo)
        if b["tubes"] and kind != "cabopen":
            down = b.get("tube_dir") != "up"
            ztube = zc + top - 0.002 if down else zc + bot + 0.002
            for tb in b["tubes"]:
                tube(-tb["x"], D / 2 - depth * 0.35, ztube, tb["cls"], down, root, s)
            # trasformatori
            tm = mat("transformer", (18, 18, 20), 0.5, 0.5)
            for k, fx in enumerate((-0.38, 0.40)):
                box("trafo", 0.07 * s / 0.62, 0.06 * s / 0.62, 0.05 * s / 0.62, tm,
                    (fx * W, D / 2 - depth * 0.7, (ztube - 0.05 * s / 0.62) if down else ztube), root, corner=0.002)
        # pannelli posteriori (combo aperti), feritoie (testate), gabbia (SVT)
        if kind in ("open", "cabopen"):
            Lo = top - bot
            pnl = [(zc + bot, zc + bot + 0.50 * Lo), (zc + bot + 0.62 * Lo, zc + bot + 0.72 * Lo)] if kind == "open" else []
            for (za, zb_) in pnl:
                if zb_ > za:
                    box("back_panel", W - 2 * t + 0.004, 0.010, zb_ - za, cov, (0, D / 2 - 0.006, za), root, corner=0.002, edge=0.002)
        elif kind == "vent":
            for k in range(4):
                zz = zc + bot + (top - bot) * (k + 0.5) / 4.5
                box("slat", W - 2 * t + 0.004, 0.008, 0.012 * s / 0.62, cov, (0, D / 2 - 0.005, zz), root, corner=0.002, edge=0.002)
        elif kind == "cage":
            for k in range(14):
                xx = -W / 2 + t + (W - 2 * t) * (k + 0.5) / 14
                cyl("cage_rod", 0.0015, top - bot, G["MAT_CHROME"], (xx, D / 2 - 0.003, zc + bot), root, seg=10)
    # piastra del telaio o delle prese
    pimg = os.path.join(tex, rid + "_aback.png")
    fender = A.get("knob") in ("skirt", "pointer") and A["kind"] == "combo"
    bm_ = tex_mat("back_" + rid, pimg, pw, ph, 0.85 if fender else 0.3, 0.3 if fender else 0.45, 0.0, 0.4 if fender else 0.0)
    pp = empty("back_plate_piv", ((px0 + px1) / 2, (py0 + py1) / 2, 0.0), (0, 0, 0), bp)
    box("back_plate", pw, ph, 0.0030, bm_, (0, 0, -0.0015), pp, corner=0.0015, edge=0.0005)
    els = [dict(e, x=e["x"] - (px0 + px1) / 2, y=e["y"] - (py0 + py1) / 2) for e in b["elements"]]
    back_items(els, pp, s)
    if kind in ("cabclosed", "closed", "flip"):
        for sx in (-1, 1):
            for sy in (-1, 1):
                cyl("screw", 0.0022, 0.0012, G["MAT_NICKEL"], (sx * (W / 2 - 0.02), sy * (H / 2 - 0.02), 0.0), bp, seg=16)


def frame_camera(lay, back=False):
    """Inquadratura e camera dell'ampli (fronte o retro)."""
    PG = G["PG"]
    PG.use_frame("box", lay["back_frame"] if back else lay["frame"], lay["back_tilt"] if back else lay["tilt"])
    scene, cam, cd = G["scene"], G["cam"], G["cd"]
    scene.render.resolution_x = PG.RES_X
    scene.render.resolution_y = PG.RES_Y
    cap = 1200 if back else 1500
    scene.render.resolution_percentage = min(100, int(cap * 100 / max(PG.RES_X, PG.RES_Y)))
    cd.ortho_scale = PG.X1 - PG.X0
    direction = Vector((0.0, PG.SIN, -PG.COS))
    cam.rotation_euler = (PG.TILT, 0, 0)
    cam.location = Vector((0.0, PG.VC / PG.COS, 0.0)) - direction * 4.0


def render(m, lay, png, real):
    """Fronte e retro (<id>_back.png) di un ampli o di una cassa."""
    scene = G["scene"]
    board = G["_board"]
    tex = G["TEX"]
    use_amp_lights(True)
    board.scale = (5.0, 5.0, 1.0)
    root = build(m, lay, tex, real)
    frame_camera(lay, back=False)
    scene.render.filepath = png
    bpy.ops.render.render(write_still=True)
    root.rotation_euler = (0, 0, math.pi)
    frame_camera(lay, back=True)
    bf = bpy.data.objects.get("amp_backfill")
    if bf is not None and lay["back"]["kind"] in ("open", "vent", "cage", "cabopen"):
        bf.location = (0.0, -lay["D"] / 2 - 0.35, lay["z0"] + lay["H"] * 0.55)
        bf.data.energy = 32.0 * (lay["W"] / 0.4) ** 2
        bf.hide_render = False
    scene.render.filepath = png[:-4] + "_back.png"
    bpy.ops.render.render(write_still=True)
    board.scale = (1.0, 1.0, 1.0)
    use_amp_lights(False)
    # ripristina la camera dei pedali
    PG = G["PG"]
    PG.use_frame("boss")
    cam = G["cam"]
    cam.rotation_euler = (PG.TILT, 0, 0)
