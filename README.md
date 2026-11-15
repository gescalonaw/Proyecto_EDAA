# Proyecto Convex Hull 2D — EDAA

Código del proyecto: 4 algoritmos de convex hull en C++, herramienta de
visualización en Python e inserción incremental de puntos.

## Contenido y correlato con lo pedido

| Pedido | Dónde está |
|---|---|
| Algoritmos 2D a comparar | `cpp/hull_algorithms.hpp`: `andrew` (O(n log n)), `jarvis` (O(nh)), `quickhull` (O(n log n) promedio, O(n²) peor), `chan` (O(n log h)). NOSOTROS SOLO DEBEMOS COMPARAR 3  |
| Experimentos (≥ 5 datasets) | `cpp/generators.hpp` (7 datasets) y `cpp/bench.cpp` |
| Herramienta de visualización | `python/visualizer.py` (usa `bin/hull_cli`, que ejecuta el código C++) |
| Inserción de puntos reaprovechando el hull | `cpp/dynamic_hull.hpp` (`DynamicHull`, adaptación de Andrew, O(log h) amortizado) |
| Pruebas de corrección | `cpp/tests.cpp` (validador propio) y `python/validate_with_qhull.py` (validación externa con Qhull) |
| Gráficos y tablas del informe | `python/plot_results.py` |

Convención de salida de todos los algoritmos: vértices en sentido antihorario,
partiendo del punto lexicográficamente menor, sin colineales ni duplicados.
Coordenadas enteras (|x|,|y| ≤ 1e9) → predicados exactos.

## Compilar y probar

```bash
make            # genera bin/tests, bin/bench, bin/hull_cli (g++ con C++17)
make test       # debe imprimir "OK: ... casos"
```

## Implementación de referencia externa (Qhull)

Los algoritmos de `cpp/` son de implementación (basados en los papers citados abajo).
Como implementación conocida y citable se usa **Qhull** (Barber, Dobkin y Huhdanpaa, 1996),
la implementación de referencia de QuickHull, a través de `scipy.spatial.ConvexHull`:

```bash
pip install numpy scipy            # si hace falta
python3 python/validate_with_qhull.py 20000
```

Compara, en los 7 datasets, el hull de Qhull con el de cada algoritmo propio y con la
inserción incremental (todos deben decir `OK`). Qhull no se usa para los tiempos del informe:
corre desde Python y mide otra cosa; su tiempo se imprime solo como referencia.

## Correr los experimentos

```bash
# 1) Comparación estática de algoritmos (≈ 10-20 min con 1e6; usar --sizes más chico para probar)
./bin/bench --sizes 1000,10000,100000,1000000 --reps 5 --out resultados_static.csv

# 2) Inserción incremental vs recalcular desde cero (k=100 inserciones en un hull de n puntos)
./bin/bench --mode incremental --sizes 1000,10000,100000 --k 100 --reps 5 --out resultados_inc.csv

# 3) Gráficos y tablas
python3 python/plot_results.py static      resultados_static.csv figuras/
python3 python/plot_results.py incremental resultados_inc.csv    figuras/
```

Notas para el informe:
- Registrar la máquina (CPU, RAM, compilador y flags: `-O2`).
- Los tiempos son de reloj de pared (`std::chrono::steady_clock`), mediana de `--reps`
  repeticiones; la copia de la entrada queda fuera del cronómetro. Si el curso exige
  `uhr`, cambiar la lambda `time_ms` en `cpp/bench.cpp`.
- `--max-ms` (defecto 10000) omite los tamaños mayores cuando un algoritmo ya pasó ese
  tiempo (Jarvis en `circle` con n = 1e5 ya tarda ~16 s, porque h ≈ n).
- Las filas `dynamic` del modo estático son construir el hull insertando los n puntos
  uno a uno con `DynamicHull`.

## Visualizador

```bash
python3 python/visualizer.py                 # ventana interactiva
python3 python/visualizer.py --demo figuras/ # solo genera imágenes para el informe
```

Requiere `matplotlib`. Clic = insertar punto (verde: el hull cambió, naranja: quedó dentro;
la línea roja punteada es el hull anterior). Teclas: `g` 40 puntos al azar, `o` 20 sobre un
círculo, `1-4` recalcular desde cero con cada algoritmo y compararlo con el incremental,
`i` volver al incremental, `r` reiniciar, `q` salir.

## Notas de implementación (para la sección "Otras consideraciones")

- **Andrew**: ordena y arma las cadenas inferior y superior con una pila (se descartan
  giros no estrictamente a la izquierda).
- **Jarvis**: parte del punto lexicográficamente menor; ante empate de ángulo toma el más lejano.
- **QuickHull**: ante empate de distancia al segmento se toma el punto más avanzado hacia
  el extremo final; sin este desempate aparecían vértices colineales (error encontrado por
  los tests).
- **Chan**: grupos de tamaño m = 2^(2^t), hull de cada grupo con Andrew y gift-wrapping de
  a lo más m pasos. En vez de búsqueda binaria de la tangente (como en el paper) se usa un
  puntero por grupo que solo avanza en sentido antihorario; el costo amortizado por ronda es
  O(n log m + h·n/m + n), así que se mantiene O(n log h). Es una variante del paper:
  declararlo así en el informe.
- **DynamicHull**: cadenas inferior y superior en `std::map` por x. Test de pertenencia
  O(log h); si hay que insertar, se eliminan los vecinos que dejan de ser convexos
  (criterio de la pila de Andrew a ambos lados). Cada vértice se elimina a lo más una vez
  → O(log h) amortizado. Ver comentario de cabecera en `cpp/dynamic_hull.hpp`.
- QuickHull es recursivo; su profundidad puede llegar a O(n) en entradas adversariales.

## Referencias 

- C. B. Barber, D. P. Dobkin, H. Huhdanpaa (1996), *The Quickhull algorithm for convex hulls*, ACM TOMS 22(4); implementación Qhull: http://www.qhull.org
- P. Virtanen et al. (2020), *SciPy 1.0*, Nature Methods 17 (`scipy.spatial.ConvexHull`).
- Patrón de Andrew's monotone chain: Wikibooks, *Algorithm Implementation/Geometry/Convex hull/Monotone chain*.
- A. M. Andrew (1979), *Another efficient algorithm for convex hulls in two dimensions*.
- R. A. Jarvis (1973), *On the identification of the convex hull of a finite set of points in the plane*.
- W. F. Eddy (1977), *A new convex hull algorithm for planar sets*; Barber, Dobkin, Huhdanpaa (1996), *The Quickhull algorithm for convex hulls*.
- T. M. Chan (1996), *Optimal output-sensitive convex hull algorithms in two and three dimensions*.
- de Berg et al., *Computational Geometry: Algorithms and Applications*, cap. 1.
