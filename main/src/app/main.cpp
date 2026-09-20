#ifdef FAMI32_DESKTOP

#include "desktop_platform.h"
#include "fami32_pin.h"
#include "ftm_file.h"
#include "fami32_player.h"
#include "gfx_oled_ssd1306.h"
#include "gui/gui_common.h"
#include "keypad_io.h"
#include "src_config.h"
#include "touch_input.h"
#include "git_version.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

bool _debug_print = false;
bool _midi_output = false;
bool edit_mode = false;
int g_vol = 16;

FAMI_PLAYER player;
GfxOledSSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT);
USBMIDI MIDI;
i2s_chan_handle_t i2s_tx_handle = nullptr;
TaskHandle_t SOUND_TASK_HD = nullptr;
TaskHandle_t GUI_TASK = nullptr;
TaskHandle_t KEYPAD_TASK_HD = nullptr;

static const uint8_t kKeypadRows[KEYPAD_ROWS] = {0, 1, 2, 3};
static const uint8_t kKeypadCols[KEYPAD_COLS] = {0, 1, 2};
KeypadIO keypad(reinterpret_cast<const uint8_t *>(KEYPAD_MAP),
                kKeypadRows, kKeypadCols, KEYPAD_ROWS, KEYPAD_COLS);

const uint8_t bayerMatrix[4][4] = {
    {  0, 128,  32, 160 },
    { 192,  64, 224,  96 },
    {  48, 176,  16, 144 },
    { 240, 112, 208,  80 }
};

void drawFami32Splash(GfxOledSSD1306 &display) {
    display.fillScreen(1);
    display.drawBitmap(48, 1, fami32_logo, 32, 32, 0);
    display.setTextColor(0);
    display.setFont(&rismol57);
    display.setCursor(47, 33);
    display.print("FAMI32");
    display.setFont(&rismol35);
    display.setCursor(0, 0);
    display.printf("FM32-%s", get_version_string());
    display.setCursor(0, 47);
    display.printf("By libchara-dev\n%s %s", __DATE__, __TIME__);
}

void bayerDitherFade(GfxOledSSD1306 &display, int steps, int factor, bool fadeIn) {
    for (int i = 0; i < steps; i++) {
        drawFami32Splash(display);
        int fadeValue = fadeIn ? (i * factor) : ((steps - i) * factor);
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            for (int y = 0; y < DISPLAY_HEIGHT; y++) {
                display.drawPixel(x, y, (display.getPixel(x, y) * fadeValue) > bayerMatrix[y & 3][x & 3]);
            }
        }
        display.display();
    }
}

namespace {

void ensure_data_directory() {
#ifdef _WIN32
    if (_mkdir(FAMI32_STORAGE_DIR) != 0 && errno != EEXIST) perror("fami32_data");
#else
    if (mkdir(FAMI32_STORAGE_DIR, 0755) != 0 && errno != EEXIST) perror("fami32_data");
#endif
}

void create_default_config() {
    set_config_value("SAMPLE_RATE", CONFIG_INT, &SAMP_RATE);
    set_config_value("ENGINE_SPEED", CONFIG_INT, &ENG_SPEED);
    set_config_value("LPF_CUTOFF", CONFIG_INT, &LPF_CUTOFF);
    set_config_value("HPF_CUTOFF", CONFIG_INT, &HPF_CUTOFF);
    set_config_value("BASE_FREQ_HZ", CONFIG_INT, &BASE_FREQ_HZ);
    set_config_value("OVER_SAMPLE", CONFIG_INT, &OVER_SAMPLE);
    set_config_value("VOLUME", CONFIG_INT, &g_vol);
    int midi_output = 0;
    int debug_print = 0;
    set_config_value("MIDI_OUT", CONFIG_INT, &midi_output);
    set_config_value("DEBUG_PRINT", CONFIG_INT, &debug_print);
    write_config(config_path);
}

void load_desktop_config() {
    if (read_config(config_path) != CONFIG_SUCCESS) {
        create_default_config();
        return;
    }
    get_config_value("SAMPLE_RATE", CONFIG_INT, &SAMP_RATE);
    get_config_value("ENGINE_SPEED", CONFIG_INT, &ENG_SPEED);
    get_config_value("LPF_CUTOFF", CONFIG_INT, &LPF_CUTOFF);
    get_config_value("HPF_CUTOFF", CONFIG_INT, &HPF_CUTOFF);
    get_config_value("BASE_FREQ_HZ", CONFIG_INT, &BASE_FREQ_HZ);
    get_config_value("OVER_SAMPLE", CONFIG_INT, &OVER_SAMPLE);
    get_config_value("VOLUME", CONFIG_INT, &g_vol);
    int value = 0;
    if (get_config_value("MIDI_OUT", CONFIG_INT, &value) == CONFIG_SUCCESS) _midi_output = value != 0;
    if (get_config_value("DEBUG_PRINT", CONFIG_INT, &value) == CONFIG_SUCCESS) _debug_print = value != 0;
}

void desktop_sound_task() {
    player.reset_audio_sample_clock();
    while (!desktop_should_quit()) {
        player.process_tick();
        for (size_t i = 0; i < player.get_buf_size(); ++i) {
            player.get_buf()[i] = static_cast<int16_t>((player.get_buf()[i] * g_vol) >> 5);
        }
        desktop_audio_write(player.get_buf(), player.get_buf_size());
    }
}

} // namespace

