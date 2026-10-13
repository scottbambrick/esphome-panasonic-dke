#include "panasonic_dke.h"
#include <cmath>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef PANASONIC_DKE_SELF_TEST
#include "panasonic_dke_selftest.h"
#endif

namespace esphome::panasonic_dke {

static const char *const TAG = "panasonic_dke";
static const uint32_t SELF_ECHO_MS = 700;  // the receiver sits next to the LED: ignore our own bursts

// Select option index -> horizontal position (index 0 is Auto)
static const uint8_t HS_BY_INDEX[6] = {HS_AUTO, HS_LEFT, HS_MLEFT, HS_CENTRE, HS_MRIGHT, HS_RIGHT};
static const char *const BOOST_NAMES[2] = {"Powerful", "Quiet"};
static const char *const FAN_NAMES[5] = {"1", "2", "3", "4", "5"};

static uint8_t hs_to_index(uint8_t hs) {
  for (uint8_t i = 0; i < 6; i++)
    if (HS_BY_INDEX[i] == hs) return i;
  return 3;
}

void PanasonicDKESelect::control(size_t index) { this->parent_->on_select(this, index); }

PanasonicDKEClimate::PanasonicDKEClimate()
    : climate_ir::ClimateIR(16.0f, 30.0f, 1.0f, true, false, {climate::CLIMATE_FAN_AUTO},
                            {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL,
                             climate::CLIMATE_SWING_HORIZONTAL, climate::CLIMATE_SWING_BOTH}) {
  this->set_supported_custom_fan_modes({FAN_NAMES[0], FAN_NAMES[1], FAN_NAMES[2], FAN_NAMES[3], FAN_NAMES[4]});
  this->set_supported_custom_presets({BOOST_NAMES[0], BOOST_NAMES[1]});
}

void PanasonicDKEClimate::setup() {
  climate_ir::ClimateIR::setup();
  if (!this->has_custom_fan_mode() && !this->fan_mode.has_value()) this->fan_mode = climate::CLIMATE_FAN_AUTO;
  this->publish_extras_();
#ifdef PANASONIC_DKE_SELF_TEST
  this->set_timeout(20000, [this]() {  // after the log connection is up
    auto r = run_self_test([](const char *what, const char *detail) { ESP_LOGE(TAG, "self-test FAIL %s %s", what, detail); });
    ESP_LOGW(TAG, "SELF-TEST: %d checks, %d failed; %d state round-trips, %d failed", r.checks, r.failed, r.combos,
             r.combos_failed);
  });
#endif
}

void PanasonicDKEClimate::dump_config() {
  climate_ir::ClimateIR::dump_config();
  ESP_LOGCONFIG(TAG, "  Vertical louvre: %u, horizontal: 0x%02X", this->vswing_, this->hswing_);
}

// ---- state kept in step with the AC -------------------------------------------

void PanasonicDKEClimate::set_vswing_(uint8_t v) {
  this->vswing_ = v;
  if (v != VS_AUTO) this->last_fixed_v_ = v;
}

void PanasonicDKEClimate::set_hswing_(uint8_t h) {
  this->hswing_ = h;
  if (h != HS_AUTO) this->last_fixed_h_ = h;
}

void PanasonicDKEClimate::apply_swing_(climate::ClimateSwingMode mode) {
  bool v = mode == climate::CLIMATE_SWING_VERTICAL || mode == climate::CLIMATE_SWING_BOTH;
  bool h = mode == climate::CLIMATE_SWING_HORIZONTAL || mode == climate::CLIMATE_SWING_BOTH;
  this->set_vswing_(v ? VS_AUTO : this->last_fixed_v_);
  this->set_hswing_(h ? HS_AUTO : this->last_fixed_h_);
}

/// Push the louvres, air purifier and Powerful/Quiet to their entities.
void PanasonicDKEClimate::publish_extras_() {
  bool v = this->vswing_ == VS_AUTO, h = this->hswing_ == HS_AUTO;
  this->swing_mode = v ? (h ? climate::CLIMATE_SWING_BOTH : climate::CLIMATE_SWING_VERTICAL)
                       : (h ? climate::CLIMATE_SWING_HORIZONTAL : climate::CLIMATE_SWING_OFF);
  if (this->boost_ == 0) {
    this->clear_custom_preset_();
    this->preset = climate::CLIMATE_PRESET_NONE;
  } else {
    this->set_custom_preset_(BOOST_NAMES[this->boost_ - 1]);
  }
  if (this->vertical_select_ != nullptr) this->vertical_select_->publish_state(v ? 0 : this->vswing_);
  if (this->horizontal_select_ != nullptr) this->horizontal_select_->publish_state(hs_to_index(this->hswing_));
  if (this->air_select_ != nullptr) this->air_select_->publish_state(this->air_);
}

// ---- transmit -------------------------------------------------------------

void PanasonicDKEClimate::send_bytes_(const uint8_t *bytes, size_t len) {
  auto call = this->transmitter_->transmit();
  auto *data = call.get_data();
  data->set_carrier_frequency(38000);
  data->reserve(len * 16 + 40);
  build_burst(bytes, len, [data](int mark, int space) {
    data->mark(mark);
    if (space) data->space(space);
  });
  call.perform();
  this->last_tx_ms_ = millis();
  this->sent_once_ = true;
}

void PanasonicDKEClimate::transmit_state() {
  State s;
  s.power = this->mode != climate::CLIMATE_MODE_OFF;
  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL: s.mode = MODE_COOL; break;
    case climate::CLIMATE_MODE_HEAT: s.mode = MODE_HEAT; break;
    case climate::CLIMATE_MODE_DRY: s.mode = MODE_DRY; break;
    case climate::CLIMATE_MODE_OFF: s.mode = this->last_mode_; break;
    default: s.mode = MODE_AUTO; break;
  }
  if (s.power) this->last_mode_ = s.mode;
  s.temp = (uint8_t) clamp(lroundf(this->target_temperature), 16L, 30L);
  if (this->has_custom_fan_mode()) {
    char c = this->get_custom_fan_mode().c_str()[0];
    if (c >= '1' && c <= '5') s.fan = c - '0';
  }
  s.vswing = this->vswing_;
  s.hswing = this->hswing_;
  uint8_t bytes[STATE_LEN];
  encode_state(s, bytes);
  this->send_bytes_(bytes, STATE_LEN);
}

