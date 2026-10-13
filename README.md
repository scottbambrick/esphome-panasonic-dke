# esphome-panasonic-dke

ESPHome external component for **Panasonic air conditioners that use the "DKE" infrared protocol** (built and
tested on a **CS-E24GKR**), with everything the original remote can do, and it **reads the remote too**, so
Home Assistant stays in step when someone uses it.

ESPHome has no native support for this unit, and the usual alternative (the HeatpumpIR library) can only send
and has no Powerful, Quiet, e-ion or Patrol.

## What you get in Home Assistant

One climate entity:

| Feature | How it shows up |
|---|---|
| Mode: auto, cool, heat, dry, off | `hvac_mode` |
| Target temperature 16–30 °C | `temperature` |
| Fan: auto and speeds 1–5 | `fan_mode` |
| Swing: off / vertical / horizontal / both | `swing_mode` |
| Powerful, Quiet | `preset_mode` (none, Powerful, Quiet) |

and three optional selects:

| Select | Options |
|---|---|
| Vertical louvre | Auto, 1 (top) … 5 (bottom) |
| Horizontal louvre | Auto, Left, Centre-left, Centre, Centre-right, Right |
| Air purifier | Off, e-ion, Patrol |

Swing and the louvre selects are the same setting seen two ways: "Auto" on a select is swing on for that axis,
and turning swing off puts the louvre back where it was.

Timers are not implemented (Home Assistant automations do that job better).

## Hardware

An ESP8266/ESP32 board with an IR LED (via a transistor if you want range) and, to follow the remote, an IR
receiver module such as a VS1838B. The reference build is an **ESP-01M** (ESP8285, 1 MB): LED on GPIO4, receiver
on GPIO14. The firmware is about 470 KB and OTA updates need that much free again, so on a 1 MB chip keep the logger
at `INFO`.

## Use

See [`example.yaml`](example.yaml). Minimum:

```yaml
external_components:
  - source: github://scottbambrick/esphome-panasonic-dke
    components: [panasonic_dke]

remote_transmitter:
  pin: GPIO4
  carrier_duty_percent: 50%

remote_receiver:       # optional: lets Home Assistant follow the original remote
  id: ir_receiver
  pin: { number: GPIO14, inverted: true, mode: { input: true, pullup: true } }
  tolerance: 25%
  buffer_size: 4kb
  idle: 20ms
  filter: 100us

climate:
  - platform: panasonic_dke
    name: "Air Conditioner"
    receiver_id: ir_receiver
    sensor: room_temperature     # optional: any temperature sensor
    vertical_swing: { name: "Vertical louvre" }
    horizontal_swing: { name: "Horizontal louvre" }
    air_purifier: { name: "Air purifier" }
```

`self_test: true` runs the protocol checks on the chip 20 seconds after boot and logs one `SELF-TEST` line.

## Things to know

- **Powerful, Quiet, e-ion and Patrol are toggles with no feedback.** The remote sends the same code to turn
  each on or off and the unit never reports which, so the component keeps track itself, and follows the
  original remote's presses when a receiver is fitted. Switching the unit on resets them (Powerful and Quiet
  off, Patrol on), which the component mirrors. They are ignored while the unit is off. If the unit gets out of
  step with Home Assistant (e.g. a press the receiver missed), toggle the setting once from Home Assistant.
- The unit has no fan-only mode on this model, so the component doesn't offer one.
- The component ignores IR for 0.7 s after it transmits, so the receiver doesn't read its own LED.
- Frames from the remote that fail the checksum or length checks are dropped.

The protocol is documented in [`docs/protocol.md`](docs/protocol.md). The encoder/decoder
(`panasonic_dke_codec.h`) has no ESPHome dependencies, and `tests/codec_test.cpp` checks it against frames
captured from a real remote; CI runs that and compiles the example.

To avoid `main` changing underneath you, pin to a release:
`source: github://scottbambrick/esphome-panasonic-dke@v0.1.0`.

## Other models

Other Panasonic units with the same remote family probably work; the louvre and fan values are the likely
differences. Corrections welcome.