extern "C" int app_main(void) {
    ensure_data_directory();
    if (!desktop_platform_init("Fami32", DISPLAY_WIDTH, DISPLAY_HEIGHT)) return 1;
    if (display.begin(nullptr) != ESP_OK || !keypad.begin()) return 1;

    vTaskDelay(128);
    bayerDitherFade(display, 32, 8, true);
    load_desktop_config();

    display.setCursor(0, 59);
    display.printf("Press any key to continue...");
    display.display();
    while (!keypad.available()) {
        keypad.tick();
        vTaskDelay(2);
    }
    keypad.read();

    player.init(&ftm);
    touch_input_init();
    desktop_audio_init(SAMP_RATE);
    SOUND_TASK_HD = reinterpret_cast<TaskHandle_t>(1);
    KEYPAD_TASK_HD = reinterpret_cast<TaskHandle_t>(1);
    std::thread(desktop_sound_task).detach();

    bayerDitherFade(display, 16, 16, false);
    display.setFont(&rismol35);
    display.setTextColor(1);
    GUI_TASK = reinterpret_cast<TaskHandle_t>(1);
    gui_task(nullptr);
    return 0;
}

#else

#include "keypad_io.h"
#include "nau88c22.h"
#include "psram_allocator.h"
#include <vector>
#include "driver/gpio.h"
#include "esp_log.h"
#include <gfx_oled_ssd1306.h>
#include <driver/i2s_std.h>
#include "fami32_pin.h"
#include "ftm_file.h"
#include "fami32_player.h"
#include <dirent.h>
#include "esp_vfs_fat.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "gui/gui_common.h"
#include "gui/gui_input.h"
#include "boot_check.h"
#include "touch_input.h"
#include "git_version.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "tinyusb_helper.h"
#include "boot_router.h"
#include <USBMIDI.h>

bool _debug_print = false;
bool _midi_output = false;
bool edit_mode = false;

int g_vol = 16;

extern "C" {
#include "micro_config.h"
}

FAMI_PLAYER player;
i2s_chan_handle_t i2s_tx_handle = nullptr;
TaskHandle_t SOUND_TASK_HD = NULL;
TaskHandle_t GUI_TASK = NULL;
TaskHandle_t KEYPAD_TASK_HD = NULL;
USBMIDI MIDI;
esp_lcd_panel_handle_t panel;
GfxOledSSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT);
KeypadIO keypad;

static constexpr size_t MIDI_TIMELINE_QUEUE_SIZE = 256;
static constexpr int64_t MIDI_TIMELINE_LATENCY_MARGIN_US = 3000;
static constexpr uint32_t KEYPAD_TASK_STACK_SIZE = 4096;
static constexpr uint32_t SOUND_TASK_STACK_SIZE = 4096;
// Keep extra headroom for planned GUI animations and deeper rendering paths.
static constexpr uint32_t GUI_TASK_STACK_SIZE = 12288;

static void log_heap_state(const char *stage) {
    constexpr uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    constexpr uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    ESP_LOGI(
        "Fami32Mem",
        "%s: internal free=%u min=%u largest=%u; psram free=%u largest=%u",
        stage,
        static_cast<unsigned>(heap_caps_get_free_size(internal_caps)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(internal_caps)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(internal_caps)),
        static_cast<unsigned>(heap_caps_get_free_size(psram_caps)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(psram_caps)));
}

typedef struct {
    uint64_t sample_time;
    uint32_t sequence;
    midiEventPacket_t packet;
} midi_timeline_event_t;

static portMUX_TYPE midi_timeline_lock = portMUX_INITIALIZER_UNLOCKED;
static midi_timeline_event_t midi_timeline_queue[MIDI_TIMELINE_QUEUE_SIZE];
static size_t midi_timeline_count = 0;
static uint32_t midi_timeline_sequence = 0;
static int64_t midi_timeline_origin_us = 0;

static void handle_midi_packet(midiEventPacket_t packet);

static uint64_t midi_timeline_us_to_samples(int64_t us) {
    if (us <= 0) {
        return 0;
    }
    return ((uint64_t)us * (uint64_t)SAMP_RATE + 500000ULL) / 1000000ULL;
}

static int64_t midi_timeline_latency_us() {
    int engine_speed = ENG_SPEED;
    if (engine_speed < 1) {
        engine_speed = 1;
    }
    return (1000000LL / engine_speed) + MIDI_TIMELINE_LATENCY_MARGIN_US;
}

static void midi_timeline_reset() {
    portENTER_CRITICAL(&midi_timeline_lock);
    midi_timeline_origin_us = esp_timer_get_time();
    midi_timeline_count = 0;
    midi_timeline_sequence = 0;
    portEXIT_CRITICAL(&midi_timeline_lock);
}

static bool midi_timeline_is_earlier(const midi_timeline_event_t &a, const midi_timeline_event_t &b) {
    if (a.sample_time != b.sample_time) {
        return a.sample_time < b.sample_time;
    }
    return a.sequence < b.sequence;
}

