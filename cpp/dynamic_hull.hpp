// dynamic_hull.hpp
// ADAPTACIÓN PEDIDA EN EL PROYECTO: inserción de puntos reaprovechando el hull ya calculado.
//
// Base: Andrew's monotone chain. Ese algoritmo ya separa el hull en dos cadenas
// monótonas en x: la INFERIOR (giros a la izquierda) y la SUPERIOR (giros a la
// derecha). En lugar de reconstruir las cadenas con una pila sobre el arreglo
// ordenado, se mantienen "vivas" en dos std::map ordenados por x:
//
//     lower: x -> y mínimo de la cadena inferior en ese x
//     upper: x -> y máximo de la cadena superior en ese x
//
// Insertar un punto p = (x,y):
//   1. TEST DE PERTENENCIA, O(log h): se busca en cada mapa el segmento que
//      cubre a x (lower_bound) y se evalúa con cross() si p queda bajo la cadena
//      superior y sobre la inferior. Si es así, p está dentro o sobre el borde y
//      el hull NO cambia (no se toca nada).
//   2. Si está fuera de una cadena, se inserta p en ese mapa y se ELIMINAN los
//      vértices vecinos que dejaron de ser convexos (mismo criterio de pop de la
//      pila de Andrew, pero hacia ambos lados de p). Cada vértice se elimina a lo
//      más una vez en toda la vida de la estructura => O(log h) amortizado.
//
// Costo: O(log h) amortizado por inserción (vs O(n log n) de recalcular desde cero).
// Construir un hull insertando n puntos uno a uno cuesta O(n log h).
//
// Salida: misma convención que los algoritmos estáticos (ver geometry.hpp).
#pragma once
#include "geometry.hpp"

#include <iterator>
#include <map>

class DynamicHull {
public:
    // Inserta p. Retorna true si el hull cambió (p pasó a ser vértice).
    bool insert(const Point& p) {
        const bool inside_upper = below_or_on_upper(p);
        const bool inside_lower = above_or_on_lower(p);
        if (inside_upper && inside_lower) return false;  // dentro/borde: nada que hacer
        if (!inside_upper) insert_upper(p);
        if (!inside_lower) insert_lower(p);
        return true;
    }

    // Vértices en sentido CCW desde el lexicográficamente menor.
    std::vector<Point> hull() const {
        std::vector<Point> out;
        out.reserve(lower_.size() + upper_.size());
        for (const auto& [x, y] : lower_) out.push_back({x, y});  // izquierda -> derecha
        for (auto it = upper_.rbegin(); it != upper_.rend(); ++it) {  // derecha -> izquierda
            const Point q{it->first, it->second};
            if (q != out.back() && q != out.front()) out.push_back(q);
        }
        return out;
    }

    size_t size() const { return hull().size(); }
    bool empty() const { return lower_.empty(); }
    void clear() {
        lower_.clear();
        upper_.clear();
    }

private:
    using Chain = std::map<i64, i64>;
    Chain lower_, upper_;

    static Point pt(Chain::const_iterator it) { return {it->first, it->second}; }

    // ¿p está bajo o sobre la cadena superior? (false si x queda fuera del rango)
    bool below_or_on_upper(const Point& p) const {
        if (upper_.empty()) return false;
        auto it = upper_.lower_bound(p.x);       // primer vértice con x' >= p.x
        if (it == upper_.end()) return false;    // p.x > x máximo
        if (it->first == p.x) return p.y <= it->second;
        if (it == upper_.begin()) return false;  // p.x < x mínimo
        return cross(pt(std::prev(it)), pt(it), p) <= 0;  // a la derecha o sobre el segmento
    }

    // ¿p está sobre o encima de la cadena inferior?
    bool above_or_on_lower(const Point& p) const {
        if (lower_.empty()) return false;
        auto it = lower_.lower_bound(p.x);
        if (it == lower_.end()) return false;
        if (it->first == p.x) return p.y >= it->second;
        if (it == lower_.begin()) return false;
        return cross(pt(std::prev(it)), pt(it), p) >= 0;
    }

    // Cadena superior: debe girar siempre a la DERECHA (cross < 0) de izq. a der.
    void insert_upper(const Point& p) {
        upper_[p.x] = p.y;
        auto it = upper_.find(p.x);
        // limpiar a la izquierda
        while (it != upper_.begin()) {
            auto l1 = std::prev(it);
            if (l1 == upper_.begin()) break;
            auto l2 = std::prev(l1);
            if (cross(pt(l2), pt(l1), p) >= 0) upper_.erase(l1);  // l1 no es vértice estricto
            else break;
        }
        // limpiar a la derecha
        while (true) {
            auto r1 = std::next(it);
            if (r1 == upper_.end()) break;
            auto r2 = std::next(r1);
            if (r2 == upper_.end()) break;
            if (cross(p, pt(r1), pt(r2)) >= 0) upper_.erase(r1);
            else break;
        }
    }

    // Cadena inferior: debe girar siempre a la IZQUIERDA (cross > 0) de izq. a der.
    void insert_lower(const Point& p) {
        lower_[p.x] = p.y;
        auto it = lower_.find(p.x);
        while (it != lower_.begin()) {
            auto l1 = std::prev(it);
            if (l1 == lower_.begin()) break;
            auto l2 = std::prev(l1);
            if (cross(pt(l2), pt(l1), p) <= 0) lower_.erase(l1);
            else break;
        }
        while (true) {
            auto r1 = std::next(it);
            if (r1 == lower_.end()) break;
            auto r2 = std::next(r1);
            if (r2 == lower_.end()) break;
            if (cross(p, pt(r1), pt(r2)) <= 0) lower_.erase(r1);
            else break;
        }
    }
};
