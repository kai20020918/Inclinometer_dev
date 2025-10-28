/**
 * Copyright (c) 2024 Your Company
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * RP2350向け Dormantモード（ディープスリープ）の実装例。
 * - VREG電圧最適化とクロック停止による省電力化
 * - GPIO割り込みによるウェイクアップ (本バージョンでは無効化)
 */

#include <stdio.h> 
#include <sys/_stdint.h>
#include "pico/stdlib.h"
#include "pico/platform.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/powman.h"
#include "hardware/xosc.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/adc.h"
#include "hardware/structs/usb.h"
#include "hardware/vreg.h"       // VREG_VOLTAGE_0_60 の定義用
#include "hardware/regs/powman.h"
#include "hardware/structs/powman.h"
#include "hardware/resets.h"     // reset_block のために追加
// #include "pico/sleep.h"          // sleep_run_from_rosc() が powman_example.c にない場合の代替
// ★ powman_example.c が提供する関数を使うために、このヘッダーが必須 ★
#include "powman_example.h" 


#define AWAKE_TIME_MS 10000
#define SLEEP_TIME_MS 5000
// 

// ウェイクアップに使用するピン (加速度センサーの割り込みピンなど)
#define WAKE_PIN 0
// LEDピン (環境に合わせて変更してください)
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

// 低消費電力化のため、USB PHYを完全に無効化する
static void disable_usb() {
    usb_hw->phy_direct = USB_USBPHY_DIRECT_TX_PD_BITS | USB_USBPHY_DIRECT_RX_PD_BITS | USB_USBPHY_DIRECT_DM_PULLDN_EN_BITS | USB_USBPHY_DIRECT_DP_PULLDN_EN_BITS;
    usb_hw->phy_direct_override = USB_USBPHY_DIRECT_RX_DM_BITS | USB_USBPHY_DIRECT_RX_DP_BITS | USB_USBPHY_DIRECT_RX_DD_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_DIFFMODE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DM_PULLUP_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_FSSLEW_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_PD_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_RX_PD_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_DM_PULLDN_EN_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLDN_EN_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLUP_EN_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_DM_PULLUP_HISEL_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLUP_HISEL_OVERRIDE_EN_BITS;
}

/**
 * @brief P1.7 (全ドメインOFF) + DORMANT (オシレータOFF) 状態に移行
 * @note この関数は、現在 powman_example_off_for_ms 内で処理されるため、メインからは呼ばない
 */
void enter_dormant_p1_7(void) {
    // この関数は使わないため、中身を空にしても良いが、ここではデバッグのために残す
    // 1. P1.7 (全ドメインOFF) を要求:
    hw_set_bits(&powman_hw->state, POWMAN_STATE_REQ_BITS);

    // 2. DORMANT状態を要求 (XOSC: 外部クリスタルオシレータを停止)
    xosc_dormant();

    // 3. プロセッサをスリープさせる (Wait For Interrupt)
    __wfi();

    // --- 割り込みにより、ここで実行が再開 ---
}

/* setup_dormant_wakeup_gpio 関数は、現在、原因切り分けのためコードから除外されています。 */


int main() {
    // === 1. クロックとGPIOの低電力化初期設定 ===

    // クロックを48MHzに設定し、pll_sysを停止（低消費電力化）
    set_sys_clock_48mhz();

    // Set all pins to input (as far as SIO is concerned) and disable pulls
    gpio_set_dir_all_bits(0);
    for (int i = 2; i < NUM_BANK0_GPIOS; ++i) {
        gpio_set_function(i, GPIO_FUNC_SIO);
        if (i > NUM_BANK0_GPIOS - NUM_ADC_CHANNELS) {
            gpio_disable_pulls(i);
            gpio_set_input_enabled(i, false);
        }
    }

    // === 2. VREG 低電圧設定 (40µA達成の鍵) ===
    // 低電力モード時の VREG 電圧を 0.60V に設定
    hw_write_masked(
        &powman_hw->vreg_lp_entry,
        POWMAN_PASSWORD_BITS | ((uint)VREG_VOLTAGE_0_60 << POWMAN_VREG_LP_ENTRY_VSEL_LSB),
        POWMAN_PASSWORD_BITS | POWMAN_VREG_LP_ENTRY_VSEL_BITS
    );

    // Unlock the VREG control interface
    hw_set_bits(&powman_hw->vreg_ctrl, POWMAN_PASSWORD_BITS | POWMAN_VREG_CTRL_UNLOCK_BITS);


    // === 3. 周辺機器の停止とリセット（強化） ===

    // ADC以外の未使用周辺機器をリセットして停止し、消費電流を最小化する
    // reset_block(RESETS_RESET_ADC_BITS | 
    //             RESETS_RESET_UART0_BITS | 
    //             RESETS_RESET_SPI0_BITS | 
    //             RESETS_RESET_I2C0_BITS | 
    //             RESETS_RESET_PWM_BITS |
    //             RESETS_RESET_TIMER_BITS);

    // Turn off USB PHY and apply pull downs on DP & DM (低消費電力化)
    disable_usb();


    // === 4. powman_example の初期化とスリープ実行 ===
    
    // powman_example の初期化 (powman_timer_start() などを含む)
    // この関数は、以前の $40µA 達成コードで呼ばれていました
    // 注: 1704067200000 はダミーの時刻
    powman_example_init(1704067200000); 

    // Scratch register survives power down (printfなし)
    powman_hw->scratch[0]++; 


    // === 5. Dormantモードへ移行（powman_example の高レベル関数を使用） ===

    // アクティブな実行時間
    sleep_ms(AWAKE_TIME_MS);

    // power off (powman_example.c内の関数で低電力移行シーケンスを実行)
    int rc = powman_example_off_for_ms(SLEEP_TIME_MS); 
    // powman_example_off_for_ms は内部で powman_enable_alarm_wakeup_at_ms() を呼び出します

    // 成功すれば、ここで hard_assert が呼ばれることはありません（復帰するため）

    // === 6. ウェイクアップ後 ===
    
    // ウェイクアップ後の処理をここに記述
    // ここからプログラムが再開される

    while (true) {
        tight_loop_contents();
    }
    // powman_example_off_for_ms が成功した場合、ここに到達しないため、hard_assert(false) は削除
    return 0; // ここに到達することは稀
}
