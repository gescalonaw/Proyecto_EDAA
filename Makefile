#   make               compila tests, bench y hull_cli
#   make test          corre los tests de correctitud
#   make gui           abre el visualizador de escritorio (python/hull_gui.py)
#   make viz           abre el visualizador interactivo matplotlib (python/visualizer.py)
#   make demo          genera imagenes de ejemplo en DEMO_DIR (default: figuras_demo)
#   make run           ejecuta UN benchmark (exp1, exp2 o all) con los parametros dados
#   make quick         prueba de humo
#   make real          bateria real completa: test, env, real-n1, real-n2, real-h1, real-h2, control
#   make real-n1       exp1 vs n                   -> resultados/resultados_exp1.csv
#   make real-n2       exp2 vs n (m fijo)          -> resultados/resultados_exp2.csv
#   make real-h1       exp1 vs h (n fijo)          -> resultados/resultados_exp1_h.csv
#   make real-h2       exp2 vs h (n fijo, m fijo)  -> resultados/resultados_exp2_h.csv
#   make control       repite n=65536 al final para detectar deriva termica
#   make sweep-m       (opcional) exp2 variando m con n fijo
#   make env           guarda CPU, compilador y RAM en logs/entorno.txt
#   make clean         borra binarios
#   make distclean     borra binarios, resultados y logs
#   make clean-quick   borra solo la prueba de humo (resultados_quick)
#   Parametros run:    EXP LOWER UPPER STEP REPS K MAX_MS SEED FIXED_N MUT_OPS OUT PIN CORE MIN_MB
#   Parametros real:   REAL_LOWER REAL_UPPER REAL_M H_N H_LOWER H_UPPER REAL_MB SWEEP_N M_LIST

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
PYTHON   ?= python3
DEMO_DIR ?= figuras_demo

# bash + pipefail (requiere Linux/WSL)
SHELL        := /bin/bash
.SHELLFLAGS  := -o pipefail -c

# Fijar el benchmark a un solo nucleo (reduce ruido). Si no hay taskset queda vacio.
# Para desactivarlo:  make run PIN=      Para otro nucleo:  make run CORE=4
CORE     ?= 2
PIN      ?= $(if $(shell command -v taskset 2>/dev/null),taskset -c $(CORE),)

BIN      := bin
OUT      ?= resultados
LOGS     ?= logs

# parametros de "make run"
EXP      ?= all
LOWER    ?= 1024
UPPER    ?= 1048576
STEP     ?= 2
REPS     ?= 32
K        ?= 1
MAX_MS   ?= 10000
SEED     ?= 42
FIXED_N  ?=
MUT_OPS  ?=
MIN_MB   ?= 300

# parametros de la bateria real
# exp1/exp2 vs n: n = 1024 a 2^20 (11 puntos)
REAL_LOWER ?= 1024
REAL_UPPER ?= 1048576
# m fijo en exp2 (con el automatico m = n/10 hasta 8192 y 1000 desde 16384)
REAL_M     ?= 1000
# barrido de h: n fijo
H_N        ?= 100000
# h = 4 a 16384 (13 puntos)
H_LOWER    ?= 4
H_UPPER    ?= 16384
# RAM libre minima exigida en la bateria real
REAL_MB    ?= 1000
# sweep-m: n fijo
SWEEP_N    ?= 100000
M_LIST     ?= 10 100 1000 10000 100000

# La bateria real exige mas RAM libre que la prueba de humo
real real-n1 real-n2 real-h1 real-h2 control sweep-m: MIN_MB := $(REAL_MB)

