/**
 * @file Speaker_Class.cpp
 * @brief Speaker playback implementation for MeowKit (ES8311 + NS4150B)
 *
 * Hardware chain: ESP32-S3 I2S TX → ES8311 DAC → NS4150B Class-D Amp → 18mm Speaker
 *
 * I2S uses the legacy driver/i2s.h API (ESP-IDF 4.4 / espressif32@6.0.1).
 * ES8311 codec control via existing es8311 C driver + I2C_Class.
 *
 * Referenced from M5Atomic-EchoBase (m5stack/M5Atomic-EchoBase).
 */

#include "Speaker_Class.hpp"
#include "dac_gain.hpp"
#include <esp_log.h>
#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char* TAG = "SPK";

// ────────────────────────────────────────────────────────────
//  ES8311 Codec init
// ────────────────────────────────────────────────────────────

bool Speaker_Class::_init_codec(uint32_t sample_rate)
{
    // Route es8311 C driver to our I2C_Class instance
    es8311_set_i2c(_i2c);

    _es_handle = es8311_create((i2c_port_t)0, SPK_ES8311_ADDR);
    if (!_es_handle) {
        ESP_LOGE(TAG, "Failed to create ES8311 handle");
        return false;
    }

    // Clock config — MCLK derived from SCLK. The board also routes an MCLK
    // signal, but the existing speaker path deliberately does not use it.
    // When mclk_from_mclk_pin=false, ES8311 derives MCLK from SCLK internally.
    // mclk_frequency is ignored in this mode.
    es8311_clock_config_t es_clk = {
        .mclk_inverted      = false,
        .sclk_inverted      = false,
        .mclk_from_mclk_pin = false,
        .mclk_frequency     = 0,
        .sample_frequency   = (int)sample_rate,
    };

    // Use 16-bit resolution for both input and output serial ports
    esp_err_t err = es8311_init(_es_handle, &es_clk,
                                ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 init failed: %s", esp_err_to_name(err));
        return false;
    }

    // Apply the retained legacy volume limit before the first sample, including
    // callers which have not yet called setVolume().
    if (_volume < 0) _volume = 0;
    if (_volume > SPK_VOLUME_MAX) _volume = SPK_VOLUME_MAX;
    if (es8311_voice_volume_set(_es_handle, _volume, NULL) != ESP_OK) return false;

    // Configure microphone (analog, even if not used for speaker — required by driver)
    es8311_microphone_config(_es_handle, false);

    ESP_LOGI(TAG, "ES8311 codec initialized (rate=%lu)", sample_rate);
    return true;
}

// ────────────────────────────────────────────────────────────
//  I2S TX setup / teardown (legacy driver)
// ────────────────────────────────────────────────────────────