static void midi_timeline_push(midiEventPacket_t packet) {
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&midi_timeline_lock);
    if (midi_timeline_origin_us == 0) {
        midi_timeline_origin_us = now_us;
    }

    midi_timeline_event_t event = {
        .sample_time = midi_timeline_us_to_samples((now_us - midi_timeline_origin_us) + midi_timeline_latency_us()),
        .sequence = midi_timeline_sequence++,
        .packet = packet,
    };

    if (midi_timeline_count < MIDI_TIMELINE_QUEUE_SIZE) {
        midi_timeline_queue[midi_timeline_count++] = event;
    } else {
        size_t drop_index = 0;
        for (size_t i = 1; i < midi_timeline_count; i++) {
            if (midi_timeline_is_earlier(midi_timeline_queue[i], midi_timeline_queue[drop_index])) {
                drop_index = i;
            }
        }
        midi_timeline_queue[drop_index] = event;
    }
    portEXIT_CRITICAL(&midi_timeline_lock);
}

static bool midi_timeline_next_event(uint64_t window_start_sample,
                                     uint64_t window_end_sample,
                                     uint64_t *event_sample,
                                     void *user) {
    (void)user;

    bool found = false;
    uint64_t best_sample = 0;
    uint32_t best_sequence = 0;

    portENTER_CRITICAL(&midi_timeline_lock);
    for (size_t i = 0; i < midi_timeline_count; i++) {
        const midi_timeline_event_t &event = midi_timeline_queue[i];
        if (event.sample_time < window_start_sample || event.sample_time >= window_end_sample) {
            continue;
        }
        if (!found ||
            event.sample_time < best_sample ||
            (event.sample_time == best_sample && event.sequence < best_sequence)) {
            found = true;
            best_sample = event.sample_time;
            best_sequence = event.sequence;
        }
    }
    portEXIT_CRITICAL(&midi_timeline_lock);

    if (found && event_sample != NULL) {
        *event_sample = best_sample;
    }
    return found;
}

static bool midi_timeline_pop_due(uint64_t sample_time, midiEventPacket_t *packet) {
    bool found = false;
    size_t best_index = 0;

    portENTER_CRITICAL(&midi_timeline_lock);
    for (size_t i = 0; i < midi_timeline_count; i++) {
        if (midi_timeline_queue[i].sample_time > sample_time) {
            continue;
        }
        if (!found || midi_timeline_is_earlier(midi_timeline_queue[i], midi_timeline_queue[best_index])) {
            found = true;
            best_index = i;
        }
    }

    if (found) {
        *packet = midi_timeline_queue[best_index].packet;
        midi_timeline_count--;
        if (best_index < midi_timeline_count) {
            midi_timeline_queue[best_index] = midi_timeline_queue[midi_timeline_count];
        }
    }
    portEXIT_CRITICAL(&midi_timeline_lock);

    return found;
}

static void midi_timeline_dispatch_due(uint64_t sample_time, void *user) {
    (void)user;

    midiEventPacket_t packet;
    while (midi_timeline_pop_due(sample_time, &packet)) {
        handle_midi_packet(packet);
    }
}

enum class AudioOutput {
    None,
    Speaker,
    Headphones,
};

static constexpr uint32_t kI2sWriteTimeoutMs = 50;
static constexpr TickType_t kOutputMuteDelay = pdMS_TO_TICKS(4);
static constexpr int64_t kAudioPerfReportPeriodUs = 1000000;

static const char *audio_output_name(AudioOutput output) {
    switch (output) {
        case AudioOutput::Speaker: return "speaker/I2S0";
        case AudioOutput::Headphones: return "headphones/I2S1";
        default: return "none";
    }
}