HDR := $(wildcard cpp/*.hpp)

.NOTPARALLEL:
.PHONY: all test gui viz demo run quick real real-n1 real-n2 real-h1 real-h2 control sweep-m env \
        memcheck envcheck clean distclean clean-quick help

all: $(BIN)/tests $(BIN)/bench $(BIN)/hull_cli

help:
	@sed -n '1,20p' Makefile

# Prueba de correctitud 
$(BIN)/tests: cpp/tests.cpp $(HDR)
	mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $<

# bench.cpp usa mem_tracker.hpp: requiere -std=c++14 o superior
$(BIN)/bench: cpp/bench.cpp $(HDR)
	mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BIN)/hull_cli: cpp/hull_cli.cpp $(HDR)
	mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $<

test: $(BIN)/tests
	./$(BIN)/tests

gui: $(BIN)/hull_cli
	$(PYTHON) python/hull_gui.py

viz: $(BIN)/hull_cli
	$(PYTHON) python/visualizer.py

demo: $(BIN)/hull_cli
	$(PYTHON) python/visualizer.py --demo $(DEMO_DIR)

# Verificacion de memoria disponible (esto solo en Linux; si no existe /proc/meminfo se omite)
memcheck:
	@if [ -r /proc/meminfo ]; then \
	avail=$$(awk '/MemAvailable/ {print int($$2/1024)}' /proc/meminfo); \
	if [ "$$avail" -lt $(MIN_MB) ]; then \
	echo "ERROR: solo hay $$avail MB disponibles (se piden >= $(MIN_MB)). Cierra programas o baja UPPER."; exit 1; \
	fi; \
	else echo "memcheck omitido (no hay /proc/meminfo)"; fi

# Avisos de entorno: governor de la CPU, swap en uso y nucleo fijado valido
envcheck:
	@if [ -n "$(PIN)" ] && [ "$(CORE)" -ge "$$(nproc)" ]; then \
	echo "ERROR: CORE=$(CORE) no existe (nproc=$$(nproc)). Usa CORE=<n> o PIN= para no fijar nucleo."; exit 1; fi
	@g=/sys/devices/system/cpu/cpu$(CORE)/cpufreq/scaling_governor; \
	if [ -r "$$g" ] && [ "$$(cat $$g)" != "performance" ]; then \
	echo "AVISO: governor de la CPU $(CORE) = $$(cat $$g). Recomendado: sudo cpupower frequency-set -g performance"; fi
	@if [ -r /proc/meminfo ]; then \
	awk '/SwapTotal/ {t=$$2} /SwapFree/ {f=$$2} END {if (t-f > 0) print "AVISO: hay swap en uso (" int((t-f)/1024) " MB); puede distorsionar los tiempos"}' /proc/meminfo; fi

# Guarda el entorno de ejecucion (CPU, RAM, compilador, governor, swap)
env:
	mkdir -p $(LOGS)
	( echo "== fecha =="; date; \
		echo "== CPU =="; lscpu; \
		echo "== compilador =="; $(CXX) --version | head -n 1; echo "CXXFLAGS = $(CXXFLAGS)"; \
		echo "== kernel =="; uname -a; \
		echo "== memoria =="; free -m; \
		echo "== governor (nucleo: cantidad) =="; cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort | uniq -c; \
		echo "== swap =="; swapon --show ) > $(LOGS)/entorno.txt 2>&1
	@echo "Entorno guardado en $(LOGS)/entorno.txt"

# Ejecucion de UN benchmark:  $(call BENCH, exp, lower, upper, mut-ops (vacio=auto), fixed-n (vacio=no), nombre)
# Mide el tiempo total, muestra el avance en pantalla y guarda el log (incluye la lista de celdas omitidas).
define BENCH
mkdir -p $(OUT) $(LOGS) && \
time $(PIN) ./$(BIN)/bench --exp $(1) --lower $(2) --upper $(3) --step $(STEP) \
	--runs $(REPS) --batch-k $(K) --max-ms $(MAX_MS) --seed $(SEED) \
	$(if $(4),--mut-ops $(4),) $(if $(5),--fixed-n $(5),) \
	--out $(OUT)/$(6).csv 2>&1 | tee $(LOGS)/$(6).log && \
echo "CSV en $(OUT)/$(6).csv  |  log en $(LOGS)/$(6).log"
endef

run: $(BIN)/bench memcheck envcheck
	$(call BENCH,$(EXP),$(LOWER),$(UPPER),$(MUT_OPS),$(FIXED_N),resultados_$(EXP)$(if $(FIXED_N),_h,))

# Prueba de humo
quick:
	$(MAKE) run REPS=3 LOWER=1000 UPPER=10000 STEP=10 MAX_MS=1000 MIN_MB=100 OUT=resultados_quick

# Bateria real
real: test env real-n1 real-n2 real-h1 real-h2 control
	@echo "Bateria real terminada. CSV en $(OUT)/, logs en $(LOGS)/"

real-n1: $(BIN)/bench memcheck envcheck
	$(call BENCH,exp1,$(REAL_LOWER),$(REAL_UPPER),,,resultados_exp1)

real-n2: $(BIN)/bench memcheck envcheck
	$(call BENCH,exp2,$(REAL_LOWER),$(REAL_UPPER),$(REAL_M),,resultados_exp2)

real-h1: $(BIN)/bench memcheck envcheck
	$(call BENCH,exp1,$(H_LOWER),$(H_UPPER),,$(H_N),resultados_exp1_h)

real-h2: $(BIN)/bench memcheck envcheck
	$(call BENCH,exp2,$(H_LOWER),$(H_UPPER),$(REAL_M),$(H_N),resultados_exp2_h)

# Control de deriva termica
control: $(BIN)/bench memcheck envcheck
	$(call BENCH,exp1,65536,65536,,,control_final)

# Opcional: costo de actualizar vs m (n fijo). Muestra cuando conviene mas recalcular que insertar.
sweep-m: $(BIN)/bench memcheck envcheck
	mkdir -p $(OUT) $(LOGS)
	for m in $(M_LIST); do \
		$(PIN) ./$(BIN)/bench --exp exp2 --lower $(SWEEP_N) --upper $(SWEEP_N) --step $(STEP) \
			--runs $(REPS) --batch-k $(K) --seed $(SEED) --mut-ops $$m \
			--out $(OUT)/resultados_exp2_m$$m.csv 2>&1 | tee $(LOGS)/resultados_exp2_m$$m.log || exit 1; \
	done
	@echo "CSV en $(OUT)/resultados_exp2_m*.csv"

# Limpieza
clean:
	rm -rf $(BIN)

clean-quick:
	rm -rf resultados_quick

distclean: clean
	rm -rf resultados resultados_quick logs