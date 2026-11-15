// hull_algorithms.hpp
// Algoritmos de convex hull 2D que se comparan en el proyecto.
//
//   1. andrew()     Monotone chain (Andrew 1979)         O(n log n)
//   2. jarvis()     Gift wrapping / Jarvis march (1973)  O(n h)
//   3. quickhull()  QuickHull (Eddy 1977; Barber 1996)   O(n log n) promedio, O(n^2) peor
//   4. chan()       Chan (1996)                          O(n log h)  (output-sensitive)
//
// n = número de puntos, h = número de vértices del hull.
// Todas las funciones reciben el vector POR VALOR (algunas lo ordenan o
// reordenan) y devuelven el hull en la convención descrita en geometry.hpp.
#pragma once
#include "geometry.hpp"

#include <functional>

// ---------------------------------------------------------------------------
// 1. Andrew's monotone chain
// ---------------------------------------------------------------------------
// Ordena por (x,y) y construye por separado la cadena inferior (izq->der) y la
// superior (der->izq) con una pila, descartando vértices que no giran a la
// izquierda. Es la variante de Graham scan que no necesita ordenar por ángulo.
inline std::vector<Point> andrew(std::vector<Point> p) {
    sort_unique(p);
    const size_t n = p.size();
    if (n <= 2) return p;  // 0, 1 o 2 puntos distintos: el hull es el propio conjunto

    std::vector<Point> h(2 * n);
    size_t k = 0;
    // cadena inferior
    for (size_t i = 0; i < n; ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k;
        h[k++] = p[i];
    }
    // cadena superior (el último punto de la inferior ya está en la pila)
    for (size_t i = n - 1, t = k + 1; i-- > 0;) {
        while (k >= t && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k;
        h[k++] = p[i];
    }
    h.resize(k - 1);  // el último punto repite al primero
    return h;
}

// ---------------------------------------------------------------------------
// 2. Jarvis march (gift wrapping)
// ---------------------------------------------------------------------------
// Parte del punto lexicográficamente menor y, en cada paso, recorre TODOS los
// puntos para hallar el siguiente vértice (el que deja al resto a su izquierda;
// a igual ángulo, el más lejano). Cada paso cuesta O(n) y hay h pasos.
inline std::vector<Point> jarvis(std::vector<Point> p) {
    const size_t n = p.size();
    if (n == 0) return {};
    const Point start = *std::min_element(p.begin(), p.end());

    std::vector<Point> hull{start};
    Point cur = start;
    for (size_t steps = 0; steps <= n; ++steps) {  // cota de seguridad
        bool have = false;
        Point next{};
        for (const Point& q : p) {
            if (q == cur) continue;
            if (!have) {
                next = q;
                have = true;
                continue;
            }
            const i64 c = cross(cur, next, q);
            // q está a la derecha de cur->next, o es colineal pero más lejos
            if (c < 0 || (c == 0 && dist2(cur, q) > dist2(cur, next))) next = q;
        }
        if (!have || next == start) break;  // todos iguales, o cerramos el polígono
        hull.push_back(next);
        cur = next;
    }
    return hull;
}

// ---------------------------------------------------------------------------
// 3. QuickHull
// ---------------------------------------------------------------------------
namespace detail {
// 'pts' son puntos ESTRICTAMENTE a la derecha de a->b (afuera del hull, que
// queda a la izquierda). Agrega a 'out', en orden de a hacia b, los vértices del
// hull que están entre a y b (sin incluirlos).
inline void quickhull_rec(const std::vector<Point>& pts, const Point& a, const Point& b,
                          std::vector<Point>& out) {
    if (pts.empty()) return;
    // punto más lejano de la recta ab (cross más negativo = más a la derecha)
    // Desempate: si varios puntos están a la misma distancia (sobre una paralela
    // a ab), se toma el más avanzado hacia b. Así los demás quedan fuera de las
    // nuevas aristas o en una arista (cross == 0) y no se generan vértices colineales.
    auto along = [&](const Point& q) { return (b.x - a.x) * (q.x - a.x) + (b.y - a.y) * (q.y - a.y); };
    size_t fi = 0;
    i64 best = cross(a, b, pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) {
        const i64 c = cross(a, b, pts[i]);
        if (c < best || (c == best && along(pts[i]) > along(pts[fi]))) {
            best = c;
            fi = i;
        }
    }
    const Point f = pts[fi];
    // Los puntos dentro del triángulo (a,f,b) no pueden ser vértices: se descartan.
    std::vector<Point> s1, s2;
    for (const Point& q : pts) {
        if (cross(a, f, q) < 0) s1.push_back(q);       // afuera de la arista a->f
        else if (cross(f, b, q) < 0) s2.push_back(q);  // afuera de la arista f->b
    }
    quickhull_rec(s1, a, f, out);
    out.push_back(f);
    quickhull_rec(s2, f, b, out);
}
}  // namespace detail

inline std::vector<Point> quickhull(std::vector<Point> p) {
    if (p.empty()) return {};
    const Point a = *std::min_element(p.begin(), p.end());
    const Point b = *std::max_element(p.begin(), p.end());
    if (a == b) return {a};

    std::vector<Point> below, above;  // a la derecha de a->b / a la derecha de b->a
    for (const Point& q : p) {
        const i64 c = cross(a, b, q);
        if (c < 0) below.push_back(q);
        else if (c > 0) above.push_back(q);
    }
    std::vector<Point> hull{a};
    detail::quickhull_rec(below, a, b, hull);  // cadena inferior
    hull.push_back(b);
    detail::quickhull_rec(above, b, a, hull);  // cadena superior
    return hull;
}

// ---------------------------------------------------------------------------
// 4. Chan's algorithm
// ---------------------------------------------------------------------------
// Idea: adivinar m >= h. Se parten los puntos en grupos de tamaño m, se calcula
// el hull de cada grupo (Andrew, O(n log m) en total) y luego se hace un
// gift-wrapping de a lo más m pasos donde el "siguiente vértice" se busca solo
// entre los hulls de los grupos. Si en m pasos no se cierra el polígono, h > m y
// se repite con m = m^2 (2^(2^t)). Costo total O(n log h).
//
// Búsqueda de la tangente: el paper usa búsqueda binaria en cada hull de grupo
// (O(log m)). Aquí se usa una variante equivalente en costo amortizado: cada
// grupo mantiene un puntero a su vértice-tangente, y como el wrapping avanza en
// sentido antihorario, el puntero solo avanza (CCW) sobre el hull del grupo.
// Costo por ronda: O(n log m) (hulls) + O(h * n/m + n) (punteros).
namespace detail {
// true si b es mejor candidato que a desde 'cur' (más a la derecha, o igual
// dirección pero más lejos)
inline bool chan_better(const Point& cur, const Point& a, const Point& b) {
    const i64 c = cross(cur, a, b);
    return c < 0 || (c == 0 && dist2(cur, b) > dist2(cur, a));
}
}  // namespace detail

inline std::vector<Point> chan(std::vector<Point> p) {
    const size_t n = p.size();
    if (n == 0) return {};
    const Point p0 = *std::min_element(p.begin(), p.end());

    for (int t = 1;; ++t) {
        const size_t m = (t >= 5) ? n : std::min<size_t>(n, size_t(1) << (1u << t));  // 2^(2^t)

        // (1) hull de cada grupo de m puntos
        std::vector<std::vector<Point>> groups;
        for (size_t i = 0; i < n; i += m) {
            std::vector<Point> g(p.begin() + i, p.begin() + std::min(n, i + m));
            groups.push_back(andrew(std::move(g)));
        }
        // (2) puntero inicial = tangente desde p0 en cada grupo (scan lineal una vez)
        std::vector<size_t> ptr(groups.size(), 0);
        for (size_t g = 0; g < groups.size(); ++g) {
            const auto& H = groups[g];
            bool have = false;
            for (size_t i = 0; i < H.size(); ++i) {
                if (H[i] == p0) continue;
                if (!have || detail::chan_better(p0, H[ptr[g]], H[i])) {
                    ptr[g] = i;
                    have = true;
                }
            }
        }
        // (3) gift wrapping sobre los hulls de grupo, a lo más m pasos
        std::vector<Point> hull{p0};
        Point cur = p0;
        for (size_t step = 0; step < m; ++step) {
            bool have = false;
            Point best{};
            for (size_t g = 0; g < groups.size(); ++g) {
                const auto& H = groups[g];
                const size_t s = H.size();
                // avanzar el puntero (CCW) mientras el siguiente vértice sea mejor
                for (size_t adv = 0; adv < s; ++adv) {
                    const Point& c0 = H[ptr[g]];
                    const Point& c1 = H[(ptr[g] + 1) % s];
                    if (c0 == cur || detail::chan_better(cur, c0, c1)) {
                        if (s == 1) break;
                        ptr[g] = (ptr[g] + 1) % s;
                    } else {
                        break;
                    }
                }
                const Point& cand = H[ptr[g]];
                if (cand == cur) continue;
                if (!have || detail::chan_better(cur, best, cand)) {
                    best = cand;
                    have = true;
                }
            }
            if (!have || best == p0) return hull;  // polígono cerrado
            hull.push_back(best);
            cur = best;
        }
        // no se cerró en m pasos => h > m, probar con m más grande
    }
}

// ---------------------------------------------------------------------------
// Registro de algoritmos (usado por tests, benchmark y CLI)
// ---------------------------------------------------------------------------
struct NamedAlgorithm {
    const char* name;
    std::vector<Point> (*fn)(std::vector<Point>);
};

inline const std::vector<NamedAlgorithm>& all_algorithms() {
    static const std::vector<NamedAlgorithm> algos = {
        {"andrew", andrew}, {"jarvis", jarvis}, {"quickhull", quickhull}, {"chan", chan}};
    return algos;
}
