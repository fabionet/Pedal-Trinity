# Rigenera le serigrafie in una cartella temporanea registrando i riquadri dei testi (metri),
# poi le confronta con quelle usate dai render (tools/art/build/cat*, build/cat_real)
import sys, os, json, pickle
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
QA = os.path.dirname(os.path.abspath(__file__))
import make_textures as MT
from PIL import ImageFont
REC = {}
CUR = []
_orig_text = MT.Canvas.text
_orig_save = MT.Canvas.save
_orig_circle = MT.Canvas.circle
_orig_rect = MT.Canvas.rect
def text(self, x, y, s, size_m, font=MT.F_BOLD, fill=(240, 240, 232), anchor="mm", spacing=0.0):
    f = ImageFont.truetype(font, max(6, int(size_m * MT.PPM)))
    if spacing:
        w = sum(f.getlength(c) for c in s) + spacing * MT.PPM * (len(s) - 1)
    else:
        w = f.getlength(s)
    w /= MT.PPM
    # altezza reale dei glifi
    bb = f.getbbox(s, anchor="ls")
    asc, desc = -bb[1] / MT.PPM, bb[3] / MT.PPM
    hh = asc + desc
    x0 = x - w / 2 if anchor[0] == "m" else (x if anchor[0] == "l" else x - w)
    y0 = y - hh / 2 if anchor[1] == "m" else y
    CUR.append(("text", s, x0, y0, x0 + w, y0 + hh))
    return _orig_text(self, x, y, s, size_m, font, fill, anchor, spacing)
def circle(self, x, y, r, fill=None, outline=None, width_m=0.0):
    CUR.append(("circle", "", x - r, y - r, x + r, y + r))
    return _orig_circle(self, x, y, r, fill, outline, width_m)
def rect(self, x0, y0, x1, y1, fill=None, outline=None, width_m=0.0, radius_m=0.0):
    CUR.append(("rect", "", x0, y0, x1, y1))
    return _orig_rect(self, x0, y0, x1, y1, fill, outline, width_m, radius_m)
def save(self, name):
    REC[os.path.join(MT.OUT, name)] = dict(canvas=(self.x0, self.y0, self.x1, self.y1), ops=list(CUR))
    CUR.clear()
    import PIL
    sub = os.path.basename(MT.OUT)
    d = os.path.join(QA, "tex", sub); os.makedirs(d, exist_ok=True)
    self.img.save(os.path.join(d, name))
MT.Canvas.text = text; MT.Canvas.save = save; MT.Canvas.circle = circle; MT.Canvas.rect = rect
import builtins
import make_catalog_textures as MC
MC.main(set())
import make_real_textures as MR
MR.main(set())
pickle.dump(REC, open(os.path.join(QA, "texrec.pkl"), "wb"))
print("registrate", len(REC))
