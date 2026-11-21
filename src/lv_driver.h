/**
 * lv_driver.h  —  LVGL display and touch driver for Waveshare ESP32-S3-Touch-LCD-7
 *
 * This file is the BRIDGE between LVGL and the physical hardware.
 * It contains exactly three things:
 *
 *   1. lv_flush_cb()  — called by LVGL when it has pixels to draw
 *   2. lv_touch_cb()  — called by LVGL every tick to read touch state
 *   3. lvgl_setup()   — registers everything; call once from setup()
 *
 * Display: 800×480 RGB parallel, driven by LovyanGFX (LGFX class in HB9IIUdisplayInit.h)
 * Touch  : GT911 capacitive, accessed via tft.getTouch() — same API as TFT_eSPI
 */

#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include "HB9IIUdisplayInit.h"   // defines LGFX class and extern LGFX tft

// Declared in Bus_RGB.cpp — set by the LCD VSYNC_END ISR every ~30.8 ms.
extern volatile bool lgfx_vsync_flag;

// ─── Draw buffer ─────────────────────────────────────────────────────────────
//
// Keep the existing allocation, but flush only LVGL's invalidated rectangles.
// Wait for a fresh VSYNC once per refresh, rather than once per rectangle.

static lv_disp_draw_buf_t _draw_buf;
static lv_color_t        *_buf1 = nullptr;
static lv_color_t        *_buf2 = nullptr;

static const uint32_t DRAW_BUF_LINES = 480;
static uint32_t _perf_frames = 0, _perf_pixels = 0, _perf_render_ms = 0;
static uint32_t _perf_flush_us = 0, _perf_wait_us = 0, _perf_timeouts = 0, _perf_handler_max_us = 0;

static void lv_monitor_cb(lv_disp_drv_t*, uint32_t time_ms, uint32_t pixels) {
    ++_perf_frames;
    _perf_pixels += pixels;
    _perf_render_ms += time_ms;
}
static void lvgl_record_handler(uint32_t elapsed_us) {
    if (elapsed_us > _perf_handler_max_us) _perf_handler_max_us = elapsed_us;
}
static void lvgl_diagnostics_poll() {
    static uint32_t last = millis();
    uint32_t now = millis(), elapsed = now - last;
    if (elapsed < 5000) return;
    BootDiagnostics::log("[lcd-perf] %lu ms frames=%lu pixels=%lu render=%lu ms flush=%lu us vsync_wait=%lu us timeouts=%lu handler_max=%lu us",
        (unsigned long)elapsed, (unsigned long)_perf_frames, (unsigned long)_perf_pixels,
        (unsigned long)_perf_render_ms, (unsigned long)_perf_flush_us, (unsigned long)_perf_wait_us,
        (unsigned long)_perf_timeouts, (unsigned long)_perf_handler_max_us);
    _perf_frames = _perf_pixels = _perf_render_ms = _perf_flush_us = _perf_wait_us = _perf_timeouts = _perf_handler_max_us = 0;
    last = now;
}


// ─── 1. Display flush callback ───────────────────────────────────────────────
//
// Waits for the VSYNC_END interrupt (fired by Bus_RGB ISR), then copies the
// invalidated rectangle to the hardware framebuffer.
// MUST call lv_disp_flush_ready() at the end, or LVGL freezes.

static void lv_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    uint32_t flushStarted = micros();
    static bool refreshActive = false;
    if (!refreshActive) {
        uint32_t waitStarted = micros();
        lgfx_vsync_flag = false; // Discard an interrupt from an older frame.
        while (!lgfx_vsync_flag && micros() - waitStarted < 50000) delay(1);
        _perf_wait_us += micros() - waitStarted;
        if (!lgfx_vsync_flag) ++_perf_timeouts;
        lgfx_vsync_flag = false;
        refreshActive = true;
    }

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixels((uint16_t *)color_p, w * h, true);
    tft.endWrite();
    tft.waitDMA();

    bool lastArea = lv_disp_flush_is_last(drv);
    if (lastArea) refreshActive = false;
    _perf_flush_us += micros() - flushStarted;
    lv_disp_flush_ready(drv);   // ← MANDATORY
    static bool first_flush = true;
    if (first_flush) {
        BootDiagnostics::log("[screen] First LVGL framebuffer flush complete (%ux%u)",
                             (unsigned)w, (unsigned)h);
        first_flush = false;
    }
}


// ─── 2. Touch read callback ──────────────────────────────────────────────────
//
// LovyanGFX exposes GT911 via the same getTouch() API as TFT_eSPI.
// No calibration data needed — GT911 is capacitive.

static void lv_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    uint16_t x, y;
    if (tft.getTouch(&x, &y)) {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}


// ─── 3. lvgl_setup() ─────────────────────────────────────────────────────────
//
// Call this ONCE from setup(), after lv_init() and after initTFT().

static bool lvgl_setup() {
    // ── Allocate full-screen draw buffer from PSRAM ──────────────────────────
    const uint32_t buf_size = LV_HOR_RES_MAX * DRAW_BUF_LINES;
    _buf1 = (lv_color_t *) ps_malloc(buf_size * sizeof(lv_color_t));
    if (!_buf1) {
        BootDiagnostics::log("[lvgl] FAILED: draw buffer allocation (%u bytes)",
                             (unsigned)(buf_size * sizeof(lv_color_t)));
        BootDiagnostics::memory("LVGL allocation failure");
        return false;
    }
    lv_disp_draw_buf_init(&_draw_buf, _buf1, nullptr, buf_size);

    // ── Register display driver ───────────────────────────────────────────────
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = 800;       // screen width  (landscape)
    disp_drv.ver_res      = 480;       // screen height (landscape)
    disp_drv.flush_cb     = lv_flush_cb;
    disp_drv.draw_buf     = &_draw_buf;
    disp_drv.full_refresh = 0;
    disp_drv.monitor_cb   = lv_monitor_cb;
    lv_disp_t* display = lv_disp_drv_register(&disp_drv);
    if (!display) {
        BootDiagnostics::log("[lvgl] FAILED: display driver registration");
        free(_buf1);
        _buf1 = nullptr;
        return false;
    }

    // ── Register touch (input) driver ────────────────────────────────────────
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lv_touch_cb;
    if (!lv_indev_drv_register(&indev_drv)) {
        BootDiagnostics::log("[lvgl] FAILED: input driver registration");
        lv_disp_remove(display);
        free(_buf1);
        _buf1 = nullptr;
        return false;
    }

    BootDiagnostics::log("[lvgl] Drivers registered, draw buffer=%u bytes",
                         (unsigned)(buf_size * sizeof(lv_color_t)));
    return true;
}
