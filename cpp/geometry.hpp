// geometry.hpp
// Tipos y primitivas geométricas compartidas por todos los algoritmos.
//
// Convención de salida de TODOS los algoritmos de este proyecto (así se pueden
// comparar con ==):
//   * vértices del convex hull en sentido antihorario (CCW),
//   * partiendo del punto lexicográficamente menor (menor x, y luego menor y),
//   * sin puntos colineales intermedios (hull estrictamente convexo),
//   * sin duplicados.
//   n=0 -> {}, todos los puntos iguales -> 1 vértice, todos colineales -> 2 vértices.
//
// Coordenadas enteras (long long) => predicados exactos, sin errores de punto
// flotante. Se asume |x|,|y| <= 1e9 para que cross() no desborde 64 bits
// (diferencias <= 2e9, productos <= 4e18, resta <= 8e18 < 9.22e18).
#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

using i64 = long long;

struct Point {
    i64 x = 0, y = 0;
    bool operator<(const Point& o) const { return x < o.x || (x == o.x && y < o.y); }
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Point& o) const { return !(*this == o); }
};

// Producto cruz (a-o) x (b-o).
//   > 0 : o->a->b gira a la izquierda (CCW)
//   = 0 : colineales
//   < 0 : gira a la derecha (CW)
inline i64 cross(const Point& o, const Point& a, const Point& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Distancia al cuadrado (para desempatar puntos colineales).
inline i64 dist2(const Point& a, const Point& b) {
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
}

// Ordena y elimina duplicados (in place).
inline void sort_unique(std::vector<Point>& p) {
    std::sort(p.begin(), p.end());
    p.erase(std::unique(p.begin(), p.end()), p.end());
}
