# Differential test of this tree's non-GMP mpn.cpp/mpz.cpp (64-bit digits, the
# CatCrypt kernel objects in ..) against the upstream versions of the same files
# at the base release, v4.35.0 (32-bit digits, no kernels), read from git. See
# harness.cpp. No Lean build is involved; the tag must be present in the clone
# (git fetch origin tag v4.35.0).
#
#   make check                 # 20000 mpn + 20000 mpz cases, <= 300 words,
#                              # plus 40 + 40 cases with <= 6000 words
#   make check DEBUG=1         # the same with LEAN_DEBUG assertions compiled in
#   make stats                 # LEAN_CATCRYPT_MPN_STATS output of a short run
#   make dump CASE=<n>         # operands and results of one case, both builds
#
# Variables: LEGACY_TAG (v4.35.0), CXX, CASES, MAXLEN, SEED. The oracle tag
# must provide mpz::powm (v4.35.0 or later).

SRC        := $(abspath ../../..)
REPO       := $(abspath $(SRC)/..)
KERNELS    := $(wildcard $(abspath ..)/*.o)
LEGACY_TAG ?= v4.35.0
CASES      ?= 20000
MAXLEN     ?= 300
SEED       ?= 0x9e3779b97f4a7c15
BIG_CASES  ?= 40
BIG_MAXLEN ?= 6000

CXX        ?= c++
OPT        ?= -O2 -g
ifeq ($(DEBUG),1)
OPT        += -DLEAN_DEBUG
B          := build-debug
else
B          := build
endif
WARN       := -Wall -Wextra -Wno-unused-parameter
COMMON     := -std=c++20 $(OPT) $(WARN) -DLEAN_MULTI_THREAD -Iinclude
PATCHED_INC := $(COMMON) -I$(SRC)/include -I$(SRC)
# The oracle's files live in a directory named after the tag, so that a change
# of LEGACY_TAG does not reuse files extracted at another tag.
LEG        := $(B)/legacy-$(LEGACY_TAG)
LEGACY_INC  := $(COMMON) -I$(LEG) -I$(SRC)/include -I$(SRC)

.PHONY: all check stats dump clean
.SECONDARY:
all: check

# The oracle's headers and sources at LEGACY_TAG. $(LEG)/runtime/{mpn,mpz}.h
# shadow this tree's headers for the legacy objects: the legacy .cpp files
# include "runtime/...", searched relative to $(LEG) first, and harness.cpp
# finds them through -I$(LEG) ahead of -I$(SRC). The other runtime headers are
# this tree's, which are the base release's.
$(LEG)/runtime/%.h:
	mkdir -p $(@D)
	git -C $(REPO) show $(LEGACY_TAG):src/runtime/$*.h > $@

$(LEG)/%.cpp:
	mkdir -p $(@D)
	git -C $(REPO) show $(LEGACY_TAG):src/runtime/$*.cpp > $@

LEGACY_HDRS := $(LEG)/runtime/mpz.h $(LEG)/runtime/mpn.h

$(B)/patched/%.o: $(SRC)/runtime/%.cpp $(SRC)/runtime/mpz.h $(SRC)/runtime/mpn.h
	mkdir -p $(@D)
	$(CXX) $(PATCHED_INC) -c $< -o $@

$(B)/patched/harness.o: harness.cpp $(SRC)/runtime/mpz.h $(SRC)/runtime/mpn.h
	mkdir -p $(@D)
	$(CXX) $(PATCHED_INC) -c $< -o $@

$(B)/patched/stubs.o: stubs.cpp
	mkdir -p $(@D)
	$(CXX) $(PATCHED_INC) -c $< -o $@

$(LEG)/obj/%.o: $(LEG)/%.cpp $(LEGACY_HDRS)
	mkdir -p $(@D)
	$(CXX) $(LEGACY_INC) -c $< -o $@

$(LEG)/obj/harness.o: harness.cpp $(LEGACY_HDRS)
	mkdir -p $(@D)
	$(CXX) $(LEGACY_INC) -c $< -o $@

$(LEG)/obj/stubs.o: stubs.cpp $(LEGACY_HDRS)
	mkdir -p $(@D)
	$(CXX) $(LEGACY_INC) -c $< -o $@

$(B)/harness_patched: $(B)/patched/harness.o $(B)/patched/mpn.o $(B)/patched/mpz.o $(B)/patched/stubs.o $(KERNELS)
	$(CXX) -o $@ $^ -lpthread

$(B)/harness_legacy: $(LEG)/obj/harness.o $(LEG)/obj/mpn.o $(LEG)/obj/mpz.o $(LEG)/obj/stubs.o
	$(CXX) -o $@ $^ -lpthread

check: $(B)/harness_patched $(B)/harness_legacy
	$(B)/harness_patched $(CASES) $(MAXLEN) $(SEED) > $(B)/patched.out
	$(B)/harness_legacy  $(CASES) $(MAXLEN) $(SEED) > $(B)/legacy.out
	cmp $(B)/patched.out $(B)/legacy.out || { diff $(B)/patched.out $(B)/legacy.out | head -20; exit 1; }
	$(B)/harness_patched $(BIG_CASES) $(BIG_MAXLEN) 0x1234567 > $(B)/patched-big.out
	$(B)/harness_legacy  $(BIG_CASES) $(BIG_MAXLEN) 0x1234567 > $(B)/legacy-big.out
	cmp $(B)/patched-big.out $(B)/legacy-big.out || { diff $(B)/patched-big.out $(B)/legacy-big.out | head -20; exit 1; }
	@echo "PASS: $$(wc -l < $(B)/patched.out) + $$(wc -l < $(B)/patched-big.out) operation results identical"

stats: $(B)/harness_patched
	LEAN_CATCRYPT_MPN_STATS=1 $(B)/harness_patched 200 40 > /dev/null

dump: $(B)/harness_patched $(B)/harness_legacy
	HARNESS_DUMP=$(CASE) $(B)/harness_patched $(CASES) $(MAXLEN) $(SEED) | grep -B8 -A120 "^$(CASE) " | head -160
	HARNESS_DUMP=$(CASE) $(B)/harness_legacy  $(CASES) $(MAXLEN) $(SEED) | grep -B8 -A120 "^$(CASE) " | head -160

clean:
	rm -rf build build-debug