void PanasonicDKEClimate::send_command_(Command command) {
  uint8_t bytes[COMMAND_LEN];
  encode_command(command, bytes);
  this->send_bytes_(bytes, COMMAND_LEN);
}

void PanasonicDKEClimate::set_exclusive_(uint8_t *current, uint8_t target, Command first, Command second) {
  if (this->mode == climate::CLIMATE_MODE_OFF) {
    ESP_LOGW(TAG, "AC is off - it resets this setting on power-on, not changing it");
    return;
  }
  if (target == *current) return;
  // Pressing the active button turns it off; pressing the other one switches over.
  bool press_first = target == 0 ? *current == 1 : target == 1;
  this->send_command_(press_first ? first : second);
  *current = target;
}

void PanasonicDKEClimate::control(const climate::ClimateCall &call) {
  bool was_on = this->mode != climate::CLIMATE_MODE_OFF;
  bool send = false;  // anything the state frame carries changed
  if (call.get_mode().has_value()) this->mode = *call.get_mode(), send = true;
  if (call.get_target_temperature().has_value()) this->target_temperature = *call.get_target_temperature(), send = true;
  if (call.has_custom_fan_mode()) {
    this->set_custom_fan_mode_(call.get_custom_fan_mode());
    send = true;
  } else if (call.get_fan_mode().has_value()) {
    this->set_fan_mode_(*call.get_fan_mode());
    send = true;
  }
  if (call.get_swing_mode().has_value()) this->apply_swing_(*call.get_swing_mode()), send = true;
  if (!was_on && this->mode != climate::CLIMATE_MODE_OFF) {  // the AC resets its toggles on power-on; Patrol comes on
    this->air_ = 2;
    this->boost_ = 0;
  }
  if (send) this->transmit_state();
  if (call.has_custom_preset() || call.get_preset().has_value()) {
    uint8_t target = call.has_custom_preset() ? (call.get_custom_preset().c_str()[0] == 'P' ? 1 : 2) : 0;
    this->set_exclusive_(&this->boost_, target, CMD_POWERFUL, CMD_QUIET);
  }
  this->publish_extras_();
  this->publish_state();
}

