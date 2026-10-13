#pragma once
// Codec checks that run both on the chip (`self_test: true`, logged at boot) and on a PC
// (tests/codec_test.cpp, run by CI). Vectors are frames captured from the real remote.
#include <cstdio>
#include <cstring>
#include <vector>
#include "panasonic_dke_codec.h"

namespace esphome::panasonic_dke {

struct SelfTestResult {
  int checks{0}, failed{0};  // fixed vectors, command round trips
  int combos{0}, combos_failed{0};  // state -> pulses -> state round trips
};

namespace selftest_detail {

inline bool parse_hex(const char *s, uint8_t *out, size_t *n) {
  *n = 0;
  while (*s) {
    unsigned v;
    if (sscanf(s, "%2x", &v) != 1) return false;
    out[(*n)++] = (uint8_t) v;
    s += 2;
    if (*s == ' ') s++;
  }
  return true;
}

inline std::vector<int32_t> pulses(const uint8_t *bytes, size_t len) {
  std::vector<int32_t> t;
  t.reserve(len * 16 + 40);
  build_burst(bytes, len, [&t](int mark, int space) {
    t.push_back(mark);
    if (space) t.push_back(-space);
  });
  return t;
}

}  // namespace selftest_detail

/// `fail` is called with a description of each failed check (may be null).
template<typename Fail> SelfTestResult run_self_test(Fail fail) {
  using namespace selftest_detail;
  static const char *const GOOD_STATE[] = {
      "02 20 E0 04 00 09 32 80 A4 06 00 0E E0 00 00 81 00 00 DA",  // auto, on, 25C, fan auto
      "02 20 E0 04 00 39 3C 80 74 06 00 0E E0 00 00 81 00 00 E4",  // cool 30C fan 5, louvre 4
      "02 20 E0 04 00 39 20 80 74 06 00 0E E0 00 00 81 00 00 C8",  // cool 16C
      "02 20 E0 04 00 39 30 80 3F 0D 00 0E E0 00 00 81 00 00 AA",  // cool 24C fan 1, both louvres auto
      "02 20 E0 04 00 49 2A 80 AF 0D 00 0E E0 00 00 81 00 00 24",  // heat 21C
      "02 20 E0 04 00 08 32 80 A4 06 00 0E E0 00 00 81 00 00 D9",  // auto, power off
  };
  static const char *const GOOD_CMD[] = {"02 20 E0 04 80 86 35 41", "02 20 E0 04 80 81 33 3A",
                                         "02 20 E0 04 80 61 33 1A", "02 20 E0 04 80 63 33 1C"};
  static const char *const BAD[] = {"02 20 E0 04 80 61 33 9A",  // one flipped bit, seen on the real remote
                                    "02 20 E0 04 00 39 30 80 3F 0D 00 0E E0 00 00 81 00 00 AB"};
  SelfTestResult r;
  auto check = [&](bool ok, const char *what, const char *detail) {
    r.checks++;
    if (!ok) {
      r.failed++;
      fail(what, detail);
    }
  };
  uint8_t b[MAX_FRAME_LEN + 1];
  size_t n;
  Decoded d;

  for (auto h : GOOD_STATE) {  // decodes, and encoding the result gives the same bytes back
    bool ok = parse_hex(h, b, &n) && decode_frame(b, n, d) && d.kind == KIND_STATE;
    if (ok) {
      uint8_t out[STATE_LEN];
      encode_state(d.state, out);
      ok = memcmp(out, b, STATE_LEN) == 0;
    }
    check(ok, "state", h);
  }
  for (auto h : GOOD_CMD)
    check(parse_hex(h, b, &n) && decode_frame(b, n, d) && d.kind == KIND_COMMAND, "command", h);
  for (auto h : BAD) check(parse_hex(h, b, &n) && !decode_frame(b, n, d), "accepted a bad frame", h);

  // Every mode / fan / louvre combination through encode -> pulses -> decode_burst, then again
  // with the first mark dropped, as the real receiver often does.
  for (uint8_t mode : {MODE_AUTO, MODE_DRY, MODE_COOL, MODE_HEAT})
    for (int fan = 0; fan <= 5; fan++)
      for (uint8_t vs : {1, 3, 5, 0x0F})
        for (uint8_t hs : {HS_CENTRE, HS_LEFT, HS_MLEFT, HS_MRIGHT, HS_RIGHT, HS_AUTO}) {
          State s;
          s.power = (r.combos & 1) != 0;
          s.mode = mode;
          s.temp = 16 + r.combos % 15;
          s.fan = fan;
          s.vswing = vs;
          s.hswing = hs;
          uint8_t bytes[STATE_LEN];
          encode_state(s, bytes);
          auto t = pulses(bytes, STATE_LEN);
          Decoded x;
          bool ok = decode_burst(t.data(), t.size(), x) && x.kind == KIND_STATE && x.state.power == s.power &&
                    x.state.mode == s.mode && x.state.temp == s.temp && x.state.fan == s.fan &&
                    x.state.vswing == s.vswing && x.state.hswing == s.hswing;
          ok = ok && decode_burst(t.data() + 1, t.size() - 1, x) && x.kind == KIND_STATE && x.state.temp == s.temp;
          r.combos++;
          if (!ok) r.combos_failed++;
        }
  for (int c = 0; c < CMD_COUNT; c++) {
    uint8_t bytes[COMMAND_LEN];
    encode_command((Command) c, bytes);
    auto t = pulses(bytes, COMMAND_LEN);
    Decoded x;
    check(decode_burst(t.data(), t.size(), x) && x.kind == KIND_COMMAND && x.command == c, "command round trip", "");
  }
  return r;
}

}  // namespace esphome::panasonic_dke
