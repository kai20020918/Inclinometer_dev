cmake_minimum_required(VERSION 3.13)

# ===RP2350ターゲットを強制的に設定する行 ===
# CMakeのキャッシュに「PICO_TARGET_CHIP=rp2350」を書き込む
set(PICO_TARGET_CHIP "rp2350" CACHE STRING "PICO Build platform" FORCE)

# RP2350のボード定義（必須でないが、より正確）
set(PICO_BOARD "pico2" CACHE STRING "PICO target board" FORCE)

# Pico SDKのインポート（このファイルが同じフォルダにあること）
include(pico_sdk_import.cmake)

project(Inclinometer_dev C CXX ASM) # プロジェクト名をログに合わせて修正
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# SDK初期化
pico_sdk_init()

add_executable(Inclinometer 
    Inclinometer.c 
    powman_example.c # ★ カスタム低電力タイマー機能のソースファイルを追加 ★
)

# 共通ライブラリをリンク
target_link_libraries(Inclinometer PRIVATE pico_stdlib pico_platform hardware_spi hardware_gpio hardware_regs hardware_powman hardware_xosc hardware_sync hardware_clocks hardware_vreg hardware_adc)

# powman_example.h が powman.h の構造体を参照するために、
# カスタムハードウェアインクルードパスを追加する必要がある場合があります。
target_include_directories(Inclinometer PRIVATE
    ${PICO_SDK_PATH}/src/rp2_common/hardware_powman/include
    # もし powman_example.h が powman.h と異なるディレクトリにある場合、それも追加
    ${CMAKE_CURRENT_SOURCE_DIR}
)


# WiFiチップ搭載のボード(Pico Wなど)のとき
# if (PICO_CYW43_SUPPORTED)
#     target_link_libraries(Inclinometer pico_cyw43_arch_none)
# endif()

# map/bin/hex/uf2ファイルを生成
pico_add_extra_outputs(Inclinometer)