void PanasonicDKEClimate::on_select(PanasonicDKESelect *select, size_t index) {
  if (select == this->air_select_) {
    this->set_exclusive_(&this->air_, (uint8_t) index, CMD_EION, CMD_PATROL);
  } else {
    if (select == this->vertical_select_)
      this->set_vswing_(index == 0 ? VS_AUTO : (uint8_t) index);
    else
      this->set_hswing_(HS_BY_INDEX[index % 6]);
    this->transmit_state();
  }
  this->publish_extras_();
  this->publish_state();
}

// ---- receive (the original remote) ---------------------------------------------

void PanasonicDKEClimate::apply_toggle_(Command command) {
  if (this->mode == climate::CLIMATE_MODE_OFF) return;  // the AC ignores these while off
  switch (command) {
    case CMD_EION: this->air_ = this->air_ == 1 ? 0 : 1; break;
    case CMD_PATROL: this->air_ = this->air_ == 2 ? 0 : 2; break;
    case CMD_POWERFUL: this->boost_ = this->boost_ == 1 ? 0 : 1; break;
    case CMD_QUIET: this->boost_ = this->boost_ == 2 ? 0 : 2; break;
    default: break;
  }
}

bool PanasonicDKEClimate::on_receive(remote_base::RemoteReceiveData data) {
  if (this->sent_once_ && millis() - this->last_tx_ms_ < SELF_ECHO_MS) return false;
  const auto &raw = data.get_raw_data();
  Decoded d;
  if (!decode_burst(raw.data(), raw.size(), d)) return false;

  if (d.kind == KIND_COMMAND) {
    ESP_LOGD(TAG, "Remote: toggle %u", (unsigned) d.command);
    this->apply_toggle_(d.command);
  } else {
    const State &s = d.state;
    if (s.mode == MODE_FAN) return false;  // this AC has no fan-only mode
    bool was_on = this->mode != climate::CLIMATE_MODE_OFF;
    this->last_mode_ = s.mode;
    switch (s.power ? s.mode : 0xFF) {
      case MODE_COOL: this->mode = climate::CLIMATE_MODE_COOL; break;
      case MODE_HEAT: this->mode = climate::CLIMATE_MODE_HEAT; break;
      case MODE_DRY: this->mode = climate::CLIMATE_MODE_DRY; break;
      case 0xFF: this->mode = climate::CLIMATE_MODE_OFF; break;
      default: this->mode = climate::CLIMATE_MODE_HEAT_COOL; break;
    }
    this->target_temperature = s.temp;
    if (s.fan == 0)
      this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
    else
      this->set_custom_fan_mode_(FAN_NAMES[s.fan - 1]);
    this->set_vswing_(s.vswing);
    this->set_hswing_(s.hswing);
    if (s.power && !was_on) {
      this->air_ = 2;
      this->boost_ = 0;
    }
    ESP_LOGD(TAG, "Remote: power=%d mode=0x%02X temp=%u fan=%u v=%u h=0x%02X", s.power, s.mode, s.temp, s.fan, s.vswing,
             s.hswing);
  }
  this->publish_extras_();
  this->publish_state();
  return true;
}

}  // namespace esphome::panasonic_dke
