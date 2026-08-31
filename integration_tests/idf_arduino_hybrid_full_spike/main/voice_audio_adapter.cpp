#include "voice_audio_adapter.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "audio_codec_ctrl_if.h"
#include "display_runtime.h"
#include "driver/i2s_std.h"
#include "es7210_adc.h"
#include "es8311_codec.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_types.h"
#include "esp_log.h"

namespace {

constexpr char kTag[] = "VOICE_AUDIO";
constexpr uint8_t kEs7210Address = ES7210_CODEC_DEFAULT_ADDR >> 1;
constexpr uint8_t kEs8311Address = ES8311_CODEC_DEFAULT_ADDR >> 1;
constexpr uint32_t kCodecI2cTimeoutMs = 100;
constexpr uint32_t kSampleRate = 16000;

struct WireCodecControl {
    audio_codec_ctrl_if_t interface;
    bool open;
    uint8_t address;
};

i2s_chan_handle_t microphoneChannel = nullptr;
static i2s_chan_handle_t speakerChannel = nullptr;
const audio_codec_data_if_t* microphoneDataInterface = nullptr;
const audio_codec_if_t* microphoneCodecInterface = nullptr;
esp_codec_dev_handle_t microphoneDevice = nullptr;
const audio_codec_if_t* speakerCodecInterface = nullptr;
esp_codec_dev_handle_t speakerDevice = nullptr;

int codecControlOpen(const audio_codec_ctrl_if_t* control, void*, int)
{
    if (control == nullptr) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    reinterpret_cast<WireCodecControl*>(const_cast<audio_codec_ctrl_if_t*>(control))->open = true;
    return ESP_CODEC_DEV_OK;
}

bool codecControlIsOpen(const audio_codec_ctrl_if_t* control)
{
    if (control == nullptr) {
        return false;
    }
    return reinterpret_cast<const WireCodecControl*>(control)->open;
}

bool encodeRegister(int reg, int regLength, uint8_t* output)
{
    if (output == nullptr || regLength < 1 || regLength > 2) {
        return false;
    }
    if (regLength == 2) {
        output[0] = static_cast<uint8_t>((reg >> 8) & 0xFF);
        output[1] = static_cast<uint8_t>(reg & 0xFF);
    } else {
        output[0] = static_cast<uint8_t>(reg & 0xFF);
    }
    return true;
}

int codecControlRead(
    const audio_codec_ctrl_if_t* control,
    int reg,
    int regLength,
    void* data,
    int dataLength
)
{
    if (!codecControlIsOpen(control) || data == nullptr || dataLength <= 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    uint8_t registerBytes[2] = {};
    if (!encodeRegister(reg, regLength, registerBytes)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    const uint8_t address = reinterpret_cast<const WireCodecControl*>(control)->address;
    if (display_runtime_shared_i2c_write(
            address, registerBytes, regLength, kCodecI2cTimeoutMs) != ESP_OK) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    return display_runtime_shared_i2c_read(
               address,
               static_cast<uint8_t*>(data),
               static_cast<size_t>(dataLength),
               kCodecI2cTimeoutMs) == ESP_OK
        ? ESP_CODEC_DEV_OK
        : ESP_CODEC_DEV_READ_FAIL;
}

int codecControlWrite(
    const audio_codec_ctrl_if_t* control,
    int reg,
    int regLength,
    void* data,
    int dataLength
)
{
    if (!codecControlIsOpen(control) || data == nullptr || dataLength < 0 ||
        regLength < 1 || regLength > 2 || dataLength > 4) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    uint8_t packet[6] = {};
    if (!encodeRegister(reg, regLength, packet)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dataLength > 0) {
        memcpy(packet + regLength, data, static_cast<size_t>(dataLength));
    }
    const uint8_t address = reinterpret_cast<const WireCodecControl*>(control)->address;
    return display_runtime_shared_i2c_write(
               address,
               packet,
               static_cast<size_t>(regLength + dataLength),
               kCodecI2cTimeoutMs) == ESP_OK
        ? ESP_CODEC_DEV_OK
        : ESP_CODEC_DEV_WRITE_FAIL;
}

int codecControlClose(const audio_codec_ctrl_if_t* control)
{
    if (control == nullptr) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    reinterpret_cast<WireCodecControl*>(const_cast<audio_codec_ctrl_if_t*>(control))->open = false;
    return ESP_CODEC_DEV_OK;
}

WireCodecControl codecControl = {
    {
        codecControlOpen,
        codecControlIsOpen,
        codecControlRead,
        codecControlWrite,
        codecControlClose,
    },
    true,
    kEs7210Address,
};

WireCodecControl speakerCodecControl = {
    {
        codecControlOpen,
        codecControlIsOpen,
        codecControlRead,
        codecControlWrite,
        codecControlClose,
    },
    true,
    kEs8311Address,
};

}  // namespace

esp_codec_dev_handle_t voiceAudioAdapterInit()
{
    if (microphoneDevice != nullptr) {
        return microphoneDevice;
    }

    display_runtime_log_memory("MEM_BEFORE_I2S");
    i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channelConfig.auto_clear = true;
    esp_err_t result = i2s_new_channel(&channelConfig, &speakerChannel, &microphoneChannel);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=i2s_new_channel_failed err=%s", esp_err_to_name(result));
        return nullptr;
    }

    i2s_std_config_t standardConfig = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_42,
            .bclk = GPIO_NUM_9,
            .ws = GPIO_NUM_45,
            .dout = GPIO_NUM_8,
            .din = GPIO_NUM_10,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    result = i2s_channel_init_std_mode(speakerChannel, &standardConfig);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=i2s_tx_init_failed err=%s", esp_err_to_name(result));
        return nullptr;
    }
    result = i2s_channel_init_std_mode(microphoneChannel, &standardConfig);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=i2s_init_failed err=%s", esp_err_to_name(result));
        return nullptr;
    }
    result = i2s_channel_enable(speakerChannel);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=i2s_tx_enable_failed err=%s", esp_err_to_name(result));
        return nullptr;
    }
    result = i2s_channel_enable(microphoneChannel);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=i2s_enable_failed err=%s", esp_err_to_name(result));
        return nullptr;
    }
    display_runtime_log_memory("MEM_AFTER_I2S");

    audio_codec_i2s_cfg_t dataConfig = {};
    dataConfig.port = I2S_NUM_0;
    dataConfig.rx_handle = microphoneChannel;
    dataConfig.tx_handle = speakerChannel;
    microphoneDataInterface = audio_codec_new_i2s_data(&dataConfig);
    if (microphoneDataInterface == nullptr) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=data_interface_failed");
        return nullptr;
    }

    es7210_codec_cfg_t codecConfig = {};
    codecConfig.ctrl_if = &codecControl.interface;
    codecConfig.master_mode = false;
    codecConfig.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4;
    microphoneCodecInterface = es7210_codec_new(&codecConfig);
    if (microphoneCodecInterface == nullptr) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=es7210_interface_failed");
        return nullptr;
    }

    esp_codec_dev_cfg_t deviceConfig = {};
    deviceConfig.dev_type = ESP_CODEC_DEV_TYPE_IN;
    deviceConfig.codec_if = microphoneCodecInterface;
    deviceConfig.data_if = microphoneDataInterface;
    microphoneDevice = esp_codec_dev_new(&deviceConfig);
    if (microphoneDevice == nullptr) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=device_create_failed");
        return nullptr;
    }

    ESP_LOGI(
        kTag,
        "VOICE_AUDIO: ready codec=ES7210 i2c=WireNG addr=0x%02X rate=%u bclk=9 mclk=42 ws=45 din=10 dout=8 duplex=1",
        kEs7210Address,
        static_cast<unsigned>(kSampleRate));
    return microphoneDevice;
}

