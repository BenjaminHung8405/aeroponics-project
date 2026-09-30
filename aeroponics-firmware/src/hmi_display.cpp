/**
 * @file hmi_display.cpp
 * @brief Field Diagnostic HMI — LVGL 8.3.11 on TFT_eSPI ST7789 320x240 Landscape
 *
 * Architecture (mirrors smart-farm gui.cpp pattern):
 *   - TFT_eSPI handles raw SPI pixel flushing via my_disp_flush() callback.
 *   - LVGL 8.3 manages the widget tree (Grid, Cards, Labels, LEDs).
 *   - Double-buffer in DMA internal RAM (primary) or PSRAM (fallback).
 *   - Dirty-flag diff: Labels only updated when value actually changes.
 *   - hmi_service_tick() drives lv_tick_inc() + lv_timer_handler() each call.
 *
 * Obsidian Dark Palette (RGB565 / 32-bit LVGL hex):
 *   Canvas  #0A0D14   Card BG  #161B22   Border   #21262D
 *   Green   #00C853   Blue     #007AFF   Red      #D32F2F
 *   Amber   #FFA000   Cyan     #00D2FF   Muted    #8B949E
 */

#include "hmi_display.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

// ============================================================================
// 1. TFT & LVGL Hardware Layer
// ============================================================================

static TFT_eSPI s_tft = TFT_eSPI();
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_lv_buf1 = nullptr;
static lv_color_t *s_lv_buf2 = nullptr;
static bool s_hmi_ready = false;

/** LVGL flush callback — identical to smart-farm's my_disp_flush() */
static void hmi_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    s_tft.startWrite();
    s_tft.setAddrWindow(area->x1, area->y1, w, h);
    s_tft.pushColors(reinterpret_cast<uint16_t *>(color_p), w * h, true);
    s_tft.endWrite();
    lv_disp_flush_ready(disp);
}

// ============================================================================
// 2. Shared Telemetry Buffers (written by main loop, read by LVGL tick)
// ============================================================================

static HmiSlotData   s_slots[4]  = {};
static HmiGlobalData s_global    = {};

// ============================================================================
// 3. LVGL Widget References
// ============================================================================

// --- Top Header ---
static lv_obj_t *s_lbl_wifi   = nullptr;
static lv_obj_t *s_lbl_ip     = nullptr;
static lv_obj_t *s_lbl_mqtt   = nullptr;
static lv_obj_t *s_led_mqtt   = nullptr;
static lv_obj_t *s_lbl_clock  = nullptr;
static lv_obj_t *s_lbl_rtctag = nullptr;

// --- 4 Slot Cards (2x2 Grid) ---
struct SlotWidgets {
    lv_obj_t *card;
    lv_obj_t *lbl_title;
    lv_obj_t *led_rf;
    lv_obj_t *lbl_badge;
    lv_obj_t *lbl_current;
    lv_obj_t *lbl_opto;
    lv_obj_t *lbl_eval;
};
static SlotWidgets s_slot_w[4] = {};

// --- Bottom Footer ---
static lv_obj_t *s_lbl_rf     = nullptr;
static lv_obj_t *s_lbl_sys    = nullptr;
static lv_obj_t *s_lbl_heap   = nullptr;

// --- Shared Styles ---
static lv_style_t s_style_screen;
static lv_style_t s_style_header;
static lv_style_t s_style_footer;
static lv_style_t s_style_card;
static lv_style_t s_style_card_run;
static lv_style_t s_style_card_fault;
static lv_style_t s_style_card_idle;

// ============================================================================
// 4. Style Helpers
// ============================================================================

static void init_styles()
{
    // Screen background — Obsidian #0A0D14
    lv_style_init(&s_style_screen);
    lv_style_set_bg_color(&s_style_screen, lv_color_hex(0x0A0D14));
    lv_style_set_bg_opa(&s_style_screen, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_screen, 0);
    lv_style_set_pad_all(&s_style_screen, 0);

    // Header / Footer bars
    lv_style_init(&s_style_header);
    lv_style_set_bg_color(&s_style_header, lv_color_hex(0x0A0D14));
    lv_style_set_bg_opa(&s_style_header, LV_OPA_COVER);
    lv_style_set_radius(&s_style_header, 0);
    lv_style_set_border_width(&s_style_header, 0);
    lv_style_set_pad_all(&s_style_header, 4);

    // Card base — Obsidian surface #161B22
    lv_style_init(&s_style_card);
    lv_style_set_bg_color(&s_style_card, lv_color_hex(0x161B22));
    lv_style_set_bg_opa(&s_style_card, LV_OPA_COVER);
    lv_style_set_radius(&s_style_card, 8);
    lv_style_set_border_color(&s_style_card, lv_color_hex(0x21262D));
    lv_style_set_border_width(&s_style_card, 1);
    lv_style_set_shadow_width(&s_style_card, 0);
    lv_style_set_pad_all(&s_style_card, 5);

    // Card running state — blue left accent border
    lv_style_init(&s_style_card_run);
    lv_style_set_border_color(&s_style_card_run, lv_color_hex(0x007AFF));
    lv_style_set_border_width(&s_style_card_run, 2);

    // Card fault state — red border
    lv_style_init(&s_style_card_fault);
    lv_style_set_border_color(&s_style_card_fault, lv_color_hex(0xD32F2F));
    lv_style_set_border_width(&s_style_card_fault, 2);

    // Card idle/off — dim border
    lv_style_init(&s_style_card_idle);
    lv_style_set_border_color(&s_style_card_idle, lv_color_hex(0x21262D));
    lv_style_set_border_width(&s_style_card_idle, 1);
}

// ============================================================================
// 5. UI Build — called once from hmi_init()
// ============================================================================

