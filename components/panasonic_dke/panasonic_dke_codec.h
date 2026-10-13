#pragma once
// Panasonic "DKE" air conditioner remote protocol (CS-E24GKR), no ESPHome
// dependencies so it can be unit-tested on a PC. Byte layout from captures of
// the real remote: .claudecode/ac-remote/codes.md
#include <cstddef>
#include <cstdint>

namespace esphome::panasonic_dke {

// Operating mode (high nibble of state byte 5)
enum : uint8_t { MODE_AUTO = 0x00, MODE_DRY = 0x20, MODE_COOL = 0x30, MODE_HEAT = 0x40, MODE_FAN = 0x60 };
// Horizontal swing (state byte 9)
enum : uint8_t { HS_CENTRE = 0x06, HS_LEFT = 0x09, HS_MLEFT = 0x0A, HS_MRIGHT = 0x0B, HS_RIGHT = 0x0C, HS_AUTO = 0x0D };
inline constexpr uint8_t VS_AUTO = 0x0F;  // vertical swing: 1 (top) .. 5 (bottom), or auto

// Standalone toggle commands (each press toggles; no state is reported)
enum Command : uint8_t { CMD_POWERFUL, CMD_QUIET, CMD_EION, CMD_PATROL, CMD_COUNT };

struct State {
  bool power{false};
  uint8_t mode{MODE_AUTO};  // MODE_*
  uint8_t temp{24};         // degrees C, 16..30
  uint8_t fan{0};           // 0 = auto, 1..5
  uint8_t vswing{1};        // 1..5 or VS_AUTO
  uint8_t hswing{HS_CENTRE};
};

inline constexpr size_t HEADER_LEN = 8;
inline constexpr size_t STATE_LEN = 19;
inline constexpr size_t COMMAND_LEN = 8;
inline constexpr size_t MAX_FRAME_LEN = STATE_LEN;

inline constexpr uint8_t HEADER[HEADER_LEN] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06};

inline uint8_t checksum(const uint8_t *b, size_t n) {
  uint8_t s = 0;
  for (size_t i = 0; i < n; i++) s += b[i];
  return s;
}

inline void encode_state(const State &s, uint8_t out[STATE_LEN]) {
  static const uint8_t T[STATE_LEN] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
                                       0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0x00};
  for (size_t i = 0; i < STATE_LEN; i++) out[i] = T[i];
  out[4] = 0x00;
  out[5] = (uint8_t)(s.mode | 0x08 | (s.power ? 0x01 : 0x00));  // 0x08 = timers cancelled (always set by the remote)
  out[6] = (uint8_t)(s.temp * 2);
  out[8] = (uint8_t)((s.fan == 0 ? 0xA0 : 0x20 + 0x10 * s.fan) | (s.vswing & 0x0F));
  out[9] = s.hswing;
  out[STATE_LEN - 1] = checksum(out, STATE_LEN - 1);
}

inline void encode_command(Command c, uint8_t out[COMMAND_LEN]) {
  static const uint8_t CODES[CMD_COUNT][2] = {{0x86, 0x35}, {0x81, 0x33}, {0x61, 0x33}, {0x63, 0x33}};
  static const uint8_t T[4] = {0x02, 0x20, 0xE0, 0x04};
  for (int i = 0; i < 4; i++) out[i] = T[i];
  out[4] = 0x80;
  out[5] = CODES[c][0];
  out[6] = CODES[c][1];
  out[7] = checksum(out, 7);
}

enum Kind : uint8_t { KIND_NONE, KIND_STATE, KIND_COMMAND };
struct Decoded {
  Kind kind{KIND_NONE};
  State state;
  Command command{CMD_POWERFUL};
};

