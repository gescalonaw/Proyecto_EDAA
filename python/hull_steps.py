"""
hull_steps.py -- Versión "paso a paso" de los 4 algoritmos de cpp/hull_algorithms.hpp, para animarlos.

Cada función es un generador que reproduce la lógica del algoritmo en C++ y emite un CUADRO
(dict, ver `F`) por cada decisión relevante. El último cuadro trae `final=True` y el hull completo.
El hull que se muestra como resultado en la herramienta es siempre el calculado por el C++;
estos generadores solo sirven para explicar cómo se llega a él.

Cuadro:
    msg     texto explicativo del paso
    chain   polilínea azul (hull parcial o final)
    closed  True si la polilínea se cierra (hull final)
    segs    [(p, q, color, punteado)]  segmentos que se están evaluando
    marks   [(p, color)]               puntos destacados
    polys   [[p, ...], ...]            polígonos auxiliares (hulls de grupo en Chan)
    active  [p, ...]                   puntos que aún se consideran (QuickHull)
    final   True en el último cuadro
Colores: "ok" (verde), "warn" (naranja), "old" (rojo), "hull" (azul), "pt" (gris).
"""


def cross(o, a, b):
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])


def dist2(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2


def F(msg, chain=(), closed=False, segs=(), marks=(), polys=(), active=(), final=False):
    return dict(msg=msg, chain=list(chain), closed=closed, segs=list(segs), marks=list(marks),
                polys=[list(q) for q in polys], active=list(active), final=final)


def _final(h, name):
    return F(f"{name}: hull completo con h = {len(h)} vértices.", chain=h, closed=len(h) >= 3, final=True)


def _andrew(pts):
    """Andrew sin animación (lo usa Chan para el hull de cada grupo)."""
    p = sorted(set(pts))
    if len(p) <= 2:
        return p
    h = []
    for q in p:
        while len(h) >= 2 and cross(h[-2], h[-1], q) <= 0:
            h.pop()
        h.append(q)
    t = len(h) + 1
    for q in reversed(p[:-1]):
        while len(h) >= t and cross(h[-2], h[-1], q) <= 0:
            h.pop()
        h.append(q)
    h.pop()
    return h


# 1. Andrew
def andrew_steps(pts):
    p = sorted(set(pts))
    n = len(p)
    if n <= 2:
        yield _final(p, "Andrew")
        return
    yield F("Se ordenan los puntos por (x, y) y se eliminan duplicados.", active=p)
    h = []

    def build(order, t_extra, nombre):
        t = len(h) + 1 if t_extra else 0
        for q in order:
            while len(h) >= max(2, t) and cross(h[-2], h[-1], q) <= 0:
                yield F(f"{nombre}: ({h[-2]}, {h[-1]}, {q}) no gira a la izquierda → se descarta {h[-1]}.",
                        chain=h, segs=[(h[-2], h[-1], "warn", False), (h[-1], q, "warn", True)],
                        marks=[(h[-1], "old"), (q, "ok")], active=p)
                h.pop()
            h.append(q)
            yield F(f"{nombre}: se agrega {q} a la pila (tamaño {len(h)}).", chain=h,
                    marks=[(q, "ok")], active=p)

    yield from build(p, False, "Cadena inferior (izq→der)")
    yield from build(reversed(p[:-1]), True, "Cadena superior (der→izq)")
    h.pop()  # el último repite al primero
    yield _final(h, "Andrew")


# 2. Jarvis
def jarvis_steps(pts):
    n = len(pts)
    if n == 0:
        yield _final([], "Jarvis")
        return
    start = min(pts)
    hull = [start]
    cur = start
    yield F(f"Se parte del punto lexicográficamente menor {start}.", chain=hull, marks=[(start, "ok")])
    for _ in range(n + 1):
        nxt = None
        for q in pts:
            if q == cur:
                continue
            if nxt is None:
                nxt = q
                yield F(f"Primer candidato a siguiente vértice desde {cur}: {q}.", chain=hull,
                        segs=[(cur, nxt, "warn", True)], marks=[(cur, "ok"), (nxt, "warn")])
                continue
            c = cross(cur, nxt, q)
            if c < 0 or (c == 0 and dist2(cur, q) > dist2(cur, nxt)):
                nxt = q
                yield F(f"{q} queda más a la derecha (o más lejos en igual ángulo) → nuevo candidato.",
                        chain=hull, segs=[(cur, nxt, "warn", True)], marks=[(cur, "ok"), (nxt, "warn")])
        if nxt is None or nxt == start:
            break
        hull.append(nxt)
        cur = nxt
        yield F(f"Todos los puntos quedan a la izquierda de la arista → {nxt} es vértice (h = {len(hull)}).",
                chain=hull, marks=[(nxt, "ok")])
    yield _final(hull, "Jarvis")


# 3. QuickHull
def _qh_rec(pts, a, b, out, lado):
    if not pts:
        return
    yield F(f"Segmento {a}→{b} ({lado}): {len(pts)} puntos afuera. Se busca el más lejano.",
            chain=out, segs=[(a, b, "warn", True)], marks=[(a, "hull"), (b, "hull")], active=pts)

    def along(q):
        return (b[0] - a[0]) * (q[0] - a[0]) + (b[1] - a[1]) * (q[1] - a[1])

    fi, best = 0, cross(a, b, pts[0])
    for i in range(1, len(pts)):
        c = cross(a, b, pts[i])
        if c < best or (c == best and along(pts[i]) > along(pts[fi])):
            best, fi = c, i
    f = pts[fi]
    s1 = [q for q in pts if cross(a, f, q) < 0]
    s2 = [q for q in pts if not cross(a, f, q) < 0 and cross(f, b, q) < 0]
    yield F(f"{f} es el más lejano → vértice. Los puntos dentro del triángulo se descartan; "
            f"quedan {len(s1)} afuera de a→f y {len(s2)} afuera de f→b.",
            chain=out, segs=[(a, f, "warn", False), (f, b, "warn", False), (a, b, "pt", True)],
            marks=[(f, "ok")], active=s1 + s2)
    yield from _qh_rec(s1, a, f, out, lado)
    out.append(f)
    yield F(f"{f} confirmado como vértice del hull (h parcial = {len(out)}).", chain=out, marks=[(f, "ok")])
    yield from _qh_rec(s2, f, b, out, lado)


def quickhull_steps(pts):
    if not pts:
        yield _final([], "QuickHull")
        return
    a, b = min(pts), max(pts)
    if a == b:
        yield _final([a], "QuickHull")
        return
    yield F(f"Extremos {a} y {b} (menor y mayor lexicográfico): siempre son vértices.",
            segs=[(a, b, "warn", True)], marks=[(a, "ok"), (b, "ok")])
    below = [q for q in pts if cross(a, b, q) < 0]
    above = [q for q in pts if cross(a, b, q) > 0]
    yield F(f"La recta {a}–{b} divide los puntos: {len(below)} abajo, {len(above)} arriba.",
            segs=[(a, b, "warn", True)], marks=[(a, "ok"), (b, "ok")], active=below + above)
    hull = [a]
    yield from _qh_rec(below, a, b, hull, "cadena inferior")
    hull.append(b)
    yield F(f"{b} se agrega al hull; ahora la cadena superior.", chain=hull, marks=[(b, "ok")])
    yield from _qh_rec(above, b, a, hull, "cadena superior")
    yield _final(hull, "QuickHull")


# 4. Chan
def _better(cur, a, b):
    c = cross(cur, a, b)
    return c < 0 or (c == 0 and dist2(cur, b) > dist2(cur, a))


def chan_steps(pts):
    n = len(pts)
    if n == 0:
        yield _final([], "Chan")
        return
    p0 = min(pts)
    t = 1
    while True:
        m = n if t >= 5 else min(n, 1 << (1 << t))
        groups = [_andrew(pts[i:i + m]) for i in range(0, n, m)]
        yield F(f"Ronda t={t}: se adivina m={m} ≥ h. {len(groups)} grupo(s) de ≤{m} puntos; "
                f"hull de cada grupo con Andrew.", polys=groups, marks=[(p0, "ok")])
        ptr = [0] * len(groups)
        for g, H in enumerate(groups):
            have = False
            for i, q in enumerate(H):
                if q == p0:
                    continue
                if not have or _better(p0, H[ptr[g]], q):
                    ptr[g], have = i, True
        hull = [p0]
        cur = p0
        for step in range(m):
            best, cands = None, []
            for g, H in enumerate(groups):
                s = len(H)
                for _ in range(s):
                    c0, c1 = H[ptr[g]], H[(ptr[g] + 1) % s]
                    if c0 == cur or _better(cur, c0, c1):
                        if s == 1:
                            break
                        ptr[g] = (ptr[g] + 1) % s
                    else:
                        break
                cand = H[ptr[g]]
                if cand == cur:
                    continue
                cands.append(cand)
                if best is None or _better(cur, best, cand):
                    best = cand
            if best is None or best == p0:
                yield _final(hull, "Chan")
                return
            yield F(f"Paso {step + 1}/{m}: tangente desde {cur} a cada grupo (gris); "
                    f"se elige la más a la derecha → {best}.", chain=hull, polys=groups,
                    segs=[(cur, c, "pt", True) for c in cands] + [(cur, best, "ok", False)],
                    marks=[(cur, "hull"), (best, "ok")])
            hull.append(best)
            cur = best
        yield F(f"No se cerró el polígono en m={m} pasos → h > m. Se repite con m mayor.",
                chain=hull, polys=groups, marks=[(cur, "old")])
        t += 1


STEPS = {"andrew": andrew_steps, "jarvis": jarvis_steps, "quickhull": quickhull_steps, "chan": chan_steps}
