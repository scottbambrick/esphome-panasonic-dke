# Panasonic CS-E24GKR (DKE) infrared protocol

Captured from the original remote with an IR receiver on an ESP-01M, October 2026. Everything here was
observed on a CS-E24GKR; other DKE-family Panasonic units probably match.

## Timing

| Part | Duration |
|---|---|
| Header mark / space | 3.5 ms / 1.75 ms |
| Bit mark | ~440 µs |
| Bit space | ~430 µs = 0, ~1300 µs = 1 |
| Gap between frames | ~10 ms |
| Carrier | 38 kHz |

Bytes are sent least-significant bit first. Every button press is **two frames** back to back:

1. Frame 0, always the same 8 bytes: `02 20 E0 04 00 00 00 06`
2. Frame 1: either a 19-byte *state* frame or an 8-byte *command* frame.

A receiver often drops the first mark and the final space; the decoder tolerates both.

## State frame (19 bytes)

```
02 20 E0 04 | 00 | b5 | b6 | 80 | b8 | b9 | 00 0E E0 00 00 81 00 00 | checksum
 0  1  2  3    4    5    6    7    8    9    10 11 12 13 14 15 16 17    18
```

| Byte | Meaning |
|---|---|
| 5 | High nibble = mode: `0` auto, `2` dry, `3` cool, `4` heat (`6` fan-only exists in other models; this remote never sends it). Bit 3 (`0x08`) = timers cancelled, always set. Bit 0 = power on. |
| 6 | Target temperature in °C × 2 (`0x20` = 16 … `0x3C` = 30). |
| 8 | High nibble = fan: `A` auto, `3`…`7` = speed 1…5. Low nibble = vertical louvre: `1` (top) … `5` (bottom), `F` auto swing. |
| 9 | Horizontal louvre: `06` centre, `09` left, `0A` centre-left, `0B` centre-right, `0C` right, `0D` auto swing. |
| 18 | Checksum: sum of bytes 0…17, low byte. |

Example, auto mode, on, 25 °C, fan auto:
`02 20 E0 04 00 09 32 80 A4 06 00 0E E0 00 00 81 00 00 DA`

The remote remembers fan and louvre settings per mode.

## Command frames (8 bytes)

`02 20 E0 04 80 xx yy checksum`, with the same plain-sum checksum. Each is a **toggle**: the bytes are the
same for on and off, and the unit reports no state.

| Button | `xx yy` | Checksum |
|---|---|---|
| Powerful | `86 35` | `41` |
| Quiet | `81 33` | `3A` |
| e-ion | `61 33` | `1A` |
| Patrol | `63 33` | `1C` |

### How the toggles behave

None of them appear in the state frame (state frames were identical, apart from temperature and checksum,
with e-ion, Powerful or Quiet on), so the component has to track them itself.

- Changing mode does not affect any toggle.
- Switching the unit on resets everything: Powerful and Quiet off, e-ion off, **Patrol on**.
- e-ion and Patrol are mutually exclusive; Powerful and Quiet are mutually exclusive.
- Pressing the active button turns it off; pressing the other one of the pair switches over.

The component models this as two settings: *air* = {off, e-ion, Patrol} and *boost* = {off, Powerful, Quiet}.

## Notes

- Corrupted frames do occur (one captured Quiet frame had a single flipped bit), so a decoder must check
  the checksum and the frame length. About two minutes of partial frames were also seen once; they are
  rejected the same way.
- HeatpumpIR's `PanasonicAltDKE` sends `0x01` and `0x06` in two trailing bytes where the real remote sends
  `0x81` and `0x00`. The unit accepts both; this component sends what the remote sends.
- Timer buttons are not implemented.
