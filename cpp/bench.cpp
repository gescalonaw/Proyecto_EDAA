// bench.cpp
// Benchmark de los algoritmos de convex hull.  Escribe CSV a stdout (o --out).
//
// MODO ESTÁTICO (por defecto): tiempo de cada algoritmo sobre cada dataset y tamaño.
//   ./bin/bench --sizes 1000,10000,100000,1000000 --reps 5 --out resultados_static.csv
//   columnas: dataset,n,algorithm,rep,time_ms,h
//
// MODO INCREMENTAL: inserta k puntos nuevos en un hull de n puntos, comparando
//   (a) DynamicHull::insert  vs  (b) recalcular con Andrew desde cero tras cada inserción.
//   ./bin/bench --mode incremental --sizes 1000,10000,100000 --k 100 --out resultados_inc.csv
//   columnas: dataset,n,k,method,rep,time_ms,changed
//
// Opciones:
//   --sizes a,b,c       tamaños n (defecto 1000,10000,100000,1000000)
//   --reps R            repeticiones por celda (defecto 5)
//   --datasets a,b      subconjunto de datasets (defecto todos)
//   --algos a,b         subconjunto de algoritmos (defecto todos)
//   --seed S            semilla (defecto 42)
//   --max-ms T          si la mediana de un (dataset,algoritmo) supera T ms, se omiten
//                       los tamaños mayores (defecto 10000; evita que Jarvis tarde horas)
//   --out archivo.csv   escribe a archivo en vez de stdout
//
// Los tiempos son de reloj de pared (std::chrono::steady_clock). La copia de la
// entrada se hace ANTES de iniciar el cronómetro. Si el curso exige medir con
// "uhr", basta reemplazar la función time_ms() por la llamada correspondiente.
#include "dynamic_hull.hpp"
#include "generators.hpp"
#include "hull_algorithms.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using Clock = std::chrono::steady_clock;

static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) if (!tok.empty()) out.push_back(tok);
    return out;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

// "dynamic": construir el hull insertando los n puntos con DynamicHull
static std::vector<Point> dynamic_build(std::vector<Point> p) {
    DynamicHull dh;
    for (const Point& q : p) dh.insert(q);
    return dh.hull();
}

struct Algo {
    std::string name;
    std::vector<Point> (*fn)(std::vector<Point>);
};

int main(int argc, char** argv) {
    std::string mode = "static", out_path;
    std::vector<size_t> sizes = {1000, 10000, 100000, 1000000};
    std::set<std::string> ds_filter, algo_filter;
    int reps = 5, k = 100;
    unsigned long long seed = 42;
    double max_ms = 10000;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "falta valor para %s\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--mode") mode = next();
        else if (a == "--sizes") { sizes.clear(); for (auto& s : split(next())) sizes.push_back(std::stoull(s)); }
        else if (a == "--reps") reps = std::stoi(next());
        else if (a == "--k") k = std::stoi(next());
        else if (a == "--seed") seed = std::stoull(next());
        else if (a == "--max-ms") max_ms = std::stod(next());
        else if (a == "--datasets") { for (auto& s : split(next())) ds_filter.insert(s); }
        else if (a == "--algos") { for (auto& s : split(next())) algo_filter.insert(s); }
        else if (a == "--out") out_path = next();
        else { std::fprintf(stderr, "opción desconocida: %s\n", a.c_str()); return 2; }
    }

    std::ofstream fout;
    if (!out_path.empty()) fout.open(out_path);
    std::ostream& out = out_path.empty() ? std::cout : fout;

    std::vector<Algo> algos;
    for (const auto& a : all_algorithms()) algos.push_back({a.name, a.fn});
    algos.push_back({"dynamic", dynamic_build});

    auto time_ms = [](auto&& f) {
        const auto t0 = Clock::now();
        f();
        return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    };

    if (mode == "static") {
        out << "dataset,n,algorithm,rep,time_ms,h\n";
        std::set<std::pair<std::string, std::string>> skipped;  // (dataset, algo) ya demasiado lento
        for (const auto& ds : gen::all_datasets()) {
            if (!ds_filter.empty() && !ds_filter.count(ds.name)) continue;
            for (size_t n : sizes) {
                std::mt19937_64 rng(seed + n);
                const std::vector<Point> data = ds.fn(n, rng);
                for (const auto& al : algos) {
                    if (!algo_filter.empty() && !algo_filter.count(al.name)) continue;
                    if (skipped.count({ds.name, al.name})) {
                        std::fprintf(stderr, "[omitido] %s n=%zu %s (excedió --max-ms)\n", ds.name, n, al.name.c_str());
                        continue;
                    }
                    if (n <= 10000) al.fn(data);  // calentamiento (solo tamaños chicos)
                    std::vector<double> ts;
                    size_t h = 0;
                    for (int r = 0; r < reps; ++r) {
                        std::vector<Point> copy = data;
                        std::vector<Point> res;
                        const double t = time_ms([&] { res = al.fn(std::move(copy)); });
                        h = res.size();
                        ts.push_back(t);
                        out << ds.name << ',' << n << ',' << al.name << ',' << r << ',' << t << ',' << h << '\n';
                        if (t > max_ms) break;  // no repetir algo lentísimo
                    }
                    std::fprintf(stderr, "%-15s n=%-8zu %-10s mediana=%10.3f ms  h=%zu\n", ds.name, n,
                                 al.name.c_str(), median(ts), h);
                    if (median(ts) > max_ms) skipped.insert({ds.name, al.name});
                }
                out.flush();
            }
        }
    } else if (mode == "incremental") {
        out << "dataset,n,k,method,rep,time_ms,changed\n";
        for (const auto& ds : gen::all_datasets()) {
            if (!ds_filter.empty() && !ds_filter.count(ds.name)) continue;
            for (size_t n : sizes) {
                std::mt19937_64 rng(seed + n), rng2(seed + n + 7919);
                const std::vector<Point> base = ds.fn(n, rng);
                const std::vector<Point> extra = ds.fn(k, rng2);  // k puntos nuevos, misma distribución
                for (int r = 0; r < reps; ++r) {
                    // (a) incremental: el hull base se construye FUERA del cronómetro
                    DynamicHull dh;
                    for (const Point& q : base) dh.insert(q);
                    int changed = 0;
                    double t_inc = time_ms([&] { for (const Point& q : extra) changed += dh.insert(q); });
                    out << ds.name << ',' << n << ',' << k << ",incremental," << r << ',' << t_inc << ',' << changed << '\n';

                    // (b) recomputo desde cero con Andrew tras cada inserción
                    std::vector<Point> all = base;
                    size_t sink = 0;
                    double t_re = time_ms([&] {
                        for (const Point& q : extra) {
                            all.push_back(q);
                            sink += andrew(all).size();
                        }
                    });
                    out << ds.name << ',' << n << ',' << k << ",recompute," << r << ',' << t_re << ',' << changed << '\n';
                    if (sink == size_t(-1)) std::puts("");  // evita que el compilador elimine el cálculo
                }
                std::fprintf(stderr, "%-15s n=%-8zu listo\n", ds.name, n);
                out.flush();
            }
        }
    } else {
        std::fprintf(stderr, "modo desconocido: %s\n", mode.c_str());
        return 2;
    }
    return 0;
}
