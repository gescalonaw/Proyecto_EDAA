/*
ESQUEMA DE BENCHMARK
importaciones de bibliotecas, generadores (generators.hpp), agregadores (point_adders.hpp),
algoritmos estáticos (hull_algorithms.hpp) y estructura dinámica (dynamic_hull.hpp)

MemTracker // rastreador global de memoria heap
struct Config // contiene los parametros de ejecución
utilidades // mix (semillas deterministas), ms_between (cronómetro steady_clock), progress, shrink_half, recalc_batch_size
class Writter // calcula métricas (media, desv. estándar, varianza, cuartiles) y escribe filas en un .csv
class Collector // mapa que acumula tiempos, memoria y "changed" por (dataset, algo) para un n

exp1 // Convex-Hull estatico
exp2 // Convex-Hull dinámico: costo de ACTUALIZAR el hull tras insertar m puntos nuevos
     //   (a) dynamic_hull     : DynamicHull::insert de los m puntos + extraer hull()   
     //   (b) recompute_andrew : Andrew desde cero sobre los n+m puntos                

main(){
1. validar argumentos de entrada
2. validar parametros
3. seleccionar experimentos (exp1, exp2 o all)
4. abrir Writter y correr experimentos
}

exp1(configuración, escritor)
    obtener lista de generadores (datasets)
    instanciar registro de omitidos

    warm-up global previo al experimento

    for n = lower -> upper (paso geometrico: n *= step)
        instanciar colector para el tamaño n

        for each gen
            // 1. Fase de Tiempo
            for r = 1 -> runs
                generar datos frescos con gen(n, seed_mezclada)

                for each algo (andrew, jarvis, quickhull)
                    si (gen, algo) está en omitidos -> saltar

                    for 1 -> batch_k
                        copiar datos de entrada
                        empezar medicion de tiempo
                        ejecutar convex-hull estático
                        terminar medicion de tiempo
                        acumular tiempo del lote

                    calcular media del lote (batch_k)
                    guardar tiempo medio en el colector

                    si tiempo_medio > max_ms -> agregar (gen, algo) a omitidos

            // 2. Fase de Memoria
            generar datos con gen(n, seed_memoria)
            for each algo (andrew, jarvis, quickhull)
                si (gen, algo) está en omitidos -> saltar

                copiar datos de entrada
                empezar medicion de memoria (MemTracker::start)
                ejecutar convex-hull estático
                leer pico de memoria y detener medicion (MemTracker::stop)

                guardar pico de memoria en el colector

        volcar colector al escritor


exp2(configuración, escritor)
    obtener lista de generadores base (datasets) y agregadores de puntos

    warm-up global previo al experimento

    for n = lower -> upper (paso geometrico: n *= step)
        instanciar colector para el tamaño n
        determinar cantidad m de puntos nuevos a agregar

        for each gen
            for each add_gen (best_case_interior, worst_case_extreme, average_case_uniform)
                armar etiqueta "dataset+adder"
                // 1. Fase de Tiempo
                for r = 1 -> runs
                    generar n puntos iniciales con gen(n, seed_base) y aplicar shrink_half
                    generar m puntos nuevos con add_gen(base_pts, m, seed_mut)
                    construir hull previo con DynamicHull 
                    armar el arreglo all = base + nuevos para la línea base 

                    for 1 -> batch_k
                        // (a) incremental
                        clonar hull previo (FUERA del cronómetro)
                        empezar medicion de tiempo
                        insertar los m puntos nuevos en el hull clonado   
                        extraer el convex hull actualizado con hull()     
                        terminar medicion de tiempo
                        acumular tiempo del lote (dynamic_hull)

                        // (b) línea base: recalcular desde cero
                        copiar all (FUERA del cronómetro)
                        empezar medicion de tiempo
                        andrew(all)                                      
                        terminar medicion de tiempo
                        acumular tiempo del lote (recompute_andrew)

                    calcular medias de cada lote (batch_k) y guardar ambos tiempos medios en el colector

                // 2. Fase de Memoria
                generar base (con shrink_half) y nuevos con semillas de memoria
                armar mem_all = mem_base + mem_mut
                (a) medir pico de DynamicHull: construir hull previo, insertar m puntos y extraer hull()
                (b) medir pico de andrew sobre los n+m puntos
                guardar picos de memoria y changed en el colector

        volcar colector al escritor
*/

