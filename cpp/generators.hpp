// generators.hpp
// Generadores de datasets reproducibles (semilla fija) para el análisis experimental.
// Coordenadas enteras en [-5e8, 5e8] (ver cota en geometry.hpp).
//
//   dataset          h esperado         qué prueba
//   ---------------  -----------------  ----------------------------------------
//   uniform_square   O(log n)           caso "típico"
//   uniform_disk     O(n^(1/3))         h moderado
//   gaussian         O(sqrt(log n))     h pequeño, puntos concentrados al centro
//   circle           n                  peor caso para Jarvis/Chan (h = n)
//   square_h4        4                  mejor caso output-sensitive (h constante)
//   clustered        pequeño            distribución no uniforme (10 clusters)
//   grid             4                  muchos duplicados y colineales (degenerado)
#pragma once
#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace gen {

constexpr double R = 5e8;        // cota global de coordenadas (ver geometry.hpp)
constexpr double EXT = 0.5 * R;  // extensión máxima de los datasets base (deja margen hasta R)
constexpr double PI = 3.14159265358979323846;

// Se conserva por compatibilidad: recorta a [-R, R].
inline i64 clampi(double v) {
    if (v > R) v = R;
    if (v < -R) v = -R;
    return static_cast<i64>(std::llround(v));
}

// Recorta a [-EXT, EXT]: es el que usan los datasets.
inline i64 clampb(double v) {
    if (v > EXT) v = EXT;
    if (v < -EXT) v = -EXT;
    return static_cast<i64>(std::llround(v));
}

namespace detail {

// Polígono convexo reticular con exactamente n vértices, centrado en el origen.
//
// Construcción clásica: se toman las m = n (o n+1 si n es impar) primitivas más cortas
// (a,b) con gcd(|a|,|b|) = 1, de modo que el conjunto sea simétrico (v y -v); se ordenan por ángulo
// (comparación exacta con producto cruz) y se suman como aristas. Como todas las direcciones son
// distintas y suman 0, el polígono es cerrado y estrictamente convexo. Si n es impar se construye con n+1
// y se descarta un vértice (un subconjunto de puntos en posición convexa sigue en posición convexa).
inline std::vector<Point> convex_lattice_polygon(size_t n) {
    if (n == 0) return {};
    size_t m = (n % 2 == 0) ? n : n + 1;
    if (m < 4) m = 4;
    const size_t half = m / 2;

    // (norma^2, vector) de las primitivas del semiplano superior, hasta reunir al menos 'half'.
    struct Cand {
        i64 d;
        Point v;
    };
    std::vector<Cand> cand;
    for (i64 r = 8;; r *= 2) {
        cand.clear();
        for (i64 b = 0; b <= r; ++b)
            for (i64 a = -r; a <= r; ++a) {
                if (b == 0 && a <= 0) continue;  // semiplano: b > 0, o b == 0 y a > 0
                const i64 d = a * a + b * b;
                if (d > r * r) continue;
                if (std::gcd(a < 0 ? -a : a, b) != 1) continue;
                cand.push_back({d, Point{a, b}});
            }
        if (cand.size() >= half) break;  // están todas las de norma <= r, así que las 'half' menores son exactas
    }
    std::sort(cand.begin(), cand.end(), [](const Cand& p, const Cand& q) {
        if (p.d != q.d) return p.d < q.d;
        if (p.v.x != q.v.x) return p.v.x < q.v.x;
        return p.v.y < q.v.y;
    });

    std::vector<Point> edges;
    edges.reserve(m);
    for (size_t i = 0; i < half; ++i) {
        edges.push_back(cand[i].v);
        edges.push_back(Point{-cand[i].v.x, -cand[i].v.y});
    }
    auto upper = [](const Point& v) { return v.y > 0 || (v.y == 0 && v.x > 0); };  // ángulo en [0, pi)
    std::sort(edges.begin(), edges.end(), [&](const Point& a, const Point& b) {
        const bool ua = upper(a), ub = upper(b);
        if (ua != ub) return ua;
        return a.x * b.y - a.y * b.x > 0;  // a antes que b si b está a la izquierda (CCW) de a
    });

    std::vector<Point> pts(m);
    pts[0] = {0, 0};
    for (size_t i = 0; i + 1 < m; ++i) pts[i + 1] = {pts[i].x + edges[i].x, pts[i].y + edges[i].y};

    // centrar en el origen
    i64 mnx = pts[0].x, mxx = pts[0].x, mny = pts[0].y, mxy = pts[0].y;
    for (const Point& q : pts) {
        mnx = std::min(mnx, q.x); mxx = std::max(mxx, q.x);
        mny = std::min(mny, q.y); mxy = std::max(mxy, q.y);
    }
    const i64 cx = (mnx + mxx) / 2, cy = (mny + mxy) / 2;
    const i64 lim = static_cast<i64>(EXT);
    for (Point& q : pts) {
        q.x -= cx;
        q.y -= cy;
        if (q.x > lim || q.x < -lim || q.y > lim || q.y < -lim)
            throw std::range_error("gen::circle: n demasiado grande para el rango de coordenadas");
    }
    pts.resize(n);  // si n era impar, descarta el último vértice
    return pts;
}

}  // namespace detail