esp_codec_dev_handle_t voiceAudioAdapterSpeakerInit()
{
    if (speakerDevice != nullptr) {
        return speakerDevice;
    }
    if (voiceAudioAdapterInit() == nullptr || microphoneDataInterface == nullptr) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=speaker_requires_duplex_audio");
        return nullptr;
    }

    const audio_codec_gpio_if_t* gpioInterface = audio_codec_new_gpio();
    if (gpioInterface == nullptr) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=speaker_gpio_interface_failed");
        return nullptr;
    }

    esp_codec_dev_hw_gain_t hardwareGain = {
        .pa_voltage = 5.0f,
        .codec_dac_voltage = 3.3f,
        .pa_gain = 0.0f,
    };
    es8311_codec_cfg_t codecConfig = {};
    codecConfig.ctrl_if = &speakerCodecControl.interface;
    codecConfig.gpio_if = gpioInterface;
    codecConfig.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    codecConfig.pa_pin = GPIO_NUM_46;
    codecConfig.pa_reverted = false;
    codecConfig.master_mode = false;
    codecConfig.use_mclk = true;
    codecConfig.hw_gain = hardwareGain;
    speakerCodecInterface = es8311_codec_new(&codecConfig);
    if (speakerCodecInterface == nullptr) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=es8311_interface_failed");
        return nullptr;
    }

    esp_codec_dev_cfg_t deviceConfig = {};
    deviceConfig.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    deviceConfig.codec_if = speakerCodecInterface;
    deviceConfig.data_if = microphoneDataInterface;
    speakerDevice = esp_codec_dev_new(&deviceConfig);
    if (speakerDevice == nullptr) {
        ESP_LOGE(kTag, "VOICE_AUDIO_ERR: reason=speaker_device_create_failed");
        return nullptr;
    }
    ESP_LOGI(
        kTag,
        "VOICE_AUDIO: speaker_ready codec=ES8311 addr=0x%02X dout=8 pa=46 i2s_owner=voice_audio_adapter",
        kEs8311Address);
    return speakerDevice;
}
