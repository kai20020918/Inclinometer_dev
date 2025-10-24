/**
 * @file main.c
 * @brief A power-efficient program that periodically reads accelerometer
 * data via SPI and displays it in g-force on the PC terminal via UART.
 */


//割り込みの実装
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"

// === Configuration ===
#define UART_PC_ID          uart0
#define UART_PC_TX_PIN      0
#define UART_PC_RX_PIN      1
#define SPI_ACCEL_ID        spi0
#define SPI_ACCEL_SCK_PIN   18
#define SPI_ACCEL_MOSI_PIN  19
#define SPI_ACCEL_MISO_PIN  16
#define SPI_ACCEL_CS_PIN    17

#define BAUD_RATE           9600
#define DISPLAY_INTERVAL_MS 1000 // 1秒ごとに更新

// ADXL367 Register and Command Defines
#define ADXL367_REG_DATA_START    0x0E
#define ADXL367_REG_POWER_CTL     0x2D
#define ADXL367_SPI_READ_CMD      0x0B
#define ADXL367_SPI_WRITE_CMD     0x0A
#define SENSITIVITY_2G            0.00025f // 2gレンジでの感度 (0.25 mg/LSB)

// --- グローバル変数 ---
volatile bool display_update_needed = false;

// --- ADXL367 Helper Functions (変更なし) ---
static inline void cs_select() {
    gpio_put(SPI_ACCEL_CS_PIN, 0);
}

static inline void cs_deselect() {
    gpio_put(SPI_ACCEL_CS_PIN, 1);
}

void adxl367_write_register(uint8_t reg_addr, uint8_t data) {
    uint8_t tx_buf[3] = {ADXL367_SPI_WRITE_CMD, reg_addr, data};
    cs_select();
    spi_write_blocking(SPI_ACCEL_ID, tx_buf, 3);
    cs_deselect();
}

void adxl367_read_registers(uint8_t reg_addr, uint8_t *buffer, uint16_t len) {
    uint8_t cmd[2] = {ADXL367_SPI_READ_CMD, reg_addr};
    cs_select();
    spi_write_blocking(SPI_ACCEL_ID, cmd, 2);
    spi_read_blocking(SPI_ACCEL_ID, 0, buffer, len);
    cs_deselect();
}

void adxl367_read_accel_data(int16_t *x, int16_t *y, int16_t *z) {
    uint8_t data[6];
    adxl367_read_registers(ADXL367_REG_DATA_START, data, 6);
    *x = (int16_t)((data[0] << 8) | data[1]) >> 2;
    *y = (int16_t)((data[2] << 8) | data[3]) >> 2;
    *z = (int16_t)((data[4] << 8) | data[5]) >> 2;
}

// --- 割り込みハンドラ ---
// 定期タイマーのコールバック関数
bool repeating_timer_callback(struct repeating_timer *t) {
    display_update_needed = true; // メインループに更新を通知
    return true; // タイマーを継続
}


int main() {
    // システムクロックを設定
    set_sys_clock_khz(125000, true);
    
    // USBシリアル(stdio)の初期化
    stdio_init_all();
    sleep_ms(2000); // 起動待機
    printf("\n--- Pico Accelerometer Reader ---\n");
    printf("Displaying sensor data (g-force) every %d ms.\n", DISPLAY_INTERVAL_MS);

    // --- Hardware Initialization ---
    // SPIの初期化 (加速度センサー用)
    spi_init(SPI_ACCEL_ID, 1000 * 1000); // 1MHz
    gpio_set_function(SPI_ACCEL_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_ACCEL_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_ACCEL_MISO_PIN, GPIO_FUNC_SPI);
    gpio_init(SPI_ACCEL_CS_PIN);
    gpio_set_dir(SPI_ACCEL_CS_PIN, GPIO_OUT);
    gpio_put(SPI_ACCEL_CS_PIN, 1); // Deselect
    spi_set_format(SPI_ACCEL_ID, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    
    // 加速度センサーを測定モードに設定
    adxl367_write_register(ADXL367_REG_POWER_CTL, 0x02);
    sleep_ms(100);

    // --- 割り込み設定 ---
    // 指定した間隔でコールバックを呼ぶリピーティングタイマーを作成
    struct repeating_timer timer;
    add_repeating_timer_ms(DISPLAY_INTERVAL_MS, repeating_timer_callback, NULL, &timer);

    // --- メインループ ---
    while (1) {
        // タイマー割り込みによってフラグが立ったか確認
        if (display_update_needed) {
            display_update_needed = false; // フラグをリセット

            // 加速度センサーからデータを読み込む
            int16_t raw_x, raw_y, raw_z;
            adxl367_read_accel_data(&raw_x, &raw_y, &raw_z);

            // ★修正点1: LSB値をg (重力加速度) に変換
            float g_x = (float)raw_x * SENSITIVITY_2G;
            float g_y = (float)raw_y * SENSITIVITY_2G;
            float g_z = (float)raw_z * SENSITIVITY_2G;

            // ★修正点2: 変換後のg単位のデータをPCに送信
            printf("X: %6.3fg, Y: %6.3fg, Z: %6.3fg\n", g_x, g_y, g_z);
        }

        // 割り込みが発生するまでCPUをスリープさせ、電力を節約する
        __wfi(); // Wait For Interrupt
    }

    return 0; // 到達しない
}
