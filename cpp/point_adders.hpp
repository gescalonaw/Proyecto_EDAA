// point_adders.hpp
// Generadores agregadores de m puntos a partir de un conjunto base actual.
// Mantienen las coordenadas dentro del rango entero [-5e8, 5e8] (ver geometry.hpp y generators.hpp).
//
//   agregador              efecto sobre el hull previo
//   ---------------------  ----------------------------------------------------
//   best_case_interior     m puntos en el interior del hull actual (sin cambio)
//   worst_case_extreme     m puntos en direcciones exteriores (expande y poda)
//   average_case_uniform   m puntos uniformes en el bounding box actual
#pragma once
#include "dynamic_hull.hpp"
#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace add {

constexpr double R = 5e8;

struct BBox {
    i64 min_x = 0, max_x = 0;
    i64 min_y = 0, max_y = 0;
    bool valid = false;
};

inline i64 clampi(double v) {
    if (v > R) v = R;
    if (v < -R) v = -R;
    return static_cast<i64>(std::llround(v));
}

inline BBox compute_bbox(const std::vector<Point>& pts) {
    if (pts.empty()) return {};
    BBox b;
    b.min_x = b.max_x = pts[0].x;
    b.min_y = b.max_y = pts[0].y;
    for (const auto& p : pts) {
        if (p.x < b.min_x) b.min_x = p.x;
        if (p.x > b.max_x) b.max_x = p.x;
        if (p.y < b.min_y) b.min_y = p.y;
        if (p.y > b.max_y) b.max_y = p.y;
    }
    b.valid = true;
    return b;
}

// Mejor caso: m puntos estrictamente hacia el interior del hull actual.
inline std::vector<Point> best_case_interior(
    const std::vector<Point>& current_pts,
    std::size_t m,
    std::mt19937_64& rng)
{
    std::vector<Point> added;
    added.reserve(m);
    if (m == 0 || current_pts.empty()) return added;

    DynamicHull dh;
    for (const auto& p : current_pts) dh.insert(p);
    const std::vector<Point> h = dh.hull();

    const auto& src = (h.size() >= 3) ? h : current_pts;
    double sx = 0.0, sy = 0.0;
    for (const auto& p : src) {
        sx += static_cast<double>(p.x);
        sy += static_cast<double>(p.y);
    }
    const double cx = sx / static_cast<double>(src.size());
    const double cy = sy / static_cast<double>(src.size());

    std::uniform_real_distribution<double> dist_t(0.05, 0.85);
    std::uniform_int_distribution<std::size_t> dist_idx(0, src.size() - 1);

    for (std::size_t i = 0; i < m; ++i) {
        const Point& v = src[dist_idx(rng)];
        const double t = dist_t(rng);
        const double px = cx + t * (static_cast<double>(v.x) - cx);
        const double py = cy + t * (static_cast<double>(v.y) - cy);
        added.push_back(Point{clampi(px), clampi(py)});
    }
    return added;
}

// Peor caso: m puntos fuera del rango actual.
inline std::vector<Point> worst_case_circle(const std::vector<Point>& /*current_pts*/, std::size_t m,
                                            std::mt19937_64& rng) {
    std::vector<Point> added;
    added.reserve(m);
    if (m == 0) return added;

    const double rho = 0.9 * add::R;
    const double two_pi = 2.0 * 3.14159265358979323846;
    std::uniform_real_distribution<double> phase(0.0, two_pi);
    const double th0 = phase(rng);

    for (std::size_t i = 0; i < m; ++i) {
        const double a = th0 + two_pi * static_cast<double>(i) / static_cast<double>(m);
        added.push_back(Point{add::clampi(rho * std::cos(a)), add::clampi(rho * std::sin(a))});
    }
    std::shuffle(added.begin(), added.end(), rng);
    return added;
}

// Caso promedio: m puntos uniformes dentro del bounding box de current_pts.
inline std::vector<Point> average_case_uniform(
    const std::vector<Point>& current_pts,
    std::size_t m,
    std::mt19937_64& rng)
{
    std::vector<Point> added;
    added.reserve(m);
    if (m == 0) return added;

    const BBox b = compute_bbox(current_pts);
    const double lo_x = b.valid ? static_cast<double>(b.min_x) : -1000.0;
    const double hi_x = b.valid ? std::max<double>(static_cast<double>(b.min_x), static_cast<double>(b.max_x)) : 1000.0;
    const double lo_y = b.valid ? static_cast<double>(b.min_y) : -1000.0;
    const double hi_y = b.valid ? std::max<double>(static_cast<double>(b.min_y), static_cast<double>(b.max_y)) : 1000.0;

    std::uniform_real_distribution<double> dist_x(lo_x, hi_x);
    std::uniform_real_distribution<double> dist_y(lo_y, hi_y);

    for (std::size_t i = 0; i < m; ++i) {
        added.push_back(Point{clampi(dist_x(rng)), clampi(dist_y(rng))});
    }
    return added;
}

struct AddGenerator {
    const char* name;
    std::vector<Point> (*fn)(const std::vector<Point>&, std::size_t, std::mt19937_64&);
};

inline const std::vector<AddGenerator>& all_adders() {
    static const std::vector<AddGenerator> gens = {
        {"best_case_interior",   best_case_interior},
        {"worst_case_extreme",   worst_case_circle},
        {"average_case_uniform", average_case_uniform},
    };
    return gens;
}

}