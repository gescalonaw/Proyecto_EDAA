#   make            compila tests, bench, hull_cli y experimentos
#   make test       corre los tests de correctitud
#   make run        corrida completa de experimentos
#   make quick      prueba de humo
#   make clean      borra binarios
#   make distclean  borra binarios y resultados
#   Parametros: RUNS LOWER UPPER STEP BATCH_K MAX_MS SEED EXP OUT PIN

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
PIN      ?=

BIN     := bin
OUT     ?= resultados

EXP     ?= all
RUNS    ?= 32
LOWER   ?= 1024
UPPER   ?= 1048576
STEP    ?= 2
BATCH_K ?= 1
MAX_MS  ?= 10000
SEED    ?= 1
MIN_MB  ?= 300

HDR := $(wildcard cpp/*.hpp)

.NOTPARALLEL:
.PHONY: all test run quick memcheck clean distclean help

all: $(BIN)/tests $(BIN)/bench $(BIN)/hull_cli $(BIN)/experimentos

help:
	@sed -n '1,8p' Makefile

# Compilacion
$(BIN)/tests: cpp/tests.cpp $(HDR)
	mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BIN)/bench: cpp/bench.cpp $(HDR)
	mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BIN)/hull_cli: cpp/hull_cli.cpp $(HDR)
	mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $<

# experimentos.cpp reemplaza operator new/delete para medir memoria: requiere -std=c++14 o superior
$(BIN)/experimentos: cpp/experimentos.cpp $(HDR)
	mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Tests de correctitud
test: $(BIN)/tests
	./$(BIN)/tests

# Verificacion de memoria disponible (solo Linux; si no existe /proc/meminfo se omite)
memcheck:
	@if [ -r /proc/meminfo ]; then \
	avail=$$(awk '/MemAvailable/ {print int($$2/1024)}' /proc/meminfo); \
	if [ "$$avail" -lt $(MIN_MB) ]; then \
	echo "ERROR: solo hay $$avail MB disponibles (se piden >= $(MIN_MB)). Cierra programas o baja UPPER."; exit 1; \
	fi; \
	else echo "memcheck omitido (no hay /proc/meminfo)"; fi

# Ejecucion completa
run: $(BIN)/experimentos memcheck
	mkdir -p $(OUT)
	$(PIN) ./$(BIN)/experimentos --exp $(EXP) --out $(OUT)/resultados.csv \
		--runs $(RUNS) --lower $(LOWER) --upper $(UPPER) --step $(STEP) \
		--batch-k $(BATCH_K) --max-ms $(MAX_MS) --seed $(SEED)
	@echo "CSV en $(OUT)/resultados.csv"

# Prueba de humo (menos de 30 repeticiones: solo para comprobar que todo corre)
quick:
	$(MAKE) run RUNS=3 UPPER=16384 MAX_MS=1000 MIN_MB=100 OUT=resultados_quick

# Limpieza
clean:
	rm -rf $(BIN)

distclean: clean
	rm -rf resultados resultados_quick