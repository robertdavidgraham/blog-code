# This is a vibe coded Makefile. It produces three targets:
#	- bin/fastai - benchmarking a few algorithms
#	- bin/fastip - benhmarking more algorithms
#	- bin/perfip - profile guided optimizations
#
# Makefile: macOS + Linux
# Layout:
#   src/  : sources
#   tmp/  : all intermediates (objs + PGO profiles)
#   bin/  : final binaries
#
# Targets:
#   make fastip   -> normal build
#   make fastai   -> defines FASTAI
#   make perfip   -> PGO build: instrument -> run -> rebuild using profile -> delete profile data
#
# Notes:
# - Includes C++ source: src/parse-ip-cpp.cpp
# - Uses C++17 (needed for <charconv>/std::from_chars)
# - Links with C++ linker (CXX) because a .cpp is present
# - Supports clang and gcc PGO. For clang, requires llvm-profdata in PATH.
# - PGO profile data stored under tmp/pgo and deleted after perfip is produced.

SHELL := /bin/sh

SRC_DIR := src
OBJ_DIR := tmp
BIN_DIR := bin

# Compilers
CC  ?= cc
CXX ?= c++

# Basic flags (tune as desired)
CSTD   ?= -std=c11
CXXSTD ?= -std=c++17
WARN   ?= -Wall -Wextra -Wpedantic
OPT    ?= -O2
DEBUG  ?= -g

CPPFLAGS ?= -I$(SRC_DIR)
CFLAGS   ?= $(CSTD)   $(WARN) $(OPT) $(DEBUG) $(CPPFLAGS)
CXXFLAGS ?= $(CXXSTD) $(WARN) $(OPT) $(DEBUG) $(CPPFLAGS)

LDFLAGS  ?=
LDLIBS   ?=

# Detect clang vs gcc (for PGO flavor)
IS_CLANG := $(shell $(CC) -v 2>&1 | grep -qi clang && echo 1 || echo 0)

# Sources
C_SRCS := \
	$(SRC_DIR)/bench.c \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/parse-ip-dfa.c \
	$(SRC_DIR)/parse-ip-fsm2.c \
	$(SRC_DIR)/parse-ip-swar.c \
	$(SRC_DIR)/parse-ip-ai.c \
	$(SRC_DIR)/parse-ip-fsm.c \
	$(SRC_DIR)/parse-ip-neon.c

CXX_SRCS := \
	$(SRC_DIR)/parse-ip-cpp.cpp

# If you have more headers, add them here for simple rebuilding
HDRS := \
	$(SRC_DIR)/bench.h

# Per-target object dirs (keeps FASTAI/PGO from clobbering fastip objs)
FASTIP_OBJ := $(OBJ_DIR)/fastip
FASTAI_OBJ := $(OBJ_DIR)/fastai
PGO_DIR    := $(OBJ_DIR)/pgo
PGO_INSTR  := $(PGO_DIR)/instr
PGO_USE    := $(PGO_DIR)/use

# Profile data locations (ALL under tmp/)
CLANG_PROFRAW  := $(PGO_DIR)/default.profraw
CLANG_PROFDATA := $(PGO_DIR)/default.profdata
GCC_PROF_DIR   := $(PGO_DIR)/gcc-prof

# Object lists (preserve src subpaths)
FASTIP_C_OBJS   := $(patsubst $(SRC_DIR)/%.c,$(FASTIP_OBJ)/%.o,$(C_SRCS))
FASTIP_CXX_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(FASTIP_OBJ)/%.o,$(CXX_SRCS))
FASTIP_OBJS     := $(FASTIP_C_OBJS) $(FASTIP_CXX_OBJS)

FASTAI_C_OBJS   := $(patsubst $(SRC_DIR)/%.c,$(FASTAI_OBJ)/%.o,$(C_SRCS))
FASTAI_CXX_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(FASTAI_OBJ)/%.o,$(CXX_SRCS))
FASTAI_OBJS     := $(FASTAI_C_OBJS) $(FASTAI_CXX_OBJS)