bool Speaker_Class::_init_i2s()
{
    _uninstall_i2s();

    i2s_config_t i2s_cfg = {};
    i2s_cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_cfg.sample_rate          = _cfg.sample_rate;
    i2s_cfg.bits_per_sample      = (_cfg.bits_per_sample == 32)
                                     ? I2S_BITS_PER_SAMPLE_32BIT
                                     : I2S_BITS_PER_SAMPLE_16BIT;
    i2s_cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    i2s_cfg.dma_buf_count        = _cfg.dma_buf_count;
    i2s_cfg.dma_buf_len          = _cfg.dma_buf_len;
    i2s_cfg.use_apll             = false;
    i2s_cfg.tx_desc_auto_clear   = true;   // Auto-clear DMA on underflow (silence)
    i2s_cfg.fixed_mclk           = 0;

    esp_err_t err = i2s_driver_install(_cfg.i2s_port, &i2s_cfg, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    i2s_pin_config_t pin_cfg = {};
    pin_cfg.mck_io_num   = -1;             // No dedicated MCLK for speaker path
    pin_cfg.bck_io_num   = _cfg.pin_bclk;
    pin_cfg.ws_io_num    = _cfg.pin_ws;
    pin_cfg.data_out_num = _cfg.pin_data_out;
    pin_cfg.data_in_num  = I2S_PIN_NO_CHANGE;

    err = i2s_set_pin(_cfg.i2s_port, &pin_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_set_pin failed: %s", esp_err_to_name(err));
        i2s_driver_uninstall(_cfg.i2s_port);
        return false;
    }

    i2s_zero_dma_buffer(_cfg.i2s_port);
    i2s_start(_cfg.i2s_port);
    _i2s_installed = true;

    ESP_LOGI(TAG, "I2S%d TX configured: rate=%lu, DOUT=%d, BCLK=%d, WS=%d",
             _cfg.i2s_port, _cfg.sample_rate,
             _cfg.pin_data_out, _cfg.pin_bclk, _cfg.pin_ws);
    return true;
}

void Speaker_Class::_uninstall_i2s()
{
    if (_i2s_installed) {
        i2s_stop(_cfg.i2s_port);
        i2s_driver_uninstall(_cfg.i2s_port);
        _i2s_installed = false;
    }
}

// ────────────────────────────────────────────────────────────
//  Public API
// ────────────────────────────────────────────────────────────

bool Speaker_Class::begin(I2C_Class* i2c)
{
    end();
    _i2c = i2c;

    // Init codec
    if (!_init_codec(_cfg.sample_rate)) {
        end();
        return false;
    }

    // Init I2S TX
    if (!_init_i2s()) {
        end();
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "Speaker initialized");
    return true;
}

bool Speaker_Class::beginCodecOnly(I2C_Class* i2c)
{
    end();
    _i2c = i2c;
    if (!_init_codec(_cfg.sample_rate)) {
        end();
        return false;
    }
    _initialized = true;
    return true;
}

void Speaker_Class::end()
{
    stop();
    _uninstall_i2s();

    if (_es_handle) {
        es8311_voice_mute(_es_handle, true);
        es8311_delete(_es_handle);
        _es_handle = nullptr;
    }

    _initialized = false;
    ESP_LOGI(TAG, "Speaker de-initialized");
}

// ── Volume & Mute ──

bool Speaker_Class::setVolume(int volume)
{
    if (volume < 0)            volume = 0;
    if (volume > SPK_VOLUME_MAX) volume = SPK_VOLUME_MAX;  /* Retained legacy volume limit */

    if (!_es_handle) return false;

    esp_err_t err = es8311_voice_volume_set(_es_handle, volume, NULL);
    if (err == ESP_OK) {
        _volume = volume;
        return true;
    }
    ESP_LOGE(TAG, "setVolume failed");
    return false;
}

int Speaker_Class::getVolume()
{
    if (!_es_handle) return -1;

    int vol = 0;
    if (es8311_voice_volume_get(_es_handle, &vol) == ESP_OK) {
        _volume = vol;
        return vol;
    }
    return -1;
}

bool Speaker_Class::setMute(bool enable)
{
    if (!_es_handle) return false;
    return (es8311_voice_mute(_es_handle, enable) == ESP_OK);
}

bool Speaker_Class::setPlayerDacAttenuation()
{
    // Explicit opt-in for the external decoder session; legacy speaker callers
    // keep their existing volume behavior and cannot select this mode by accident.
    if (!_initialized || !_es_handle || _i2s_installed) return false;
    if (es8311_voice_gain_set_db(_es_handle, meow::audio::PlayerDacGainDb) != ESP_OK) return false;
    ESP_LOGI(TAG, "Player DAC gain: %d dB (register readback verified)", meow::audio::PlayerDacGainDb);
    return true;
}

// ── I2S write helper ──

bool Speaker_Class::_i2s_write(const void* data, size_t size_bytes)
{
    if (!_i2s_installed) return false;

    size_t bytes_written = 0;
    esp_err_t err = i2s_write(_cfg.i2s_port, data, size_bytes,
                              &bytes_written, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_write failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

// ── Playback ──

bool Speaker_Class::play(const int16_t* data, size_t length, uint32_t sample_rate)
{
    if (!_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    // Reconfigure sample rate if needed
    if (sample_rate > 0 && sample_rate != _cfg.sample_rate) {
        if (!setSampleRate(sample_rate)) {
            return false;
        }
    }

    _is_playing = true;

    // For I2S stereo format, we need L+R interleaved.
    // ES8311 expects stereo I2S (R+L channel format).
    // If input is mono, duplicate each sample to both channels.
    const size_t chunk_samples = _cfg.dma_buf_len;
    int16_t stereo_buf[chunk_samples * 2];  // L+R interleaved

    size_t offset = 0;
    while (offset < length && _is_playing) {
        size_t remaining = length - offset;
        size_t count = (remaining > chunk_samples) ? chunk_samples : remaining;

        // Duplicate mono → stereo
        for (size_t i = 0; i < count; i++) {
            stereo_buf[i * 2]     = data[offset + i];  // L
            stereo_buf[i * 2 + 1] = data[offset + i];  // R
        }

        if (!_i2s_write(stereo_buf, count * 2 * sizeof(int16_t))) {
            _is_playing = false;
            return false;
        }
        offset += count;
    }

    _is_playing = false;
    return true;
}

bool Speaker_Class::play(const uint8_t* data, size_t size)
{
    if (!_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    _is_playing = true;

    // Write raw bytes directly to I2S (caller is responsible for format)
    const size_t chunk_bytes = _cfg.dma_buf_len * sizeof(int16_t) * 2;
    size_t offset = 0;
    while (offset < size && _is_playing) {
        size_t remaining = size - offset;
        size_t count = (remaining > chunk_bytes) ? chunk_bytes : remaining;

        if (!_i2s_write(data + offset, count)) {
            _is_playing = false;
            return false;
        }
        offset += count;
    }

    _is_playing = false;
    return true;
}

void Speaker_Class::tone(uint32_t freq_hz, uint32_t duration_ms, int volume)
{
    if (!_initialized) return;
    if (freq_hz == 0 || duration_ms == 0) return;

    // Temporarily set volume if requested
    int prev_vol = _volume;
    if (volume > 0 && volume != _volume) {
        setVolume(volume);
    }

    _is_playing = true;

    const uint32_t rate = _cfg.sample_rate;
    const uint32_t total_samples = (uint32_t)((uint64_t)rate * duration_ms / 1000);
    const float omega = 2.0f * (float)M_PI * freq_hz / rate;
    const float amplitude = 16000.0f;  // ~50% of int16_t range

    const size_t chunk_samples = _cfg.dma_buf_len;
    int16_t stereo_buf[chunk_samples * 2];

    uint32_t generated = 0;
    while (generated < total_samples && _is_playing) {
        size_t remaining = total_samples - generated;
        size_t count = (remaining > chunk_samples) ? chunk_samples : remaining;

        for (size_t i = 0; i < count; i++) {
            int16_t sample = (int16_t)(amplitude * sinf(omega * (generated + i)));
            stereo_buf[i * 2]     = sample;  // L
            stereo_buf[i * 2 + 1] = sample;  // R
        }

        _i2s_write(stereo_buf, count * 2 * sizeof(int16_t));
        generated += count;
    }

    // Fade out to avoid click: write a short zero buffer
    memset(stereo_buf, 0, chunk_samples * 2 * sizeof(int16_t));
    _i2s_write(stereo_buf, chunk_samples * 2 * sizeof(int16_t));

    _is_playing = false;

    // Restore volume
    if (volume > 0 && prev_vol != volume) {
        setVolume(prev_vol);
    }
}

void Speaker_Class::stop()
{
    _is_playing = false;

    if (_i2s_installed) {
        i2s_zero_dma_buffer(_cfg.i2s_port);
    }
}

bool Speaker_Class::setSampleRate(uint32_t rate)
{
    if (!_initialized) return false;

    // Reconfigure I2S clock
    esp_err_t err = i2s_set_sample_rates(_cfg.i2s_port, rate);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_set_sample_rates failed: %s", esp_err_to_name(err));
        return false;
    }

    // Reconfigure ES8311 clock dividers
    // ES8311 in SCLK-derived mode: mclk = sample_rate * bits * 2
    int mclk = rate * _cfg.bits_per_sample * 2;
    err = es8311_sample_frequency_config(_es_handle, mclk, rate);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "es8311_sample_frequency_config failed: %s", esp_err_to_name(err));
        return false;
    }

    _cfg.sample_rate = rate;
    ESP_LOGI(TAG, "Sample rate => %lu Hz", rate);
    return true;
}
