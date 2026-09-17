/**
 * @file power_mgmt.h
 * @brief AXP173 power lifecycle manager — production build.
 *
 * State machine:
 *
 *   [Ship/Off] ──── VBUS 插入 ────► AXP173 检测 VBUS → 自动上电  (开箱激活, 纯硬件)
 *
 *   [Running]
 *     ├─ charging          → 正常，不触发低电警告
 *     ├─ bat ≤ WARN_PCT    → 调用 warn_cb 一次（充电后重置）
 *     ├─ bat ≤ CRIT_PCT    → 自动 powerOFF（调用 pre_shutdown_cb）
 *     ├─ 空闲 ≥ screen_off → Launcher 关闭背光（power_idle_seconds 驱动）
 *     └─ 空闲 ≥ auto_off  → Launcher 调用 power_shutdown()
 *
 *   [Shipping Mode] ← power_enter_ship_mode()   同 [Off]，下次 VBUS 自动唤醒
 *
 * 初始化（在 Launcher::onCreate 中调用一次）:
 *   power_init(&device->pmu);
 *
 * 轮询（Launcher::onLoop ~1 Hz）:
 *   power_tick();
 *
 * 用户活动时调用（触屏 / 按键）:
 *   power_reset_sleep_timer();
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
#include "../bsp/power/AXP173.hpp"
/* C++ only — AXP173_Class* is a C++ type, invisible to C translation units. */
extern "C" void power_init(AXP173_Class* pmu);
extern "C" {
#endif

/* ── Battery thresholds ──────────────────────────────── */
#define POWER_WARN_BAT_PCT   15   /* 电量警告：黄色图标，调用 warn_cb      */
#define POWER_CRIT_BAT_PCT    5   /* 强制关机：调用 pre_shutdown_cb + powerOFF */
#define POWER_SLEEP_MIN_PCT  20   /* Sleep-mode backstop: below this (on battery) power off instead of sleeping */
#define POWER_LOW_BAT_PCT   POWER_WARN_BAT_PCT   /* 向后兼容别名 */

/* AXP173 硬件欠压关机阈值（mV）。与 devices.cpp setVoffVoltage(2900) 保持一致。 */
#define POWER_VOFF_MV       2900

/**
 * 开机保护窗口（秒）。
 * AXP173 库仑计在上电后需要一段时间稳定，期间 getBatLevel() 可能返回 0。
 * 保护窗口内 power_tick() 不触发低电量检测，power_battery_pct() 将 0 视为满电占位。
 */
#define POWER_BOOT_GRACE_S   10

/* ── Lifecycle ───────────────────────────────────────── */

/** ~1 Hz 轮询：检测低电量 + 触发强制关机。在 Launcher::onLoop 中调用。 */
void power_tick(void);

/** 立即关机：调用 pre_shutdown_cb 后执行 AXP173 powerOFF。 */
void power_shutdown(void);

/**
 * 注册关机前回调（LED 动画、settings_flush 等）。
 * 回调中可使用 delay；禁止调用 LVGL（已释放显示器锁）。
 */
void power_set_pre_shutdown_cb(void (*cb)(void));

/**
 * 进入 Shipping Mode（出货/长期存放）。
 * 与 power_shutdown 相同，但记录原因以备调试。
 * AXP173 在检测到 VBUS 上升沿时自动退出（开箱激活）。
 */
void power_enter_ship_mode(void);

/**
 * 进入 ESP32 深睡眠。wake_after_sec > 0 时使用 RTC 定时器唤醒。
 * 注意：HAL_PIN_PMU_IRQ(GPIO43) 为非 RTC GPIO，无法作为 EXT0/EXT1 唤醒源。
 *       如需 PEK 唤醒深睡眠，须在硬件上将 AXP173 IRQ 路由到 RTC GPIO (0-21)。
 */
void power_deep_sleep(uint32_t wake_after_sec);

/* Return codes for power_light_sleep(). */
#define PWR_WAKE_BUTTON  0   /* a button/touch press woke the device */
#define PWR_WAKE_LOWBAT  1   /* on battery and ≤ min_pct — caller should power off */

/**
 * 进入 ESP32 轻睡眠（保留 RAM，唤醒后从调用处继续执行）。Sleep-mode 低功耗档。
 * 唤醒源：A/B + 五向摇杆按键（GPIO 低电平），以及每 battery_check_sec 秒的定时器
 * 用于检查电量。定时器唤醒时：若在充电则返回 PWR_WAKE_BUTTON（唤醒显示充电）；
 * 若放电且电量 ≤ min_pct 则返回 PWR_WAKE_LOWBAT；否则继续睡眠。
 * 按键唤醒返回 PWR_WAKE_BUTTON。阻塞直到真正唤醒。
 */
int power_light_sleep(uint32_t battery_check_sec, int min_pct);

/* ── 空闲计时器 ──────────────────────────────────────── */

/** 任何用户操作（触屏 / 按键）时调用，重置空闲计时器。 */
void power_reset_sleep_timer(void);

/**
 * Poll the AXP173 PEK (power button) short-press latch. Returns true once per
 * short press and clears the latch. Used to trigger sleep on demand. A ~4 s
 * long-press still powers off in AXP173 hardware, independent of this.
 */
bool power_consume_pek_short(void);

/** 返回自上次 power_reset_sleep_timer 以来的秒数。供 Launcher 控制背光和自动关机。 */
uint32_t power_idle_seconds(void);

/* ── 低电量警告回调 ───────────────────────────────────── */

/**
 * 注册低电量警告回调。电量首次降至 WARN_BAT_PCT 且不在充电时调用一次。
 * 充电后自动重置，下次放电时再次触发。
 * @param cb  回调函数，参数为当前电量百分比（0-100）
 */
void power_set_warn_cb(void (*cb)(int pct));

/**
 * 开机保护窗口结束后返回 true（已过 POWER_BOOT_GRACE_S 秒）。
 * 窗口内 getBatLevel() 可能未稳定，power_tick() 暂停所有电量保护逻辑。
 */
bool power_is_ready(void);

/* ── 状态查询 ─────────────────────────────────────────── */
int  power_battery_pct(void);           /* 0-100；保护窗口内若读到 0 则返回 100 占位 */
bool power_is_charging(void);
bool power_is_low_battery(void);        /* ≤ WARN_BAT_PCT 且未充电 */
bool power_is_critical_battery(void);   /* ≤ CRIT_BAT_PCT 且未充电 */
bool power_vbus_present(void);

#ifdef __cplusplus
}
#endif
