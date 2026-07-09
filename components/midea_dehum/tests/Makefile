# Build test harness for ESPHome Dehumidifier component.
# Uses mock ESPHome headers so the component code compiles with host g++/clang++.

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0

# Include paths: the mock headers shadow the real ESPHome includes
#    -I.          → mock_esphome.h, esphome forwarding headers
#    -I..         → component source files (midea_dehum.h, etc.)
INCLUDES := -I. -I..

# Compile flags — enable V1 + V2 + all optional features we want to test
DEFINES  := -DMIDEA_PROTOCOL_V1 -DMIDEA_PROTOCOL_V2 \
            -DMIDEA_PROTOCOL_AUTO               \
            -DUSE_MIDEA_DEHUM_HANDSHAKE           \
            -DUSE_MIDEA_DEHUM_ERROR               \
            -DUSE_MIDEA_DEHUM_BEEP                \
            -DUSE_MIDEA_DEHUM_ION                 \
            -DUSE_MIDEA_DEHUM_PUMP                \
            -DUSE_MIDEA_DEHUM_SLEEP               \
            -DUSE_MIDEA_DEHUM_LIGHT               \
            -DUSE_MIDEA_DEHUM_SWING               \
            -DUSE_MIDEA_DEHUM_HORIZONTAL_SWING    \
            -DUSE_MIDEA_DEHUM_FILTER              \
            -DUSE_MIDEA_DEHUM_FILTER_BUTTON       \
            -DUSE_MIDEA_DEHUM_TIMER               \
            -DUSE_MIDEA_DEHUM_CAPABILITIES

# All component source files (except __init__.py)
COMPONENT_SRC := ../midea_dehum.cpp          \
                 ../midea_dehum_uart.cpp      \
                 ../midea_dehum_features.cpp  \
                 ../midea_dehum_state.cpp     \
                 ../midea_dehum_protocol_v1.cpp \
                 ../midea_dehum_protocol_v2.cpp \
                 ../midea_dehum_protocol_auto.cpp

OBJS := test_harness.o \
        midea_dehum.o \
        midea_dehum_uart.o \
        midea_dehum_features.o \
        midea_dehum_state.o \
        midea_dehum_protocol_v1.o \
        midea_dehum_protocol_v2.o \
        midea_dehum_protocol_auto.o

.PHONY: all clean run-v1 run-v2 run-all test test-asan standalone

all: test_harness

# ── Combined harness ────────────────────────────────────────────────────

test_harness: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

test_harness.o: test_harness.cpp mock_esphome.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c -o $@ test_harness.cpp

midea_dehum.o: ../midea_dehum.cpp ../midea_dehum.h mock_esphome.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c -o $@ ../midea_dehum.cpp

midea_dehum_uart.o: ../midea_dehum_uart.cpp ../midea_dehum.h mock_esphome.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c -o $@ ../midea_dehum_uart.cpp

midea_dehum_features.o: ../midea_dehum_features.cpp ../midea_dehum.h mock_esphome.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c -o $@ ../midea_dehum_features.cpp

midea_dehum_state.o: ../midea_dehum_state.cpp ../midea_dehum.h mock_esphome.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c -o $@ ../midea_dehum_state.cpp

midea_dehum_protocol_v1.o: ../midea_dehum_protocol_v1.cpp ../midea_dehum.h mock_esphome.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c -o $@ ../midea_dehum_protocol_v1.cpp

midea_dehum_protocol_v2.o: ../midea_dehum_protocol_v2.cpp ../midea_dehum.h mock_esphome.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c -o $@ ../midea_dehum_protocol_v2.cpp

midea_dehum_protocol_auto.o: ../midea_dehum_protocol_auto.cpp ../midea_dehum.h mock_esphome.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c -o $@ ../midea_dehum_protocol_auto.cpp

run-v1: test_harness
	./test_harness v1

run-v2: test_harness
	./test_harness v2

run-all: test_harness
	./test_harness all

# ── CI targets ──────────────────────────────────────────────────────────

test: test_harness
	./test_harness all

# ASAN: rebuild with address sanitizer and run
test-asan: clean
	$(MAKE) CXXFLAGS="-std=c++17 -fsanitize=address -g -O0"
	./test_harness all

# ── Standalone single-suite executables ─────────────────────────────────

STANDALONE_OBJS := midea_dehum.o midea_dehum_uart.o midea_dehum_features.o \
                  midea_dehum_state.o midea_dehum_protocol_v1.o \
                  midea_dehum_protocol_v2.o midea_dehum_protocol_auto.o

standalone: test_handshake_solo test_commands_solo test_e2e_solo test_system_solo

test_%_solo: test_%.cpp fixtures.h mock_esphome.h $(STANDALONE_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -o $@ $< $(STANDALONE_OBJS)

clean:
	rm -f test_harness *.o *_solo