// Decode the second frame of a burst (8 or 19 bytes). Rejects bad checksums
// and anything outside the values the remote is known to send.
inline bool decode_frame(const uint8_t *b, size_t n, Decoded &out) {
  if (n < 8 || b[0] != 0x02 || b[1] != 0x20 || b[2] != 0xE0 || b[3] != 0x04) return false;
  if (checksum(b, n - 1) != b[n - 1]) return false;
  if (n == COMMAND_LEN && b[4] == 0x80) {
    for (int c = 0; c < CMD_COUNT; c++) {
      uint8_t ref[COMMAND_LEN];
      encode_command((Command) c, ref);
      if (ref[5] == b[5] && ref[6] == b[6]) {
        out.kind = KIND_COMMAND;
        out.command = (Command) c;
        return true;
      }
    }
    return false;
  }
  if (n == STATE_LEN && b[4] == 0x00 && b[7] == 0x80) {
    uint8_t mode = b[5] & 0xF0, fan = b[8] & 0xF0, vs = b[8] & 0x0F, hs = b[9];
    uint8_t temp = b[6] / 2;
    if (mode != MODE_AUTO && mode != MODE_DRY && mode != MODE_COOL && mode != MODE_HEAT && mode != MODE_FAN)
      return false;
    if (b[6] & 1 || temp < 16 || temp > 30) return false;
    if (fan != 0xA0 && !(fan >= 0x30 && fan <= 0x70)) return false;
    if (vs != VS_AUTO && (vs < 1 || vs > 5)) return false;
    if (hs != HS_CENTRE && hs != HS_AUTO && !(hs >= HS_LEFT && hs <= HS_RIGHT)) return false;
    out.kind = KIND_STATE;
    out.state.power = b[5] & 0x01;
    out.state.mode = mode;
    out.state.temp = temp;
    out.state.fan = fan == 0xA0 ? 0 : (fan - 0x20) / 0x10;
    out.state.vswing = vs;
    out.state.hswing = hs;
    return true;
  }
  return false;
}

// ---- timings <-> bytes -------------------------------------------------
// Raw timings use the ESPHome convention: positive = mark, negative = space (microseconds).
inline constexpr int HDR_MARK = 3500, HDR_SPACE = 1750, BIT_MARK = 440, ZERO_SPACE = 430, ONE_SPACE = 1300,
                 FRAME_GAP = 10000;

// Parse a received burst: frame 0 must be the fixed header, frame 1 is decoded.
// Tolerates the receiver dropping the very first mark and the final space.
inline bool decode_burst(const int32_t *raw, size_t n, Decoded &out) {
  uint8_t frames[2][MAX_FRAME_LEN + 1];
  size_t nbits[2] = {0, 0};
  int f = 0;
  uint8_t cur = 0;
  size_t i = 0;
  while (i < n && f < 2) {
    int32_t v = raw[i];
    if (v > 2500) {  // header mark
      i += 2;
      continue;
    }
    if (v < 0) {  // stray space (dropped header mark) or gap
      if (v < -5000) {
        f++;
        cur = 0;
      }
      i++;
      continue;
    }
    if (i + 1 >= n) break;  // trailing stop mark
    int32_t sp = -raw[i + 1];
    if (sp > 5000) {  // last mark of a frame, then the gap
      f++;
      cur = 0;
      i += 2;
      continue;
    }
    if (v < 250 || v > 700 || sp < 250) return false;
    size_t bit = nbits[f];
    if (bit / 8 > MAX_FRAME_LEN) return false;
    if (bit % 8 == 0) cur = 0;
    if (sp > 800) cur |= (uint8_t)(1 << (bit & 7));
    nbits[f] = ++bit;
    if (bit % 8 == 0) frames[f][bit / 8 - 1] = cur;
    i += 2;
  }
  if (f < 1 || nbits[0] != HEADER_LEN * 8) return false;
  for (size_t k = 0; k < HEADER_LEN; k++)
    if (frames[0][k] != HEADER[k]) return false;
  if (nbits[1] % 8 != 0) return false;
  return decode_frame(frames[1], nbits[1] / 8, out);
}

// Emit a full burst (header frame + `b`, `n` bytes) through `emit(mark_us, space_us)`.
template<typename F> inline void build_burst(const uint8_t *b, size_t n, F emit) {
  auto frame = [&](const uint8_t *d, size_t len, bool last) {
    emit(HDR_MARK, HDR_SPACE);
    for (size_t i = 0; i < len; i++)
      for (int j = 0; j < 8; j++) emit(BIT_MARK, (d[i] >> j) & 1 ? ONE_SPACE : ZERO_SPACE);
    emit(BIT_MARK, last ? 0 : FRAME_GAP);
  };
  frame(HEADER, HEADER_LEN, false);
  frame(b, n, true);
}

}  // namespace esphome::panasonic_dke