static esp_err_t create_i2s_output(i2s_port_t port,
                                   gpio_num_t mclk,
                                   gpio_num_t bclk,
                                   gpio_num_t ws,
                                   gpio_num_t dout,
                                   const void *silence,
                                   size_t silence_size,
                                   i2s_chan_handle_t *handle) {
    if (handle == nullptr || *handle != nullptr || silence == nullptr || silence_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&channel_config, handle, nullptr);
    if (err != ESP_OK) return err;

    i2s_std_clk_config_t clock_config = I2S_STD_CLK_DEFAULT_CONFIG(48000);
    clock_config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    const i2s_std_config_t standard_config = {
        .clk_cfg = clock_config,
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    err = i2s_channel_init_std_mode(*handle, &standard_config);
    if (err == ESP_OK) {
        size_t loaded = 0;
        err = i2s_channel_preload_data(*handle, silence, silence_size, &loaded);
        if (err == ESP_OK && loaded != silence_size) {
            ESP_LOGE("Fami32Audio", "I2S%u preload incomplete: %u/%u bytes",
                     static_cast<unsigned>(port),
                     static_cast<unsigned>(loaded),
                     static_cast<unsigned>(silence_size));
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK) {
        err = i2s_channel_enable(*handle);
    }
    if (err != ESP_OK) {
        const esp_err_t delete_result = i2s_del_channel(*handle);
        if (delete_result == ESP_OK) {
            *handle = nullptr;
        } else {
            ESP_LOGE("Fami32Audio", "I2S cleanup after start failure failed: %s",
                     esp_err_to_name(delete_result));
            err = delete_result;
        }
    }
    return err;
}

static esp_err_t stop_i2s_output(AudioOutput output, bool codec_ready) {
    keypad.setAudioReady(false);
    if (output == AudioOutput::Headphones && codec_ready) {
        const esp_err_t mute_result = fami32_codec_set_muted(true);
        if (mute_result != ESP_OK) {
            ESP_LOGW("Fami32Audio", "codec mute failed while switching: %s",
                     esp_err_to_name(mute_result));
        }
    }

    if (output != AudioOutput::None) {
        vTaskDelay(kOutputMuteDelay);
    }

    if (i2s_tx_handle != nullptr) {
        const esp_err_t disable_result = i2s_channel_disable(i2s_tx_handle);
        if (disable_result != ESP_OK) {
            ESP_LOGW("Fami32Audio", "I2S disable failed: %s",
                     esp_err_to_name(disable_result));
        }
        const esp_err_t delete_result = i2s_del_channel(i2s_tx_handle);
        if (delete_result != ESP_OK) {
            ESP_LOGE("Fami32Audio", "I2S delete failed: %s",
                     esp_err_to_name(delete_result));
            return delete_result;
        }
        i2s_tx_handle = nullptr;
    }
    return ESP_OK;
}

static esp_err_t start_i2s_output(AudioOutput output,
                                  bool codec_ready,
                                  const void *silence,
                                  size_t silence_size) {
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (output == AudioOutput::Speaker) {
        err = create_i2s_output(
            static_cast<i2s_port_t>(SPEAKER_I2S_PORT),
            I2S_GPIO_UNUSED,
            static_cast<gpio_num_t>(SPEAKER_I2S_BCLK_GPIO),
            static_cast<gpio_num_t>(SPEAKER_I2S_WS_GPIO),
            static_cast<gpio_num_t>(SPEAKER_I2S_DOUT_GPIO),
            silence,
            silence_size,
            &i2s_tx_handle);
        if (err == ESP_OK) {
            keypad.setAudioReady(true);
        }
    } else if (output == AudioOutput::Headphones) {
        if (!codec_ready) return ESP_ERR_INVALID_STATE;
        err = create_i2s_output(
            static_cast<i2s_port_t>(CODEC_I2S_PORT),
            static_cast<gpio_num_t>(CODEC_I2S_MCLK_GPIO),
            static_cast<gpio_num_t>(CODEC_I2S_BCLK_GPIO),
            static_cast<gpio_num_t>(CODEC_I2S_WS_GPIO),
            static_cast<gpio_num_t>(CODEC_I2S_DOUT_GPIO),
            silence,
            silence_size,
            &i2s_tx_handle);
        if (err == ESP_OK) {
            const esp_err_t unmute_result = fami32_codec_set_muted(false);
            if (unmute_result != ESP_OK) {
                ESP_LOGE("Fami32Audio", "codec unmute failed: %s",
                         esp_err_to_name(unmute_result));
                const esp_err_t stop_result = stop_i2s_output(
                    AudioOutput::Headphones, codec_ready);
                err = stop_result == ESP_OK ? unmute_result : stop_result;
            }
        }
    }

    if (err == ESP_OK) {
        ESP_LOGI("Fami32Audio", "active output: %s", audio_output_name(output));
    }
    return err;
}

void sound_task(void *arg) {
    (void)arg;
    log_heap_state("sound/before-player");
    player.init(&ftm);
    log_heap_state("sound/after-player");

    const bool codec_ready = fami32_codec_init() == ESP_OK;

    PsramVector<int16_t> stereo_buffer(player.get_buf_size() * 2, 0);
    const size_t stereo_buffer_bytes = stereo_buffer.size() * sizeof(int16_t);
    log_heap_state("sound/ready");
    AudioOutput active_output = AudioOutput::None;

    player.reset_audio_sample_clock();
    midi_timeline_reset();

    int64_t perf_window_start_us = 0;
    uint64_t perf_process_total_us = 0;
    uint64_t perf_pack_total_us = 0;
    uint64_t perf_feed_total_us = 0;
    uint64_t perf_write_total_us = 0;
    uint64_t perf_event_total_cycles = 0;
    uint64_t perf_mix_total_cycles = 0;
    uint64_t perf_other_total_cycles = 0;
    uint64_t perf_channel_total_cycles[FAMI32_MAX_CHANNELS] = {};
    uint32_t perf_process_max_us = 0;
    uint32_t perf_pack_max_us = 0;
    uint32_t perf_feed_max_us = 0;
    uint32_t perf_write_min_us = 0;
    uint32_t perf_write_max_us = 0;
    uint32_t perf_blocks = 0;
    uint32_t perf_late_blocks = 0;
    uint32_t perf_i2s_errors = 0;
    uint32_t perf_peak_frame = 0;
    uint32_t perf_peak_row = 0;
    bool perf_was_playing = false;

    auto reset_perf_window = [&]() {
        perf_window_start_us = 0;
        perf_process_total_us = 0;
        perf_pack_total_us = 0;
        perf_feed_total_us = 0;
        perf_write_total_us = 0;
        perf_event_total_cycles = 0;
        perf_mix_total_cycles = 0;
        perf_other_total_cycles = 0;
        memset(perf_channel_total_cycles, 0, sizeof(perf_channel_total_cycles));
        perf_process_max_us = 0;
        perf_pack_max_us = 0;
        perf_feed_max_us = 0;
        perf_write_min_us = 0;
        perf_write_max_us = 0;
        perf_blocks = 0;
        perf_late_blocks = 0;
        perf_i2s_errors = 0;
        perf_peak_frame = 0;
        perf_peak_row = 0;
    };

    const uint32_t audio_block_budget_us = static_cast<uint32_t>(
        (player.get_buf_size() * 1000000ULL + SAMP_RATE - 1) / SAMP_RATE);

    for (;;) {
        const AudioOutput requested_output = keypad.headphonesInserted()
            ? AudioOutput::Headphones
            : AudioOutput::Speaker;
        if (requested_output != active_output) {
            const esp_err_t stop_result = stop_i2s_output(active_output, codec_ready);
            active_output = AudioOutput::None;
            if (stop_result != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            const esp_err_t start_result = start_i2s_output(
                requested_output,
                codec_ready,
                stereo_buffer.data(),
                stereo_buffer_bytes);
            if (start_result != ESP_OK) {
                ESP_LOGE("Fami32Audio", "cannot start %s: %s",
                         audio_output_name(requested_output),
                         esp_err_to_name(start_result));
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            active_output = requested_output;
        }

        const int64_t process_start_us = esp_timer_get_time();
        player.process_tick(midi_timeline_next_event, midi_timeline_dispatch_due, NULL);
        const int64_t process_end_us = esp_timer_get_time();
        const fami_player_perf_t &player_perf = player.get_last_perf();
        for (int i = 0; i < player.get_buf_size(); i++) {
            int32_t sample = (static_cast<int32_t>(player.get_buf()[i]) * g_vol) >> 5;
            if (sample > INT16_MAX) sample = INT16_MAX;
            if (sample < INT16_MIN) sample = INT16_MIN;
            stereo_buffer[i * 2] = static_cast<int16_t>(sample);
            stereo_buffer[i * 2 + 1] = static_cast<int16_t>(sample);
        }
        const int64_t pack_end_us = esp_timer_get_time();

        size_t written = 0;
        const esp_err_t write_result = i2s_channel_write(
            i2s_tx_handle,
            stereo_buffer.data(),
            stereo_buffer_bytes,
            &written,
            kI2sWriteTimeoutMs);
        const int64_t write_end_us = esp_timer_get_time();
        const bool i2s_error = write_result != ESP_OK || written != stereo_buffer_bytes;

        const uint32_t process_us = static_cast<uint32_t>(process_end_us - process_start_us);
        const uint32_t pack_us = static_cast<uint32_t>(pack_end_us - process_end_us);
        const uint32_t feed_us = static_cast<uint32_t>(pack_end_us - process_start_us);
        const uint32_t write_us = static_cast<uint32_t>(write_end_us - pack_end_us);
        const bool playing = player.get_play_status();

        if (!playing) {
            if (perf_was_playing) reset_perf_window();
            perf_was_playing = false;
        } else {
            if (!perf_was_playing) {
                reset_perf_window();
                perf_was_playing = true;
            }
            if (perf_window_start_us == 0) perf_window_start_us = process_start_us;

            perf_process_total_us += process_us;
            perf_pack_total_us += pack_us;
            perf_feed_total_us += feed_us;
            perf_write_total_us += write_us;
            perf_event_total_cycles += player_perf.event_cycles;
            perf_mix_total_cycles += player_perf.mix_cycles;
            perf_other_total_cycles += player_perf.other_cycles;
            const uint32_t perf_channel_count = player.get_channel_count();
            for (uint32_t c = 0; c < perf_channel_count; ++c) {
                perf_channel_total_cycles[c] += player_perf.channel_cycles[c];
            }
            if (process_us > perf_process_max_us) perf_process_max_us = process_us;
            if (pack_us > perf_pack_max_us) perf_pack_max_us = pack_us;
            if (feed_us > perf_feed_max_us) {
                perf_feed_max_us = feed_us;
                perf_peak_frame = static_cast<uint32_t>(player.get_frame());
                perf_peak_row = static_cast<uint32_t>(player.get_row());
            }
            if (perf_blocks == 0 || write_us < perf_write_min_us) perf_write_min_us = write_us;
            if (write_us > perf_write_max_us) perf_write_max_us = write_us;
            perf_blocks++;
            if (feed_us > audio_block_budget_us) perf_late_blocks++;
            if (i2s_error) perf_i2s_errors++;

            const int64_t report_elapsed_us = write_end_us - perf_window_start_us;
            if (report_elapsed_us >= kAudioPerfReportPeriodUs) {
                const uint64_t rate_x10 =
                    (static_cast<uint64_t>(perf_blocks) * 10000000ULL +
                     static_cast<uint64_t>(report_elapsed_us / 2)) /
                    static_cast<uint64_t>(report_elapsed_us);
                uint32_t top_channel[3] = {};
                uint64_t top_cycles[3] = {};
                uint64_t render_total_cycles = 0;
                for (uint32_t c = 0; c < perf_channel_count; ++c) {
                    const uint64_t cycles = perf_channel_total_cycles[c];
                    render_total_cycles += cycles;
                    for (uint32_t rank = 0; rank < 3; ++rank) {
                        if (cycles > top_cycles[rank]) {
                            for (uint32_t move = 2; move > rank; --move) {
                                top_cycles[move] = top_cycles[move - 1];
                                top_channel[move] = top_channel[move - 1];
                            }
                            top_cycles[rank] = cycles;
                            top_channel[rank] = c;
                            break;
                        }
                    }
                }
                const uint64_t stage_divisor =
                    static_cast<uint64_t>(perf_blocks) * CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
                printf(
                    "AudioPerf blocks=%u rate=%llu.%lluHz budget=%uus "
                    "process(avg/max)=%llu/%uus pack(avg/max)=%llu/%uus "
                    "feed(avg/max)=%llu/%uus late=%u peak=%u:%u "
                    "write(avg/min/max)=%llu/%u/%uus i2s_err=%u\n",
                    static_cast<unsigned>(perf_blocks),
                    static_cast<unsigned long long>(rate_x10 / 10),
                    static_cast<unsigned long long>(rate_x10 % 10),
                    static_cast<unsigned>(audio_block_budget_us),
                    static_cast<unsigned long long>(perf_process_total_us / perf_blocks),
                    static_cast<unsigned>(perf_process_max_us),
                    static_cast<unsigned long long>(perf_pack_total_us / perf_blocks),
                    static_cast<unsigned>(perf_pack_max_us),
                    static_cast<unsigned long long>(perf_feed_total_us / perf_blocks),
                    static_cast<unsigned>(perf_feed_max_us),
                    static_cast<unsigned>(perf_late_blocks),
                    static_cast<unsigned>(perf_peak_frame),
                    static_cast<unsigned>(perf_peak_row),
                    static_cast<unsigned long long>(perf_write_total_us / perf_blocks),
                    static_cast<unsigned>(perf_write_min_us),
                    static_cast<unsigned>(perf_write_max_us),
                    static_cast<unsigned>(perf_i2s_errors));
                printf(
                    "AudioStage avg_us event=%llu render=%llu mix=%llu other=%llu "
                    "top=ch%u/m%u:%llu,ch%u/m%u:%llu,ch%u/m%u:%llu\n",
                    static_cast<unsigned long long>(perf_event_total_cycles / stage_divisor),
                    static_cast<unsigned long long>(render_total_cycles / stage_divisor),
                    static_cast<unsigned long long>(perf_mix_total_cycles / stage_divisor),
                    static_cast<unsigned long long>(perf_other_total_cycles / stage_divisor),
                    static_cast<unsigned>(top_channel[0]),
                    static_cast<unsigned>(player.channel[top_channel[0]].get_mode()),
                    static_cast<unsigned long long>(top_cycles[0] / stage_divisor),
                    static_cast<unsigned>(top_channel[1]),
                    static_cast<unsigned>(player.channel[top_channel[1]].get_mode()),
                    static_cast<unsigned long long>(top_cycles[1] / stage_divisor),
                    static_cast<unsigned>(top_channel[2]),
                    static_cast<unsigned>(player.channel[top_channel[2]].get_mode()),
                    static_cast<unsigned long long>(top_cycles[2] / stage_divisor));
                reset_perf_window();
            }
        }

        if (i2s_error) {
            ESP_LOGE("Fami32Audio", "%s write failed: %s, %u/%u bytes",
                     audio_output_name(active_output),
                     esp_err_to_name(write_result),
                     static_cast<unsigned>(written),
                     static_cast<unsigned>(stereo_buffer_bytes));
            (void)stop_i2s_output(active_output, codec_ready);
            active_output = AudioOutput::None;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void keypad_task(void *arg) {
    (void)arg;
    for (;;) {
        keypad.tick();
        const int volume_delta = keypad.takeVolumeDelta();
        if (volume_delta != 0) {
            g_vol += volume_delta;
            if (g_vol < 0) g_vol = 0;
            if (g_vol > 64) g_vol = 64;
            ESP_LOGI("Fami32Volume", "volume=%d", g_vol);
        }
        vTaskDelay(2);
    }
}

esp_lcd_panel_handle_t oled_init(void)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;

    gpio_config_t power_config = {};
    power_config.pin_bit_mask = (1ULL << DISPLAY_POWER_EN);
    power_config.mode = GPIO_MODE_OUTPUT;
    power_config.pull_up_en = GPIO_PULLUP_DISABLE;
    power_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    power_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&power_config));
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(DISPLAY_POWER_EN), 1));
    vTaskDelay(pdMS_TO_TICKS(20));

    spi_bus_config_t buscfg = {
        .mosi_io_num = DISPLAY_SDA,
        .miso_io_num = -1,
        .sclk_io_num = DISPLAY_SCL,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT / 8,
    };

    ESP_ERROR_CHECK(
        spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO)
    );

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = DISPLAY_CS,
        .dc_gpio_num = DISPLAY_DC,
        .spi_mode = 0,
        .pclk_hz = 10 * 1000 * 1000,
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .flags = {
            .dc_low_on_param = 1,
        },
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)SPI2_HOST,
            &io_config,
            &io_handle
        )
    );

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = DISPLAY_HEIGHT,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = DISPLAY_RESET,
        .bits_per_pixel = 1,
        .vendor_config = &ssd1306_config,
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_ssd1306(
            io_handle,
            &panel_config,
            &panel_handle
        )
    );

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    /* The installed OLED is physically opposite to the previous controller
     * orientation.  Flipping both axes rotates the visible image 180 degrees
     * relative to the previous firmware. */
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    return panel_handle;
}

