/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/settings/settings.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <mooncake_log.h>
#include <driver/i2s_std.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>

static const std::string_view _tag = "HAL-Audio";

// Speaker volume, 0..100. esp_codec_dev's default curve maps this range onto
// -50..0 dB linearly, so this is an attenuation dial and not a percentage of
// anything: 92 is -4 dB, and the 80 that used to sit here was throwing away a
// full 10 dB. The 4 dB left on the table is headroom for the speaker -- the
// English recordings are peak-normalised to 0.9 full scale, so 100 would put
// their loudest moments right against the rail.
//
// This constant is the only source of truth. There is no volume UI in this
// firmware, but a device that has run the stock M5StopWatch-UserDemo carries
// that firmware's own spk_vol in the shared "system" NVS namespace, and it
// would otherwise silently win over anything set here.
static constexpr int kDefaultSpeakerVolume = 92;

#define I2S_PORT         I2S_NUM_0
#define I2S_MCLK_PIN     (gpio_num_t)18
#define I2S_BCLK_PIN     (gpio_num_t)17
#define I2S_DADC_IN_PIN  (gpio_num_t)16
#define I2S_LRCK_PIN     (gpio_num_t)15
#define I2S_DDAC_OUT_PIN (gpio_num_t)21

static class AudioCodec {
public:
    static constexpr int sample_rate = 44100;

    void init(i2c_master_bus_handle_t i2c_bus)
    {
        _silence_buffer.resize(sample_rate * 0.1);
        _silence_buffer.assign(_silence_buffer.size(), 0);
        xTaskCreate([](void* obj) { static_cast<AudioCodec*>(obj)->_task_entry(); }, "audio_task", 4 * 1024, this, 5,
                    &_task_handle);

        _i2s_init();

        audio_codec_i2s_cfg_t i2s_cfg = {
            .rx_handle = _rx_handle,
            .tx_handle = _tx_handle,
        };
        _data_if = audio_codec_new_i2s_data(&i2s_cfg);

        audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = i2c_bus};
        _ctrl_if                      = audio_codec_new_i2c_ctrl(&i2c_cfg);

        _gpio_if = audio_codec_new_gpio();

        es8311_codec_cfg_t es8311_cfg = {
            .ctrl_if     = _ctrl_if,
            .gpio_if     = _gpio_if,
            .codec_mode  = ESP_CODEC_DEV_WORK_MODE_BOTH,
            .pa_pin      = GPIO_NUM_NC,
            .pa_reverted = false,
            .use_mclk    = true,
        };
        _codec_if = es8311_codec_new(&es8311_cfg);

        esp_codec_dev_cfg_t dev_cfg = {
            .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
            .codec_if = _codec_if,
            .data_if  = _data_if,
        };
        _codec_dev = esp_codec_dev_new(&dev_cfg);

        esp_codec_dev_set_in_gain(_codec_dev, 30.0);

        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel         = 1,
            .sample_rate     = sample_rate,
        };
        esp_codec_dev_open(_codec_dev, &fs);
    }

    void setVolume(int volume)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        esp_codec_dev_set_out_vol(_codec_dev, volume);
    }

    int getVolume()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        int volume = 0;
        esp_codec_dev_get_out_vol(_codec_dev, &volume);
        return volume;
    }

    void setMicGain(float gain)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        esp_codec_dev_set_in_gain(_codec_dev, gain);
    }

    void play(std::vector<int16_t>& data, bool async)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (async) {
            // Support interruption: overwrite data and notify task
            _audio_data = data;
            _is_playing = true;
            xTaskNotifyGive(_task_handle);
        } else {
            if (_is_playing) {
                mclog::tagWarn(_tag, "audio is playing");
                return;
            }
            _write(data);
        }
    }

    void record(std::vector<int16_t>& data, uint16_t durationMs, float gain)
    {
        std::lock_guard<std::mutex> lock(_mutex);

        esp_codec_dev_set_in_gain(_codec_dev, gain);

        size_t sample_count = (size_t)(sample_rate * durationMs / 1000);
        size_t byte_size    = sample_count * sizeof(int16_t);

        data.resize(sample_count);

        esp_err_t ret = esp_codec_dev_read(_codec_dev, data.data(), byte_size);
        if (ret != ESP_OK) {
            mclog::tagError(_tag, "record failed: {}", ret);
            data.clear();
        }
    }

