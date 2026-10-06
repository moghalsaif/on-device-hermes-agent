// The shared I2C bus, and audio through codec chips: an ES8311 DAC or AW88298
// speaker amplifier and an ES7210 ADC for the microphones, on one duplex I2S
// bus (esp_codec_dev does the codec register work).
#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include <cstring>

#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"

namespace hgp {
namespace {

const char* TAG = "hg.codec";
constexpr size_t kMicChunk = 320;             // 20 ms at 16 kHz
constexpr size_t kSpeakerBuffer = 48 * 1024;  // ~1.5 s at 16 kHz; the server paces 0.5 s ahead
constexpr size_t kSpeakerChunk = 512;         // samples per codec write
constexpr uint32_t kSpeakerTaskStack = 8192;  // stereo staging plus esp_codec_dev's write path
constexpr uint8_t kMic1And2 = 0x03;           // ES7210 inputs MIC1 | MIC2 (ES7120_SEL_MIC1 | ES7120_SEL_MIC2)

}  // namespace

// --------------------------------------------------------------------------
// I2C

namespace i2c {
i2c_master_bus_handle_t bus(const I2cBusConfig& cfg) {
  static i2c_master_bus_handle_t handle = nullptr;
  if (handle || cfg.sda < 0 || cfg.scl < 0) return handle;
  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = I2C_NUM_0;
  bus_cfg.sda_io_num = static_cast<gpio_num_t>(cfg.sda);
  bus_cfg.scl_io_num = static_cast<gpio_num_t>(cfg.scl);
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = cfg.internal_pullup;
  if (i2c_new_master_bus(&bus_cfg, &handle) != ESP_OK) {
    ESP_LOGE(TAG, "I2C bus on SDA %d / SCL %d failed", cfg.sda, cfg.scl);
    handle = nullptr;
  }
  return handle;
}
}  // namespace i2c

// --------------------------------------------------------------------------
// Codecs

bool CodecAudio::begin(const CodecAudioConfig& cfg, i2c_master_bus_handle_t bus,
                       const audio_codec_gpio_if_t* board_gpio_if) {
  if (!bus) return false;
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  chan.auto_clear = true;  // silence on underrun instead of repeating the last buffer
  if (i2s_new_channel(&chan, &tx_, &rx_) != ESP_OK) return false;
  i2s_std_config_t std_cfg = {};
  std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRate);  // MCLK = 256 x fs
  std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_16BIT, cfg.duplex_es8311 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO);
  std_cfg.gpio_cfg.mclk = static_cast<gpio_num_t>(cfg.mclk);
  std_cfg.gpio_cfg.bclk = static_cast<gpio_num_t>(cfg.bclk);
  std_cfg.gpio_cfg.ws = static_cast<gpio_num_t>(cfg.ws);
  std_cfg.gpio_cfg.dout = static_cast<gpio_num_t>(cfg.dout);
  std_cfg.gpio_cfg.din = static_cast<gpio_num_t>(cfg.din);
  if (i2s_channel_init_std_mode(tx_, &std_cfg) != ESP_OK || i2s_channel_init_std_mode(rx_, &std_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "I2S setup failed");
    return false;
  }
  i2s_channel_enable(tx_);
  i2s_channel_enable(rx_);

  audio_codec_i2s_cfg_t i2s_cfg = {};
  i2s_cfg.port = I2S_NUM_1;
  i2s_cfg.rx_handle = rx_;
  i2s_cfg.tx_handle = tx_;
  const audio_codec_data_if_t* data_if = audio_codec_new_i2s_data(&i2s_cfg);
  const audio_codec_gpio_if_t* gpio_if = board_gpio_if ? board_gpio_if : audio_codec_new_gpio();

  audio_codec_i2c_cfg_t dac_i2c = {};
  dac_i2c.port = I2C_NUM_0;
  dac_i2c.addr = cfg.speaker == SpeakerCodec::Aw88298 ? AW88298_CODEC_DEFAULT_ADDR : ES8311_CODEC_DEFAULT_ADDR;
  dac_i2c.bus_handle = bus;
  es8311_codec_cfg_t dac = {};
  dac.ctrl_if = audio_codec_new_i2c_ctrl(&dac_i2c);
  dac.gpio_if = gpio_if;
  dac.codec_mode = cfg.duplex_es8311 ? ESP_CODEC_DEV_WORK_MODE_BOTH : ESP_CODEC_DEV_WORK_MODE_DAC;
  dac.pa_pin = static_cast<int16_t>(cfg.pa);
  dac.pa_reverted = false;
  dac.master_mode = false;
  dac.use_mclk = true;
  dac.hw_gain.pa_voltage = cfg.amp_supply_v;
  dac.hw_gain.codec_dac_voltage = 3.3;
  const audio_codec_if_t* speaker_codec = nullptr;
  if (cfg.speaker == SpeakerCodec::Aw88298) {
    aw88298_codec_cfg_t amp = {};
    amp.ctrl_if = dac.ctrl_if;
    amp.gpio_if = gpio_if;
    amp.hw_gain.pa_gain = 15;
    speaker_codec = aw88298_codec_new(&amp);
  } else {
    speaker_codec = es8311_codec_new(&dac);
  }
  esp_codec_dev_cfg_t out_cfg = {};
  out_cfg.dev_type = ESP_CODEC_DEV_TYPE_OUT;
  out_cfg.codec_if = speaker_codec;
  out_cfg.data_if = data_if;
  out_ = out_cfg.codec_if ? esp_codec_dev_new(&out_cfg) : nullptr;

  esp_codec_dev_cfg_t in_cfg = {};
  in_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
  if (cfg.duplex_es8311) {
    in_cfg.codec_if = speaker_codec;
  } else {
    audio_codec_i2c_cfg_t adc_i2c = {};
    adc_i2c.port = I2C_NUM_0;
    adc_i2c.addr = ES7210_CODEC_DEFAULT_ADDR;
    adc_i2c.bus_handle = bus;
    es7210_codec_cfg_t adc = {};
    adc.ctrl_if = audio_codec_new_i2c_ctrl(&adc_i2c);
    adc.mic_selected = kMic1And2;
    in_cfg.codec_if = es7210_codec_new(&adc);
  }
  in_cfg.data_if = data_if;
  in_ = in_cfg.codec_if ? esp_codec_dev_new(&in_cfg) : nullptr;

  // Both stay open at one rate: they share the I2S clocks.
  esp_codec_dev_sample_info_t fs = {};
  fs.sample_rate = kRate;
  fs.channel = cfg.duplex_es8311 ? 2 : 1;
  fs.bits_per_sample = 16;
  if (out_ && esp_codec_dev_open(out_, &fs) != ESP_CODEC_DEV_OK) out_ = nullptr;
  if (in_ && esp_codec_dev_open(in_, &fs) != ESP_CODEC_DEV_OK) in_ = nullptr;
  if (out_) {
    esp_codec_dev_set_out_vol(out_, 70);
    esp_codec_dev_set_out_mute(out_, true);  // unmuted while something plays
  }
  if (in_) esp_codec_dev_set_in_gain(in_, cfg.mic_gain_db);
  ESP_LOGI(TAG, "codecs: speaker %s, microphones %s", out_ ? "ready" : "missing", in_ ? "ready" : "missing");
  return out_ || in_;
}