PGO_IN_C_OBJS   := $(patsubst $(SRC_DIR)/%.c,$(PGO_INSTR)/%.o,$(C_SRCS))
PGO_IN_CXX_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(PGO_INSTR)/%.o,$(CXX_SRCS))
PGO_IN_OBJS     := $(PGO_IN_C_OBJS) $(PGO_IN_CXX_OBJS)

PGO_US_C_OBJS   := $(patsubst $(SRC_DIR)/%.c,$(PGO_USE)/%.o,$(C_SRCS))
PGO_US_CXX_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(PGO_USE)/%.o,$(CXX_SRCS))
PGO_US_OBJS     := $(PGO_US_C_OBJS) $(PGO_US_CXX_OBJS)

# Binaries
FASTIP_BIN    := $(BIN_DIR)/fastip
FASTAI_BIN    := $(BIN_DIR)/fastai
PERFIP_BIN    := $(BIN_DIR)/perfip
PERFIP_INSTR  := $(BIN_DIR)/perfip.instr

# Default
.PHONY: all
all: fastip

# Create output dirs
$(BIN_DIR) $(FASTIP_OBJ) $(FASTAI_OBJ) $(PGO_DIR) $(PGO_INSTR) $(PGO_USE) $(GCC_PROF_DIR):
	@mkdir -p $@

# =========================
# fastip
# =========================
.PHONY: fastip
fastip: $(FASTIP_BIN)

