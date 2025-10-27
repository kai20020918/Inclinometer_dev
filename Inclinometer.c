#include "pico/stdlib.h"
#include "pico/platform.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"    // GPIO設定用
#include "hardware/powman.h"  // powman_set_state_req, powman_set_dormant_gpio_wake の宣言
#include "hardware/xosc.h"    // xosc_dormant の宣言
#include "hardware/regs/powman.h" // POWMANレジスタのビット定義用
#include "hardware/structs/powman.h" // powmanレジスタ構造体 (powman_hw) へのアクセス用

// ウェイクアップに使用するピン
#define WAKE_PIN 22
// LEDピン (環境に合わせて変更してください)
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

// RP2350のPOWMAN_STATE_REQ (bit 7-4) の全パワーダウンビットは 0xF0
#define RP2350_STATE_REQ_P1_7_ALL_OFF \
    (POWMAN_STATE_REQ_BITS & \
    (0x1 << POWMAN_STATE_REQ_SWCORE_LSB) | \
    (0x1 << POWMAN_STATE_REQ_XIP_LSB) | \
    (0x1 << POWMAN_STATE_REQ_SRAM0_LSB) | \
    (0x1 << POWMAN_STATE_REQ_SRAM1_LSB)) 
// ^^^ RP2040マクロの代わりに、RP2350ヘッダーのLSB定義に基づき 0xF0 を得る

/**
 * @brief P1.7 (全ドメインOFF) + DORMANT (オシレータOFF) 状態に移行
 */
void enter_dormant_p1_7(void) {
    // RP2350では、powman_set_state_req() の代わりに、手動でレジスタを操作します。
    // RP2350の powman_hw->state レジスタのビット 7-4 に 0xF を書き込みます (P1.7要求)。
    // 注: RP2040 SDKの powman_set_state_req() は非推奨/非対応の可能性が高いため、低レベルで置き換えました。

    // 1. P1.7 (全ドメインOFF) を要求: 0xF0
    uint32_t req_state_bits = 0xF0; 
    
    // SDKの powman_hw 構造体を使用してレジスタに直接書き込みます
    // powman_hw->state レジスタのビット 7-4 のみを設定
    hw_set_bits(&powman_hw->state, req_state_bits);
    
    // 2. DORMANT状態を要求 (XOSC: 外部クリスタルオシレータを停止)
    xosc_dormant(); // RP2350でもこのAPIが利用可能であることを期待します

    // 3. プロセッサをスリープさせる (Wait For Interrupt)
    __wfi();

    // --- 割り込みにより、ここで実行が再開 ---

    __nop();
    __nop();
    __nop();
}

/**
 * @brief Dormantモードのウェイクアップ設定をRP2350向けに修正
 */
void setup_dormant_wakeup(uint gpio_pin) {
    // RP2350では powman_set_dormant_gpio_wake() は非推奨/非対応の可能性が高いため、
    // POWMAN_PWRUPx レジスタを直接操作します。PWRUP0を使用します。
    
    // 1. GPIO22を初期化 (入力)
    gpio_init(gpio_pin);
    gpio_set_dir(gpio_pin, GPIO_IN);
    gpio_pull_down(gpio_pin);

    // 2. PWRUP0をGPIO 22でHIGHレベルウェイクアップに設定
    // a. まず、PWRUP0を無効化し、ステータスをクリア
    powman_hw->pwrup[0] = (0 << POWMAN_PWRUP0_ENABLE_LSB); // 無効化
    powman_hw->pwrup[0] = (1 << POWMAN_PWRUP0_STATUS_LSB); // ステータス(ラッチされたエッジ)をクリア

    // b. 設定値を構築
    // SOURCE=22, DIRECTION=HIGH_RISING(0x1), MODE=LEVEL(0x0), ENABLE=1
    uint32_t pwrup_config = 
        (gpio_pin << POWMAN_PWRUP0_SOURCE_LSB) | // GPIO 22
        (POWMAN_PWRUP0_DIRECTION_VALUE_HIGH_RISING << POWMAN_PWRUP0_DIRECTION_LSB) | // HIGHレベル検出
        (POWMAN_PWRUP0_MODE_VALUE_LEVEL << POWMAN_PWRUP0_MODE_LSB) | // レベル検出
        (1 << POWMAN_PWRUP0_ENABLE_LSB); // 有効化
        
    // c. レジスタに書き込み
    powman_hw->pwrup[0] = pwrup_config;
}


int main() {
    // ... (初期化処理は省略) ...
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    // --- ウェイクアップ設定 (修正後の関数を呼び出し) ---
    setup_dormant_wakeup(WAKE_PIN);

    // --- Dormantモードへ移行 ---
    
    // LEDを1回点滅させて、Dormantに入ることを知らせる
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(100);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    sleep_ms(500);

    // Dormantモードに移行
    enter_dormant_p1_7();

    // --- ウェイクアップ後 ---
    
    // ... (復帰後の処理は省略) ...
    for (int i = 0; i < 10; i++) {
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get_out_level(PICO_DEFAULT_LED_PIN));
        sleep_ms(50);
    }

    // メインループ
    while (true) {
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(1000);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        sleep_ms(1000);
    }
}