const uint8_t bayerMatrix[4][4] = {
    {  0, 128,  32, 160 },
    { 192,  64, 224,  96 },
    {  48, 176,  16, 144 },
    { 240, 112, 208,  80 }
};

void drawFami32Splash(GfxOledSSD1306 &display) {
    display.fillScreen(1);
    display.drawBitmap(48, 1, fami32_logo, 32, 32, 0);
    display.setTextColor(0);
    display.setFont(&rismol57);
    display.setCursor(47, 33);
    display.print("FAMI32");
    display.setFont(&rismol35);
    display.setCursor(0, 0);
    display.printf("FM32-%s", get_version_string());
    display.setCursor(0, 47);
    display.printf("By libchara-dev\n%s %s", __DATE__, __TIME__);
}

void bayerDitherFade(GfxOledSSD1306 &display, int steps, int factor, bool fadeIn) {
    for (int i = 0; i < steps; i++) {
        drawFami32Splash(display);
        int fadeValue = fadeIn ? (i * factor) : ((steps - i) * factor);
        for (int x = 0; x < 128; x++) {
            for (int y = 0; y < 64; y++) {
                display.drawPixel(x, y, (display.getPixel(x, y) * fadeValue) > bayerMatrix[y & 3][x & 3]);
            }
        }
        display.display();
    }
}

