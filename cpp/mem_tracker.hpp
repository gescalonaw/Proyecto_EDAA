#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

/*
 * MemTracker: cuenta bytes pedidos al heap mientras 'active' es true.
 * Mide memoria adicional: lo reservado antes de start() (p. ej. la copia de la
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

/**
 * @brief Rastreador del pico adicional de RSS (VmHWM sobre VmRSS base en Linux).
 */
struct RssTracker {
    static inline long long base_kb = -1;
    static inline bool ok = false;

    static long long read_kb(const char* key) {  // "VmRSS:" / "VmHWM:" en /proc/self/status
        std::FILE* f = std::fopen("/proc/self/status", "r");
        if (!f) return -1;
        char line[256];
        long long v = -1;
        const std::size_t len = std::strlen(key);
        while (std::fgets(line, sizeof line, f)) {
            if (std::strncmp(line, key, len) == 0) {
                std::sscanf(line + len, "%lld", &v);
                break;
            }
        }
        std::fclose(f);
        return v;
    }

    static void start() {
        ok = false;
#if defined(__GLIBC__)
        malloc_trim(0);  // devuelve al SO lo liberado antes
#endif
        std::FILE* f = std::fopen("/proc/self/clear_refs", "w");  // "5" = reiniciar VmHWM
        if (!f) return;
        const bool w = std::fputs("5", f) >= 0;
        const bool c = std::fclose(f) == 0;  // el error de escritura aparece al cerrar
        base_kb = read_kb("VmRSS:");
        ok = w && c && base_kb >= 0;
    }

    static long long peak_bytes() {
        if (!ok) return -1;
        const long long hwm = read_kb("VmHWM:");
        if (hwm < 0) return -1;
        return (hwm > base_kb ? hwm - base_kb : 0) * 1024LL;
    }
};

// GCC 13 emite un falso positivo (-Wmismatched-new-delete) al inlinear estos operadores.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

/**
 * @brief Sobrecarga global de 'operator new' para contabilizar asignaciones en el heap.
 * @param size Cantidad de bytes solicitados.
 * @return Puntero a la memoria reservada.
 * @throws std::bad_alloc Si 'std::malloc' retorna 'nullptr'.
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
 * @brief Sobrecarga global de 'operator new[]' delegada a 'operator new'.
 * @param size Cantidad de bytes solicitados.
 * @return Puntero a la memoria reservada.
 */
void* operator new[](std::size_t size) {
    return ::operator new(size);
}

/**
 * @brief Sobrecarga global de 'operator delete' sin tamaño conocido.
 * @param ptr Puntero a liberar.
 */
void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

/**
 * @brief Sobrecarga global de 'operator delete[]' sin tamaño conocido.
 * @param ptr Puntero a liberar.
 */
void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

/**
 * @brief Sobrecarga global de 'operator delete' con tamaño (C++14+) para descontar memoria.
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
 * @brief Sobrecarga global de 'operator delete[]' con tamaño (C++14+) delegada a 'operator delete'.
 * @param ptr Puntero a liberar.
 * @param size Tamaño en bytes del arreglo liberado.
 */
void operator delete[](void* ptr, std::size_t size) noexcept {
    ::operator delete(ptr, size);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif