#ifndef HMI_DISPLAY_H
#define HMI_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Slot States mapping from NodeFSM MacroState for Field Diagnostic HMI
 *        Displayed on 2.4" TFT SPI 320x240 Landscape via LVGL 8.3
 */
enum HmiSlotState : uint8_t {
    HMI_STATE_BOOT_OFF = 0,
    HMI_STATE_SCHEDULE_SPRAY,
    HMI_STATE_SCHEDULE_COOLDOWN,
    HMI_STATE_OVERRIDE_RUN,
    HMI_STATE_FAULT_LATCH,
    HMI_STATE_DISCONNECTED
};

/**
 * @brief Telemetry snapshot for a single Node Slot
 */
struct HmiSlotData {
    uint8_t      node_id;
    uint8_t      group_id;
    uint8_t      target_type;       // 0 empty, 1 node, 2 group
    HmiSlotState state;
    uint16_t     current_ma;       // ACS712 load current
    bool         opto_feedback;    // Optocoupler / gate driver feedback
    uint16_t     countdown_sec;    // Countdown for cooldown / spray window
    uint32_t     last_seen_ms;     // Monotonic ms of last RF packet
    char         fault_msg[12];    // Short fault code: "STALL", "NO_LOAD"
};

/**
 * @brief Gateway-level telemetry snapshot
 */
struct HmiGlobalData {
    int8_t   wifi_rssi;
    char     ip_short[16];
    bool     wifi_connected;
    bool     mqtt_connected;
    bool     rtc_synced;
    char     clock_str[10];    // "HH:MM:SS"
    uint16_t rf_tx_count;
    uint16_t rf_rx_count;
    uint8_t  sys_safety_mode;  // 0: NORMAL, 1: DWELL, 2: E-STOP
    uint32_t free_heap_kb;
    bool     portal_ap_active;
};

/**
 * @brief Initialize LVGL, TFT driver, allocate DMA buffers, build UI tree.
 *        Must be called once after tft.begin().
 */
void hmi_init(void);

/**
 * @brief Update a single slot's telemetry data (thread-safe copy into display buffer).
 */
void hmi_update_slot(uint8_t slot_idx, const HmiSlotData &data);

/**
 * @brief Update gateway global data.
 */
void hmi_update_global(const HmiGlobalData &data);

/**
 * @brief Periodic service tick. Call from main loop every ~5ms minimum.
 *        Drives lv_tick_inc() and lv_timer_handler().
 *        UI label refresh happens at 250ms intervals.
 * @param current_ms Current millis()
 */
void hmi_service_tick(uint32_t current_ms);

#endif // HMI_DISPLAY_H