static void build_ui()
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &s_style_screen, 0);

    // ── TOP HEADER (height 28px) ─────────────────────────────────────────────
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_add_style(header, &s_style_header, 0);
    lv_obj_set_size(header, 320, 28);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x21262D), 0);
    lv_obj_set_style_border_width(header, 1, 0);

    // WiFi RSSI
    s_lbl_wifi = lv_label_create(header);
    lv_obj_set_style_text_font(s_lbl_wifi, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(0x00C853), 0);
    lv_label_set_text(s_lbl_wifi, "WiFi: ---");
    lv_obj_align(s_lbl_wifi, LV_ALIGN_LEFT_MID, 4, 0);

    // Short IP
    s_lbl_ip = lv_label_create(header);
    lv_obj_set_style_text_font(s_lbl_ip, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_ip, lv_color_hex(0x8B949E), 0);
    lv_label_set_text(s_lbl_ip, "IP:---");
    lv_obj_align(s_lbl_ip, LV_ALIGN_LEFT_MID, 86, 0);

    // MQTT LED
    s_led_mqtt = lv_led_create(header);
    lv_obj_set_size(s_led_mqtt, 6, 6);
    lv_led_set_color(s_led_mqtt, lv_color_hex(0xD32F2F));
    lv_led_on(s_led_mqtt);
    lv_obj_align(s_led_mqtt, LV_ALIGN_LEFT_MID, 148, 0);

    // MQTT Label
    s_lbl_mqtt = lv_label_create(header);
    lv_obj_set_style_text_font(s_lbl_mqtt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_mqtt, lv_color_hex(0xD32F2F), 0);
    lv_label_set_text(s_lbl_mqtt, "MQTT:LOST");
    lv_obj_align(s_lbl_mqtt, LV_ALIGN_LEFT_MID, 158, 0);

    // Clock
    s_lbl_clock = lv_label_create(header);
    lv_obj_set_style_text_font(s_lbl_clock, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_clock, lv_color_white(), 0);
    lv_label_set_text(s_lbl_clock, "00:00:00");
    lv_obj_align(s_lbl_clock, LV_ALIGN_RIGHT_MID, -44, 0);

    // RTC source tag [NTP]/[RTC]
    s_lbl_rtctag = lv_label_create(header);
    lv_obj_set_style_text_font(s_lbl_rtctag, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_rtctag, lv_color_hex(0xFFA000), 0);
    lv_label_set_text(s_lbl_rtctag, "[RTC]");
    lv_obj_align(s_lbl_rtctag, LV_ALIGN_RIGHT_MID, -2, 0);

    // ── MAIN GRID 2x2 (height 172px, y=28..199) ─────────────────────────────
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_size(grid, 320, 172);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 29);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 3, 0);

    // 2 columns × 2 rows — each cell exactly 152×82 px
    static lv_coord_t col_dsc[] = {152, 152, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {82, 82, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_pad_row(grid, 4, 0);
    lv_obj_set_style_pad_column(grid, 4, 0);

    for (uint8_t i = 0; i < 4; i++) {
        uint8_t col = i % 2;
        uint8_t row = i / 2;

        // Card container
        lv_obj_t *card = lv_obj_create(grid);
        lv_obj_add_style(card, &s_style_card, 0);
        lv_obj_set_grid_cell(card,
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);
        s_slot_w[i].card = card;

        // Row 1: Title "SLOT X: N0Y [Gz]"
        lv_obj_t *lbl_title = lv_label_create(card);
        lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x8B949E), 0);
        lv_label_set_text_fmt(lbl_title, "SLOT %u: N0%u [G?]", i + 1, i + 4);
        lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 2, 0);
        s_slot_w[i].lbl_title = lbl_title;

        // RF status dot (LED widget top-right)
        lv_obj_t *led_rf = lv_led_create(card);
        lv_obj_set_size(led_rf, 7, 7);
        lv_led_set_color(led_rf, lv_color_hex(0x8B949E));
        lv_led_on(led_rf);
        lv_obj_align(led_rf, LV_ALIGN_TOP_RIGHT, -1, 2);
        s_slot_w[i].led_rf = led_rf;

        // Row 2: Macro state badge pill (filled rect)
        lv_obj_t *lbl_badge = lv_label_create(card);
        lv_obj_set_style_text_font(lbl_badge, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_badge, lv_color_white(), 0);
        lv_obj_set_style_bg_color(lbl_badge, lv_color_hex(0x21262D), 0);
        lv_obj_set_style_bg_opa(lbl_badge, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(lbl_badge, 4, 0);
        lv_obj_set_style_pad_hor(lbl_badge, 4, 0);
        lv_obj_set_style_pad_ver(lbl_badge, 2, 0);
        lv_label_set_text(lbl_badge, "BOOT / OFF");
        lv_obj_align(lbl_badge, LV_ALIGN_TOP_MID, 0, 18);
        s_slot_w[i].lbl_badge = lbl_badge;

        // Row 3: Current label "I: XXXX mA"
        lv_obj_t *lbl_curr = lv_label_create(card);
        lv_obj_set_style_text_font(lbl_curr, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_curr, lv_color_hex(0x8B949E), 0);
        lv_label_set_text(lbl_curr, "I: 0 mA");
        lv_obj_align(lbl_curr, LV_ALIGN_TOP_LEFT, 2, 42);
        s_slot_w[i].lbl_current = lbl_curr;

        // Opto feedback "Opto: OFF"
        lv_obj_t *lbl_opto = lv_label_create(card);
        lv_obj_set_style_text_font(lbl_opto, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_opto, lv_color_hex(0x8B949E), 0);
        lv_label_set_text(lbl_opto, "Opto:OFF");
        lv_obj_align(lbl_opto, LV_ALIGN_TOP_RIGHT, -2, 42);
        s_slot_w[i].lbl_opto = lbl_opto;

        // Row 4: Load evaluation tag
        lv_obj_t *lbl_eval = lv_label_create(card);
        lv_obj_set_style_text_font(lbl_eval, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_eval, lv_color_hex(0x8B949E), 0);
        lv_label_set_text(lbl_eval, "STANDBY");
        lv_obj_align(lbl_eval, LV_ALIGN_TOP_LEFT, 2, 58);
        s_slot_w[i].lbl_eval = lbl_eval;
    }

    // ── BOTTOM FOOTER (height 36px, y=200..235) ──────────────────────────────
    lv_obj_t *footer = lv_obj_create(scr);
    lv_obj_add_style(footer, &s_style_header, 0);
    lv_obj_set_size(footer, 320, 36);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(footer, lv_color_hex(0x21262D), 0);
    lv_obj_set_style_border_width(footer, 1, 0);

    s_lbl_rf = lv_label_create(footer);
    lv_obj_set_style_text_font(s_lbl_rf, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_rf, lv_color_hex(0x00D2FF), 0);
    lv_label_set_text(s_lbl_rf, "RF-433: T:0 R:0");
    lv_obj_align(s_lbl_rf, LV_ALIGN_LEFT_MID, 4, 0);

    s_lbl_sys = lv_label_create(footer);
    lv_obj_set_style_text_font(s_lbl_sys, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_sys, lv_color_hex(0x00C853), 0);
    lv_label_set_text(s_lbl_sys, "SYS:NORMAL");
    lv_obj_align(s_lbl_sys, LV_ALIGN_CENTER, 0, 0);

    s_lbl_heap = lv_label_create(footer);
    lv_obj_set_style_text_font(s_lbl_heap, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_heap, lv_color_hex(0x8B949E), 0);
    lv_label_set_text(s_lbl_heap, "Heap:---K");
    lv_obj_align(s_lbl_heap, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_scr_load(scr);
}

