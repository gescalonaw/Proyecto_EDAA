// hull_cli.cpp
// Puente entre el visualizador (Python) y los algoritmos en C++.
// Lee comandos por stdin (uno por línea) y responde UNA línea por comando:
//
//   ADD x y        inserta el punto con DynamicHull (inserción incremental)
//                  -> "<cambió 0|1> <h> x1 y1 x2 y2 ..."
//   ALGO nombre    recalcula el hull DESDE CERO con andrew|jarvis|quickhull|chan
//                  sobre todos los puntos insertados hasta ahora
//                  -> "<0> <h> x1 y1 ..."
//   FILE ruta      carga masiva de puntos desde un archivo "x y" por línea
//                  (los inserta con DynamicHull)     -> "1 <h> x1 y1 ..."
//   RESET          borra todos los puntos            -> "0 0"
//   QUIT           termina
//
// Error: "ERR mensaje".
#include "dynamic_hull.hpp"
#include "hull_algorithms.hpp"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

static void print_hull(int changed, const std::vector<Point>& h) {
    std::printf("%d %zu", changed, h.size());
    for (const Point& p : h) std::printf(" %lld %lld", p.x, p.y);
    std::printf("\n");
    std::fflush(stdout);
}

int main() {
    DynamicHull dh;
    std::vector<Point> all;  // todos los puntos insertados (para ALGO)
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd == "ADD") {
            Point p;
            if (!(ss >> p.x >> p.y)) { std::printf("ERR uso: ADD x y\n"); std::fflush(stdout); continue; }
            all.push_back(p);
            const bool changed = dh.insert(p);
            print_hull(changed ? 1 : 0, dh.hull());
        } else if (cmd == "ALGO") {
            std::string name;
            ss >> name;
            bool found = false;
            for (const auto& a : all_algorithms()) {
                if (name == a.name) {
                    print_hull(0, a.fn(all));
                    found = true;
                    break;
                }
            }
            if (!found) { std::printf("ERR algoritmo desconocido\n"); std::fflush(stdout); }
        } else if (cmd == "FILE") {
            // carga masiva: archivo de texto con pares "x y"; inserta todos con DynamicHull
            std::string path;
            ss >> path;
            std::FILE* f = std::fopen(path.c_str(), "r");
            if (!f) { std::printf("ERR no se pudo abrir el archivo\n"); std::fflush(stdout); continue; }
            Point p;
            while (std::fscanf(f, "%lld %lld", &p.x, &p.y) == 2) {
                all.push_back(p);
                dh.insert(p);
            }
            std::fclose(f);
            print_hull(1, dh.hull());
        } else if (cmd == "RESET") {
            dh.clear();
            all.clear();
            print_hull(0, {});
        } else if (cmd == "QUIT") {
            break;
        } else if (!cmd.empty()) {
            std::printf("ERR comando desconocido\n");
            std::fflush(stdout);
        }
    }
    return 0;
}
