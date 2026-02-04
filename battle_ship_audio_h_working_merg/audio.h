#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "driver/i2s.h"

// ===================== AUDIO CONFIG =====================
static const int I2S_BCLK = 12;
static const int I2S_LRC  = 13;
static const int I2S_DOUT = 14;

static const int AUDIO_VOL_POT_PIN = 5;

// ===================== WAV PARSING =====================
struct WavInfo {
  uint32_t sampleRate = 0;
  uint16_t numChannels = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
};

static inline uint32_t readLE32(File &f) {
  uint8_t b[4];
  if (f.read(b, 4) != 4) return 0;
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static inline uint16_t readLE16(File &f) {
  uint8_t b[2];
  if (f.read(b, 2) != 2) return 0;
  return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static inline bool parseWav(File &f, WavInfo &w) {
  w = WavInfo{};
  f.seek(0);

  uint8_t id[4];
  if (f.read(id, 4) != 4 || memcmp(id, "RIFF", 4) != 0) return false;
  readLE32(f); // file size
  if (f.read(id, 4) != 4 || memcmp(id, "WAVE", 4) != 0) return false;

  bool gotFmt = false, gotData = false;

  while (f.available()) {
    if (f.read(id, 4) != 4) break;
    uint32_t size = readLE32(f);

    if (memcmp(id, "fmt ", 4) == 0) {
      uint16_t audioFormat = readLE16(f);
      w.numChannels = readLE16(f);
      w.sampleRate = readLE32(f);
      readLE32(f); // byteRate
      readLE16(f); // blockAlign
      w.bitsPerSample = readLE16(f);

      if (size > 16) f.seek(f.position() + (size - 16));

      // PCM, 16-bit, mono only
      if (audioFormat != 1 || w.bitsPerSample != 16 || w.numChannels != 1) return false;
      gotFmt = true;
    }
    else if (memcmp(id, "data", 4) == 0) {
      w.dataOffset = f.position();
      w.dataSize = size;
      gotData = true;
      break;
    }
    else {
      f.seek(f.position() + size);
    }

    if (size & 1) f.seek(f.position() + 1);
  }

  return gotFmt && gotData;
}

// ===================== I2S SETUP =====================
static uint32_t g_audio_rate = 0;

static inline void setupI2S(uint32_t sampleRate) {
  if (g_audio_rate == sampleRate && sampleRate != 0) return;
  g_audio_rate = sampleRate;

  i2s_driver_uninstall(I2S_NUM_0);

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = sampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = I2S_BCLK;
  pins.ws_io_num  = I2S_LRC;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// ===================== PUBLIC API =====================
static inline bool audioBegin(bool formatOnFail = true) {
  pinMode(AUDIO_VOL_POT_PIN, INPUT);
  analogReadResolution(12);

  if (!LittleFS.begin(formatOnFail)) {
    Serial.println("LittleFS mount failed (audio disabled)");
    return false;
  }
  return true;
}

static inline bool audioPlayWav(const char *path) {
  File f = LittleFS.open(path, "r");
  if (!f) { Serial.printf("Missing: %s\n", path); return false; }

  WavInfo w;
  if (!parseWav(f, w)) {
    f.seek(0);
    uint8_t h[4]; f.read(h, 4);
    if (memcmp(h, "RIFF", 4) != 0) Serial.printf("Not RIFF: %s\n", path);
    else Serial.printf("Bad WAV fmt (need mono 16-bit PCM): %s\n", path);
    f.close();
    return false;
  }

  setupI2S(w.sampleRate);
  f.seek(w.dataOffset);

  uint8_t buf[1024];
  int16_t out[512];
  uint32_t left = w.dataSize;

  while (left) {
    float vol = analogRead(AUDIO_VOL_POT_PIN) / 4095.0f;

    int n = f.read(buf, min((uint32_t)sizeof(buf), left));
    if (n <= 0) break;
    left -= n;

    int frames = 0;
    for (int i = 0; i + 1 < n && frames < 256; i += 2) {
      int16_t s = (int16_t)(buf[i] | (buf[i + 1] << 8));
      int32_t v = (int32_t)(s * vol);
      v = constrain(v, -32768, 32767);
      out[frames * 2]     = (int16_t)v; // L
      out[frames * 2 + 1] = (int16_t)v; // R
      frames++;
    }

    size_t written;
    i2s_write(I2S_NUM_0, out, frames * 2 * sizeof(int16_t), &written, portMAX_DELAY);
  }

  f.close();
  return true;
}
