# Lettura dei cataloghi C++ (Catalog.inc / CatalogReal.inc) in dizionari Python
import re
import os
W = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
NUM = r"(-?[0-9.]+)f"

def _strs(s):
    return re.findall(r'"((?:[^"\\]|\\.)*)"', s)

def parse(path):
    txt = open(path).read()
    out = []
    for blk in txt.split("\n    { ")[1:]:
        m = re.match(r'"([^"]+)", "((?:[^"\\]|\\.)*)", "([^"]*)", "([^"]*)", "([^"]*)", Family::(\w+), (\d+),', blk)
        if not m:
            continue
        d = dict(id=m.group(1), name=m.group(2), code=m.group(3), inspired=m.group(4), cat=m.group(5), family=m.group(6),
                 n=int(m.group(7)))
        ctrls = []
        for cm in re.finditer(r'\{ "((?:[^"\\]|\\.)*)", "([^"]*)", CK::(\w+), (\d+), ([-0-9.]+)f, (\d+), U::(\w+), ([-0-9.]+)f, ([-0-9.]+)f, (nullptr|"[^"]*"), ' + ", ".join([NUM] * 5) + r' \}', blk):
            g = cm.groups()
            ctrls.append(dict(label=g[0], role=g[1], kind=g[2], strip=int(g[3]), x=float(g[10]), y=float(g[11]),
                              tx=float(g[12]), ty=float(g[13]), r=float(g[14])))
        assert len(ctrls) == d["n"], (d["id"], len(ctrls), d["n"])
        d["controls"] = ctrls
        rest = blk[blk.index("} },") + 4:] if "} }," in blk else blk
        mm = re.search(r'"(\w+)", 0x([0-9a-f]+)u, (nullptr|"\w+"), ' + ", ".join([NUM] * 5) + r',\s*\{ ([^}]*) \}, \{ ([^}]*) \}, ' + ", ".join([NUM] * 4) + r',\s*' + ", ".join([NUM] * 4) + r', (true|false)', rest)
        assert mm, d["id"]
        g = mm.groups()
        d["style"] = g[0]
        d["image"] = None if g[2] == "nullptr" else g[2].strip('"')
        d["imageW"], d["imageH"] = float(g[3]), float(g[4])
        d["led"] = (float(g[5]), float(g[6]), float(g[7]))
        fx = [float(v.rstrip("f")) for v in g[8].split(",")]
        fy = [float(v.rstrip("f")) for v in g[9].split(",")]
        d["foot"] = list(zip(fx, fy))
        d["disp"] = tuple(float(v) for v in g[10:14])
        d["jacks"] = tuple(float(v) for v in g[14:18])
        d["stereo"] = g[18] == "true"
        tail = rest[mm.end():]
        bm = re.search(r'",\s*' + ", ".join([NUM] * 5) + r' \}', tail)
        d["body"] = tuple(float(v) for v in bm.groups()) if bm else None
        out.append(d)
    return out

def normal():
    return parse(W + "/Source/engine/Catalog.inc")

def real():
    return parse(W + "/Source/engine/CatalogReal.inc")

if __name__ == "__main__":
    a, b = normal(), real()
    print(len(a), len(b))
    print(b[0])
