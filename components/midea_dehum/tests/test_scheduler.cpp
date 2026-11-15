#include "mock_esphome.h"

int main() {
  reset_scheduler();
  esphome::Component first, second;
  int a = 0, b = 0;
  esphome::App.scheduler.set_timeout(&first, "same", 100, [&]() { a = 1; });
  esphome::App.scheduler.set_timeout(&first, "same", 200, [&]() { a = 2; });
  esphome::App.scheduler.set_timeout(&second, "same", 100, [&]() { b = 1; });
  assert(millis() == 0 && millis() == 0);
  advance_time(99);
  assert(a == 0 && b == 0);
  advance_time(1);
  assert(a == 0 && b == 1);
  advance_time(100);
  assert(a == 2);
  esphome::App.scheduler.set_timeout(&first, "cancel", 10, [&]() { a = 3; });
  assert(esphome::App.scheduler.cancel_timeout(&first, "cancel"));
  advance_time(10);
  assert(a == 2);
  {
    esphome::Component temporary;
    esphome::App.scheduler.set_timeout(&temporary, "destroyed", 10, [&]() { a = 4; });
  }
  advance_time(10);
  assert(a == 2);
  test_time_ms = UINT32_MAX - 5ULL;
  esphome::App.scheduler.set_timeout(&first, "rollover", 10, [&]() { a = 5; });
  advance_time(9);
  assert(a == 2);
  advance_time(1);
  assert(a == 5 && millis() == 4);
  reset_scheduler();
  printf("Scheduler deadline/ownership/cancellation/rollover tests passed.\n");
}