// --------------------------------------------------------------------------
// Microphone

bool CodecMic::begin(esp_codec_dev_handle_t dev, bool stereo) {
  if (!dev) return false;
  dev_ = dev;
  stereo_ = stereo;
  xTaskCreate(&CodecMic::task, "hg-mic", 4096, this, 6, nullptr);
  return true;
}

bool CodecMic::start(uint32_t sample_rate) {
  if (!dev_) return false;
  if (sample_rate != CodecAudio::kRate) {
    ESP_LOGW(TAG, "microphone runs at %u Hz only", static_cast<unsigned>(CodecAudio::kRate));
    return false;
  }
  capturing_ = true;
  return true;
}

void CodecMic::task(void* arg) {
  auto* self = static_cast<CodecMic*>(arg);
  int16_t pcm[kMicChunk];
  int16_t stereo[kMicChunk * 2];
  for (;;) {
    // Read continuously so a capture starts with fresh samples, not a stale DMA backlog.
    void* data = self->stereo_ ? static_cast<void*>(stereo) : static_cast<void*>(pcm);
    const int bytes = self->stereo_ ? sizeof(stereo) : sizeof(pcm);
    if (esp_codec_dev_read(self->dev_, data, bytes) != ESP_CODEC_DEV_OK) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (self->stereo_) {
      // The StickS3's single microphone is wired to the left I2S slot.
      for (size_t i = 0; i < kMicChunk; ++i) pcm[i] = stereo[2 * i];
    }
    if (self->capturing_) events::post(EventType::Mic, pcm, sizeof(pcm));
  }
}

