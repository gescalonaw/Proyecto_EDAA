// generators.hpp
// Generadores de datasets reproducibles (semilla fija) para el análisis experimental.
// Coordenadas enteras en [-5e8, 5e8] (ver cota en geometry.hpp).
//
//   dataset          h esperado         qué prueba
//   ---------------  -----------------  ----------------------------------------
//   uniform_square   O(log n)           caso "típico"
//   uniform_disk     O(n^(1/3))         h moderado
//   gaussian         O(sqrt(log n))     h pequeño, puntos concentrados al centro
//   circle           ~n                 peor caso para Jarvis/Chan (h = n)
//   square_h4        4                  mejor caso output-sensitive (h constante)
//   clustered        pequeño            distribución no uniforme (10 clusters)
//   grid             4                  muchos duplicados y colineales (degenerado)
#pragma once
#include "geometry.hpp"

#include <cmath>
#include <random>
#include <string>

namespace gen {

constexpr double R = 5e8;
constexpr double PI = 3.14159265358979323846;

inline i64 clampi(double v) {
    if (v > R) v = R;
    if (v < -R) v = -R;
    return static_cast<i64>(std::llround(v));
}

inline std::vector<Point> uniform_square(size_t n, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> u(-R, R);
    std::vector<Point> p(n);
    for (auto& q : p) q = {clampi(u(rng)), clampi(u(rng))};
    return p;
}

inline std::vector<Point> uniform_disk(size_t n, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<Point> p(n);
    for (auto& q : p) {
        const double r = R * std::sqrt(u(rng)), a = 2 * PI * u(rng);
        q = {clampi(r * std::cos(a)), clampi(r * std::sin(a))};
    }
    return p;
}

inline std::vector<Point> gaussian(size_t n, std::mt19937_64& rng) {
    std::normal_distribution<double> g(0.0, R / 5.0);
    std::vector<Point> p(n);
    for (auto& q : p) q = {clampi(g(rng)), clampi(g(rng))};
    return p;
}

inline std::vector<Point> circle(size_t n, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> u(0.0, 2 * PI);
    std::vector<Point> p(n);
    for (auto& q : p) {
        const double a = u(rng);
        q = {clampi(R * std::cos(a)), clampi(R * std::sin(a))};
    }
    return p;
}

inline std::vector<Point> square_h4(size_t n, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> u(-R + 1, R - 1);
    std::vector<Point> p;
    p.reserve(n);
    const i64 c = static_cast<i64>(R);
    const Point corners[4] = {{-c, -c}, {c, -c}, {c, c}, {-c, c}};
    for (size_t i = 0; i < n; ++i)
        p.push_back(i < 4 ? corners[i] : Point{clampi(u(rng)), clampi(u(rng))});
    std::shuffle(p.begin(), p.end(), rng);
    return p;
}

inline std::vector<Point> clustered(size_t n, std::mt19937_64& rng) {
    constexpr int K = 10;
    std::uniform_real_distribution<double> u(-R * 0.8, R * 0.8);
    Point centers[K];
    for (auto& c : centers) c = {clampi(u(rng)), clampi(u(rng))};
    std::normal_distribution<double> g(0.0, R / 25.0);
    std::vector<Point> p(n);
    for (auto& q : p) {
        const Point& c = centers[rng() % K];
        q = {clampi(double(c.x) + g(rng)), clampi(double(c.y) + g(rng))};
    }
    return p;
}

inline std::vector<Point> grid(size_t n, std::mt19937_64& rng) {
    const i64 side = std::max<i64>(2, static_cast<i64>(std::sqrt(double(n))));
    const i64 step = 1000;
    std::vector<Point> p(n);
    for (auto& q : p) q = {(i64(rng() % side) - side / 2) * step, (i64(rng() % side) - side / 2) * step};
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

}  // namespace gen