static void handle_midi_packet(midiEventPacket_t packet) {
    midiEventData_t e;
    memcpy(&e, &packet, 4);
    if (_debug_print) {
        ESP_LOGI("MIDI_CALLBACK", "%X%X %X%X %02X %02X", e.cn, e.cin, e.ch, e.event, e.note, e.vol);
    }
    const bool note_on = e.event == MIDI_CIN_NOTE_ON && e.vol > 0;
    const bool note_off = e.event == MIDI_CIN_NOTE_OFF || (e.event == MIDI_CIN_NOTE_ON && e.vol == 0);
    if (edit_mode) {
        set_channel_sel_pos(e.ch);
    }

    if (note_on) {
        if (edit_mode) {
            player.channel[channel_sel_pos].set_inst(inst_sel_pos);
            player.channel[channel_sel_pos].set_note(e.note);
            player.channel[channel_sel_pos].set_vol(e.vol >> 3);
            player.channel[channel_sel_pos].note_start();
            unpk_item_t pt_tmp = ftm.get_pt_item(channel_sel_pos, player.get_cur_frame_map(channel_sel_pos), player.get_row());
            pt_tmp.note = (e.note % 12) + 1;
            pt_tmp.octave = (e.note / 12) - 2;
            pt_tmp.instrument = inst_sel_pos;
            pt_tmp.volume = e.vol >> 3;
            ftm.set_pt_item(channel_sel_pos, player.get_cur_frame_map(channel_sel_pos), player.get_row(), pt_tmp);
            if (!player.get_play_status()) {
                player.set_row(player.get_row() + 1);
            }
        } else {
            note_io_preview_note_on(e.note, e.vol >> 3, NOTE_IO_SOURCE_MIDI, e.ch);
        }
    } else if (note_off) {
        if (edit_mode) {
            player.channel[channel_sel_pos].note_end();
            if (player.get_play_status()) {
                unpk_item_t pt_tmp = ftm.get_pt_item(channel_sel_pos, player.get_cur_frame_map(channel_sel_pos), player.get_row());
                pt_tmp.note = NOTE_END;
                pt_tmp.instrument = NO_INST;
                pt_tmp.octave = NO_OCT;
                ftm.set_pt_item(channel_sel_pos, player.get_cur_frame_map(channel_sel_pos), player.get_row(), pt_tmp);
            }
        } else {
            note_io_preview_note_off(e.note, NOTE_IO_SOURCE_MIDI, e.ch);
        }
    } else if (e.event == MIDI_CIN_CONTROL_CHANGE) {
        if (e.note == 0x20) {
            uint8_t set_prog = e.vol;
            if (set_prog >= ftm.inst_block.inst_num) {
                set_prog = ftm.inst_block.inst_num - 1;
            }
            inst_sel_pos = set_prog;
        } else if (e.note == 0x7B) {
            if (edit_mode) {
                player.channel[channel_sel_pos].note_cut();
            } else {
                note_io_preview_all_notes_off();
            }
            printf("ALL NOTES OFF\n");
        } else {
            printf("UNKNOW CC: CMD=%02X, PARAM=%02X\n", e.note, e.vol);
        }
    } else if (e.event == MIDI_CIN_PITCH_BEND_CHANGE) {
        BASE_FREQ_HZ = 440 + (1024 - (((e.vol << 8) | e.note) / 16));
        printf("FINETUNE: %d, PITCH_BEND: 0x%04X\n", BASE_FREQ_HZ, (e.vol << 8) | e.note);
    } else {
        player.channel[channel_sel_pos].note_cut();
        printf("UNKNOW USB MIDI EVENT: 0x%X (%d) -> DATA=%02X %02X\n", e.event, e.event, e.note, e.vol);
    }
}