private:
    void _task_entry()
    {
        mclog::tagInfo(_tag, "start audio play task");
        std::vector<int16_t> current_data;

        while (1) {
            // Wait for play request
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            while (true) {
                // Fetch data safely
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    if (_audio_data.empty()) {
                        _is_playing = false;
                        break;
                    }
                    current_data = _audio_data;
                    _audio_data.clear();
                    _is_playing = true;
                }

                if (current_data.empty()) {
                    break;
                }

                size_t offset        = 0;
                size_t total_samples = current_data.size();
                bool interrupted     = false;
                // Chunk size in samples (e.g. 1024 bytes = 512 samples)
                const size_t CHUNK_SAMPLES = 512;

                while (offset < total_samples) {
                    // Check for interruption (new play request)
                    if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
                        // mclog::tagInfo(_tag, "playback interrupted");
                        interrupted = true;
                        break;
                    }

                    size_t remain        = total_samples - offset;
                    size_t write_samples = (remain > CHUNK_SAMPLES) ? CHUNK_SAMPLES : remain;

                    esp_codec_dev_write(_codec_dev, (void*)&current_data[offset], write_samples * sizeof(int16_t));
                    offset += write_samples;
                }

                if (interrupted) {
                    // Stop current playback immediately and flush DMA
                    i2s_channel_disable(_tx_handle);
                    i2s_channel_enable(_tx_handle);
                    continue;
                }

                // Normal finish, play silence to avoid pop/waiting
                esp_codec_dev_write(_codec_dev, (void*)_silence_buffer.data(),
                                    _silence_buffer.size() * sizeof(int16_t));
            }
        }
    }

    void _write(const std::vector<int16_t>& data)
    {
        esp_codec_dev_write(_codec_dev, (void*)data.data(), data.size() * sizeof(int16_t));
        esp_codec_dev_write(_codec_dev, (void*)_silence_buffer.data(), _silence_buffer.size() * sizeof(int16_t));
    }

    void _i2s_init()
    {
        mclog::tagInfo(_tag, "i2s init");

        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
        i2s_std_config_t std_cfg   = {
            .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg =
                {
                    .mclk = I2S_MCLK_PIN,
                    .bclk = I2S_BCLK_PIN,
                    .ws   = I2S_LRCK_PIN,
                    .dout = I2S_DDAC_OUT_PIN,
                    .din  = I2S_DADC_IN_PIN,
                },
        };

        ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &_tx_handle, &_rx_handle));
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(_tx_handle, &std_cfg));
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(_rx_handle, &std_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(_tx_handle));
        ESP_ERROR_CHECK(i2s_channel_enable(_rx_handle));
    }

    i2s_chan_handle_t _tx_handle          = NULL;
    i2s_chan_handle_t _rx_handle          = NULL;
    esp_codec_dev_handle_t _codec_dev     = NULL;
    const audio_codec_data_if_t* _data_if = NULL;
    const audio_codec_ctrl_if_t* _ctrl_if = NULL;
    const audio_codec_gpio_if_t* _gpio_if = NULL;
    const audio_codec_if_t* _codec_if     = NULL;

    TaskHandle_t _task_handle;
    std::mutex _mutex;
    std::vector<int16_t> _audio_data;
    std::vector<int16_t> _silence_buffer;
    bool _is_playing = false;
} _audio_codec;

void Hal::audio_init()
{
    mclog::tagInfo(_tag, "init");

    _audio_codec.init(i2c_bus_get_internal_bus_handle(_i2c_bus));

    ioe_speaker_enable(true);

    // Write back only when the stored value disagrees, so a device carrying a
    // stale volume gets corrected once rather than burning a flash write on
    // every boot (Settings::SetInt marks itself dirty unconditionally and
    // commits from the destructor).
    const bool stored_matches = getSpeakerVolume(true) == kDefaultSpeakerVolume;
    setSpeakerVolume(kDefaultSpeakerVolume, !stored_matches);
}

void Hal::setSpeakerVolume(int volume, bool saveToSettings)
{
    _spk_volume = volume;
    _spk_volume = uitk::clamp(_spk_volume, 0, 100);

    mclog::tagInfo(_tag, "set speaker volume to {}", _spk_volume);
    _audio_codec.setVolume(_spk_volume);

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("spk_vol", _spk_volume);
        mclog::tagInfo(_tag, "volume saved to settings: {}", _spk_volume);
    }
}

int Hal::getSpeakerVolume(bool loadFromSettings)
{
    _spk_volume = _audio_codec.getVolume();

    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _spk_volume = settings.GetInt("spk_vol", kDefaultSpeakerVolume);
        _spk_volume = uitk::clamp(_spk_volume, 0, 100);
        mclog::tagInfo(_tag, "volume loaded from settings: {}", _spk_volume);
    }

    return _spk_volume;
}

void Hal::audioRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain)
{
    _audio_codec.record(data, durationMs, gain);
}

void Hal::audioPlay(std::vector<int16_t>& data, bool async)
{
    _audio_codec.play(data, async);
}

int Hal::getAudioSampleRate()
{
    return _audio_codec.sample_rate;
}
