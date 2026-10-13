// Host-side test of the codec: g++ -std=c++17 -I components/panasonic_dke tests/codec_test.cpp && ./a.out
#include <cstdio>
#include "panasonic_dke_selftest.h"

int main() {
  auto r = esphome::panasonic_dke::run_self_test(
      [](const char *what, const char *detail) { printf("FAIL %s %s\n", what, detail); });
  printf("%d checks, %d failed; %d state round-trips, %d failed\n", r.checks, r.failed, r.combos, r.combos_failed);
  return r.failed || r.combos_failed ? 1 : 0;
}
