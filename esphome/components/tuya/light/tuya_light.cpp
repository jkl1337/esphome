#include "esphome/core/log.h"
#include "tuya_light.h"

namespace esphome {
namespace tuya {

static const char *TAG = "tuya.light";

void TuyaLight::setup() {
  if (dimmer_id_.has_value()) {
    this->parent_->register_listener(*this->dimmer_id_, [this](TuyaDatapoint datapoint) {
      // Ignore dimmer values received after switch is commanded off.
      // This allows restoring the present brightness on next switch on.
      if (!this->state_->current_values.is_on() || this->state_->has_transformer()) {
        return;
      }

      this->inhibit_next_send_ = true;

      auto lower = std::min(this->min_value_, this->max_value_);
      auto upper = std::max(this->min_value_, this->max_value_);
      auto value = std::min(upper, std::max(lower, static_cast<int32_t>(datapoint.value_uint)));
      float brightness = float(value - this->min_value_) / (this->max_value_ - this->min_value_);
      brightness = powf(brightness, 1.0 / this->state_->get_gamma_correct());

      // Handle case where reported value is <= lower bound but not
      // zero, but we don't want light to appear off by setting
      // brightness = 0.0.
      // This can occur when we sent a value near the lower bound
      // and the returned value is not exactly what we set.
      if (lower > 0 && brightness == 0.0f) {
        brightness = 1.0 / (upper - lower);
      }

      ESP_LOGV(TAG, "Received brightness: %f %d", brightness, value);
      auto call = this->state_->make_call();
      call.set_brightness(brightness);
      call.perform();
    });
  }
  if (switch_id_.has_value()) {
    this->parent_->register_listener(*this->switch_id_, [this](TuyaDatapoint datapoint) {
      if (this->state_->has_transformer()) {
        return;
      }

      this->inhibit_next_send_ = true;
      ESP_LOGV(TAG, "Received switch: %d", datapoint.value_bool);
      auto call = this->state_->make_call();
      call.set_state(datapoint.value_bool);
      call.perform();
    });
  }
  if (min_value_datapoint_id_.has_value()) {
    TuyaDatapoint datapoint{};
    datapoint.id = *this->min_value_datapoint_id_;
    datapoint.type = TuyaDatapointType::INTEGER;
    datapoint.value_int = this->min_value_;
    parent_->set_datapoint_value(datapoint);
  }
}

void TuyaLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya Dimmer:");
  if (this->dimmer_id_.has_value())
    ESP_LOGCONFIG(TAG, "   Dimmer has datapoint ID %u", *this->dimmer_id_);
  if (this->switch_id_.has_value())
    ESP_LOGCONFIG(TAG, "   Switch has datapoint ID %u", *this->switch_id_);
}

light::LightTraits TuyaLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supports_brightness(this->dimmer_id_.has_value());
  return traits;
}

void TuyaLight::setup_state(light::LightState *state) { state_ = state; }

void TuyaLight::write_state(light::LightState *state) {
  if (this->inhibit_next_send_) {
    this->inhibit_next_send_ = false;
    return;
  }

  float brightness;
  state->current_values_as_brightness(&brightness);
  bool is_on = brightness != 0.0f;

  if (this->dimmer_id_.has_value() && (is_on || this->zero_brightness_when_off_)) {
    uint32_t brightness_int = is_on ?
      std::ceil(brightness * (this->max_value_ - this->min_value_) + this->min_value_) : 0;

    ESP_LOGV(TAG, "Setting brightness: %f %d", brightness, brightness_int);
    TuyaDatapoint datapoint{};
    datapoint.id = *this->dimmer_id_;
    datapoint.type = TuyaDatapointType::INTEGER;
    datapoint.value_int = brightness_int;
    parent_->set_datapoint_value(datapoint);
  }

  if (this->switch_id_.has_value()) {
    ESP_LOGV(TAG, "Setting switch: %d", is_on);
    TuyaDatapoint datapoint{};
    datapoint.id = *this->switch_id_;
    datapoint.type = TuyaDatapointType::BOOLEAN;
    datapoint.value_bool = is_on;
    parent_->set_datapoint_value(datapoint);
  }
}

}  // namespace tuya
}  // namespace esphome
