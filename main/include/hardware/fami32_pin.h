#ifndef FAMI32_PIN_H
#define FAMI32_PIN_H

#include <stdint.h>

/* Board wiring source: Fami32_08-main hardware bring-up record referencing
 * Netlist_FAMI_807_2026-08-20.net and the OLED daughterboard netlist.
 * All physical GPIO numbers belong here; logical key IDs live in fami32_keys.h.
 */

#include "fami32_keys.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

/* Shared I2C0 bus: PCF8575 (0x20) and NAU88C22 (0x1A). */
#define FAMI32_I2C_PORT       0
#define FAMI32_I2C_SCL_GPIO   38
#define FAMI32_I2C_SDA_GPIO   39
#define FAMI32_I2C_FREQ_HZ    400000
#define PCF8575_I2C_ADDRESS   0x20
#define NAU88C22_I2C_ADDRESS  0x1A
#define PCF8575_INT_GPIO      40

/* PCF8575 port assignment from Netlist_FAMI_807_2026-08-20.net. */
#define PCF8575_L5_BIT          0
#define PCF8575_L4_BIT          1
#define PCF8575_L3_BIT          2
#define PCF8575_L2_BIT          3
#define PCF8575_L1_BIT          4
#define PCF8575_KEY3_IN_BIT     5
#define PCF8575_MAX_SD_MODE_BIT 6
#define PCF8575_SD_DET_BIT      7
#define PCF8575_VOL_MINUS_BIT   8
#define PCF8575_VOL_PLUS_BIT    9
#define PCF8575_H1_BIT          10
#define PCF8575_H2_BIT          11
#define PCF8575_H3_BIT          12
#define PCF8575_H4_BIT          13
#define PCF8575_H5_BIT          14
#define PCF8575_H6_BIT          15

/* Two EC11-style rotary encoders. */
#define ENCODER_1_A_GPIO 41
#define ENCODER_1_B_GPIO 42
#define ENCODER_2_A_GPIO 2
#define ENCODER_2_B_GPIO 1

/* NAU88C22 headphone I2S1 path. */
#define CODEC_I2S_PORT      1
#define CODEC_I2S_DOUT_GPIO 4
#define CODEC_I2S_WS_GPIO   5
#define CODEC_I2S_BCLK_GPIO 6
#define CODEC_I2S_MCLK_GPIO 7

/* MAX98357A speaker I2S0 path. */
#define SPEAKER_I2S_PORT      0
#define SPEAKER_I2S_BCLK_GPIO 15
#define SPEAKER_I2S_WS_GPIO   16
#define SPEAKER_I2S_DOUT_GPIO 17

#define HP_DET_GPIO 18

/* Battery divider: VBAT -- 1.8 MOhm -- ADC -- 100 kOhm -- GND. */
#define BAT_ADC_GPIO            8
#define BAT_ADC_DIVIDER_TOP_OHM 1800000
#define BAT_ADC_DIVIDER_BOT_OHM 100000

/* SSD1306, 128 x 64, 4-wire SPI with switched VBAT/charge-pump supply. */
#define DISPLAY_SCL            9
#define DISPLAY_SDA            10
#define DISPLAY_DC             11
#define DISPLAY_RESET          12
#define DISPLAY_CS             13
#define DISPLAY_POWER_EN       14

/* SDMMC in 1-bit mode. GPIO47/48 are 3.3 V I/O on the N16R8 module. */
#define SD_SCLK_GPIO  21
#define SD_CMD_GPIO   47
#define SD_DATA0_GPIO 48

/* Fixed USB/UART/boot connections, owned by ROM/IDF rather than UI input. */
#define USB_DM_GPIO 19
#define USB_DP_GPIO 20
#define UART_TX_GPIO 43
#define UART_RX_GPIO 44
#define BOOT_GPIO 0

#ifdef FAMI32_DESKTOP
#define FAMI32_STORAGE_DIR "./fami32_data"
#else
#define FAMI32_STORAGE_DIR "/flash"
#endif

#ifdef __cplusplus
namespace fami32_board {
constexpr int peripheral_pins[] = {
    FAMI32_I2C_SCL_GPIO, FAMI32_I2C_SDA_GPIO, PCF8575_INT_GPIO,
    ENCODER_1_A_GPIO, ENCODER_1_B_GPIO, ENCODER_2_A_GPIO, ENCODER_2_B_GPIO,
    CODEC_I2S_DOUT_GPIO, CODEC_I2S_WS_GPIO, CODEC_I2S_BCLK_GPIO, CODEC_I2S_MCLK_GPIO,
    SPEAKER_I2S_BCLK_GPIO, SPEAKER_I2S_WS_GPIO, SPEAKER_I2S_DOUT_GPIO,
    HP_DET_GPIO, BAT_ADC_GPIO, DISPLAY_SCL, DISPLAY_SDA, DISPLAY_DC,
    DISPLAY_RESET, DISPLAY_CS, DISPLAY_POWER_EN, SD_SCLK_GPIO, SD_CMD_GPIO, SD_DATA0_GPIO
};
constexpr bool valid_pins() {
    for (unsigned i = 0; i < sizeof(peripheral_pins) / sizeof(peripheral_pins[0]); ++i) {
        const int pin = peripheral_pins[i];
        // Exclude strapping, internal Flash/Octal PSRAM, native USB and console.
        if (pin <= BOOT_GPIO || pin == 3 || (pin >= 22 && pin <= 37) ||
            pin == 45 || pin == 46 || pin > 48 || pin == USB_DM_GPIO ||
            pin == USB_DP_GPIO || pin == UART_TX_GPIO || pin == UART_RX_GPIO) return false;
        for (unsigned j = 0; j < i; ++j) if (pin == peripheral_pins[j]) return false;
    }
    return true;
}
static_assert(valid_pins(), "N16R8 pin conflict or reserved GPIO assignment");
}
#endif

#define DBG_PRINTF(fmt, ...)            \
    do {                                \
        if (_debug_print) {             \
            printf(fmt, ##__VA_ARGS__); \
        }                               \
    } while (0)

#endif /* FAMI32_PIN_H */