void midi_callback(midiEventPacket_t packet) {
    midi_timeline_push(packet);
}

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LEN_MIDI_CDC \
    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

TU_ATTR_ALIGNED(4)
uint8_t const usb_cfg_desc_midi[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN_MIDI_CDC, 0x80, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, 0x03, 0x83, 64),
};

extern "C" void app_main(void) {
    touch_input_init();
    panel = oled_init();
    display.begin(panel);
    if (!keypad.begin()) ESP_LOGW("Fami32", "Input unavailable; continuing boot");
    for (int i = 0; i < 5; ++i) {
        keypad.tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    boot_router();

    // The ROM USB Serial/JTAG port is lost when TinyUSB takes over GPIO19/20.
    // Enumerate CDC together with MIDI before the remaining startup checks so
    // failures in boot diagnostics, storage and task creation stay observable.
    const bool usb_ready = init_tinyusb(usb_cfg_desc_midi, sizeof(usb_cfg_desc_midi));
    if (usb_ready && !init_tinyusb_console()) {
        ESP_LOGW("Fami32", "USB CDC monitor unavailable; continuing without it");
    }

    if (boot_check()) show_check_info(&display, &keypad);
    if (keypad.volumeDownPressed() || keypad.isPressed(KEY_BACK)) boot_router_set_mode(USB_MSC);
    keypad.discardEvents();
    touch_input_flush();
    
    vTaskDelay(128);

    bayerDitherFade(display, 32, 8, true);

    esp_vfs_fat_mount_config_t fat_conf = {
        .format_if_mount_failed = true,
        .max_files = 4
    };
    wl_handle_t wl_flash_handle;
    esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl("/flash", "flash", &fat_conf, &wl_flash_handle);
    printf("\nFatFS mount %d: %s\n", ret, esp_err_to_name(ret));

    if (read_config(config_path) != CONFIG_SUCCESS) {
        display.setCursor(0, 59);
        display.printf("Create new config...");
        display.display();
        printf("NO CONFIG FILE FOUND.\nCREATE IT...\n");
        set_config_value("SAMPLE_RATE", CONFIG_INT, &SAMP_RATE);
        set_config_value("ENGINE_SPEED", CONFIG_INT, &ENG_SPEED);
        set_config_value("LPF_CUTOFF", CONFIG_INT, &LPF_CUTOFF);
        set_config_value("HPF_CUTOFF", CONFIG_INT, &HPF_CUTOFF);
        set_config_value("BASE_FREQ_HZ", CONFIG_INT, &BASE_FREQ_HZ);
        set_config_value("OVER_SAMPLE", CONFIG_INT, &OVER_SAMPLE);
        set_config_value("VOLUME", CONFIG_INT, &g_vol);
        int midi_output = _midi_output ? 1 : 0;
        int debug_print = _debug_print ? 1 : 0;
        set_config_value("MIDI_OUT", CONFIG_INT, &midi_output);
        set_config_value("DEBUG_PRINT", CONFIG_INT, &debug_print);
        if (write_config(config_path) != CONFIG_SUCCESS) {
            printf("Failed to write config file.\n");
        }
    }

    get_config_value("SAMPLE_RATE", CONFIG_INT, &SAMP_RATE);
    get_config_value("ENGINE_SPEED", CONFIG_INT, &ENG_SPEED);
    get_config_value("LPF_CUTOFF", CONFIG_INT, &LPF_CUTOFF);
    get_config_value("HPF_CUTOFF", CONFIG_INT, &HPF_CUTOFF);
    get_config_value("BASE_FREQ_HZ", CONFIG_INT, &BASE_FREQ_HZ);
    get_config_value("OVER_SAMPLE", CONFIG_INT, &OVER_SAMPLE);
    get_config_value("VOLUME", CONFIG_INT, &g_vol);
    bool config_migrated = false;
    if (SAMP_RATE != 48000) {
        SAMP_RATE = 48000;
        set_config_value("SAMPLE_RATE", CONFIG_INT, &SAMP_RATE);
        config_migrated = true;
    }
    if (g_vol < 0 || g_vol > 64) {
        g_vol = g_vol < 0 ? 0 : 64;
        set_config_value("VOLUME", CONFIG_INT, &g_vol);
        config_migrated = true;
    }
    int midi_output = _midi_output ? 1 : 0;
    if (get_config_value("MIDI_OUT", CONFIG_INT, &midi_output) == CONFIG_SUCCESS) {
        _midi_output = midi_output != 0;
    } else {
        set_config_value("MIDI_OUT", CONFIG_INT, &midi_output);
        config_migrated = true;
    }

    int debug_print = _debug_print ? 1 : 0;
    if (get_config_value("DEBUG_PRINT", CONFIG_INT, &debug_print) == CONFIG_SUCCESS) {
        _debug_print = debug_print != 0;
    } else {
        set_config_value("DEBUG_PRINT", CONFIG_INT, &debug_print);
        config_migrated = true;
    }

    if (config_migrated) {
        write_config(config_path);
    }

    if (usb_ready) MIDI.begin();

    // Bring-up must reach the application without a working panel key map.
    keypad.discardEvents();
    touch_input_flush();

    log_heap_state("app/before-tasks");

    BaseType_t task_result = xTaskCreatePinnedToCore(
        keypad_task, "KEYPAD", KEYPAD_TASK_STACK_SIZE, NULL, 4, &KEYPAD_TASK_HD, 1);
    if (task_result != pdPASS) {
        ESP_LOGE("Fami32", "failed to create KEYPAD task");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    task_result = xTaskCreatePinnedToCore(
        sound_task, "SOUND TASK", SOUND_TASK_STACK_SIZE, NULL, 20, &SOUND_TASK_HD, 0);
    if (task_result != pdPASS) {
        ESP_LOGE("Fami32", "failed to create SOUND TASK");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    MIDI.setCallback(midi_callback);

    bayerDitherFade(display, 16, 16, false);

    display.setFont(&rismol35);
    display.setTextColor(1);

    task_result = xTaskCreatePinnedToCore(
        gui_task, "GUI", GUI_TASK_STACK_SIZE, NULL, 2, &GUI_TASK, 1);
    if (task_result != pdPASS) {
        ESP_LOGE("Fami32", "failed to create GUI task");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}

#endif