// ============================================================================
// 6. Public API
// ============================================================================

void hmi_init(void)
{
    // Init TFT driver
    s_tft.init();
    s_tft.setRotation(1); // 320x240 Landscape
    s_tft.setSwapBytes(true);

#ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
#endif

    lv_init();

    // Allocate double DMA buffer (320×20 lines each) — identical to smart-farm
    const uint32_t buf_pixels = s_tft.width() * 20;
    const uint32_t buf_size   = buf_pixels * sizeof(lv_color_t);

    s_lv_buf1 = static_cast<lv_color_t *>(
        heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    s_lv_buf2 = static_cast<lv_color_t *>(
        heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));

    if (!s_lv_buf1 || !s_lv_buf2) {
        // Fallback: PSRAM single buffer
        if (s_lv_buf1) { heap_caps_free(s_lv_buf1); s_lv_buf1 = nullptr; }
        if (s_lv_buf2) { heap_caps_free(s_lv_buf2); s_lv_buf2 = nullptr; }
        s_lv_buf1 = static_cast<lv_color_t *>(
            heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }

    if (!s_lv_buf1) { return; } // Out of memory — HMI disabled

    lv_disp_draw_buf_init(&s_draw_buf, s_lv_buf1, s_lv_buf2, buf_pixels);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res   = s_tft.width();
    disp_drv.ver_res   = s_tft.height();
    disp_drv.flush_cb  = hmi_disp_flush;
    disp_drv.draw_buf  = &s_draw_buf;
    lv_disp_drv_register(&disp_drv);

    init_styles();
    build_ui();

    s_hmi_ready = true;
}

void hmi_update_slot(uint8_t slot_idx, const HmiSlotData &data)
{
    if (slot_idx < 4) s_slots[slot_idx] = data;
}

void hmi_update_global(const HmiGlobalData &data)
{
    s_global = data;
}

// ============================================================================
// 7. Periodic UI Refresh — Dirty-Flag Diff (only redraw on delta)
// ============================================================================

static void refresh_header()
{
    static bool      lv_wifi_ok_prev   = false;
    static int8_t    lv_rssi_prev      = 0;
    static bool      lv_mqtt_prev      = false;
    static char      lv_clock_prev[10] = "";
    static bool      lv_rtc_prev       = false;

    // WiFi RSSI
    if (s_global.wifi_connected != lv_wifi_ok_prev || s_global.wifi_rssi != lv_rssi_prev) {
        if (s_global.wifi_connected) {
            lv_obj_set_style_text_color(s_lbl_wifi,
                s_global.wifi_rssi >= -75 ? lv_color_hex(0x00C853) : lv_color_hex(0xFFA000), 0);
            lv_label_set_text_fmt(s_lbl_wifi, "W:%ddBm", s_global.wifi_rssi);
            lv_label_set_text_fmt(s_lbl_ip, "IP:%s", s_global.ip_short);
        } else {
            lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(0xD32F2F), 0);
            lv_label_set_text(s_lbl_wifi, "WiFi:LOST");
            lv_label_set_text(s_lbl_ip, "IP:---");
        }
        lv_wifi_ok_prev = s_global.wifi_connected;
        lv_rssi_prev    = s_global.wifi_rssi;
    }

    // MQTT
    if (s_global.mqtt_connected != lv_mqtt_prev) {
        if (s_global.mqtt_connected) {
            lv_led_set_color(s_led_mqtt, lv_color_hex(0x00C853));
            lv_obj_set_style_text_color(s_lbl_mqtt, lv_color_hex(0x00C853), 0);
            lv_label_set_text(s_lbl_mqtt, "MQTT:OK");
        } else {
            lv_led_set_color(s_led_mqtt, lv_color_hex(0xD32F2F));
            lv_obj_set_style_text_color(s_lbl_mqtt, lv_color_hex(0xD32F2F), 0);
            lv_label_set_text(s_lbl_mqtt, "MQTT:ERR");
        }
        lv_mqtt_prev = s_global.mqtt_connected;
    }

    // Clock
    if (strcmp(s_global.clock_str, lv_clock_prev) != 0) {
        lv_label_set_text(s_lbl_clock, s_global.clock_str);
        strncpy(lv_clock_prev, s_global.clock_str, sizeof(lv_clock_prev) - 1);
    }

    // RTC source tag
    if (s_global.rtc_synced != lv_rtc_prev) {
        if (s_global.rtc_synced) {
            lv_obj_set_style_text_color(s_lbl_rtctag, lv_color_hex(0x00D2FF), 0);
            lv_label_set_text(s_lbl_rtctag, "[NTP]");
        } else {
            lv_obj_set_style_text_color(s_lbl_rtctag, lv_color_hex(0xFFA000), 0);
            lv_label_set_text(s_lbl_rtctag, "[RTC]");
        }
        lv_rtc_prev = s_global.rtc_synced;
    }
}

