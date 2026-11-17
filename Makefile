# The suite's Makefile skeleton, taken from lang-wasm's (which came from
# color's, and that from model's). SUITE, PROJECT, the dependency block and
# the object discovery are what changed. lang-wasm's spec corpus, JIT and
# debugger gates are not this library's yet; the benchmark harness is (bench/,
# `make bench`, and the check-bench gate in `make test`).

SUITE := ghoti.io
PROJECT := lang-ecmascript

BUILD ?= release
MAJOR_VERSION := 0
MINOR_VERSION := 0.0
VERSION_MINOR_ONLY := $(word 1,$(subst ., ,$(MINOR_VERSION)))
VERSION_PATCH_ONLY := $(or $(word 2,$(subst ., ,$(MINOR_VERSION))),0)
VERSION := $(MAJOR_VERSION).$(MINOR_VERSION)

# Names this build everywhere: the .pc file, the install directory, the soname
# and the symbol token. Defaults to the major version.
BRANCH ?= -$(MAJOR_VERSION)

ifeq ($(BRANCH),-$(MAJOR_VERSION))
VERSION_STRING := $(VERSION)
else
VERSION_STRING := $(VERSION)$(BRANCH)
endif

# override: BRANCH may have come from the command line, which otherwise beats
# a plain assignment and would give a debug build the release symbol token.
ifeq ($(BUILD),debug)
    override BRANCH := $(BRANCH)-debug
    override VERSION_STRING := $(VERSION_STRING)-debug
endif

ifeq ($(BUILD),debug)
OPT_CFLAGS := -O0
else
OPT_CFLAGS := -O2
endif

BASE_NAME := lib$(SUITE)-$(PROJECT)$(BRANCH).so
LIBVER_SYMBOL := $(shell echo "ghotiio_$(PROJECT)$(BRANCH)" | sed 's/[.-]/_/g')
BASE_NAME_PREFIX := lib$(SUITE)-$(PROJECT)$(BRANCH)
SO_NAME := $(BASE_NAME).$(MAJOR_VERSION)
STATIC_TARGET := $(BASE_NAME_PREFIX).a

PKG_CONFIG_PATH_ENV := $(PKG_CONFIG_PATH)

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
	OS_NAME := Linux
	LIB_EXTENSION := so
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,-soname,$(SO_NAME)
	TARGET := $(SO_NAME).$(MINOR_VERSION)
	EXE_EXTENSION :=
	PC_INSTALL_PATH := /usr/local/share/pkgconfig
	INCLUDE_INSTALL_PATH := /usr/local/include
	LIB_INSTALL_PATH := /usr/local/lib
	PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
	override BUILD := linux/$(BUILD)

else ifeq ($(UNAME_S), Darwin)
	OS_NAME := Mac
	LIB_EXTENSION := dylib
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,-install_name,$(BASE_NAME_PREFIX).dylib
	TARGET := $(BASE_NAME_PREFIX).dylib
	EXE_EXTENSION :=
	PC_INSTALL_PATH := /usr/local/share/pkgconfig
	INCLUDE_INSTALL_PATH := /usr/local/include
	LIB_INSTALL_PATH := /usr/local/lib
	PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
	override BUILD := mac/$(BUILD)

# TODO(windows): the Windows branches in this file were adapted from
# lang-wasm's and have never been run here, nor has GLECMA_API's
# dllexport/dllimport switching.
else ifeq ($(findstring MINGW32_NT,$(UNAME_S)),MINGW32_NT)
	OS_NAME := Windows
	LIB_EXTENSION := dll
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG = -Wl,--out-implib,$(APP_DIR)/$(BASE_NAME_PREFIX).dll.a
	TARGET := $(BASE_NAME_PREFIX).dll
	EXE_EXTENSION := .exe
	PC_INSTALL_PATH := /mingw32/lib/pkgconfig
	INCLUDE_INSTALL_PATH := /mingw32/include
	LIB_INSTALL_PATH := /mingw32/lib
	BIN_INSTALL_PATH := /mingw32/bin
	PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
	PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
	override BUILD := win32/$(BUILD)

else ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)
	OS_NAME := Windows
	LIB_EXTENSION := dll
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG = -Wl,--out-implib,$(APP_DIR)/$(BASE_NAME_PREFIX).dll.a
	TARGET := $(BASE_NAME_PREFIX).dll
	EXE_EXTENSION := .exe
	PC_INSTALL_PATH := /mingw64/lib/pkgconfig
	INCLUDE_INSTALL_PATH := /mingw64/include
	LIB_INSTALL_PATH := /mingw64/lib
	BIN_INSTALL_PATH := /mingw64/bin
	PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
	PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
	override BUILD := win64/$(BUILD)

else
    $(error Unsupported OS: $(UNAME_S))
endif

ifdef PREFIX
INCLUDE_INSTALL_PATH := $(PREFIX)/include
LIB_INSTALL_PATH := $(PREFIX)/lib
BIN_INSTALL_PATH := $(PREFIX)/bin
PC_INSTALL_PATH := $(PREFIX)/share/pkgconfig
ifeq ($(OS_NAME), Windows)
PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
else
PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
endif
LDCONF_INSTALL_PATH :=
endif

PKG_CONFIG_LOOKUP_PATH := $(if $(PKG_CONFIG_PATH_ENV),$(PKG_CONFIG_PATH_ENV):)$(PC_INSTALL_PATH)

