#pragma once

#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/components/select/select.h"
#include "panasonic_dke_codec.h"

namespace esphome::panasonic_dke {

class PanasonicDKEClimate;

/// One of the extra controls: vertical or horizontal louvre position, or the air purifier.
class PanasonicDKESelect final : public select::Select {
 public:
  void set_parent(PanasonicDKEClimate *parent) { this->parent_ = parent; }

 protected:
  void control(size_t index) override;
  PanasonicDKEClimate *parent_{nullptr};
};

/// Panasonic CS-E24GKR climate. Fan speed, swing and Powerful/Quiet (as presets) live in the
/// climate entity; fixed louvre positions and e-ion/Patrol are selects.
class PanasonicDKEClimate final : public climate_ir::ClimateIR {
 public:
  PanasonicDKEClimate();
  void setup() override;
  void dump_config() override;

  void set_vertical_select(PanasonicDKESelect *s) { this->vertical_select_ = s; }
  void set_horizontal_select(PanasonicDKESelect *s) { this->horizontal_select_ = s; }
  void set_air_select(PanasonicDKESelect *s) { this->air_select_ = s; }

  /// Called by the selects.
  void on_select(PanasonicDKESelect *select, size_t index);

 protected:
  void control(const climate::ClimateCall &call) override;
  void transmit_state() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

  void send_bytes_(const uint8_t *bytes, size_t len);
  void send_command_(Command command);
  void set_vswing_(uint8_t v);
  void set_hswing_(uint8_t h);
  void apply_swing_(climate::ClimateSwingMode mode);
  void publish_extras_();
  /// Press whatever buttons are needed to move a mutually exclusive pair to `target` (0 = off).
  void set_exclusive_(uint8_t *current, uint8_t target, Command first, Command second);
  void apply_toggle_(Command command);

  PanasonicDKESelect *vertical_select_{nullptr};
  PanasonicDKESelect *horizontal_select_{nullptr};
  PanasonicDKESelect *air_select_{nullptr};

  uint8_t vswing_{1};  // 1..5 or VS_AUTO
  uint8_t hswing_{HS_CENTRE};
  uint8_t last_fixed_v_{1};  // where the louvre goes when swing is turned off
  uint8_t last_fixed_h_{HS_CENTRE};
  uint8_t last_mode_{MODE_AUTO};  // mode to send while the climate is "off"
  uint8_t air_{2};    // 0 off, 1 e-ion, 2 Patrol (Patrol is on after power-on)
  uint8_t boost_{0};  // 0 off, 1 Powerful, 2 Quiet
  uint32_t last_tx_ms_{0};
  bool sent_once_{false};
};

}  // namespace esphome::panasonic_dke