static void refresh_footer()
{
    static uint16_t lv_tx_prev   = 0xFFFF;
    static uint16_t lv_rx_prev   = 0xFFFF;
    static uint8_t  lv_sys_prev  = 0xFF;
    static uint32_t lv_heap_prev = 0;

    if (s_global.rf_tx_count != lv_tx_prev || s_global.rf_rx_count != lv_rx_prev) {
        lv_label_set_text_fmt(s_lbl_rf, "RF-433 T:%u R:%u",
            s_global.rf_tx_count, s_global.rf_rx_count);
        lv_tx_prev = s_global.rf_tx_count;
        lv_rx_prev = s_global.rf_rx_count;
    }

    if (s_global.sys_safety_mode != lv_sys_prev) {
        if (s_global.sys_safety_mode == 2) {
            lv_obj_set_style_text_color(s_lbl_sys, lv_color_hex(0xD32F2F), 0);
            lv_label_set_text(s_lbl_sys, "SYS:E-STOP");
        } else if (s_global.sys_safety_mode == 1) {
            lv_obj_set_style_text_color(s_lbl_sys, lv_color_hex(0x00D2FF), 0);
            lv_label_set_text(s_lbl_sys, "SYS:DWELL");
        } else {
            lv_obj_set_style_text_color(s_lbl_sys, lv_color_hex(0x00C853), 0);
            lv_label_set_text(s_lbl_sys, "SYS:NORMAL");
        }
        lv_sys_prev = s_global.sys_safety_mode;
    }

    if (s_global.free_heap_kb != lv_heap_prev) {
        lv_label_set_text_fmt(s_lbl_heap, "Heap:%uK", s_global.free_heap_kb);
        lv_heap_prev = s_global.free_heap_kb;
    }
}