inline std::vector<Point> uniform_square(size_t n, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> u(-EXT, EXT);
    std::vector<Point> p(n);
    for (auto& q : p) q = {clampb(u(rng)), clampb(u(rng))};
    return p;
}

inline std::vector<Point> uniform_disk(size_t n, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<Point> p(n);
    for (auto& q : p) {
        const double r = EXT * std::sqrt(u(rng)), a = 2 * PI * u(rng);
        q = {clampb(r * std::cos(a)), clampb(r * std::sin(a))};
    }
    return p;
}

inline std::vector<Point> gaussian(size_t n, std::mt19937_64& rng) {
    std::normal_distribution<double> g(0.0, EXT / 5.0);
    std::vector<Point> p(n);
    for (auto& q : p) q = {clampb(g(rng)), clampb(g(rng))};
    return p;
}

// Todos los puntos son vértices del hull (h = n). La forma es siempre la misma; 'rng' solo
// decide el orden de inserción.
inline std::vector<Point> circle(size_t n, std::mt19937_64& rng) {
    std::vector<Point> p = detail::convex_lattice_polygon(n);
    std::shuffle(p.begin(), p.end(), rng);
    return p;
}

inline std::vector<Point> square_h4(size_t n, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> u(-EXT + 1, EXT - 1);
    std::vector<Point> p;
    p.reserve(n);
    const i64 c = static_cast<i64>(EXT);
    const Point corners[4] = {{-c, -c}, {c, -c}, {c, c}, {-c, c}};
    for (size_t i = 0; i < n; ++i)
        p.push_back(i < 4 ? corners[i] : Point{clampb(u(rng)), clampb(u(rng))});
    std::shuffle(p.begin(), p.end(), rng);
    return p;
}

inline std::vector<Point> clustered(size_t n, std::mt19937_64& rng) {
    constexpr int K = 10;
    std::uniform_real_distribution<double> u(-EXT * 0.8, EXT * 0.8);
    Point centers[K];
    for (auto& c : centers) c = {clampb(u(rng)), clampb(u(rng))};
    std::normal_distribution<double> g(0.0, EXT / 25.0);
    std::vector<Point> p(n);
    for (auto& q : p) {
        const Point& c = centers[rng() % K];
        q = {clampb(double(c.x) + g(rng)), clampb(double(c.y) + g(rng))};
    }
    return p;
}

// Puntos al azar sobre una grilla de lado ~sqrt(n) y paso 1000 (muchos duplicados y colineales).
// Para n >= 4 se fuerzan las 4 esquinas de la grilla => h = 4 exacto.
inline std::vector<Point> grid(size_t n, std::mt19937_64& rng) {
    const i64 side = std::max<i64>(2, static_cast<i64>(std::sqrt(double(n))));
    const i64 step = 1000;
    const i64 lo = -(side / 2) * step, hi = (side - 1 - side / 2) * step;
    std::vector<Point> p(n);
    for (auto& q : p) q = {(i64(rng() % side) - side / 2) * step, (i64(rng() % side) - side / 2) * step};
    if (n >= 4) {
        p[0] = {lo, lo};
        p[1] = {hi, lo};
        p[2] = {hi, hi};
        p[3] = {lo, hi};
        std::shuffle(p.begin(), p.end(), rng);
    }
    return p;
}

inline size_t h_target = 3;   // lo fija el benchmark antes de generar

inline std::vector<Point> hull_h(size_t n, std::mt19937_64& rng) {
    const size_t h = std::min(std::max<size_t>(h_target, 3), n);
    std::vector<Point> p = detail::convex_lattice_polygon(h);
    i64 mx = 1;
    for (const Point& q : p) mx = std::max({mx, std::abs(q.x), std::abs(q.y)});
    const i64 s = std::max<i64>(1, static_cast<i64>(EXT) / mx);
    double cx = 0, cy = 0;
    for (Point& q : p) { q.x *= s; q.y *= s; cx += q.x; cy += q.y; }
    cx /= h; cy /= h;                      // centroide de los vértices: estrictamente interior
    double rin = 1e30;                     // distancia mínima del centroide a las aristas (CCW)
    for (size_t i = 0; i < h; ++i) {
        const Point &a = p[i], &b = p[(i + 1) % h];
        rin = std::min(rin, ((b.x - a.x) * (cy - a.y) - (b.y - a.y) * (cx - a.x)) / std::sqrt(double(dist2(a, b))));
    }
    const double rad = 0.9 * rin - 1.0;
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (size_t i = h; i < n; ++i) {
        const double r = rad * std::sqrt(u(rng)), a = 2 * PI * u(rng);
        p.push_back({clampb(cx + r * std::cos(a)), clampb(cy + r * std::sin(a))});
    }
    std::shuffle(p.begin(), p.end(), rng);
    return p;
}

struct NamedDataset {
    const char* name;
    std::vector<Point> (*fn)(size_t, std::mt19937_64&);
};

inline const std::vector<NamedDataset>& all_datasets() {
    static const std::vector<NamedDataset> d = {
        {"uniform_square", uniform_square}, {"uniform_disk", uniform_disk}, {"gaussian", gaussian},
        {"circle", circle},                 {"square_h4", square_h4},       {"clustered", clustered},
        {"grid", grid}};
    return d;
}

inline const std::vector<NamedDataset>& h_datasets() {
    static const std::vector<NamedDataset> d = {{"hull_h", hull_h}};
    return d;
}

}  // namespace gen