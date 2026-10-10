# Controllo delle serigrafie: testo fuori dal pannello o sul bordo smussato, testi sovrapposti,
# etichette coperte dai pomelli disegnati, dal LED, dal display o dai footswitch (coordinate immagine)
import sys, os, math, pickle, json
QA = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, QA)
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import incparse, catalog, pedal_layout as PL, pedal_geometry as PG, real_catalog, real_layout
from check_geo import silhouette, in_poly, ROT
REC = pickle.load(open(os.path.join(QA, "texrec.pkl"), "rb"))
BEVEL = 0.0022

def rect_poly_hit(box, pts):
    x0, y0, x1, y1 = box
    # punti della sagoma dentro il riquadro o riquadro (centro/angoli) dentro la sagoma
    if any(x0 <= p[0] <= x1 and y0 <= p[1] <= y1 for p in pts):
        return True
    for q in ((x0, y0), (x1, y0), (x0, y1), (x1, y1), ((x0 + x1) / 2, (y0 + y1) / 2), ((x0 + x1) / 2, y0), ((x0 + x1) / 2, y1)):
        if in_poly(q, pts[:len(pts) // 2]) or in_poly(q, pts[len(pts) // 2:]):
            return True
    return False

def overlap(a, b, pad=0.0):
    return a[0] < b[2] - pad and b[0] < a[2] - pad and a[1] < b[3] - pad and b[1] < a[3] - pad

def analyse(tag, m, lay, d, texname, z, kind):
    key = None
    for k in REC:
        if k.endswith("/" + texname) and (("cat_real" in k) == (tag == "REAL")):
            key = k
    if key is None:
        return [("texture", "manca " + texname)]
    rec = REC[key]
    cx0, cy0, cx1, cy1 = rec["canvas"]
    issues = []
    texts = [o for o in rec["ops"] if o[0] == "text" and o[1].strip()]
    for o in texts:
        _, s, x0, y0, x1, y1 = o
        if x0 < cx0 - 1e-5 or x1 > cx1 + 1e-5 or y0 < cy0 - 1e-5 or y1 > cy1 + 1e-5:
            issues.append(("testo fuori dal pannello", "%s '%s' (%.1f mm)" % (texname, s, 1000 * max(cx0 - x0, x1 - cx1, cy0 - y0, y1 - cy1))))
        elif min(x0 - cx0, cx1 - x1, y0 - cy0, cy1 - y1) < BEVEL and kind != "tread":
            issues.append(("testo sul bordo smussato", "%s '%s' (%.1f mm dal bordo)" % (texname, s, 1000 * min(x0 - cx0, cx1 - x1, y0 - cy0, cy1 - y1))))
    for i in range(len(texts)):
        for j in range(i + 1, len(texts)):
            a, b = texts[i][2:], texts[j][2:]
            if overlap(a, b, 0.00015):
                issues.append(("testi sovrapposti", "%s '%s' / '%s'" % (texname, texts[i][1], texts[j][1])))
    if kind == "tread" or z is None:
        return issues
    PG.use_frame(m.get("style", "boss"), lay.get("frame"))
    def box_img(o):
        _, s, x0, y0, x1, y1 = o
        zz = z((x0 + x1) / 2, (y0 + y1) / 2) if callable(z) else z
        a = PG.proj(x0, y1, zz); b = PG.proj(x1, y0, zz)
        return (a[0], a[1], b[0], b[1])
    lx, ly, lr = d["led"]
    for o in texts:
        bi = box_img(o)
        for c in d["controls"]:
            if c["strip"] in ROT or c["strip"] in (6,):
                pts = silhouette(c)
                if c["strip"] == 6:
                    pts = pts + pts
                if rect_poly_hit(bi, pts):
                    issues.append(("etichetta coperta da un comando", "%s '%s' sotto %s" % (texname, o[1], c["label"])))
            elif c["strip"] == 5:
                if overlap(bi, (c["x"] - 5, min(c["y"], c["ty"]) - 4, c["x"] + 5, max(c["y"], c["ty"]) + 4)):
                    issues.append(("etichetta coperta da un cursore", "%s '%s' sotto %s" % (texname, o[1], c["label"])))
            elif c["strip"] in (255, 253):
                if overlap(bi, (c["x"] - c["r"], c["y"] - c["r"], c["x"] + c["r"], c["y"] + c["r"])):
                    issues.append(("etichetta sotto un footswitch", "%s '%s' sotto %s" % (texname, o[1], c["label"])))
        if overlap(bi, (lx - lr * 1.45, ly - lr * 1.45, lx + lr * 1.45, ly + lr * 1.45)):
            issues.append(("etichetta coperta dal LED", "%s '%s'" % (texname, o[1])))
        dx, dy, dw, dh = d["disp"]
        if dw > 0 and overlap(bi, (dx, dy, dx + dw, dy + dh)):
            issues.append(("etichetta sotto il display", "%s '%s'" % (texname, o[1])))
    return issues

def stomp_z(lay):
    """Quota del piano dei pedali "stomp" nel punto (x, y): profilo a spezzata (tappa 4) o cuneo, coperchi-pedale."""
    zf, zb = lay["top"]
    D = lay["body"]["D"]
    prof = lay.get("profile")
    covers = [p for p in lay["parts"] if p["type"] == "cover"]

    def z(x, y):
        for c in covers:
            if c["x0"] <= x <= c["x1"] and c["y0"] <= y <= c["y1"]:
                return c["zf"] + (c["zb"] - c["zf"]) * (y - c["y0"]) / max(1e-9, c["y1"] - c["y0"])
        if prof:
            if y >= prof[0][0]:
                return prof[0][1]
            for (ya, za), (yb, zb_) in zip(prof, prof[1:]):
                if y >= yb:
                    return za + (zb_ - za) * (y - ya) / ((yb - ya) or 1e-9)
            return prof[-1][1]
        return zf + (zb - zf) * (y + D / 2) / D
    return z


def run():
    res = {}
    models, _ = catalog.load_all(strict=False)
    inc = {d["id"]: d for d in incparse.normal()}
    for m in models:
        lay = PL.pedal_layout(m)
        d = inc[m["id"]]
        iss = []
        if m["style"] == "ts":
            iss += analyse("NORM", m, lay, d, m["id"] + "_top.png", PL.TS_H, "top")
        elif m["style"] == "nam":
            iss += analyse("NORM", m, lay, d, m["id"] + "_top.png", PL.NAM_H, "top")
        elif lay.get("builder") == "stomp":
            iss += analyse("NORM", m, lay, d, m["id"] + "_top.png", stomp_z(lay), "top")
        elif m["style"] in ("box", "treadle"):
            pass                      # bilancieri e scatole sagomate: check_tex_wah.py
        else:
            iss += analyse("NORM", m, lay, d, m["id"] + "_panel.png", PL.PANEL_H, "panel")
            iss += analyse("NORM", m, lay, d, m["id"] + "_tread.png", None, "tread")
        if iss:
            res["NORM:" + m["id"]] = iss
    inc = {d["id"]: d for d in incparse.real()}
    for r in real_catalog.real_models(strict=False):
        lay = real_layout.real_layout(r)
        d = inc[r["id"]]
        iss = []
        if lay["builder"] == "compact":
            iss += analyse("REAL", r, lay, d, r["id"] + "_panel.png", PL.PANEL_H, "panel")
            iss += analyse("REAL", r, lay, d, r["id"] + "_tread.png", None, "tread")
        elif lay["builder"] == "treadle":
            iss += analyse("REAL", r, lay, d, r["id"] + "_back.png", lay["back_block"]["z"], "top")
            iss += analyse("REAL", r, lay, d, r["id"] + "_plate.png", None, "tread")
        elif lay["builder"] == "stomp":
            iss += analyse("REAL", r, lay, d, r["id"] + "_top.png", stomp_z(lay), "top")
        elif lay["builder"] == "wah":
            pass                      # check_tex_wah.py
        else:
            iss += analyse("REAL", r, lay, d, r["id"] + "_top.png", lay["body"]["H"], "top")
        if iss:
            res["REAL:" + r["id"]] = iss
    return res

if __name__ == "__main__":
    res = run()
    json.dump(res, open(os.path.join(QA, "tex_issues.json"), "w"), indent=1)
    import collections
    cnt = collections.Counter(t for v in res.values() for t, _ in v)
    print(cnt)
    print("modelli:", len(res), " NORM:", sum(k.startswith("NORM") for k in res), " REAL:", sum(k.startswith("REAL") for k in res))