# -ffp-contract=off everywhere: a Number is an IEEE double whose bits the
# interpreter and compiled code must agree on, and a fused multiply-add rounds
# once where the C the interpreter is written in rounds twice.
CXX := g++
CXXFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wno-error=unused-function -Wfatal-errors -std=c++20 -O1 -g -ffp-contract=off $(EXTRA_CXXFLAGS)
CC := cc
CFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wfloat-conversion -fstrict-aliasing -Wstrict-aliasing=1 -Wno-error=unused-function -Wfatal-errors -std=c17 $(OPT_CFLAGS) -g -ffp-contract=off $(EXTRA_CFLAGS)
ifeq ($(OS_NAME), Windows)
CFLAGS += -DGLECMA_STATIC
CXXFLAGS += -DGLECMA_STATIC
endif
LIB_CFLAGS := $(CFLAGS) -fvisibility=hidden -DGLECMA_BUILD $(EXTRA_CFLAGS)
LDFLAGS := -L /usr/lib -lstdc++ -lm $(EXTRA_LDFLAGS)
ifdef PREFIX
LDFLAGS += -Wl,-rpath,$(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Windows)
export PATH := $(BIN_INSTALL_PATH):$(PATH)
endif
endif

BUILD_DIR := ./build/$(BUILD)
OBJ_DIR := $(BUILD_DIR)/objects
FLAGS_STAMP := $(OBJ_DIR)/.flags
GEN_DIR := $(BUILD_DIR)/generated
APP_DIR := $(BUILD_DIR)/apps

ifeq ($(UNAME_S), Linux)
	LIB_CFLAGS += -fPIC
endif

INCLUDE := -I include/ -I $(GEN_DIR)/ -I src/

DEPLESS_GOALS := docs docs-pdf clean fuzz-clean cloc help
ifeq ($(filter-out $(DEPLESS_GOALS),$(or $(MAKECMDGOALS),all)),)
SKIP_DEP_CHECK := 1
endif

####################################################################
# Dependencies, through pkg-config only
####################################################################

# Dependencies stay on their current major while this library's branch may
# move on its own. The debug suffix carries over, and each can be overridden.
DEPENDENCY_RELEASE_BRANCH := $(patsubst %-debug,%,$(BRANCH))
DEPENDENCY_BUILD_SUFFIX := $(if $(filter %-debug,$(BRANCH)),-debug,)
CUTIL_BRANCH ?= $(if $(filter -dev,$(DEPENDENCY_RELEASE_BRANCH)),-dev,-0)$(DEPENDENCY_BUILD_SUFFIX)
RCORE_BRANCH ?= $(if $(filter -dev,$(DEPENDENCY_RELEASE_BRANCH)),-dev,-0)$(DEPENDENCY_BUILD_SUFFIX)
CUTIL_PC ?= ghoti.io-cutil$(CUTIL_BRANCH)
RCORE_PC ?= ghoti.io-runtime-core$(RCORE_BRANCH)

CUTIL_CFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --cflags $(CUTIL_PC) 2>/dev/null)
CUTIL_LIBS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs $(CUTIL_PC) 2>/dev/null)
CUTIL_LIBDIR := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs-only-L $(CUTIL_PC) 2>/dev/null | awk '{print $$1}' | sed 's/^-L//')
ifeq ($(strip $(CUTIL_CFLAGS)),)
ifndef SKIP_DEP_CHECK
$(error $(CUTIL_PC) was not found by pkg-config. Build and install the suite's libraries into a prefix (the suite repository's install.sh does this in dependency order), then pass the same PREFIX here, or point PKG_CONFIG_PATH at the directory holding its .pc file. There is deliberately no sibling-checkout fallback.)
endif
endif
INCLUDE += $(CUTIL_CFLAGS)

RCORE_CFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --cflags $(RCORE_PC) 2>/dev/null)
RCORE_LIBS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs $(RCORE_PC) 2>/dev/null)
RCORE_EXISTS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --exists $(RCORE_PC) 2>/dev/null && echo yes)
RCORE_LIBDIR := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs-only-L $(RCORE_PC) 2>/dev/null | awk '{print $$1}' | sed 's/^-L//')
ifneq ($(RCORE_EXISTS),yes)
ifndef SKIP_DEP_CHECK
$(error $(RCORE_PC) was not found by pkg-config. Install runtime-core into the same PREFIX and set PKG_CONFIG_PATH. There is deliberately no sibling-checkout fallback.)
endif
endif
INCLUDE += $(RCORE_CFLAGS)

SOURCES := $(shell find src -type f -name '*.c')
LIBOBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SOURCES))

TESTFLAGS := `PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs --cflags gtest`

# fuzz-replay joins these when the first parser lands with its harness and
# seed corpus; until then there is nothing to replay.
TEST_GATES ?= check-symbols check-aliasing check-stamps check-fp-contract check-labels check-edges check-bench check-gtest-full

VALGRIND_FLAGS := --leak-check=full --show-leak-kinds=definite,indirect,possible --track-origins=yes --error-exitcode=1 --suppressions=tests/valgrind.supp

ECMALIBRARY := -Wl,--whole-archive $(APP_DIR)/$(STATIC_TARGET) -Wl,--no-whole-archive $(RCORE_LIBS) $(CUTIL_LIBS)