#include "generators.hpp"
#include "point_adders.hpp"
#include "hull_algorithms.hpp"
#include "dynamic_hull.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using Clock = std::chrono::steady_clock;

/*
 * MemTracker: cuenta bytes pedidos al heap mientras 'active' es true.
 * Mide memoria adicional: lo reservado ANTES de start() (p. ej. la copia de la
 * entrada) no se cuenta. El decremento depende del operator delete con tamaño
 * (C++14+: GCC/Clang/MSVC lo usan por defecto para std::vector y std::map).
 */
#if defined(__GNUC__) && !defined(__clang__) && !defined(__cpp_sized_deallocation)
#warning "Sin sized deallocation: el pico de memoria saldría subestimado. Compila con -std=c++17."
#endif

/**
 * @brief Rastreador global del consumo adicional de memoria en el heap.
 */
struct MemTracker {
    static inline std::size_t current = 0;         ///< Bytes activos actualmente rastreados.
    static inline std::size_t peak = 0;            ///< Máximo histórico de bytes alcanzado durante la sesión.
    static inline bool active = false;             ///< Indica si el rastreo está habilitado.
    static inline bool tracking_paused = false;  ///< Guardia anti-reentrancia para el rastreador.

    /**
     * @brief Reinicia los contadores y activa el rastreo de memoria.
     */
    static void start() {
        current = 0;
        peak = 0;
        tracking_paused = false;
        active = true;
    }

    /**
     * @brief Desactiva el rastreo de memoria y limpia el estado de pausa.
     */
    static void stop() {
        active = false;
        tracking_paused = false;
    }
};

// GCC 13 emite un falso positivo (-Wmismatched-new-delete) al inlinear estos operadores.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

/**
 * @brief Sobrecarga global de `operator new` para contabilizar asignaciones en el heap.
 * @param size Cantidad de bytes solicitados.
 * @return Puntero a la memoria reservada.
 * @throws std::bad_alloc Si `std::malloc` retorna `nullptr`.
 */
void* operator new(std::size_t size) {
    if (size == 0) size = 1;
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();

    if (MemTracker::active && !MemTracker::tracking_paused) {
        MemTracker::tracking_paused = true;
        MemTracker::current += size;
        if (MemTracker::current > MemTracker::peak) {
            MemTracker::peak = MemTracker::current;
        }
        MemTracker::tracking_paused = false;
    }
    return ptr;
}

/**
 * @brief Sobrecarga global de `operator new[]` delegada a `operator new`.
 * @param size Cantidad de bytes solicitados.
 * @return Puntero a la memoria reservada.
 */
void* operator new[](std::size_t size) {
    return ::operator new(size);
}

/**
 * @brief Sobrecarga global de `operator delete` sin tamaño conocido.
 * @param ptr Puntero a liberar.
 */
void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

/**
 * @brief Sobrecarga global de `operator delete[]` sin tamaño conocido.
 * @param ptr Puntero a liberar.
 */
void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

/**
 * @brief Sobrecarga global de `operator delete` con tamaño (C++14+) para descontar memoria.
 * @param ptr Puntero a liberar.
 * @param size Tamaño en bytes del bloque liberado.
 */
void operator delete(void* ptr, std::size_t size) noexcept {
    if (MemTracker::active && !MemTracker::tracking_paused) {
        MemTracker::tracking_paused = true;
        MemTracker::current = (size <= MemTracker::current) ? (MemTracker::current - size) : 0;
        MemTracker::tracking_paused = false;
    }
    std::free(ptr);
}

/**
 * @brief Sobrecarga global de `operator delete[]` con tamaño (C++14+) delegada a `operator delete`.
 * @param ptr Puntero a liberar.
 * @param size Tamaño en bytes del arreglo liberado.
 */