$(FASTIP_BIN): $(FASTIP_OBJS) | $(BIN_DIR)
	# Link with C++ linker because we have C++ objects
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(FASTIP_OBJ)/%.o: $(SRC_DIR)/%.c $(HDRS) | $(FASTIP_OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

$(FASTIP_OBJ)/%.o: $(SRC_DIR)/%.cpp $(HDRS) | $(FASTIP_OBJ)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# =========================
# fastai (adds -DFASTAI)
# =========================
.PHONY: fastai
fastai: $(FASTAI_BIN)

$(FASTAI_BIN): $(FASTAI_OBJS) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(FASTAI_OBJ)/%.o: $(SRC_DIR)/%.c $(HDRS) | $(FASTAI_OBJ)
	$(CC) $(CFLAGS) -DFASTAI -c $< -o $@

$(FASTAI_OBJ)/%.o: $(SRC_DIR)/%.cpp $(HDRS) | $(FASTAI_OBJ)
	$(CXX) $(CXXFLAGS) -DFASTAI -c $< -o $@

# =========================
# perfip (PGO)
# =========================
.PHONY: perfip
perfip: $(PERFIP_BIN)

# The perfip target performs:
#  1) build instrumented binary
#  2) run it to generate profile data
#  3) rebuild optimized binary using profile
#  4) delete profile data + instrumented binary
$(PERFIP_BIN): | $(BIN_DIR) $(PGO_DIR) $(PGO_INSTR) $(PGO_USE) $(GCC_PROF_DIR)
	@$(MAKE) --no-print-directory pgo-instrument
	@$(MAKE) --no-print-directory pgo-run
	@$(MAKE) --no-print-directory pgo-rebuild
	@$(MAKE) --no-print-directory pgo-clean
	@echo "==> PGO build complete: $(PERFIP_BIN)"

.PHONY: pgo-instrument pgo-run pgo-rebuild pgo-clean

pgo-instrument: $(PERFIP_INSTR)

# IMPORTANT: link-time flag is required for clang PGO runtime
$(PERFIP_INSTR): $(PGO_IN_OBJS) | $(BIN_DIR)
ifeq ($(IS_CLANG),1)
	$(CXX) $(LDFLAGS) -fprofile-instr-generate -o $@ $^ $(LDLIBS)
else
	$(CXX) $(LDFLAGS) -fprofile-generate=$(GCC_PROF_DIR) -o $@ $^ $(LDLIBS)
endif

pgo-run:
	@echo "==> Running instrumented binary to generate profile data..."
ifeq ($(IS_CLANG),1)
	@LLVM_PROFILE_FILE="$(CLANG_PROFRAW)" $(PERFIP_INSTR)
	@echo "==> Merging profraw -> profdata (tmp-only)..."
	@xcrun llvm-profdata merge -output="$(CLANG_PROFDATA)" "$(CLANG_PROFRAW)"
else
	@$(PERFIP_INSTR)
endif

pgo-rebuild: $(PERFIP_BIN).pgo_stamp

# IMPORTANT: link-time "use" flag is recommended/required for some toolchains
$(PERFIP_BIN).pgo_stamp: $(PGO_US_OBJS) | $(BIN_DIR)
	@echo "==> Linking PGO-optimized binary..."
ifeq ($(IS_CLANG),1)
	$(CXX) $(LDFLAGS) -fprofile-instr-use="$(CLANG_PROFDATA)" -o $(PERFIP_BIN) $^ $(LDLIBS)
else
	$(CXX) $(LDFLAGS) -fprofile-use=$(GCC_PROF_DIR) -fprofile-correction -o $(PERFIP_BIN) $^ $(LDLIBS)
endif
	@touch $@

# Instrumented objects
$(PGO_INSTR)/%.o: $(SRC_DIR)/%.c $(HDRS) | $(PGO_INSTR) $(GCC_PROF_DIR)
ifeq ($(IS_CLANG),1)
	$(CC) $(CFLAGS) -fprofile-instr-generate -c $< -o $@
else
	$(CC) $(CFLAGS) -fprofile-generate=$(GCC_PROF_DIR) -c $< -o $@
endif

$(PGO_INSTR)/%.o: $(SRC_DIR)/%.cpp $(HDRS) | $(PGO_INSTR) $(GCC_PROF_DIR)
ifeq ($(IS_CLANG),1)
	$(CXX) $(CXXFLAGS) -fprofile-instr-generate -c $< -o $@
else
	$(CXX) $(CXXFLAGS) -fprofile-generate=$(GCC_PROF_DIR) -c $< -o $@
endif

# "Use" objects
$(PGO_USE)/%.o: $(SRC_DIR)/%.c $(HDRS) | $(PGO_USE)
ifeq ($(IS_CLANG),1)
	$(CC) $(CFLAGS) -fprofile-instr-use="$(CLANG_PROFDATA)" -c $< -o $@
else
	$(CC) $(CFLAGS) -fprofile-use=$(GCC_PROF_DIR) -fprofile-correction -c $< -o $@
endif

$(PGO_USE)/%.o: $(SRC_DIR)/%.cpp $(HDRS) | $(PGO_USE)
ifeq ($(IS_CLANG),1)
	$(CXX) $(CXXFLAGS) -fprofile-instr-use="$(CLANG_PROFDATA)" -c $< -o $@
else
	$(CXX) $(CXXFLAGS) -fprofile-use=$(GCC_PROF_DIR) -fprofile-correction -c $< -o $@
endif

# Remove ONLY profile artifacts and instrumented binary after perfip is produced.
# (Leaves normal build objs and final bins intact.)
pgo-clean:
	@rm -f  "$(CLANG_PROFRAW)" "$(CLANG_PROFDATA)"
	@rm -rf "$(GCC_PROF_DIR)"
	@rm -f  "$(PERFIP_INSTR)" "$(PERFIP_BIN).pgo_stamp"

# =========================
# Utilities
# =========================
.PHONY: clean
clean:
	rm -rf "$(OBJ_DIR)" "$(BIN_DIR)"

.PHONY: run-fastip run-fastai run-perfip
run-fastip: fastip
	@$(FASTIP_BIN)

run-fastai: fastai
	@$(FASTAI_BIN)

run-perfip: perfip
	@$(PERFIP_BIN)