ifeq ($(OS_NAME), Windows)
TEST_LDFLAGS := -Wl,--wrap=__mingw_fprintf
else
TEST_LDFLAGS :=
endif

TEST_PAIRS := $(shell find tests -type f -name 'test_*.cpp' 2>/dev/null | sort | while read f; do \
	echo "$$f|$$(basename "$$f" .cpp | sed 's/test_/test/; s/^test\([a-z]\)/test\U\1/')"; done)
TEST_SOURCES := $(foreach pair,$(TEST_PAIRS),$(word 1,$(subst |, ,$(pair))))
TEST_NAMES := $(foreach pair,$(TEST_PAIRS),$(word 2,$(subst |, ,$(pair))))
TEST_EXECUTABLES := $(addprefix $(APP_DIR)/,$(addsuffix $(EXE_EXTENSION),$(TEST_NAMES)))

EXAMPLE_SOURCES := $(sort $(wildcard examples/*.c))
EXAMPLE_EXECUTABLES := $(patsubst examples/%.c,$(APP_DIR)/examples/%$(EXE_EXTENSION),$(EXAMPLE_SOURCES))

TEST_LD_PATH := $(APP_DIR):$(RCORE_LIBDIR):$(CUTIL_LIBDIR):$(LIB_INSTALL_PATH)/$(SUITE)

all: $(APP_DIR)/$(TARGET) $(APP_DIR)/$(STATIC_TARGET) ## Build shared + static libraries

TEST_DEPFILES := $(foreach pair,$(TEST_PAIRS),$(OBJ_DIR)/tests/$(basename $(notdir $(word 1,$(subst |, ,$(pair))))).d)
DEPFILES := $(LIBOBJECTS:.o=.d) $(TEST_DEPFILES)
-include $(DEPFILES)

LIBVER_GEN := $(GEN_DIR)/ghoti.io/$(PROJECT)/libver_gen.h

.PHONY: force-libver
force-libver:

# Regenerated on every build and rewritten only on a change, so a different
# MAJOR_VERSION or BRANCH takes effect without touching the Makefile.
$(LIBVER_GEN): force-libver
	@if [ -z "$(LIBVER_SYMBOL)" ]; then \
		printf "### LIBVER_SYMBOL is empty ###\n" >&2; \
		printf "Every exported symbol would lose its version namespace.\n" >&2; \
		exit 1; \
	fi
	@mkdir -p $(@D)
	@printf '%s\n' \
		'// Generated by the Makefile from BRANCH and the version numbers. Do not edit.' \
		'#ifndef GHOTI_IO_GLECMA_LIBVER_GEN_H' \
		'#define GHOTI_IO_GLECMA_LIBVER_GEN_H' \
		'' \
		'/** The symbol namespace for this build, from the Makefile'"'"'s BRANCH. */' \
		'#define GHOTIIO_LANG_ECMASCRIPT_NAME $(LIBVER_SYMBOL)' \
		'' \
		'/** Human-readable version of this build. */' \
		'#define GHOTIIO_LANG_ECMASCRIPT_VERSION "$(VERSION_STRING)"' \
		'' \
		'/** The same version as three integers. */' \
		'#define GHOTIIO_LANG_ECMASCRIPT_VERSION_MAJOR $(MAJOR_VERSION)' \
		'#define GHOTIIO_LANG_ECMASCRIPT_VERSION_MINOR $(VERSION_MINOR_ONLY)' \
		'#define GHOTIIO_LANG_ECMASCRIPT_VERSION_PATCH $(VERSION_PATCH_ONLY)' \
		'' \
		'#endif // GHOTI_IO_GLECMA_LIBVER_GEN_H' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(OBJ_DIR)/%.o: src/%.c $(FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(LIB_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(APP_DIR)/$(TARGET): $(LIBOBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -shared -o $@ $^ $(LDFLAGS) $(RCORE_LIBS) $(CUTIL_LIBS) $(OS_SPECIFIC_LIBRARY_NAME_FLAG)
ifeq ($(OS_NAME), Linux)
	@ln -f -s $(TARGET) $(APP_DIR)/$(SO_NAME)
	@ln -f -s $(SO_NAME) $(APP_DIR)/$(BASE_NAME)
endif

$(APP_DIR)/$(STATIC_TARGET): $(LIBOBJECTS)
	@mkdir -p $(@D)
	@rm -f $@
	ar rcs $@ $^

$(OBJ_DIR)/tests/%.o: tests/unit/%.cpp $(FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DGLECMA_TEST_BUILD $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

# The archive is what a test links, so it is a normal prerequisite; the
# shared library is order-only, because nothing here links it.
define test-executable-rule
TEST_OBJ_$1 := $(OBJ_DIR)/tests/$(basename $(notdir $1)).o

$(APP_DIR)/$2$(EXE_EXTENSION): $$(TEST_OBJ_$1) $(APP_DIR)/$(STATIC_TARGET) | $(APP_DIR)/$(TARGET)
	@mkdir -p $$(@D)
	$(CXX) $(CXXFLAGS) -o $$@ $$(TEST_OBJ_$1) $(LDFLAGS) $(TEST_LDFLAGS) $(ECMALIBRARY) $(TESTFLAGS)
endef

$(foreach pair,$(TEST_PAIRS),\
	$(eval $(call test-executable-rule,$(word 1,$(subst |, ,$(pair))),$(word 2,$(subst |, ,$(pair))))))

$(APP_DIR)/examples/%$(EXE_EXTENSION): examples/%.c $(APP_DIR)/$(STATIC_TARGET) $(FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(LDFLAGS) $(ECMALIBRARY)

.PHONY: clean cloc docs docs-pdf coverage check-symbols check-stamps check-aliasing check-labels check-edges
.PHONY: check-fp-contract check-gtest-full check-install examples bench check-bench
.PHONY: all install test test-quiet test-asan test-tsan test-valgrind uninstall
.PHONY: fuzz fuzz-clean help

check-aliasing: ## Fail if -Wstrict-aliasing is not armed at level 1
	@mkdir -p $(BUILD_DIR)
	@printf 'int glecma_alias_probe(float * f);\nint glecma_alias_probe(float * f) { int * i = (int *)f; *i = 7; return *i; }\n' > $(BUILD_DIR)/alias_probe.c
	@printf 'int glecma_alias_clean(int * i);\nint glecma_alias_clean(int * i) { *i = 7; return *i; }\n' > $(BUILD_DIR)/alias_clean.c
	@probe=$$($(CC) $(CFLAGS) -Wno-error -c $(BUILD_DIR)/alias_probe.c -o $(BUILD_DIR)/alias_probe.o 2>&1); \
	ctl=$$($(CC) $(CFLAGS) -Wno-error -c $(BUILD_DIR)/alias_clean.c -o $(BUILD_DIR)/alias_clean.o 2>&1); ctlrc=$$?; \
	if [ $$ctlrc -ne 0 ]; then \
		printf 'check-aliasing: the control file did not compile, so this gate is measuring nothing:\n%s\n' "$$ctl" >&2; \
		exit 1; \
	fi; \
	if printf '%s' "$$ctl" | grep -q 'strict-aliasing'; then \
		printf 'check-aliasing: the control file drew a strict-aliasing diagnostic, so the probe proves nothing:\n%s\n' "$$ctl" >&2; \
		exit 1; \
	fi; \
	if printf '%s' "$$probe" | grep -q 'strict-aliasing'; then \
		printf 'check-aliasing: the planted violation is reported\n'; \
		exit 0; \
	fi; \
	qout=$$($(CC) -Q --help=warnings $(CFLAGS) 2>/dev/null); qrc=$$?; \
	level=$$(printf '%s' "$$qout" | awk '/-Wstrict-aliasing=</{print $$2}'); \
	if [ $$qrc -ne 0 ] || [ -z "$$level" ]; then \
		printf 'check-aliasing: no diagnostic, and %s reports no -Wstrict-aliasing level. That is a compiler which accepts the option and implements nothing.\n' "$(CC)" >&2; \
	elif [ "$$level" = 1 ]; then \
		printf 'check-aliasing: level 1 is set and the planted store still drew no diagnostic.\n' >&2; \
	else \
		printf 'check-aliasing: CFLAGS resolves to -Wstrict-aliasing=%s; only level 1 reports this probe.\n' "$$level" >&2; \
	fi; \
	exit 1

check-stamps: ## Fail if a compile rule names no flags stamp, or a stamp omits a variable
	@python3 tools/check-stamps.py

check-fp-contract: ## Fail if any guest-float compile flags permit contraction
	@tools/check-fp-contract.sh

check-symbols: $(APP_DIR)/$(TARGET) ## Fail if any exported symbol lacks the version namespace
	@tools/check-symbols.sh $(APP_DIR)/$(TARGET) $(LIBVER_SYMBOL) .
ifeq ($(OS_NAME), Windows)
	@tools/check-symbols-pe-controls.sh $(LIBVER_SYMBOL) .
endif

check-labels: ## Fail if a public header has no @stability free label
	@tools/check-labels.sh .

check-edges: $(APP_DIR)/$(TARGET) ## Fail on a dependency outside cutil and runtime-core
	@tools/check-edges.sh --includes .
	@tools/check-edges.sh --links $(APP_DIR)

check-gtest-full: ## Prove full-suite selection and zero-test rejection
	@tools/run-gtest-full.sh --selftest

test: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES) $(TEST_GATES) ## Build and run the tests
	@for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		printf '\n### Running %s ###\n\n' "$$test_name"; \
		LD_LIBRARY_PATH="$(TEST_LD_PATH)" tools/run-gtest-full.sh $$test_exe || exit 1; \
	done

test-quiet: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES) ## Run tests, one line per suite
	@for test_exe in $(TEST_EXECUTABLES); do \
		output=$$(LD_LIBRARY_PATH="$(TEST_LD_PATH)" tools/run-gtest-full.sh $$test_exe 2>&1); \
		rc=$$?; \
		if [ $$rc -ne 0 ]; then printf '%s\n' "$$output"; exit $$rc; fi; \
		summary=$$(printf '%s\n' "$$output" | grep '^run-gtest-full: completed ' | tail -1); \
		if [ -z "$$summary" ]; then printf '%s\n' "$$output"; exit 2; fi; \
		printf '%s: %s\n' "$$(basename $$test_exe $(EXE_EXTENSION))" "$$summary"; \
	done

test-valgrind: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES) ## Run the tests under Valgrind
ifeq ($(OS_NAME), Linux)
	@for test_exe in $(TEST_EXECUTABLES); do \
		printf '\n### Valgrind %s ###\n\n' "$$(basename $$test_exe)"; \
		LD_LIBRARY_PATH="$(TEST_LD_PATH)" tools/run-gtest-full.sh valgrind $(VALGRIND_FLAGS) $$test_exe || exit 1; \
	done
else
	@printf 'Valgrind is only available on Linux\n' >&2; exit 1
endif

examples: $(EXAMPLE_EXECUTABLES) ## Build and run every example
	@for example in $(EXAMPLE_EXECUTABLES); do \
		printf '\n### %s ###\n\n' "$$(basename $$example)"; \
		LD_LIBRARY_PATH="$(TEST_LD_PATH)" $$example || exit 1; \
	done

####################################################################
# Benchmarks (AD-26)
####################################################################

BENCH_SOURCES := $(shell find bench -type f -name '*.c' 2>/dev/null | sort)
BENCH_EXECUTABLES := $(patsubst bench/%.c,$(APP_DIR)/bench/%$(EXE_EXTENSION),$(BENCH_SOURCES))
-include $(patsubst bench/%.c,$(APP_DIR)/bench/%.d,$(BENCH_SOURCES))

$(APP_DIR)/bench/%$(EXE_EXTENSION): bench/%.c $(APP_DIR)/$(STATIC_TARGET) $(FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -MMD -MP -MF $(@D)/$(*F).d -o $@ $< $(LDFLAGS) $(ECMALIBRARY)

bench: $(BENCH_EXECUTABLES) ## Run the benchmark harness (prints a calibration result first)
	@if [ -z "$(strip $(BENCH_EXECUTABLES))" ]; then \
		printf 'bench: no benchmark sources under bench/, so this measures nothing\n' >&2; exit 1; \
	fi
	@for b in $(BENCH_EXECUTABLES); do \
		LD_LIBRARY_PATH="$(TEST_LD_PATH)" $$b || exit 1; \
	done

check-bench: $(BENCH_EXECUTABLES) ## Fail if bench/ is empty; build and smoke-run the harness
	@if [ -z "$(strip $(BENCH_EXECUTABLES))" ]; then \
		printf 'check-bench: no benchmark harness under bench/ (AD-26 requires one)\n' >&2; exit 1; \
	fi
	@for b in $(BENCH_EXECUTABLES); do \
		printf '\n### Benchmark smoke %s ###\n\n' "$$(basename $$b $(EXE_EXTENSION))"; \
		LD_LIBRARY_PATH="$(TEST_LD_PATH)" $$b --smoke || exit 1; \
	done

####################################################################
# ASan + UBSan, in their own tree
####################################################################

UBSAN_CHECKS := undefined,float-cast-overflow
ASAN_UBSAN_FLAGS := -fsanitize=address,$(UBSAN_CHECKS) -fno-sanitize-recover=$(UBSAN_CHECKS) -fno-omit-frame-pointer -g -O1
COV_BUILD_DIR := ./build/$(BUILD)-cov
ASAN_BUILD_DIR := ./build/$(BUILD)-asan
ASAN_OBJ_DIR := $(ASAN_BUILD_DIR)/objects
ASAN_FLAGS_STAMP := $(ASAN_OBJ_DIR)/.flags
ASAN_APP_DIR := $(ASAN_BUILD_DIR)/apps
ASAN_LIBOBJECTS := $(patsubst src/%.c,$(ASAN_OBJ_DIR)/%.o,$(SOURCES))
ASAN_DEPFILES := $(ASAN_LIBOBJECTS:.o=.d) \
    $(foreach pair,$(TEST_PAIRS),$(ASAN_OBJ_DIR)/tests/$(basename $(notdir $(word 1,$(subst |, ,$(pair))))).d)
-include $(ASAN_DEPFILES)
ASAN_ARCHIVE := $(ASAN_APP_DIR)/$(STATIC_TARGET)
ASAN_ECMALIBRARY := -Wl,--whole-archive $(ASAN_ARCHIVE) -Wl,--no-whole-archive $(RCORE_LIBS) $(CUTIL_LIBS)
ASAN_CFLAGS := $(CFLAGS) $(ASAN_UBSAN_FLAGS) -DGLECMA_BUILD
ASAN_CXXFLAGS := $(CXXFLAGS) $(ASAN_UBSAN_FLAGS)
ASAN_LDFLAGS := $(LDFLAGS) $(ASAN_UBSAN_FLAGS)
ifeq ($(UNAME_S), Linux)
	ASAN_CFLAGS += -fPIC
endif

$(ASAN_OBJ_DIR)/%.o: src/%.c $(ASAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(ASAN_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(ASAN_ARCHIVE): $(ASAN_LIBOBJECTS)
	@mkdir -p $(@D)
	@rm -f $@
	ar rcs $@ $^

$(ASAN_OBJ_DIR)/tests/%.o: tests/unit/%.cpp $(ASAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) -DGLECMA_TEST_BUILD $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

define asan-test-executable-rule
ASAN_TEST_OBJ_$1 := $(ASAN_OBJ_DIR)/tests/$(basename $(notdir $1)).o

$(ASAN_APP_DIR)/$2$(EXE_EXTENSION): $$(ASAN_TEST_OBJ_$1) $(ASAN_ARCHIVE)
	@mkdir -p $$(@D)
	$(CXX) $(ASAN_CXXFLAGS) -o $$@ $$(ASAN_TEST_OBJ_$1) $(ASAN_LDFLAGS) $(TEST_LDFLAGS) $(ASAN_ECMALIBRARY) $(TESTFLAGS)
endef

$(foreach pair,$(TEST_PAIRS),\
	$(eval $(call asan-test-executable-rule,$(word 1,$(subst |, ,$(pair))),$(word 2,$(subst |, ,$(pair))))))

ASAN_TEST_EXECUTABLES := $(addprefix $(ASAN_APP_DIR)/,$(addsuffix $(EXE_EXTENSION),$(TEST_NAMES)))
ASAN_RUNTIME := $(shell $(CXX) -print-file-name=libasan.so 2>/dev/null)
# The runtime is preloaded into the test process, never into the wrapper
# script's shell, whose own allocations LeakSanitizer would then report.
ifneq ($(findstring clang,$(shell $(CXX) --version 2>/dev/null)),)
ASAN_PRELOAD = GTEST_FULL_TEST_LD_PRELOAD= LD_PRELOAD=
else
ASAN_PRELOAD = GTEST_FULL_TEST_LD_PRELOAD="$(ASAN_RUNTIME)$${LD_PRELOAD:+:$$LD_PRELOAD}" LD_PRELOAD=
endif

test-asan: $(ASAN_TEST_EXECUTABLES) ## Build with ASan+UBSan and run the tests
	@for test_exe in $(ASAN_TEST_EXECUTABLES); do \
		printf '\n### ASan+UBSan %s ###\n\n' "$$(basename $$test_exe)"; \
		$(ASAN_PRELOAD) \
		LD_LIBRARY_PATH="$(ASAN_APP_DIR):$(RCORE_LIBDIR):$(CUTIL_LIBDIR):$(LIB_INSTALL_PATH)/$(SUITE)" \
			tools/run-gtest-full.sh $$test_exe || exit 1; \
	done
	@printf '\nASan+UBSan suite clean.\n'

####################################################################
# ThreadSanitizer, in its own tree
####################################################################

TSAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer -g -O1
TSAN_BUILD_DIR := ./build/$(BUILD)-tsan
TSAN_OBJ_DIR := $(TSAN_BUILD_DIR)/objects
TSAN_FLAGS_STAMP := $(TSAN_OBJ_DIR)/.flags
TSAN_APP_DIR := $(TSAN_BUILD_DIR)/apps
TSAN_LIBOBJECTS := $(patsubst src/%.c,$(TSAN_OBJ_DIR)/%.o,$(SOURCES))
TSAN_DEPFILES := $(TSAN_LIBOBJECTS:.o=.d) \
    $(foreach pair,$(TEST_PAIRS),$(TSAN_OBJ_DIR)/tests/$(basename $(notdir $(word 1,$(subst |, ,$(pair))))).d)
-include $(TSAN_DEPFILES)
TSAN_ARCHIVE := $(TSAN_APP_DIR)/$(STATIC_TARGET)
TSAN_ECMALIBRARY := -Wl,--whole-archive $(TSAN_ARCHIVE) -Wl,--no-whole-archive $(RCORE_LIBS) $(CUTIL_LIBS)
TSAN_CFLAGS := $(CFLAGS) $(TSAN_FLAGS) -DGLECMA_BUILD
TSAN_CXXFLAGS := $(CXXFLAGS) $(TSAN_FLAGS)
TSAN_LDFLAGS := $(LDFLAGS) $(TSAN_FLAGS)
ifeq ($(UNAME_S), Linux)
	TSAN_CFLAGS += -fPIC
endif

$(TSAN_OBJ_DIR)/%.o: src/%.c $(TSAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(TSAN_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(TSAN_ARCHIVE): $(TSAN_LIBOBJECTS)
	@mkdir -p $(@D)
	@rm -f $@
	ar rcs $@ $^

$(TSAN_OBJ_DIR)/tests/%.o: tests/unit/%.cpp $(TSAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CXX) $(TSAN_CXXFLAGS) -DGLECMA_TEST_BUILD $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

define tsan-test-executable-rule
TSAN_TEST_OBJ_$1 := $(TSAN_OBJ_DIR)/tests/$(basename $(notdir $1)).o

$(TSAN_APP_DIR)/$2$(EXE_EXTENSION): $$(TSAN_TEST_OBJ_$1) $(TSAN_ARCHIVE)
	@mkdir -p $$(@D)
	$(CXX) $(TSAN_CXXFLAGS) -o $$@ $$(TSAN_TEST_OBJ_$1) $(TSAN_LDFLAGS) $(TEST_LDFLAGS) $(TSAN_ECMALIBRARY) $(TESTFLAGS)
endef

$(foreach pair,$(TEST_PAIRS),\
	$(eval $(call tsan-test-executable-rule,$(word 1,$(subst |, ,$(pair))),$(word 2,$(subst |, ,$(pair))))))

TSAN_TEST_EXECUTABLES := $(addprefix $(TSAN_APP_DIR)/,$(addsuffix $(EXE_EXTENSION),$(TEST_NAMES)))
TSAN_RUN_ENV := LD_PRELOAD= LD_LIBRARY_PATH="$(TSAN_APP_DIR):$(RCORE_LIBDIR):$(CUTIL_LIBDIR):$(LIB_INSTALL_PATH)/$(SUITE)" \
	TSAN_OPTIONS="halt_on_error=1:history_size=7:second_deadlock_stack=1"

test-tsan: $(TSAN_TEST_EXECUTABLES) ## Build with TSan and run the tests (Linux only)
ifeq ($(OS_NAME), Linux)
	@for test_exe in $(TSAN_TEST_EXECUTABLES); do \
		printf '\n### TSan %s ###\n\n' "$$(basename $$test_exe)"; \
		$(TSAN_RUN_ENV) tools/run-gtest-full.sh $$test_exe || exit 1; \
	done
	@printf '\nTSan suite clean.\n'
else
	@printf 'ThreadSanitizer builds are only supported on Linux\n' >&2; exit 1
endif

####################################################################
# Fuzzing. Each tests/fuzz/fuzz_<name>.c becomes a libFuzzer harness;
# there are none until the first parser exists.
####################################################################

FUZZ_CC ?= clang
FUZZ_CXX ?= clang++
FUZZ_CC_OK := $(shell which $(FUZZ_CC) 2>/dev/null)
FUZZ_SAN := -fsanitize=address,$(UBSAN_CHECKS) -fno-omit-frame-pointer -g -O1 -fstrict-aliasing -ffp-contract=off
FUZZ_LIB_FLAGS := $(FUZZ_SAN) -fsanitize=fuzzer-no-link
FUZZ_BIN_FLAGS := $(FUZZ_SAN) -fsanitize=fuzzer
FUZZ_DIR := $(BUILD_DIR)/fuzz
FUZZ_OBJ_DIR := $(FUZZ_DIR)/objects
FUZZ_FLAGS_STAMP := $(FUZZ_OBJ_DIR)/.flags
FUZZ_APP_DIR := $(FUZZ_DIR)/apps
FUZZ_OBJECTS := $(patsubst src/%.c,$(FUZZ_OBJ_DIR)/%.o,$(SOURCES))
FUZZ_CORPUS := tests/fuzz/corpus
FUZZ_TIME ?= 5

$(FUZZ_OBJ_DIR)/%.o: src/%.c $(FUZZ_FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(FUZZ_CC) $(FUZZ_LIB_FLAGS) -std=c17 -fvisibility=hidden -DGLECMA_BUILD $(INCLUDE) -c $< -o $@

define fuzz-rule
fuzz-$2: $$(FUZZ_APP_DIR)/fuzz_$2 ## Build the $2 fuzzer

$$(FUZZ_APP_DIR)/fuzz_$2: tests/fuzz/fuzz_$2.c $$(FUZZ_OBJECTS) $$(FUZZ_FLAGS_STAMP) | $(LIBVER_GEN)
	@if [ -z "$$(FUZZ_CC_OK)" ]; then \
		echo "fuzzing requires $$(FUZZ_CC)"; \
		exit 1; \
	fi
	@mkdir -p $$(@D) $$(FUZZ_CORPUS)/$2
	$$(FUZZ_CC) $$(FUZZ_BIN_FLAGS) -std=c17 $(INCLUDE) \
		-o $$@ $$< $$(FUZZ_OBJECTS) $(RCORE_LIBS) $(CUTIL_LIBS)

fuzz-run-$2: $$(FUZZ_APP_DIR)/fuzz_$2 ## Run the $2 fuzzer for $$(FUZZ_TIME) seconds
	@mkdir -p $$(FUZZ_CORPUS)/$2
	@LD_LIBRARY_PATH="$(TEST_LD_PATH)" $$(FUZZ_APP_DIR)/fuzz_$2 $$(FUZZ_CORPUS)/$2 \
		-max_total_time=$$(FUZZ_TIME) -print_final_stats=1
endef

FUZZ_SOURCES := $(wildcard tests/fuzz/fuzz_*.c)
FUZZERS := $(patsubst tests/fuzz/fuzz_%.c,%,$(FUZZ_SOURCES))

$(foreach f,$(FUZZERS),$(eval $(call fuzz-rule,$(f),$(f))))

fuzz: $(addprefix fuzz-run-,$(FUZZERS)) ## Build and run every fuzzer briefly
ifeq ($(strip $(FUZZERS)),)
	@printf 'fuzz: no fuzzers in tests/fuzz yet; the first arrives with the first parser\n' >&2; exit 1
endif

fuzz-clean: ## Remove the fuzz build
	-@rm -rf $(FUZZ_DIR)

####################################################################
# Install
####################################################################

LDCONF_INSTALL_PATH ?= /etc/ld.so.conf.d
PC_REQUIRES := $(CUTIL_PC) $(RCORE_PC)
PKGCONFIG_INSTALL_PATH ?= $(PC_INSTALL_PATH)

CHECK_INSTALL_PREFIX ?= $(if $(PREFIX),$(PREFIX),/usr/local)
CHECK_INSTALL_MODULE ?= ghoti.io-lang-ecmascript$(BRANCH)
check-install: ## Verify an installed consumer using pkg-config and the public umbrella header
	@tools/check-install.sh "$(CHECK_INSTALL_PREFIX)" "$(CHECK_INSTALL_MODULE)"

install: all ## Install the library
	@mkdir -p $(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Linux)
	@cp $(APP_DIR)/$(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/
	@ln -f -s $(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/$(SO_NAME)
	@ln -f -s $(SO_NAME) $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then mkdir -p $(LDCONF_INSTALL_PATH); fi
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then echo "$(LIB_INSTALL_PATH)/$(SUITE)" > $(LDCONF_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).conf; fi
endif
ifeq ($(OS_NAME), Windows)
	@mkdir -p $(BIN_INSTALL_PATH) $(LIB_INSTALL_PATH)/$(SUITE)
	@cp $(APP_DIR)/$(TARGET).a $(LIB_INSTALL_PATH)/$(SUITE)/
	@cp $(APP_DIR)/$(TARGET) $(BIN_INSTALL_PATH)/
endif
	@rm -rf $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@mkdir -p $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@if [ -d include/ghoti.io ]; then \
		cp -r include/ghoti.io $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ ; \
	fi
	@if [ -d $(GEN_DIR)/ghoti.io ]; then \
		cp -r $(GEN_DIR)/ghoti.io $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ ; \
	fi
	@mkdir -p $(PKGCONFIG_INSTALL_PATH)
	@cat pkgconfig/$(SUITE)-$(PROJECT).pc | sed 's/(SUITE)/$(SUITE)/g; s/(PROJECT)/$(PROJECT)/g; s/(BRANCH)/$(BRANCH)/g; s/(VERSION)/$(VERSION)/g; s|(PC_LIB_DIR)|$(PC_LIB_DIR)|g; s|(PC_INCLUDE_DIR)|$(PC_INCLUDE_DIR)|g; s|(REQUIRES)|$(PC_REQUIRES)|g' > $(PKGCONFIG_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
ifeq ($(OS_NAME), Linux)
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then ldconfig >> /dev/null 2>&1; fi
endif
	@echo "Ghoti.io $(PROJECT)$(BRANCH) installed"

uninstall: ## Delete the installed files
ifeq ($(OS_NAME), Linux)
	@rm -f $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)*
	@rm -f $(LDCONF_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).conf
endif
	@rm -rf $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@rm -f $(PKGCONFIG_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
	@echo "Ghoti.io $(PROJECT)$(BRANCH) has been uninstalled"

docs: ## Generate documentation under docs/
	doxygen

docs-pdf: docs ## Generate the PDF manual
	cd ./docs/latex/ && make
	mv -f ./docs/latex/refman.pdf ./docs/$(SUITE)-$(PROJECT)$(BRANCH)-docs.pdf

cloc: ## Count the lines of code used in the project
	cloc src include tests Makefile

# One shell command, so the restoration runs whatever fails: --coverage
# objects left in the tree would be linked into the next ordinary build.
coverage: ## Build instrumented, run the tests, and report line coverage
	@rm -rf $(COV_BUILD_DIR)/objects/*.gcda \
		$(COV_BUILD_DIR)/objects/*/*.gcda 2> /dev/null || true
	@status=0; \
	$(MAKE) --no-print-directory test TEST_GATES= \
		BUILD_DIR=$(COV_BUILD_DIR) \
		EXTRA_CFLAGS="--coverage -O0 -fprofile-update=atomic" \
		EXTRA_LDFLAGS="--coverage" > /dev/null || status=$$?; \
	if [ $$status -eq 0 ]; then \
		tools/coverage.sh $(COV_BUILD_DIR)/objects || status=$$?; \
	else \
		printf 'coverage: the instrumented test run failed; no report\n' >&2; \
	fi; \
	exit $$status

clean: ## Remove the build directories
	-@rm -rf ./build

help: ## Display this help
	@grep -E '^[ a-zA-Z_-]+:.*?## .*$$' Makefile | sort | sed 's/\([^:]*\):.*## \(.*\)/\1:\2/' | awk -F: '{printf "%-22s %s\n", $$1, $$2}'

####################################################################
# Flag stamps. At the end so they are not the default goal.
####################################################################

.PHONY: force-flags

$(FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(CC) $(CXX) $(LIB_CFLAGS) $(CFLAGS) $(CXXFLAGS) $(LDFLAGS) $(INCLUDE) $(ECMALIBRARY) $(RCORE_LIBS) $(RCORE_CFLAGS) $(CUTIL_LIBS) $(CUTIL_CFLAGS) $(TESTFLAGS) $(TEST_LDFLAGS) $(OS_SPECIFIC_LIBRARY_NAME_FLAG)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@

$(ASAN_FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(CC) $(CXX) $(ASAN_CFLAGS) $(ASAN_CXXFLAGS) $(ASAN_LDFLAGS) $(INCLUDE) $(ASAN_ECMALIBRARY) $(RCORE_LIBS) $(CUTIL_LIBS) $(TESTFLAGS) $(TEST_LDFLAGS)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@

$(TSAN_FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(CC) $(CXX) $(TSAN_CFLAGS) $(TSAN_CXXFLAGS) $(TSAN_LDFLAGS) $(INCLUDE) $(TSAN_ECMALIBRARY) $(RCORE_LIBS) $(CUTIL_LIBS) $(TESTFLAGS) $(TEST_LDFLAGS)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@

$(FUZZ_FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(FUZZ_CC) $(FUZZ_CXX) $(FUZZ_LIB_FLAGS) $(FUZZ_BIN_FLAGS) $(INCLUDE) $(RCORE_LIBS) $(RCORE_CFLAGS) $(CUTIL_LIBS) $(FUZZ_SAN)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@
