# Controllo delle serigrafie dei pedali sagomati (builder "wah"): testi fuori dalla superficie, testi
# sovrapposti, scritte coperte dai pomelli/levette/pulsanti/LED (in coordinate del pedale, con la sommita'
# proiettata dei pomelli). Registra i testi rigenerando le serigrafie in una cartella temporanea.
import os, sys, math, json, tempfile
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import make_textures as MT
from PIL import ImageFont
import catalog, shaped_layout as SL, shaped_textures as ST, real_catalog, real_layout, pedal_layout as PL

REC, CUR = {}, []
_t, _s = MT.Canvas.text, MT.Canvas.save
def text(self, x, y, s, size_m, font=MT.F_BOLD, fill=(240, 240, 232), anchor="mm", spacing=0.0):
    f = ImageFont.truetype(font, max(6, int(size_m * MT.PPM)))
    w = (sum(f.getlength(c) for c in s) + spacing * MT.PPM * (len(s) - 1)) if spacing else f.getlength(s)
    w /= MT.PPM
    bb = f.getbbox(s or " ", anchor="ls"); hh = (bb[3] - bb[1]) / MT.PPM
    x0 = x - w / 2 if anchor[0] == "m" else (x if anchor[0] == "l" else x - w)
    y0 = y - hh / 2 if anchor[1] == "m" else y
    CUR.append((s, x0, y0, x0 + w, y0 + hh))
    return _t(self, x, y, s, size_m, font, fill, anchor, spacing)
def save(self, name):
    REC[name] = dict(canvas=(self.x0, self.y0, self.x1, self.y1), texts=list(CUR)); CUR.clear()
MT.Canvas.text, MT.Canvas.save = text, save
tmp = tempfile.mkdtemp()

def ov(a, b, pad=0.0):
    return a[0] < b[2] - pad and b[0] < a[2] - pad and a[1] < b[3] - pad and b[1] < a[3] - pad

def obstacles(r, lay):
    """Sagome dei comandi in coordinate del pedale: (x0, y0, x1, y1, etichetta)."""
    out = []
    for k, c in enumerate(r["controls"]):
        if k == 0:
            continue
        p = lay["controls"][k]
        if p["strip"] in ("boss", "boss_small"):
            out.append((p["x"] - p["r"], p["y"] - p["r"], p["x"] + p["r"], p["y"] + p["r"] + PL.knob_shadow(p["h"]), c["label"]))
        elif p["strip"] == "toggle":
            out.append((p["x"] - 0.003, p["y"] - 0.003, p["x"] + 0.003, p["y"] + 0.007, c["label"]))
        else:
            rr = p["r"] * 1.3
            out.append((p["x"] - rr, p["y"] - rr, p["x"] + rr, p["y"] + rr, c["label"]))
    for part in lay["parts"]:
        if part["type"] == "led":
            out.append((part["x"] - 0.003, part["y"] - 0.003, part["x"] + 0.003, part["y"] + 0.003, "LED"))
    return out

def check(tag, r, lay):
    issues = []
    REC.clear()
    ST.wah_textures(r, lay, tmp)
    obs = obstacles(r, lay)
    for name, rec in REC.items():
        cx0, cy0, cx1, cy1 = rec["canvas"]
        # offset dal sistema della texture a quello del pedale
        dx = dy = 0.0
        if "_pod" in name:
            k = int(name.split("_pod")[1].split(".")[0]); pod = lay["pods"][k]
            dx, dy = (pod["x0"] + pod["x1"]) / 2, (pod["y0"] + pod["y1"]) / 2
        on_top = "_pod" in name or "_panel" in name or "_base" in name
        T = rec["texts"]
        for s, x0, y0, x1, y1 in T:
            if x0 < cx0 - 1e-5 or x1 > cx1 + 1e-5 or y0 < cy0 - 1e-5 or y1 > cy1 + 1e-5:
                issues.append("%s: testo fuori dalla superficie '%s'" % (name, s))
            if on_top:
                box = (x0 + dx, y0 + dy, x1 + dx, y1 + dy)
                for o in obs:
                    if ov(box, o[:4], 0.0001):
                        issues.append("%s: '%s' coperto da %s" % (name, s, o[4]))
        for i in range(len(T)):
            for j in range(i + 1, len(T)):
                if ov(T[i][1:], T[j][1:], 0.00015):
                    issues.append("%s: testi sovrapposti '%s' / '%s'" % (name, T[i][0], T[j][0]))
    return issues

res = {}
models, _ = catalog.load_all()
for m in models:
    if m.get("real") and m["style"] == "treadle":
        r = SL.as_shaped(m); lay = SL.wah_layout(r)
        iss = check("NORM", r, lay)
        if lay.get("warnings"): iss += ["layout: " + w for w in lay["warnings"]]
        if iss: res["NORM:" + m["id"]] = iss
for r in real_catalog.real_models():
    if r["series"] == "shaped":
        lay = real_layout.real_layout(r)
        iss = check("REAL", r, lay)
        if iss: res["REAL:" + r["id"]] = iss
for k, v in res.items():
    print(k); [print("   ", x) for x in v]
print("modelli con segnalazioni:", len(res))
json.dump(res, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "tex_wah_issues.json"), "w"), indent=1)
