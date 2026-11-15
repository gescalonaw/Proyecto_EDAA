// tests.cpp
// Pruebas de corrección. Compilar y correr con: make test
//
// Para cada entrada aleatoria (incluyendo casos degenerados: duplicados,
// colineales, n=0,1,2, coordenadas muy repetidas) se verifica:
//   (a) el resultado de cada algoritmo cumple la DEFINICIÓN de convex hull
//       (validador independiente, O(n*h));
//   (b) todos los algoritmos coinciden entre sí;
//   (c) DynamicHull insertando punto a punto coincide con recalcular desde cero
//       en cada paso, y su bandera "cambió" es correcta.
#include "dynamic_hull.hpp"
#include "hull_algorithms.hpp"

#include <cmath>
#include <cstdio>
#include <random>

static int failures = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            ++failures;                                               \
            if (failures <= 15) std::printf("FALLO: %s\n", msg);      \
        }                                                             \
    } while (0)

// Validador independiente de la definición de convex hull.
static bool valid_hull(const std::vector<Point>& pts, const std::vector<Point>& h) {
    if (pts.empty()) return h.empty();
    if (h.empty()) return false;
    // vértices pertenecen a la entrada
    for (const Point& v : h)
        if (std::find(pts.begin(), pts.end(), v) == pts.end()) return false;
    // empieza en el lexicográficamente menor
    if (h[0] != *std::min_element(pts.begin(), pts.end())) return false;
    const size_t k = h.size();
    if (k == 1) {
        for (const Point& q : pts) if (q != h[0]) return false;
        return true;
    }
    if (k == 2) {  // todos los puntos colineales y dentro del segmento
        for (const Point& q : pts) {
            if (cross(h[0], h[1], q) != 0) return false;
            if (q < std::min(h[0], h[1]) || std::max(h[0], h[1]) < q) return false;
        }
        return h[0] != h[1];
    }
    for (size_t i = 0; i < k; ++i) {
        const Point &a = h[i], &b = h[(i + 1) % k], &c = h[(i + 2) % k];
        if (cross(a, b, c) <= 0) return false;  // estrictamente convexo y CCW
        for (const Point& q : pts)
            if (cross(a, b, q) < 0) return false;  // todos a la izquierda o sobre la arista
    }
    return true;
}

int main() {
    std::mt19937_64 rng(12345);
    long cases = 0;

    for (int iter = 0; iter < 60000; ++iter) {
        static const int ranges[] = {1, 2, 3, 5, 10, 100, 1000000};
        const int R = ranges[rng() % 7];
        const int n = int(rng() % 70);
        std::vector<Point> pts(n);
        const int mode = int(rng() % 4);
        for (auto& q : pts) {
            q.x = i64(rng() % (R + 1));
            q.y = (mode == 0) ? q.x : i64(rng() % (R + 1));  // mode 0: todos colineales
            if (mode == 1) q.y = 2 * q.x + 1;                // otra recta
        }
        ++cases;

        std::vector<Point> ref;
        for (const auto& a : all_algorithms()) {
            auto h = a.fn(pts);
            if (!valid_hull(pts, h)) {
                CHECK(false, a.name);
                if (failures <= 3) {
                    std::printf("  algoritmo=%s n=%d R=%d modo=%d\n  entrada:", a.name, n, R, mode);
                    for (auto& q : pts) std::printf(" (%lld,%lld)", q.x, q.y);
                    std::printf("\n  salida:");
                    for (auto& q : h) std::printf(" (%lld,%lld)", q.x, q.y);
                    std::printf("\n");
                }
            }
            if (ref.empty()) ref = h;
            else CHECK(ref == h, "algoritmos discrepan");
        }

        // (c) inserción incremental vs recomputo
        DynamicHull dh;
        std::vector<Point> so_far;
        std::vector<Point> prev;
        for (const Point& q : pts) {
            so_far.push_back(q);
            const bool changed = dh.insert(q);
            const auto expect = andrew(so_far);
            const auto got = dh.hull();
            if (got != expect) {
                CHECK(false, "DynamicHull != Andrew");
                break;
            }
            CHECK(changed == (expect != prev), "bandera 'changed' incorrecta");
            prev = expect;
        }
    }

    // casos a mano
    {
        std::vector<Point> sq = {{0, 0}, {4, 0}, {4, 4}, {0, 4}, {2, 2}, {2, 0}, {4, 2}};
        for (const auto& a : all_algorithms()) {
            auto h = a.fn(sq);
            CHECK(h.size() == 4, "cuadrado con puntos interiores/borde debe tener 4 vértices");
        }
    }

    // pruebas grandes: todos los algoritmos coinciden con Andrew
    for (int rep = 0; rep < 20; ++rep) {
        const int n = 2000 + int(rng() % 5000);
        std::vector<Point> pts(n);
        for (auto& q : pts) {
            // mezcla: disco grande y muchos puntos sobre un círculo
            if (rep % 2 == 0) {
                q.x = i64(rng() % 2000001) - 1000000;
                q.y = i64(rng() % 2000001) - 1000000;
            } else {
                const double a = (rng() % 1000000) * 6.283185307179586 / 1000000.0;
                q.x = i64(500000000.0 * std::cos(a));
                q.y = i64(500000000.0 * std::sin(a));
            }
        }
        const auto ref = andrew(pts);
        CHECK(valid_hull(pts, ref), "andrew grande inválido");
        for (const auto& a : all_algorithms()) CHECK(a.fn(pts) == ref, "discrepancia en caso grande");
        ++cases;
    }

    if (failures == 0) std::printf("OK: %ld casos, todos los algoritmos y DynamicHull correctos\n", cases);
    else std::printf("%d FALLOS\n", failures);
    return failures == 0 ? 0 : 1;
}