static void refresh_slot(uint8_t i)
{
    const HmiSlotData &slot = s_slots[i];
    SlotWidgets       &w    = s_slot_w[i];

    // Title
    if (slot.target_type == 2) {
        lv_label_set_text_fmt(w.lbl_title, "S%u:GROUP %u N%02u", i + 1, slot.group_id, slot.node_id);
    } else if (slot.target_type == 0) {
        lv_label_set_text_fmt(w.lbl_title, "S%u:UNASSIGNED", i + 1);
    } else {
        lv_label_set_text_fmt(w.lbl_title, "S%u:N%02u [G%u]", i + 1, slot.node_id, slot.group_id);
    }

    // RF link dot
    uint32_t age_ms = millis() - slot.last_seen_ms;
    if (slot.last_seen_ms == 0 || age_ms > 30000) {
        lv_led_set_color(w.led_rf, lv_color_hex(0xD32F2F));
    } else if (age_ms > 8000) {
        lv_led_set_color(w.led_rf, lv_color_hex(0xFFA000));
    } else {
        lv_led_set_color(w.led_rf, lv_color_hex(0x00C853));
    }

    // Badge pill + card border color
    lv_obj_remove_style(w.card, &s_style_card_run, 0);
    lv_obj_remove_style(w.card, &s_style_card_fault, 0);
    lv_obj_remove_style(w.card, &s_style_card_idle, 0);

    switch (slot.state) {
        case HMI_STATE_SCHEDULE_SPRAY:
            lv_obj_set_style_bg_color(w.lbl_badge, lv_color_hex(0x00C853), 0);
            lv_obj_set_style_text_color(w.lbl_badge, lv_color_white(), 0);
            lv_label_set_text(w.lbl_badge, "SPRAYING");
            lv_obj_add_style(w.card, &s_style_card_run, 0);
            break;
        case HMI_STATE_OVERRIDE_RUN:
            lv_obj_set_style_bg_color(w.lbl_badge, lv_color_hex(0x007AFF), 0);
            lv_obj_set_style_text_color(w.lbl_badge, lv_color_white(), 0);
            lv_label_set_text(w.lbl_badge, "OVERRIDE RUN");
            lv_obj_add_style(w.card, &s_style_card_run, 0);
            break;
        case HMI_STATE_SCHEDULE_COOLDOWN: {
            lv_obj_set_style_bg_color(w.lbl_badge, lv_color_hex(0x21262D), 0);
            lv_obj_set_style_text_color(w.lbl_badge, lv_color_hex(0x8B949E), 0);
            char cd[20];
            snprintf(cd, sizeof(cd), "WAIT %02um%02us",
                slot.countdown_sec / 60, slot.countdown_sec % 60);
            lv_label_set_text(w.lbl_badge, cd);
            lv_obj_add_style(w.card, &s_style_card_idle, 0);
            break;
        }
        case HMI_STATE_FAULT_LATCH:
            lv_obj_set_style_bg_color(w.lbl_badge, lv_color_hex(0xD32F2F), 0);
            lv_obj_set_style_text_color(w.lbl_badge, lv_color_white(), 0);
            lv_label_set_text(w.lbl_badge,
                slot.fault_msg[0] ? slot.fault_msg : "FAULT LATCH");
            lv_obj_add_style(w.card, &s_style_card_fault, 0);
            break;
        case HMI_STATE_DISCONNECTED:
            lv_obj_set_style_bg_color(w.lbl_badge, lv_color_hex(0x2D2107), 0);
            lv_obj_set_style_text_color(w.lbl_badge, lv_color_hex(0xFFA000), 0);
            lv_label_set_text(w.lbl_badge, "DISCONNECTED");
            lv_obj_add_style(w.card, &s_style_card_idle, 0);
            break;
        default:
            lv_obj_set_style_bg_color(w.lbl_badge, lv_color_hex(0x21262D), 0);
            lv_obj_set_style_text_color(w.lbl_badge, lv_color_hex(0x8B949E), 0);
            lv_label_set_text(w.lbl_badge, "IDLE / OFF");
            lv_obj_add_style(w.card, &s_style_card_idle, 0);
            break;
    }

    // Current mA
    lv_color_t curr_color = slot.current_ma > 100
        ? lv_color_white() : lv_color_hex(0x8B949E);
    lv_obj_set_style_text_color(w.lbl_current, curr_color, 0);
    lv_label_set_text_fmt(w.lbl_current, "I:%umA", slot.current_ma);

    // Opto
    lv_obj_set_style_text_color(w.lbl_opto,
        slot.opto_feedback ? lv_color_hex(0x00C853) : lv_color_hex(0x8B949E), 0);
    lv_label_set_text(w.lbl_opto,
        slot.opto_feedback ? "Opto:ON" : "Opto:OFF");

    // Load eval tag
    if (slot.state == HMI_STATE_SCHEDULE_SPRAY || slot.state == HMI_STATE_OVERRIDE_RUN) {
        if (slot.current_ma > 3800) {
            lv_obj_set_style_text_color(w.lbl_eval, lv_color_hex(0xD32F2F), 0);
            lv_label_set_text(w.lbl_eval, "! STALL TRIP !");
        } else if (slot.current_ma < 150) {
            lv_obj_set_style_text_color(w.lbl_eval, lv_color_hex(0xD32F2F), 0);
            lv_label_set_text(w.lbl_eval, "! OPEN LOAD !");
        } else if (slot.current_ma < 1200) {
            lv_obj_set_style_text_color(w.lbl_eval, lv_color_hex(0xFFA000), 0);
            lv_label_set_text(w.lbl_eval, "? DRY RUN ?");
        } else {
            lv_obj_set_style_text_color(w.lbl_eval, lv_color_hex(0x00C853), 0);
            lv_label_set_text(w.lbl_eval, "LOAD:NORMAL");
        }
    } else if (slot.state == HMI_STATE_FAULT_LATCH) {
        lv_obj_set_style_text_color(w.lbl_eval, lv_color_hex(0xD32F2F), 0);
        lv_label_set_text(w.lbl_eval, "LOCKED:NEED RST");
    } else {
        lv_obj_set_style_text_color(w.lbl_eval, lv_color_hex(0x8B949E), 0);
        lv_label_set_text(w.lbl_eval, "STANDBY");
    }
}

void hmi_service_tick(uint32_t current_ms)
{
    if (!s_hmi_ready) return;

    // Drive LVGL internal tick — mirrors smart-farm loop() pattern
    static uint32_t s_last_tick_ms = 0;
    uint32_t delta = current_ms - s_last_tick_ms;
    if (delta > 0) {
        lv_tick_inc(delta);
        s_last_tick_ms = current_ms;
    }

    // UI data refresh at 250ms intervals
    static uint32_t s_last_ui_ms = 0;
    if (current_ms - s_last_ui_ms >= 250) {
        s_last_ui_ms = current_ms;
        refresh_header();
        refresh_footer();
        for (uint8_t i = 0; i < 4; i++) {
            refresh_slot(i);
        }
    }

    // LVGL rendering pass — mirrors smart-farm's gui_handler() -> lv_timer_handler()
    lv_timer_handler();
}

#else

void hmi_init(void) {}
void hmi_update_slot(uint8_t slot_idx, const HmiSlotData &data) { (void)slot_idx; (void)data; }
void hmi_update_global(const HmiGlobalData &data) { (void)data; }
void hmi_service_tick(uint32_t current_ms) { (void)current_ms; }

#endif // ESP_PLATFORM || ARDUINO
