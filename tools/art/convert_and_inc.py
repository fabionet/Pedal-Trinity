# Converte SOLO le immagini indicate (build/pedals -> Resources/pedals, build/real -> Resources/pedals_real)
# e rigenera Catalog.inc e CatalogReal.inc senza cancellare ne' riconvertire altre immagini.
# Uso: python3 convert_and_inc.py [--norm id,id,...] [--real id,id,...] [--inc DIR]
import os, sys
W = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(W, "tools", "art"))
import assemble_catalog as A
import catalog, real_catalog, real_layout, pedal_geometry as PG, pedal_layout as PL

args = sys.argv[1:]
def opt(name):
    if name in args:
        v = args[args.index(name) + 1]
        return [x for x in v.split(",") if x]
    return []
norm, real = set(opt("--norm")), set(opt("--real"))
inc_dir = opt("--inc")[0] if opt("--inc") else os.path.join(W, "Source", "engine")

models, _ = catalog.load_all(strict=False)
for m in models:
    if m["id"] in norm:
        geo = PG.geometry(m)
        if m["style"] in ("treadle", "box"):
            A.convert_image(m["id"], logical=(geo["imageW"], geo["imageH"]), max_side=1150)
        else:
            A.convert_image(m["id"])
        print("normale", m["id"])
for r in real_catalog.real_models(strict=False):
    if r["id"] in real:
        lay = real_layout.real_layout(r)
        geo = PG.geometry(r, lay)
        A.convert_image(r["id"], A.REAL_SRC, A.REAL_RES, "r_" + r["id"], (geo["imageW"], geo["imageH"]), max_side=1150)
        print("real", r["id"])

# .inc senza toccare immagini
A.convert_image = lambda *a, **k: None
_exists = os.path.exists
def fake_exists(p):
    if p.startswith(A.PEDALS) and p.endswith(".png"):
        return _exists(os.path.join(A.RES, os.path.basename(p)[:-4] + ".jpg"))
    if p.startswith(A.REAL_SRC) and p.endswith(".png"):
        return _exists(os.path.join(A.REAL_RES, "r_" + os.path.basename(p)[:-4] + ".jpg"))
    return _exists(p)
A.os.path.exists = fake_exists
A.os.listdir = lambda d: []
A.os.makedirs = lambda *a, **k: None
A.os.path.getsize = lambda p: 0
A.os.remove = lambda p: (_ for _ in ()).throw(RuntimeError("remove vietato: " + p))
A.INC = os.path.join(inc_dir, "Catalog.inc")
A.REAL_INC = os.path.join(inc_dir, "CatalogReal.inc")
A.main()
A.main_real()
