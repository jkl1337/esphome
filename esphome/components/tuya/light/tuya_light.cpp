#include "esphome/core/log.h"
#include "tuya_light.h"

namespace esphome {
namespace tuya {

static const char *const TAG = "tuya.light";

static const float MIN_INPUT_BRI = 3/255.0;

void TuyaLight::setup() {
  if (this->color_temperature_id_.has_value()) {
    this->parent_->register_listener(*this->color_temperature_id_, [this](const TuyaDatapoint &datapoint) {
      auto call = this->state_->make_call();
      call.set_color_temperature(this->cold_white_temperature_ +
                                 (this->warm_white_temperature_ - this->cold_white_temperature_) *
                                     (float(datapoint.value_uint) / float(this->color_temperature_max_value_)));
      call.perform();
    });
  }
  if (this->dimmer_id_.has_value()) {
    this->parent_->register_listener(*this->dimmer_id_, [this](const TuyaDatapoint &datapoint) {
      auto call = this->state_->make_call();
      auto val = std::max(this->min_value_, datapoint.value_uint) - this->min_value_;
      call.set_brightness(val * (1.0f - MIN_INPUT_BRI) / (this->max_value_ - this->min_value_) + MIN_INPUT_BRI);
      call.perform();
    });
  }
  if (switch_id_.has_value()) {
    this->parent_->register_listener(*this->switch_id_, [this](const TuyaDatapoint &datapoint) {
      auto call = this->state_->make_call();
      call.set_state(datapoint.value_bool);
      call.perform();
    });
  }
  if (min_value_datapoint_id_.has_value()) {
    parent_->set_integer_datapoint_value(*this->min_value_datapoint_id_, this->min_value_);
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
  if (this->color_temperature_id_.has_value() && this->dimmer_id_.has_value()) {
    traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
    traits.set_min_mireds(this->cold_white_temperature_);
    traits.set_max_mireds(this->warm_white_temperature_);
  } else if (this->dimmer_id_.has_value()) {
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
  } else {
    traits.set_supported_color_modes({light::ColorMode::ON_OFF});
  }
  return traits;
}

void TuyaLight::setup_state(light::LightState *state) { state_ = state; }

void TuyaLight::write_state(light::LightState *state) {
  float brightness;
  state->current_values_as_brightness(&brightness);

  if (brightness == 0.0f) {
    // turning off, first try via switch (if exists), then dimmer
    if (switch_id_.has_value()) {
      parent_->set_boolean_datapoint_value(*this->switch_id_, false);
    } else if (dimmer_id_.has_value()) {
      parent_->set_integer_datapoint_value(*this->dimmer_id_, 0);
    }
    return;
  }

  if (this->color_temperature_id_.has_value()) {
    uint32_t color_temp_int =
        static_cast<uint32_t>(this->color_temperature_max_value_ *
                              (state->current_values.get_color_temperature() - this->cold_white_temperature_) /
                              (this->warm_white_temperature_ - this->cold_white_temperature_));
    parent_->set_integer_datapoint_value(*this->color_temperature_id_, color_temp_int);
  }

  brightness = std::max(0.0f, brightness - MIN_INPUT_BRI);
  auto brightness_int = uint32_t(std::round(brightness * (this->max_value_ - this->min_value_) /
                                            (1.0f - MIN_INPUT_BRI))) + this->min_value_;

  if (this->dimmer_id_.has_value()) {
    parent_->set_integer_datapoint_value(*this->dimmer_id_, brightness_int);
  }
  if (this->switch_id_.has_value()) {
    parent_->set_boolean_datapoint_value(*this->switch_id_, true);
  }
}

}  // namespace tuya
}  // namespace esphome