void operator delete[](void* ptr, std::size_t size) noexcept {
    ::operator delete(ptr, size);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

/**
 * @brief Parámetros de configuración de la línea de comandos para los experimentos.
 */
struct Config {
    std::string exp = "all";       ///< Experimento a ejecutar: `exp1`, `exp2` o `all`.
    std::string out = "resultados.csv"; ///< Ruta del archivo CSV de salida.
    std::size_t runs = 32;         ///< Cantidad de repeticiones por tamaño `n`.
    std::size_t lower = 1024;      ///< Tamaño inicial de entrada `n`.
    std::size_t upper = 1u << 20;  ///< Tamaño máximo de entrada `n`.
    std::size_t step = 2;          ///< Factor multiplicativo geométrico para `n`.
    std::size_t batch_k = 1;       ///< Iteraciones internas por repetición.
    std::size_t mut_ops = 0;       ///< Cantidad `m` de puntos nuevos en `exp2` (0 = automático).
    double max_ms = 10000.0;       ///< Límite de tiempo en ms antes de omitir en `exp1`.
    std::uint64_t seed = 1;        ///< Semilla base para la generación reproducible.
};

/**
 * @brief Mezcla determinísticamente dos enteros de 64 bits (SplitMix64).
 * @param a Primer valor de 64 bits.
 * @param b Segundo valor de 64 bits.
 * @return Valor hash/mezclado de 64 bits.
 */
static std::uint64_t mix(std::uint64_t a, std::uint64_t b) {
    std::uint64_t z = a + 0x9e3779b97f4a7c15ULL * (b + 1);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/**
 * @brief Calcula la diferencia de tiempo entre dos instantes en milisegundos.
 * @param a Instante inicial.
 * @param b Instante final.
 * @return Duración transcurrida en milisegundos.
 */
static inline double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

/**
 * @brief Imprime en `std::cerr` el progreso actual de la ejecución.
 * @param tag Etiqueta del experimento.
 * @param ds Nombre del dataset o combinación activa.
 * @param n Tamaño actual del problema.
 * @param r Índice base de repetición (0-based).
 * @param R Total de repeticiones configuradas.
 */
static void progress(const std::string& tag, const std::string& ds, std::size_t n, std::size_t r, std::size_t R) {
    std::cerr << "\r[" << tag << " | " << ds << "] n=" << n << "  rep " << (r + 1) << "/" << R << "      " << std::flush;
}

/**
 * @brief Reduce a la mitad el rango de coordenadas de un conjunto de puntos.
 *
 * Deja el conjunto dentro de `[-R/2, R/2]^2` con margen hacia `±R` para que
 * los agregadores de casos extremos puedan insertar puntos fuera del hull previo.
 *
 * @param p Vector de puntos a escalar.
 * @return Vector de puntos con coordenadas divididas entre 2.
 */
static std::vector<Point> shrink_half(std::vector<Point> p) {
    for (auto& q : p) {
        q.x /= 2;
        q.y /= 2;
    }
    return p;
}

/*
 * Writter: una fila por (dataset, n, algo). Columnas originales primero; las nuevas al final.
 *   runs        : número de muestras efectivamente tomadas
 *   t_var_ms2   : varianza muestral (n-1) de los tiempos = t_stdev_ms^2
 *   changed     : (solo exp2/dynamic_hull) cuántas de las m inserciones modificaron el hull; -1 = no aplica
 *   status      : ok | truncado (menos de --runs muestras por superar --max-ms) | omitido (sin muestras)
 */

/**
 * @brief Calcula estadísticos descriptivos y escribe los resultados en un archivo CSV.
 */
class Writter {
    std::ofstream f; ///< Flujo de salida hacia el archivo CSV.

public:
    /**
     * @brief Abre el archivo CSV y escribe la fila de encabezados.
     * @param path Ruta del archivo de salida.
     */
    explicit Writter(const std::string& path) : f(path) {
        if (!f.is_open()) {
            std::cerr << "Error abriendo " << path << "\n";
            std::exit(EXIT_FAILURE);
        }
        f << "dataset,n,algo,runs,t_mean_ms,t_stdev_ms,t_Q0,t_Q1,t_Q2,t_Q3,t_Q4,peak_mem_bytes,"
             "t_var_ms2,changed,status\n";
    }

    /**
     * @brief Calcula media, desviación estándar, varianza y cuartiles y escribe una fila.
     * @param ds Nombre del dataset o escenario.
     * @param n Tamaño de entrada.
     * @param algo Nombre del algoritmo o variante evaluada.
     * @param v Vector de muestras de tiempo en milisegundos.
     * @param peak_mem Pico de memoria adicional medido en bytes.
     * @param changed Cantidad de inserciones que alteraron el hull (-1 si no aplica).
     * @param target_runs Cantidad objetivo de repeticiones configuradas.
     */
    void row(const std::string& ds, std::size_t n, const std::string& algo,
             std::vector<double> v, std::size_t peak_mem, long long changed,
             std::size_t target_runs) {
        if (v.empty()) return;
        const double m = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
        double ss = 0.0;
        for (double t : v) ss += (t - m) * (t - m);
        const double var = ss / (v.size() > 1 ? static_cast<double>(v.size() - 1) : 1.0);
        const double sd = std::sqrt(var);

        std::sort(v.begin(), v.end());
        auto q = [&](double p) { return v[static_cast<std::size_t>(p * static_cast<double>(v.size() - 1))]; };

        f << ds << ',' << n << ',' << algo << ',' << v.size() << ','
          << m << ',' << sd << ',' << q(0.0) << ',' << q(0.25) << ',' << q(0.5) << ','
          << q(0.75) << ',' << q(1.0) << ',' << peak_mem << ','
          << var << ',' << changed << ',' << (v.size() < target_runs ? "truncado" : "ok") << '\n';
        f.flush();
    }

    /**
     * @brief Escribe una fila marcada como `omitido` para celdas no ejecutadas.
     * @param ds Nombre del dataset.
     * @param n Tamaño de entrada.
     * @param algo Nombre del algoritmo omitido.
     */
    void skipped_row(const std::string& ds, std::size_t n, const std::string& algo) {
        f << ds << ',' << n << ',' << algo << ",0";
        for (int i = 0; i < 11; ++i) f << ',';  // 10 campos numéricos vacíos
        f << "omitido\n";
        f.flush();
    }
};

/**
 * @brief Acumula tiempos, pico de memoria y contador de cambios por `(dataset, algo)` para un `n`.
 */
class Collector {
    struct Entry {
        std::vector<double> times_ms; ///< Muestras de tiempo promedio por repetición (ms).
        std::size_t peak_mem = 0;     ///< Pico de memoria adicional en bytes.
        long long changed = -1;       ///< Número de inserciones que modificaron el hull (-1 si N/A).
    };

    std::map<std::tuple<std::string, std::string>, Entry> m; ///< Mapa `(dataset, algo)` -> métricas.

public:
    /**
     * @brief Registra una muestra de tiempo para la pareja `(ds, algo)`.
     * @param ds Nombre del dataset o escenario.
     * @param algo Nombre del algoritmo.
     * @param t_ms Tiempo promedio del lote en milisegundos.
     */
    void add_time(const std::string& ds, const std::string& algo, double t_ms) {
        m[{ds, algo}].times_ms.push_back(t_ms);
    }

    /**
     * @brief Registra el pico de memoria y, opcionalmente, el contador `changed`.
     * @param ds Nombre del dataset o escenario.
     * @param algo Nombre del algoritmo.
     * @param bytes Pico de memoria adicional en bytes.
     * @param changed Cantidad de inserciones que cambiaron el hull (-1 por defecto).
     */
    void set_mem(const std::string& ds, const std::string& algo, std::size_t bytes, long long changed = -1) {
        Entry& e = m[{ds, algo}];
        e.peak_mem = bytes;
        e.changed = changed;
    }

    /**
     * @brief Vuelca todas las entradas acumuladas al `Writter` y limpia el colector.
     * @param w Escritor CSV destino.
     * @param n Tamaño de entrada asociado al lote.
     * @param target_runs Cantidad objetivo de repeticiones.
     * @param skipped Conjunto opcional de celdas omitidas para emitir filas `omitido`.
     */
    void flush(Writter& w, std::size_t n, std::size_t target_runs,
               const std::set<std::pair<std::string, std::string>>* skipped = nullptr) {
        for (auto& [key, e] : m) {
            w.row(std::get<0>(key), n, std::get<1>(key), std::move(e.times_ms), e.peak_mem, e.changed,
                  target_runs);
        }
        if (skipped) {
            for (const auto& [ds, algo] : *skipped) {
                if (m.find({ds, algo}) == m.end() || m[{ds, algo}].times_ms.empty()) w.skipped_row(ds, n, algo);
            }
        }
        m.clear();
    }
};

/**
 * @brief Descriptor de un algoritmo estático de envolvente convexa.
 */
struct StaticAlgoEntry {
    const char* name;                              ///< Identificador del algoritmo.
    std::vector<Point> (*fn)(std::vector<Point>);  ///< Puntero a la función del algoritmo.
};

/**
 * @brief Devuelve la colección de algoritmos estáticos a evaluar en `exp1`.
 * @return Referencia constante al vector de algoritmos estáticos.
 */
static const std::vector<StaticAlgoEntry>& static_algos() {
    static const std::vector<StaticAlgoEntry> algos = {
        {"andrew",    &andrew},
        {"jarvis",    &jarvis},
        {"quickhull", &quickhull},
    };
    return algos;
}

/**
 * @brief Calcula la cantidad `m` de puntos nuevos a agregar en una actualización de `exp2`.
 * @param c Configuración del benchmark.
 * @param n Tamaño actual del conjunto base.
 * @return Cantidad `m` de puntos nuevos a insertar.
 */
static std::size_t recalc_batch_size(const Config& c, std::size_t n) {
    if (c.mut_ops > 0) return std::min(c.mut_ops, n);
    return std::max<std::size_t>(1, std::min<std::size_t>(n / 10, 1000));
}

/**
 * @brief Ejecuta el Experimento 1: evaluación de algoritmos estáticos de convex hull.
 * @param c Configuración del benchmark.
 * @param w Escritor CSV de resultados.
 */
static void exp1(const Config& c, Writter& w) {
    const auto& algos = static_algos();
    const auto& datasets = gen::all_datasets();
    std::set<std::pair<std::string, std::string>> skipped; // (dataset, algo) que superaron max_ms

    // 0. Warm-up global único previo al experimento
    {
        std::mt19937_64 rng_warm(c.seed);
        const std::vector<Point> warm_pts = gen::uniform_square(4096, rng_warm);
        for (const auto& algo : algos) {
            algo.fn(warm_pts);
        }
    }

    for (std::size_t n = c.lower; n <= c.upper; n *= c.step) {
        Collector col;

        for (const auto& ds : datasets) {
            // 1. Fase de Tiempo (r = 0 .. runs - 1)
            for (std::size_t r = 0; r < c.runs; ++r) {
                progress("exp1", ds.name, n, r, c.runs);
                const std::uint64_t sd = mix(mix(mix(c.seed, 1), n), r);
                std::mt19937_64 rng(sd);
                const std::vector<Point> base_pts = ds.fn(n, rng);

                for (const auto& algo : algos) {
                    if (skipped.count({ds.name, algo.name})) continue;

                    double batch_ms = 0.0;
                    for (std::size_t k = 0; k < c.batch_k; ++k) {
                        std::vector<Point> copy = base_pts;
                        const auto a = Clock::now();
                        std::vector<Point> res = algo.fn(std::move(copy));
                        const auto b = Clock::now();

                        batch_ms += ms_between(a, b);
                    }

                    const double mean_k_ms = batch_ms / static_cast<double>(c.batch_k);
                    col.add_time(ds.name, algo.name, mean_k_ms);

                    if (mean_k_ms > c.max_ms) {
                        skipped.insert({ds.name, algo.name});
                    }
                }
            }

            // 2. Fase de Memoria (1 sola ejecución aislada sin cronómetro)
            std::mt19937_64 rng_mem(mix(mix(c.seed, 999), n));
            const std::vector<Point> mem_pts = ds.fn(n, rng_mem);

            for (const auto& algo : algos) {
                if (skipped.count({ds.name, algo.name})) continue;

                std::vector<Point> copy = mem_pts;
                MemTracker::start();
                std::vector<Point> res = algo.fn(std::move(copy));
                const std::size_t peak = MemTracker::peak;
                MemTracker::stop();

                col.set_mem(ds.name, algo.name, peak);
            }
        }

        col.flush(w, n, c.runs, &skipped);
    }
    std::cerr << std::endl;

    if (!skipped.empty()) {
        std::cerr << "[exp1] celdas omitidas por superar --max-ms (" << c.max_ms << " ms):\n";
        for (const auto& [ds, algo] : skipped) std::cerr << "         " << ds << " / " << algo << "\n";
    }
}

/**
 * @brief Ejecuta el Experimento 2: actualización dinámica vs. recálculo desde cero con Andrew.
 * @param c Configuración del benchmark.
 * @param w Escritor CSV de resultados.
 */
static void exp2(const Config& c, Writter& w) {
    const auto& datasets = gen::all_datasets();
    const auto& add_gens = add::all_adders();

    // 0. Warm-up global único previo al experimento (ambos caminos: incremental y recompute)
    {
        std::mt19937_64 rng_warm(c.seed);
        const std::vector<Point> warm_pts = gen::uniform_square(4096, rng_warm);
        DynamicHull warm_dh;
        for (const Point& q : warm_pts) warm_dh.insert(q);
        std::vector<Point> res = warm_dh.hull();
        std::vector<Point> res2 = andrew(warm_pts);
    }

    for (std::size_t n = c.lower; n <= c.upper; n *= c.step) {
        Collector col;
        const std::size_t m = recalc_batch_size(c, n);

        for (const auto& ds : datasets) {
            for (const auto& add_gen : add_gens) {
                const std::string label = std::string(ds.name) + "+" + add_gen.name;

                // 1. Fase de Tiempo (r = 0 .. runs - 1)
                for (std::size_t r = 0; r < c.runs; ++r) {
                    progress("exp2", label, n, r, c.runs);
                    const std::uint64_t sd_base = mix(mix(mix(c.seed, 20), n), r);
                    const std::uint64_t sd_mut  = mix(mix(mix(c.seed, 21), n), r);

                    std::mt19937_64 rng_base(sd_base);
                    std::mt19937_64 rng_mut(sd_mut);

                    const std::vector<Point> base_pts = shrink_half(ds.fn(n, rng_base));
                    const std::vector<Point> new_pts  = add_gen.fn(base_pts, m, rng_mut);

                    // Construir hull previo directamente con DynamicHull FUERA del cronómetro
                    DynamicHull initial_hull;
                    for (const Point& q : base_pts) {
                        initial_hull.insert(q);
                    }

                    // Conjunto completo (antiguos + nuevos) para la línea base, FUERA del cronómetro
                    std::vector<Point> all_pts = base_pts;
                    all_pts.insert(all_pts.end(), new_pts.begin(), new_pts.end());

                    double inc_ms = 0.0, rec_ms = 0.0;
                    for (std::size_t k = 0; k < c.batch_k; ++k) {
                        // (a) Incremental: clonar hull FUERA; insertar m puntos + extraer hull DENTRO
                        DynamicHull hull_state = initial_hull;
                        const auto a = Clock::now();
                        for (const Point& q : new_pts) {
                            hull_state.insert(q);
                        }
                        std::vector<Point> updated_hull = hull_state.hull();
                        const auto b = Clock::now();
                        inc_ms += ms_between(a, b);

                        // (b) Línea base: Andrew desde cero sobre los n+m puntos (copia FUERA)
                        std::vector<Point> copy = all_pts;
                        const auto a2 = Clock::now();
                        std::vector<Point> recomputed = andrew(std::move(copy));
                        const auto b2 = Clock::now();
                        rec_ms += ms_between(a2, b2);
                    }

                    col.add_time(label, "dynamic_hull", inc_ms / static_cast<double>(c.batch_k));
                    col.add_time(label, "recompute_andrew", rec_ms / static_cast<double>(c.batch_k));
                }

                // 2. Fase de Memoria (1 ejecución aislada sin cronómetro)
                {
                    std::mt19937_64 rng_mem_base(mix(mix(c.seed, 2000), n));
                    std::mt19937_64 rng_mem_mut(mix(mix(c.seed, 2001), n));

                    const std::vector<Point> mem_base = shrink_half(ds.fn(n, rng_mem_base));
                    const std::vector<Point> mem_mut  = add_gen.fn(mem_base, m, rng_mem_mut);

                    std::vector<Point> mem_all = mem_base;
                    mem_all.insert(mem_all.end(), mem_mut.begin(), mem_mut.end());

                    // (a) pico del hull dinámico (estructura + construcción + m inserciones + extracción)
                    long long changed = 0;
                    MemTracker::start();
                    DynamicHull mem_hull;
                    for (const Point& q : mem_base) mem_hull.insert(q);
                    for (const Point& q : mem_mut)  changed += mem_hull.insert(q) ? 1 : 0;
                    std::vector<Point> mem_recalc = mem_hull.hull();
                    const std::size_t peak_dyn = MemTracker::peak;
                    MemTracker::stop();

                    // (b) pico de recalcular con Andrew sobre n+m puntos (copia de entrada fuera del rastreador)
                    std::vector<Point> copy = mem_all;
                    MemTracker::start();
                    std::vector<Point> mem_rec = andrew(std::move(copy));
                    const std::size_t peak_rec = MemTracker::peak;
                    MemTracker::stop();

                    col.set_mem(label, "dynamic_hull", peak_dyn, changed);
                    col.set_mem(label, "recompute_andrew", peak_rec);
                }
            }
        }

        col.flush(w, n, c.runs);
    }
    std::cerr << std::endl;
}

/**
 * @brief Muestra en `std::cerr` la ayuda de uso y las opciones de línea de comandos.
 * @param prog Nombre del ejecutable (`argv[0]`).
 */
static void print_usage(const char* prog) {
    std::cerr << "Uso: " << prog << " [opciones]\n"
              << "  --exp <exp1|exp2|all>   Experimento a ejecutar (default: all)\n"
              << "  --out <archivo.csv>     Archivo CSV de salida (default: resultados.csv)\n"
              << "  --runs <N>              Repeticiones por tamaño (default: 32)\n"
              << "  --lower <N>             Tamaño inicial n (default: 1024)\n"
              << "  --upper <N>             Tamaño máximo n (default: 1048576)\n"
              << "  --step <K>              Factor geométrico de n (default: 2)\n"
              << "  --batch-k <K>           Ejecuciones internas por repetición (default: 1)\n"
              << "  --mut-ops <M>           Cantidad m de puntos agregados en exp2 (default: auto)\n"
              << "  --max-ms <MS>           Límite de tiempo en ms para omitir en exp1 (default: 10000)\n"
              << "  --seed <S>              Semilla base (default: 1)\n";
}

/**
 * @brief Punto de entrada del programa de benchmarking.
 * @param argc Cantidad de argumentos de línea de comandos.
 * @param argv Vector de argumentos de línea de comandos.
 * @return `EXIT_SUCCESS` si finaliza correctamente, o `EXIT_FAILURE` ante error.
 */
int main(int argc, char** argv) {
    Config c;

    // 1. Validar argumentos de entrada
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next_val = [&](const std::string& flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Falta valor para " << flag << "\n";
                std::exit(EXIT_FAILURE);
            }
            return argv[++i];
        };

        if (arg == "--exp")          c.exp = next_val(arg);
        else if (arg == "--out")     c.out = next_val(arg);
        else if (arg == "--runs")    c.runs = std::stoull(next_val(arg));
        else if (arg == "--lower")   c.lower = std::stoull(next_val(arg));
        else if (arg == "--upper")   c.upper = std::stoull(next_val(arg));
        else if (arg == "--step")    c.step = std::stoull(next_val(arg));
        else if (arg == "--batch-k") c.batch_k = std::stoull(next_val(arg));
        else if (arg == "--mut-ops") c.mut_ops = std::stoull(next_val(arg));
        else if (arg == "--max-ms")  c.max_ms = std::stod(next_val(arg));
        else if (arg == "--seed")    c.seed = std::stoull(next_val(arg));
        else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            std::cerr << "Argumento desconocido: " << arg << "\n";
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    // 2. Validar parámetros
    if (c.runs == 0 || c.lower == 0 || c.upper < c.lower || c.step < 2 || c.batch_k == 0) {
        std::cerr << "Parámetros inválidos (revisa runs, lower, upper, step >= 2, batch_k >= 1).\n";
        return EXIT_FAILURE;
    }
    if (c.exp != "exp1" && c.exp != "exp2" && c.exp != "all") {
        std::cerr << "Experimento inválido: " << c.exp << " (usa exp1, exp2 o all).\n";
        return EXIT_FAILURE;
    }
    if (c.runs < 30) {
        std::cerr << "Aviso: --runs=" << c.runs << " < 30; el enunciado exige al menos 30 repeticiones.\n";
    }

    // 3 y 4. Abrir Writter y correr experimentos seleccionados
    Writter w(c.out);

    if (c.exp == "exp1" || c.exp == "all") {
        exp1(c, w);
    }
    if (c.exp == "exp2" || c.exp == "all") {
        exp2(c, w);
    }

    return EXIT_SUCCESS;
}