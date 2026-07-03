# ESPHome Dehumidifier — System Tests

Host-side system tests for the Midea dehumidifier ESPHome component.
Compiles `midea_dehum.cpp` against mock ESPHome headers — no hardware needed.

## Quick Start

```bash
cd tests
make             # build all test binaries (takes ~2s)
make test-all    # build and run everything
```

## Test Files

| Binary | Category | What it tests |
|--------|----------|---------------|
| `test_handshake` | Handshake | V1 opening sequence, disabled mode, early status |
| `test_commands` | Commands | Every setter: power, mode, fan, humidity, pump, ion, sleep, beep, swing |
| `test_e2e` | End-to-End | Power cycle, mode switch, fan ramp, push notifications, 50-frame soak |
| `test_system` | System | Error handling, protocol compliance, state consistency |

## Expected Output

```
$ make test-all

=== Category 1: Handshake ===
=== V1 handshake: 0 failures ===
=== Handshake disabled: 0 failures ===
=== Early status: 0 failures ===
✓ All handshake tests passed!

=== Category 2: Commands ===
... (11 suites) ...
✓ All command tests passed!

=== Category 3: End-to-End ===
... (8 scenarios) ...
✓ All E2E tests passed!

=== Categories 4-6: System ===
... (16 tests) ...
✓ All system verification tests passed!

=== TEST SUMMARY ===
  ALL TESTS PASSED (full config)
```

## Configuration Matrix

```bash
make config-full      # all features enabled  (passes)
make config-minimal   # HANDSHAKE only        (expected compile errors)
make config-nohs      # no HANDSHAKE          (expected compile errors)
```

The minimal/no-handshake configs fail to compile because the original code
references optional features without `#ifdef` guards — this documents
a known limitation of the upstream code.

## Architecture

```
test_*.cpp ──▶ fixtures.h (TestMideaDehum) ──▶ midea_dehum.cpp (component)
                   │
                   ├── setup() / loop()        ← ESPHome lifecycle
                   ├── control(ClimateCall)    ← HA user commands
                   ├── rx_enqueue()            ← UART → processPacket
                   └── pub_*() / raw_*()       ← state verification
```

## Adding a New Test

1. Create `test_thing.cpp` with `#include "fixtures.h"`
2. Write test functions using `TestMideaDehum` + `ASSERT`/`ASSERT_EQ`
3. Add to `ALL_TESTS` in `Makefile`
4. Run `make && ./test_thing`
