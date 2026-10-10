# Controllo automatico della geometria dei comandi sopra i render (alpha del render, LED, footswitch, corpo)
import sys, os, math, json
import numpy as np
from PIL import Image
QA = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, QA)
import incparse
R = os.path.join(os.path.dirname(QA), "..", "..", "Resources")
COS = math.cos(math.radians(22.0))
ROT = {0, 1, 2, 3, 4}

def silhouette(c):
    """Punti del contorno del pomello disegnato (unione delle ellissi di base e sommita')."""
    pts = []
    r = c["r"]
    if c["strip"] in ROT:
        for cx, cy in ((c["x"], c["y"]), (c["tx"], c["ty"])):
            for a in np.linspace(0, 2 * math.pi, 48, endpoint=False):
                pts.append((cx + r * math.cos(a), cy + r * COS * math.sin(a)))
    elif c["strip"] == 6:     # levetta
        for a in np.linspace(0, 2 * math.pi, 24, endpoint=False):
            pts.append((c["x"] + 6 * math.cos(a), c["y"] + 6 * math.sin(a)))
    elif c["strip"] == 5:     # cursore: binario + cappuccio
        for t in np.linspace(0, 1, 12):
            y = c["y"] + (c["ty"] - c["y"]) * t
            pts += [(c["x"] - 6, y), (c["x"] + 6, y)]
    else:                      # footswitch / pulsanti / bilanciere
        for a in np.linspace(0, 2 * math.pi, 32, endpoint=False):
            pts.append((c["x"] + r * 0.9 * math.cos(a), c["y"] + r * 0.9 * math.sin(a)))
    return pts

def ellipse_overlap(a, b):
    """Sovrapposizione tra due pomelli (sagome come capsule verticali): profondita' in px (>0 = si toccano)."""
    def seg(c):
        return (c["x"], min(c["y"], c["ty"]), max(c["y"], c["ty"]), c["r"])
    ax, ay0, ay1, ar = seg(a); bx, by0, by1, br = seg(b)
    dy = max(0.0, max(ay0, by0) - min(ay1, by1)) / COS
    d = math.hypot(ax - bx, dy)
    return ar + br - d

def in_poly(p, poly):
    x, y = p; ins = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]; x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1 + 1e-12) + x1:
            ins = not ins
    return ins

def check(d, real):
    issues = []
    stem = d["image"][:-4] if d["image"] else None
    if not stem:
        return [("immagine", "nessuna immagine")]
    folder = R + ("/pedals_real/" if real else "/pedals/")
    ap = folder + stem + "_a.png"
    if not os.path.exists(ap):
        return [("immagine", "manca alpha " + ap)]
    A = np.asarray(Image.open(ap)).astype(np.float32)
    k = A.shape[1] / d["imageW"]
    def alpha(x, y):
        xi, yi = int(round(x * k)), int(round(y * k))
        if xi < 0 or yi < 0 or xi >= A.shape[1] or yi >= A.shape[0]:
            return 0
        return A[yi, xi]
    cs = d["controls"]
    body = d["body"]
    for i, c in enumerate(cs):
        pts = silhouette(c)
        out = [p for p in pts if alpha(*p) < 240]
        if out:
            issues.append(("fuori dal corpo (alpha)", "%s: %d/%d punti" % (c["label"], len(out), len(pts))))
        if real and body and c["strip"] not in (254,):
            bx, by, bw, bh, bf = body
            o2 = [p for p in pts if not (bx - 1 <= p[0] <= bx + bw + 1 and by - 1 <= p[1] <= by + bh + 1)]
            if o2:
                issues.append(("fuori dal rettangolo body", "%s: %d punti" % (c["label"], len(o2))))
        # LED
        lx, ly, lr = d["led"]
        if c["strip"] in ROT or c["strip"] == 6:
            dmin = min(math.hypot(p[0] - lx, p[1] - ly) for p in pts)
            inside = in_poly((lx, ly), pts[:48]) or in_poly((lx, ly), pts[48:]) if c["strip"] in ROT else math.hypot(c["x"] - lx, c["y"] - ly) < 6
            if inside or dmin < lr:
                issues.append(("sovrapposto al LED", c["label"]))
        # footswitch (zona foot)
        if c["strip"] in ROT or c["strip"] in (5, 6):
            fin = [p for p in pts if in_poly(p, d["foot"])]
            if fin:
                issues.append(("sopra il footswitch/pedale", "%s: %d punti" % (c["label"], len(fin))))
        # display
        dx, dy, dw, dh = d["disp"]
        if dw > 0 and (c["strip"] in ROT or c["strip"] in (5, 6)):
            din = [p for p in pts if dx <= p[0] <= dx + dw and dy <= p[1] <= dy + dh]
            if din:
                issues.append(("sopra il display", c["label"]))
    # sovrapposizioni tra comandi
    for i in range(len(cs)):
        for j in range(i + 1, len(cs)):
            a, b = cs[i], cs[j]
            if a["strip"] in (3,) and b["strip"] == 4 and abs(a["x"] - b["x"]) < 0.5:
                continue
            if a["strip"] in ROT and b["strip"] in ROT:
                ov = ellipse_overlap(a, b)
                if ov > 0.5:
                    issues.append(("pomelli sovrapposti", "%s/%s %.1f px" % (a["label"], b["label"], ov)))
            elif (a["strip"] in ROT and b["strip"] == 6) or (b["strip"] in ROT and a["strip"] == 6):
                kn, tg = (a, b) if a["strip"] in ROT else (b, a)
                dmin = min(math.hypot(p[0] - tg["x"], p[1] - tg["y"]) for p in silhouette(kn))
                ins = in_poly((tg["x"], tg["y"]), silhouette(kn)[:48]) or in_poly((tg["x"], tg["y"]), silhouette(kn)[48:])
                if ins or dmin < 7:
                    issues.append(("levetta sovrapposta a pomello", "%s/%s" % (kn["label"], tg["label"])))
            elif a["strip"] in (255, 253) and b["strip"] in (255, 253):
                if math.hypot(a["x"] - b["x"], a["y"] - b["y"]) < (a["r"] + b["r"]) * 0.9:
                    issues.append(("footswitch sovrapposti", "%s/%s" % (a["label"], b["label"])))
            elif {a["strip"], b["strip"]} & {255, 253, 254} and ({a["strip"], b["strip"]} & (ROT | {5, 6})):
                fs, kn = (a, b) if a["strip"] in (255, 253, 254) else (b, a)
                if fs["strip"] == 254:
                    continue
                if any(math.hypot(p[0] - fs["x"], p[1] - fs["y"]) < fs["r"] for p in silhouette(kn)):
                    issues.append(("comando sopra un footswitch", "%s/%s" % (kn["label"], fs["label"])))
    return issues

if __name__ == "__main__":
    res = {}
    for tag, lst, real in (("NORM", incparse.normal(), False), ("REAL", incparse.real(), True)):
        for d in lst:
            iss = check(d, real)
            if iss:
                res[tag + ":" + d["id"]] = iss
                print(tag, d["id"], d["style"], iss)
    json.dump(res, open(os.path.join(QA, "geo_issues.json"), "w"), indent=1)
    print("modelli con segnalazioni:", len(res))
