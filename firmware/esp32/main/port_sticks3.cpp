// StickS3 K150 hardware facts follow M5Stack's v0.6 schematic. The M5PM1
// register behavior was verified against the working local Muse StickS3 port.
#include "port.hpp"

#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/task.h"

namespace hgp {

namespace {

constexpr char TAG[] = "hg.sticks3";

constexpr uint8_t PMIC_ADDR = 0x6E;

constexpr uint8_t PMIC_PWR_CFG  = 0x06;
constexpr uint8_t PMIC_GPIO_DIR = 0x10;
constexpr uint8_t PMIC_GPIO_OUT = 0x11;
constexpr uint8_t PMIC_GPIO_OD  = 0x13;

constexpr uint8_t PMIC_PWR_5V_OUT = 0x08;
constexpr uint8_t PMIC_PWR_LED    = 0x10;

constexpr uint8_t PMIC_L3B = (1 << 2);  // LCD + audio rail
constexpr uint8_t PMIC_SPK = (1 << 3);  // AW8737 amplifier enable

StickS3Board* active_board = nullptr;

}  // namespace

bool StickS3Board::read(uint8_t reg, uint8_t& value) {
  if (!pmic_) return false;
  esp_err_t err = ESP_FAIL;
  for (int attempt = 0; attempt < 3 && err != ESP_OK; ++attempt)
    err = i2c_master_transmit_receive(pmic_, &reg, 1, &value, 1, 50);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "failed reading M5PM1 register 0x%02x", reg);
    return false;
  }
  return true;
}

bool StickS3Board::update(uint8_t reg, uint8_t mask, bool enabled) {
  uint8_t value = 0;
  if (!read(reg, value)) return false;

  const uint8_t current = value;
  if (enabled)
    value |= mask;
  else
    value &= static_cast<uint8_t>(~mask);
  if (value == current) return true;

  const uint8_t data[] = {reg, value};

  esp_err_t err = ESP_FAIL;
  for (int attempt = 0; attempt < 3 && err != ESP_OK; ++attempt)
    err = i2c_master_transmit(pmic_, data, sizeof(data), 50);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "failed writing M5PM1 register 0x%02x", reg);
    return false;
  }

  return true;
}

bool StickS3Board::begin(i2c_master_bus_handle_t bus) {
  if (!bus) return false;

  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = PMIC_ADDR;
  cfg.scl_speed_hz = 100000;

  if (i2c_master_bus_add_device(bus, &cfg, &pmic_) != ESP_OK) {
    ESP_LOGE(TAG, "failed to attach M5PM1");
    return false;
  }
  active_board = this;

  // L3B and SPK are push-pull outputs.
  if (!update(PMIC_GPIO_OD, PMIC_L3B | PMIC_SPK, false)) return false;
  if (!update(PMIC_GPIO_DIR, PMIC_L3B | PMIC_SPK, true)) return false;

  // Keep the speaker amplifier muted during boot.
  if (!update(PMIC_GPIO_OUT, PMIC_SPK, false)) return false;

  // Power the LCD and ES8311 audio rail.
  if (!update(PMIC_GPIO_OUT, PMIC_L3B, true)) return false;
  vTaskDelay(pdMS_TO_TICKS(20));

  // Match the known-working StickS3 power configuration.
  if (!update(PMIC_PWR_CFG, PMIC_PWR_5V_OUT, false)) return false;
  if (!update(PMIC_PWR_CFG, PMIC_PWR_LED, false)) return false;

  ESP_LOGI(TAG, "M5PM1 ready; LCD/audio rail enabled");
  return true;
}

const audio_codec_gpio_if_t* StickS3Board::audio_gpio() {
  static const audio_codec_gpio_if_t gpio = {
      .setup = &StickS3Board::gpio_setup,
      .set = &StickS3Board::gpio_set,
      .get = &StickS3Board::gpio_get,
  };
  return &gpio;
}

int StickS3Board::gpio_setup(int16_t, audio_gpio_dir_t, audio_gpio_mode_t) {
  return ESP_CODEC_DEV_OK;
}

int StickS3Board::gpio_set(int16_t, bool high) {
  return active_board && active_board->set_speaker_amp(high)
             ? ESP_CODEC_DEV_OK
             : ESP_CODEC_DEV_WRITE_FAIL;
}

bool StickS3Board::gpio_get(int16_t) {
  uint8_t value = 0;
  return active_board && active_board->read(PMIC_GPIO_OUT, value) && (value & PMIC_SPK);
}

bool StickS3Board::set_speaker_amp(bool enabled) {
  if (!update(PMIC_GPIO_OUT, PMIC_SPK, enabled)) {
    ESP_LOGE(TAG, "failed to %s speaker amplifier",
             enabled ? "enable" : "disable");
    return false;
  }

  ESP_LOGI(TAG, "speaker amplifier %s", enabled ? "enabled" : "disabled");
  return true;
}

}  // namespace hgp