// --------------------------------------------------------------------------
// Speaker

bool CodecSpeaker::begin(esp_codec_dev_handle_t dev, bool stereo) {
  if (!dev) return false;
  dev_ = dev;
  stereo_ = stereo;
  uint8_t* storage = static_cast<uint8_t*>(heap_caps_malloc(kSpeakerBuffer + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  static StaticStreamBuffer_t control;
  if (storage) buffer_ = xStreamBufferCreateStatic(kSpeakerBuffer, 1, storage, &control);
  else buffer_ = xStreamBufferCreate(16 * 1024, 1);
  if (!buffer_) return false;
  xTaskCreate(&CodecSpeaker::task, "hg-spk", kSpeakerTaskStack, this, 7, nullptr);
  return true;
}

bool CodecSpeaker::begin(uint32_t sample_rate) {
  if (!dev_) return false;
  if (sample_rate != CodecAudio::kRate) {
    // The host resamples to the rate the device declares, so this is not expected.
    ESP_LOGW(TAG, "speaker runs at %u Hz only", static_cast<unsigned>(CodecAudio::kRate));
    return false;
  }
  abort();
  open_ = true;
  draining_ = false;
  return true;
}

void CodecSpeaker::write(const int16_t* samples, size_t count) {
  if (!open_) return;
  size_t bytes = count * sizeof(int16_t);
  size_t sent = xStreamBufferSend(buffer_, samples, bytes, 0);
  if (sent < bytes) ESP_LOGW(TAG, "playback buffer full, dropped %u bytes", static_cast<unsigned>(bytes - sent));
}

void CodecSpeaker::end() {
  open_ = false;
  draining_ = true;
}

void CodecSpeaker::abort() {
  open_ = false;
  draining_ = false;
  flush_ = true;
}

bool CodecSpeaker::busy() const {
  return open_ || draining_ || (buffer_ && xStreamBufferBytesAvailable(buffer_) > 0);
}

void CodecSpeaker::set_volume(uint8_t percent) {
  if (dev_) esp_codec_dev_set_out_vol(dev_, percent > 100 ? 100 : percent);
}

void CodecSpeaker::task(void* arg) {
  auto* self = static_cast<CodecSpeaker*>(arg);
  int16_t chunk[kSpeakerChunk];
  int16_t stereo[kSpeakerChunk * 2];
  bool playing = false;
  for (;;) {
    if (self->flush_.exchange(false)) xStreamBufferReset(self->buffer_);
    size_t got = xStreamBufferReceive(self->buffer_, chunk, sizeof(chunk), pdMS_TO_TICKS(20));
    if (got == 0) {
      if (playing && !self->open_) {
        // Everything queued has been written out: mute so the amplifier stays quiet.
        self->draining_ = false;
        esp_codec_dev_set_out_mute(self->dev_, true);
        playing = false;
      }
      continue;
    }
    if (!playing) {
      esp_codec_dev_set_out_mute(self->dev_, false);
      playing = true;
    }
    if (self->stereo_) {
      const size_t samples = (got & ~static_cast<size_t>(1)) / sizeof(int16_t);
      for (size_t i = 0; i < samples; ++i) stereo[2 * i] = stereo[2 * i + 1] = chunk[i];
      esp_codec_dev_write(self->dev_, stereo, static_cast<int>(samples * 2 * sizeof(int16_t)));
    } else {
      esp_codec_dev_write(self->dev_, chunk, static_cast<int>(got & ~static_cast<size_t>(1)));
    }
  }
}

}  // namespace hgp
