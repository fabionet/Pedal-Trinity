# Analisi AC (MNA) degli stadi a gyrator BOSS con transistor a modello ibrido-pi e op-amp quasi ideale.
import numpy as np

VT = 0.02585


class Circuit:
    def __init__(self):
        self.nodes = {"0": -1}
        self.elems = []

    def n(self, name):
        if name not in self.nodes:
            self.nodes[name] = len(self.nodes) - 1
        return self.nodes[name]

    def R(self, a, b, r): self.elems.append(("Y", self.n(a), self.n(b), lambda s, r=r: 1.0 / r))
    def C(self, a, b, c): self.elems.append(("Y", self.n(a), self.n(b), lambda s, c=c: s * c))
    def G(self, op, on, cp, cn, gm): self.elems.append(("G", self.n(op), self.n(on), self.n(cp), self.n(cn), gm))
    def V(self, a, val): self.elems.append(("V", self.n(a), val))
    # op-amp: uscita o, ingressi p, m; guadagno A0 con polo (GBW)
    def OA(self, p, m, o, gbw=10e6, a0=1e5): self.elems.append(("OA", self.n(p), self.n(m), self.n(o), gbw, a0))

    def npn(self, b, c, e, ie, beta, cpi=20e-12, cmu=3e-12):
        gm = ie * beta / (beta + 1) / VT
        self.R(b, e, beta / gm)
        self.G(c, e, b, e, gm)
        self.C(b, e, cpi); self.C(b, c, cmu)

    def solve(self, f, out):
        N = len(self.nodes) - 1
        nv = sum(1 for e in self.elems if e[0] in ("V", "OA"))
        res = []
        for fr in np.atleast_1d(f):
            s = 2j * np.pi * fr
            A = np.zeros((N + nv, N + nv), complex); z = np.zeros(N + nv, complex)
            k = N
            for e in self.elems:
                if e[0] == "Y":
                    _, a, b, fy = e; y = fy(s)
                    for i, j, v in ((a, a, y), (b, b, y), (a, b, -y), (b, a, -y)):
                        if i >= 0 and j >= 0: A[i, j] += v
                elif e[0] == "G":
                    _, op, on, cp, cn, gm = e
                    for i, sg in ((op, 1), (on, -1)):
                        if i < 0: continue
                        if cp >= 0: A[i, cp] += sg * gm
                        if cn >= 0: A[i, cn] -= sg * gm
                elif e[0] == "V":
                    _, a, val = e
                    A[a, k] += 1; A[k, a] += 1; z[k] = val; k += 1
                elif e[0] == "OA":
                    _, p, m, o, gbw, a0 = e
                    Aol = a0 / (1 + s * a0 / (2 * np.pi * gbw))
                    A[o, k] += 1               # corrente d'uscita
                    A[k, o] = 1.0
                    if p >= 0: A[k, p] -= Aol
                    if m >= 0: A[k, m] += Aol
                    k += 1
            x = np.linalg.solve(A, z)
            res.append(x[self.nodes[out]])
        return np.array(res)


def bias_ie(vref_minus_vbe, r1, re, beta):
    return vref_minus_vbe / (re + r1 / (beta + 1))


def gyr(c, x, top, R1, R2, C1, C2, RE, beta, tag):
    """gyrator BOSS: top -C1- x ; x -C2- base ; base -R1- Vref(AC massa) ; emett. -R2- x ; emett. -RE- massa."""
    b, e, col = tag + "b", tag + "e", tag + "c"
    c.C(top, x, C1); c.C(x, b, C2); c.R(b, "0", R1); c.R(e, x, R2); c.R(e, "0", RE)
    ie = bias_ie(4.5 - 0.62, R1, RE, beta)
    c.npn(b, "0", e, ie, beta)      # collettore a Vcc (massa AC)


def mt2_pre(beta, f):
    c = Circuit()
    c.V("in", 1.0)
    c.C("in", "p", 15e-9); c.R("p", "0", 100e3)
    c.OA("p", "m", "o")
    c.R("o", "m", 220e3); c.C("o", "m", 100e-12)
    gyr(c, "x", "m", 47e3, 2.2e3, 27e-9, 10e-9, 10e3, beta, "q")
    c.R("o", "f", 10e3); c.R("f", "0", 10e3); c.C("f", "0", 47e-9)
    return c.solve(f, "o"), c.solve(f, "f")


def mt2_post(beta, f):
    c = Circuit()
    c.V("in", 1.0)
    c.C("in", "p", 1e-6); c.R("p", "0", 100e3)
    c.OA("p", "m", "o")
    c.R("o", "m", 3.3e3); c.C("o", "m", 47e-12)
    gyr(c, "x1", "m", 47e3, 1e3, 15e-9, 1.5e-9, 10e3, beta, "a")
    gyr(c, "x2", "m", 470e3, 470, 220e-9, 47e-9, 10e3, beta, "b")
    return c.solve(f, "o")


if __name__ == "__main__":
    f = np.array([31.5, 50, 63, 80, 100, 115, 125, 160, 200, 250, 315, 400, 500, 630, 800, 950, 1250, 1600, 2000, 2500,
                  3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000])
    db = lambda h: 20 * np.log10(np.abs(h))
    print("f      " + " ".join("%6g" % v for v in f))
    for beta in (100, 200, 300, 400):
        o, fl = mt2_pre(beta, f)
        print("pre  b%3d op " % beta + " ".join("%6.1f" % v for v in db(o)))
        print("pre  b%3d fl " % beta + " ".join("%6.1f" % v for v in db(fl)))
    for beta in (100, 200, 300, 400):
        print("post b%3d    " % beta + " ".join("%6.1f" % v for v in db(mt2_post(beta, f))))
