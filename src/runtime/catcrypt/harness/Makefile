# Differential test of this tree's non-GMP mpn.cpp/mpz.cpp (64-bit digits, the
# CatCrypt kernel objects in ..) against the v4.33.1 versions of the same files
# (32-bit digits). See harness.cpp. No Lean build is involved.
#
#   make check                 # 20000 mpn + 20000 mpz cases, <= 300 words,
#                              # plus 40 + 40 cases with <= 6000 words
#   make check DEBUG=1         # the same with LEAN_DEBUG assertions compiled in
#   make stats                 # LEAN_CATCRYPT_MPN_STATS output of a short run
#   make dump CASE=<n>         # operands and results of one case, both builds
#
# Variables: LEGACY_TAG (v4.33.1), CXX, CASES, MAXLEN, SEED.

SRC        := $(abspath ../../..)
REPO       := $(abspath $(SRC)/..)
KERNELS    := $(wildcard $(abspath ..)/*.o)
LEGACY_TAG ?= v4.33.1
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
LEGACY_INC  := $(COMMON) -I$(B)/legacy -I$(SRC)/include -I$(SRC)

.PHONY: all check stats dump clean
.SECONDARY:
all: check

# v4.33.1 headers and sources. $(B)/legacy/runtime/{mpn,mpz}.h shadow this
# tree's headers for the legacy objects: the legacy .cpp files include
# "runtime/...", searched relative to $(B)/legacy first, and harness.cpp finds
# them through -I$(B)/legacy ahead of -I$(SRC).
$(B)/legacy/runtime/%.h:
	mkdir -p $(@D)
	git -C $(REPO) show $(LEGACY_TAG):src/runtime/$*.h > $@

$(B)/legacy/%.cpp:
	mkdir -p $(@D)
	git -C $(REPO) show $(LEGACY_TAG):src/runtime/$*.cpp > $@

LEGACY_HDRS := $(B)/legacy/runtime/mpz.h $(B)/legacy/runtime/mpn.h

$(B)/patched/%.o: $(SRC)/runtime/%.cpp $(SRC)/runtime/mpz.h $(SRC)/runtime/mpn.h
	mkdir -p $(@D)
	$(CXX) $(PATCHED_INC) -c $< -o $@

$(B)/patched/harness.o: harness.cpp $(SRC)/runtime/mpz.h $(SRC)/runtime/mpn.h
	mkdir -p $(@D)
	$(CXX) $(PATCHED_INC) -c $< -o $@

$(B)/patched/stubs.o: stubs.cpp
	mkdir -p $(@D)
	$(CXX) $(PATCHED_INC) -c $< -o $@

$(B)/legacy/obj/%.o: $(B)/legacy/%.cpp $(LEGACY_HDRS)
	mkdir -p $(@D)
	$(CXX) $(LEGACY_INC) -c $< -o $@

$(B)/legacy/obj/harness.o: harness.cpp $(LEGACY_HDRS)
	mkdir -p $(@D)
	$(CXX) $(LEGACY_INC) -c $< -o $@

$(B)/legacy/obj/stubs.o: stubs.cpp $(LEGACY_HDRS)
	mkdir -p $(@D)
	$(CXX) $(LEGACY_INC) -c $< -o $@

$(B)/harness_patched: $(B)/patched/harness.o $(B)/patched/mpn.o $(B)/patched/mpz.o $(B)/patched/stubs.o $(KERNELS)
	$(CXX) -o $@ $^ -lpthread

$(B)/harness_legacy: $(B)/legacy/obj/harness.o $(B)/legacy/obj/mpn.o $(B)/legacy/obj/mpz.o $(B)/legacy/obj/stubs.o
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
	HARNESS_DUMP=$(CASE) $(B)/harness_patched $(CASES) $(MAXLEN) $(SEED) | grep -A40 "^$(CASE) " | head -80
	HARNESS_DUMP=$(CASE) $(B)/harness_legacy  $(CASES) $(MAXLEN) $(SEED) | grep -A40 "^$(CASE) " | head -80

clean:
	rm -rf build build-debug
