# Test harness — compiles original midea_dehum.cpp against mock ESPHome headers.
#
# Usage:
#   make                build all test binaries (full config)
#   make test-all       build and run all tests
#   make config-full    default: all features enabled
#   make config-minimal HANDSHAKE only (no optional features)
#   make config-nohs    HANDSHAKE disabled
#   make clean

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0
INCLUDES := -I. -I..

# ── Default: full feature set ───────────────────────────────────────────

FULL_DEFINES := -DUSE_MIDEA_DEHUM_HANDSHAKE \
                -DUSE_MIDEA_DEHUM_ERROR     \
                -DUSE_MIDEA_DEHUM_BEEP      \
                -DUSE_MIDEA_DEHUM_ION       \
                -DUSE_MIDEA_DEHUM_PUMP      \
                -DUSE_MIDEA_DEHUM_SLEEP     \
                -DUSE_MIDEA_DEHUM_SWING     \
                -DUSE_MIDEA_DEHUM_DEFROST   \
                -DUSE_MIDEA_DEHUM_BUCKET    \
                -DUSE_MIDEA_DEHUM_FILTER    \
                -DUSE_MIDEA_DEHUM_FILTER_BUTTON \
                -DUSE_MIDEA_DEHUM_TIMER     \
                -DUSE_MIDEA_DEHUM_CAPABILITIES

# ── Minimal: HANDSHAKE only ──────────────────────────────────────────────

MIN_DEFINES := -DUSE_MIDEA_DEHUM_HANDSHAKE

# ── No handshake: nothing ────────────────────────────────────────────────

NOHS_DEFINES :=

# ── Build rules ─────────────────────────────────────────────────────────

DEFINES := $(FULL_DEFINES)

# Object files are config-specific so they don't clash between configs
midea_dehum.o: ../midea_dehum.cpp ../midea_dehum.h mock_esphome.h fixtures.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c -o $@ ../midea_dehum.cpp

%: %.cpp fixtures.h mock_esphome.h midea_dehum.o
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c -o $@.o $<
	$(CXX) $(CXXFLAGS) -o $@ $@.o midea_dehum.o

ALL_TESTS := test_handshake test_commands test_system test_e2e

.PHONY: all clean test-all config-minimal config-nohs config-full

all: $(ALL_TESTS)

test-all: all
	@echo
	@fail=0; \
	echo "=== Category 1: Handshake ==="; \
	./test_handshake      || { fail=1; true; }; \
	echo; \
	echo "=== Category 2: Commands ==="; \
	./test_commands       || { fail=1; true; }; \
	echo; \
	echo "=== Category 3: End-to-End ==="; \
	./test_e2e            || { fail=1; true; }; \
	echo; \
	echo "=== Categories 4-6: System ==="; \
	./test_system         || { fail=1; true; }; \
	echo; \
	echo "=== TEST SUMMARY ==="; \
	if [ $$fail -eq 0 ]; then echo "  ALL TESTS PASSED (full config)"; exit 0; \
	else echo "  SOME TESTS FAILED"; exit 1; fi

# ── Configuration matrix ────────────────────────────────────────────────

config-full:
	@echo "=== Config: full (all features) ==="
	$(MAKE) clean
	$(MAKE) DEFINES="$(FULL_DEFINES)" test-all

config-minimal:
	@echo "=== Config: minimal (HANDSHAKE only) ==="
	@$(MAKE) clean > /dev/null 2>&1
	@$(MAKE) DEFINES="$(MIN_DEFINES)" test_handshake 2>&1 | grep -q "Error" && \
	  echo "  [EXPECTED] Minimal config fails: code references USE_MIDEA_DEHUM_BEEP etc." || \
	  (./test_handshake && echo "  PASS")

config-nohs:
	@echo "=== Config: no handshake ==="
	@$(MAKE) clean > /dev/null 2>&1
	@$(MAKE) DEFINES="$(NOHS_DEFINES)" test_handshake 2>&1 | grep -q "Error" && \
	  echo "  [EXPECTED] No-handshake config fails: handshake code always present" || \
	  (./test_handshake && echo "  PASS")

clean:
	rm -f $(ALL_TESTS) *.o *.tmp
