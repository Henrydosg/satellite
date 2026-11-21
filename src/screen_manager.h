#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include <WiFi.h>
#include "screens_common.h"
#include "sat_tracker.h"
#include "screen_tracker.h"
#include "screen_polar.h"
#include "screen_elev.h"
#include "screen_map.h"
#include "screen_passes.h"
#include "screen_passes_all.h"
#include "screen_iss.h"
#include "screen_selector.h"
#include "screen_setup.h"
#include "screen_moon.h"

namespace ScreenManager {

enum ID { TRACKER = 0, POLAR, ELEV, MAP, PASSES, PASSES_ALL, ISS, SETUP, COUNT };

static const char* LABELS[COUNT] = {
    "TRACK", "SKY", "ELEV", "MAP", "PASS", "PASS(A)", LV_SYMBOL_EYE_OPEN " ISS", "SETUP"
};

static lv_obj_t* hdr;
static lv_obj_t* panels[COUNT];
static lv_obj_t* nav_btns[COUNT];
static lv_obj_t* lbl_sat_name;
static lv_obj_t* btn_fw_update;
static lv_obj_t* lbl_loc;
static lv_obj_t* lbl_utc;
static lv_obj_t* lbl_norad;
static lv_obj_t* tle_chip;
static lv_obj_t* lbl_tle_chip;
static lv_obj_t* lbl_wifi;
static lv_obj_t* nav_ind[COUNT];
static ID        current = TRACKER;

// -- Header geometry (800 x 40) -------------------------------------------------
//   12..142  UTC clock      152..282  local clock
//  290..562  title (satellite name / page title), centred, truncated with "..."
//  566..648  NORAD ID      654..750  TLE status chip     756..790  Wi-Fi icon
static const int HDR_UTC_X    = 12;
static const int HDR_LOC_X    = 152;
static const int HDR_TITLE_X  = 290;
static const int HDR_TITLE_W  = 272;
static const int HDR_NORAD_X  = 566;
static const int HDR_NORAD_W  = 82;
static const int HDR_CHIP_X   = 654;
static const int HDR_CHIP_W   = 96;
static const int HDR_CHIP_H   = 26;
static const int HDR_WIFI_X   = 756;
static const int HDR_WIFI_W   = 34;
static const int NAV_IND_H    = 3;        // active-tab indicator bar
static const float TLE_FRESH_H = 24.0f;   // green below this age
static const float TLE_STALE_H = 72.0f;   // amber below this age, red above

// ── Internal helpers ──────────────────────────────────────────────────────────

static void updateNavHighlight() {
    for (int i = 0; i < COUNT; i++) {
        bool active = (i == (int)current);
        ui_set_bg_color(nav_btns[i], active ? C_SURF2 : C_HDR);
        ui_set_hidden(nav_ind[i], !active);
        lv_obj_t* lbl = lv_obj_get_child(nav_btns[i], 1);
        if (lbl) ui_set_text_color(lbl, active ? C_CYAN : C_TXT2);
    }
}

static void switchTo(ID id) {
    for (int i = 0; i < COUNT; i++)
        lv_obj_add_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(panels[id], LV_OBJ_FLAG_HIDDEN);
    current = id;
    if (id == SETUP) {
        lv_obj_add_flag(hdr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(panels[SETUP], 0, 0);
        lv_obj_set_size(panels[SETUP], CONTENT_W, HEADER_H + CONTENT_H);
    } else {
        lv_obj_clear_flag(hdr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(panels[SETUP], 0, CONTENT_Y);
        lv_obj_set_size(panels[SETUP], CONTENT_W, CONTENT_H);
        lv_obj_add_flag(btn_fw_update, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_loc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_sat_name, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_utc, LV_OBJ_FLAG_HIDDEN);
    }
    updateNavHighlight();
    SatTracker::setSkyNeeded(id == POLAR || id == MAP);
    if (id == MAP)        ScreenMap::onShow();
    if (id == PASSES_ALL) ScreenPassesAll::onShow();
    if (id == ISS)        ScreenISS::onShow();
    if (id == SETUP)      ScreenSetup::onShow();
}

static void sat_name_cb(lv_event_t*) { ScreenSelector::open(); }
static void fw_update_cb(lv_event_t*) { ScreenSetup::openFirmwareUpdate(); }

static void nav_btn_cb(lv_event_t* e) {
    switchTo((ID)(intptr_t)lv_event_get_user_data(e));
}

static void timer_cb(lv_timer_t*) {
    SatTracker::update();

    // ── Persistent header: satellite name ────────────────────────────────────
    const SatTracker::State& s = SatTracker::getState();
    bool satTitle = !(current == ISS || current == PASSES_ALL);
    const char* title = current == ISS ? ScreenISS::title() :
                        current == PASSES_ALL ? "ALL SATELLITES" : s.name;
    // Page titles are longer than satellite names: use the 18 px heading font.
    const lv_font_t* titleFont = satTitle ? UI_F_TITLE : UI_F_HEAD;
    // Page titles also take over the NORAD slot, which is empty on those pages.
    if (lv_obj_get_style_text_font(lbl_sat_name, LV_PART_MAIN) != titleFont) {
        lv_obj_set_style_text_font(lbl_sat_name, titleFont, 0);
        lv_obj_set_width(lbl_sat_name, satTitle ? HDR_TITLE_W
                                                : HDR_NORAD_X + HDR_NORAD_W - HDR_TITLE_X);
    }
    ui_set_text(lbl_sat_name, title);

    // -- Persistent header: NORAD, TLE status, Wi-Fi ---------------------------
    char hbuf[24];
    if (satTitle && s.noradId) snprintf(hbuf, sizeof(hbuf), "#%lu", (unsigned long)s.noradId);
    else hbuf[0] = '\0';
    ui_set_text(lbl_norad, hbuf);

    uint32_t chipCol;
    if (!s.tleLoaded || !isfinite(s.tleAgeHours) || s.tleAgeHours < 0.0f) {
        snprintf(hbuf, sizeof(hbuf), "TLE --");
        chipCol = C_RED;
    } else {
        if (s.tleAgeHours < 48.0f) snprintf(hbuf, sizeof(hbuf), "TLE %.0fh", s.tleAgeHours);
        else                       snprintf(hbuf, sizeof(hbuf), "TLE %.1fd", s.tleAgeHours / 24.0f);
        chipCol = s.tleAgeHours < TLE_FRESH_H ? C_GREEN
                : s.tleAgeHours < TLE_STALE_H ? C_GOLD : C_RED;
    }
    ui_set_text(lbl_tle_chip, hbuf);
    ui_set_text_color(lbl_tle_chip, chipCol);
    lv_color_t chipBorder = lv_color_hex(chipCol);
    if (lv_obj_get_style_border_color(tle_chip, LV_PART_MAIN).full != chipBorder.full)
        lv_obj_set_style_border_color(tle_chip, chipBorder, 0);

    ui_set_text_color(lbl_wifi, WiFi.status() == WL_CONNECTED ? C_GREEN : C_RED);

    // ── Active screen update ──────────────────────────────────────────────────
    switch (current) {
        case TRACKER: ScreenTracker::update(); break;
        case POLAR:   ScreenPolar::update();   break;
        case ELEV:    ScreenElev::update();    break;
        case MAP:     ScreenMap::update();     break;
        case PASSES:      ScreenPasses::update();    break;
        case PASSES_ALL:  ScreenPassesAll::update(); break;
        case ISS:         ScreenISS::update();       break;
        case SETUP:   ScreenSetup::update();   break;
        default: break;
    }
    ScreenMoon::update();   // runs only when moon overlay is visible

    // ── Header clocks ─────────────────────────────────────────────────────────
    struct tm ti{};
    time_t now = time(nullptr);
    char buf[24];
    gmtime_r(&now, &ti);
    if (TLEValidation::timeReady(now)) strftime(buf, sizeof(buf), "UTC %H:%M:%S", &ti);
    else snprintf(buf, sizeof(buf), "UTC --:--:--");
    ui_set_text(lbl_utc, buf);
    localtime_r(&now, &ti);
    if (TLEValidation::timeReady(now)) strftime(buf, sizeof(buf), "LOC %H:%M:%S", &ti);
    else snprintf(buf, sizeof(buf), "LOC --:--:--");
    ui_set_text(lbl_loc, buf);
}

// ── Public entry point ────────────────────────────────────────────────────────

inline void build(lv_obj_t* scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Persistent header (y = 0 .. HEADER_H) ────────────────────────────────
    hdr = mk_panel(scr, 0, 0, 800, HEADER_H, C_HDR);

    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, lv_color_hex(C_DIV), 0);
    lv_obj_set_style_border_width(hdr, 1, 0);

    lbl_utc = mk_label(hdr, UI_F_VAL, C_VAL, HDR_UTC_X, 0, "UTC --:--:--");
    lv_obj_align(lbl_utc, LV_ALIGN_LEFT_MID, HDR_UTC_X, 0);

    lbl_loc = mk_label(hdr, UI_F_VAL, C_TXT2, HDR_LOC_X, 0, "LOC --:--:--");
    lv_obj_align(lbl_loc, LV_ALIGN_LEFT_MID, HDR_LOC_X, 0);

    // Title: fixed box so long names are truncated instead of shifting widgets.
    lbl_sat_name = mk_label(hdr, UI_F_TITLE, C_CYAN, HDR_TITLE_X, 0, "Loading...");
    lv_obj_set_size(lbl_sat_name, HDR_TITLE_W, lv_font_get_line_height(UI_F_TITLE));
    lv_label_set_long_mode(lbl_sat_name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(lbl_sat_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_sat_name, LV_ALIGN_LEFT_MID, HDR_TITLE_X, 0);
    lv_obj_add_flag(lbl_sat_name, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(lbl_sat_name, (HEADER_H - lv_font_get_line_height(UI_F_TITLE)) / 2);
    lv_obj_add_event_cb(lbl_sat_name, sat_name_cb, LV_EVENT_CLICKED, nullptr);

    lbl_norad = mk_label(hdr, UI_F_VAL, C_TXT2, HDR_NORAD_X, 0, "");
    lv_obj_set_width(lbl_norad, HDR_NORAD_W);
    lv_obj_set_style_text_align(lbl_norad, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(lbl_norad, LV_ALIGN_LEFT_MID, HDR_NORAD_X, 0);

    tle_chip = mk_panel(hdr, HDR_CHIP_X, (HEADER_H - HDR_CHIP_H) / 2, HDR_CHIP_W, HDR_CHIP_H, C_BG);
    lv_obj_set_style_radius(tle_chip, HDR_CHIP_H / 2, 0);
    lv_obj_set_style_border_width(tle_chip, 1, 0);
    lv_obj_set_style_border_color(tle_chip, lv_color_hex(C_DIM), 0);
    lbl_tle_chip = mk_label(tle_chip, UI_F_LABEL, C_DIM, 0, 0, "TLE --");
    lv_obj_center(lbl_tle_chip);

    lbl_wifi = mk_label(hdr, &lv_font_montserrat_20, C_DIM, HDR_WIFI_X, 0, LV_SYMBOL_WIFI);
    lv_obj_set_width(lbl_wifi, HDR_WIFI_W);
    lv_obj_set_style_text_align(lbl_wifi, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_LEFT_MID, HDR_WIFI_X, 0);

    btn_fw_update = lv_btn_create(hdr);
    lv_obj_set_size(btn_fw_update, 280, 36);
    lv_obj_align(btn_fw_update, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(btn_fw_update, lv_color_hex(C_SURF2), 0);
    lv_obj_set_style_bg_color(btn_fw_update, lv_color_hex(C_BORDER), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_fw_update, 0, 0);
    lv_obj_set_style_radius(btn_fw_update, 4, 0);
    lv_obj_set_style_pad_all(btn_fw_update, 0, 0);
    lv_obj_add_event_cb(btn_fw_update, fw_update_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(btn_fw_update, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* fw_label = lv_label_create(btn_fw_update);
    lv_label_set_text(fw_label, "CHECK FOR FIRMWARE UPDATE");
    lv_obj_set_style_text_font(fw_label, UI_F_NAV, 0);
    lv_obj_set_style_text_color(fw_label, lv_color_hex(C_VAL), 0);
    lv_obj_center(fw_label);

    // ── Content panels (y = CONTENT_Y .. NAV_Y) ───────────────────────────────
    for (int i = 0; i < COUNT; i++) {
        panels[i] = mk_panel(scr, 0, CONTENT_Y, CONTENT_W, CONTENT_H, C_BG);
        lv_obj_add_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
    }

    ScreenTracker::build(panels[TRACKER]);
    ScreenTracker::onSelectSat = []() { ScreenSelector::open(); };
    ScreenTracker::onMoonTap   = []() { ScreenMoon::open(); };

    ScreenPolar::build(panels[POLAR]);
    ScreenPolar::onSatSelected    = []() { ScreenTracker::onSatChanged(); switchTo(TRACKER); };
    ScreenSelector::onSatSelected = []() { ScreenTracker::onSatChanged(); switchTo(TRACKER); };

    ScreenElev::build(panels[ELEV]);
    ScreenMap::build(panels[MAP]);
    ScreenPasses::build(panels[PASSES]);

    ScreenPassesAll::build(panels[PASSES_ALL]);
    ScreenPassesAll::onSatSelected = []() { ScreenTracker::onSatChanged(); switchTo(TRACKER); };

    ScreenISS::build(panels[ISS]);
    ScreenSetup::build(panels[SETUP]);

    // Overlays — built last so they render above all content panels
    ScreenManageList::build(scr);
    ScreenManageList::onOpen = []() {
        for (int i = 0; i < COUNT; i++)
            lv_obj_add_flag(nav_btns[i], LV_OBJ_FLAG_HIDDEN);
    };
    ScreenManageList::onClose = []() {
        for (int i = 0; i < COUNT; i++)
            lv_obj_clear_flag(nav_btns[i], LV_OBJ_FLAG_HIDDEN);
    };
    ScreenSelector::build(scr);
    ScreenMoon::build(scr);

    // ── Nav bar (y = NAV_Y .. 480) ────────────────────────────────────────────
    // Widths computed via integer division so buttons tile exactly to 800 px.
    // Full 100 x 40 tiles are the touch targets; the active tile is raised and
    // marked with a cyan bar along its top edge.
    for (int i = 0; i < COUNT; i++) {
        int bx = i * 800 / COUNT;
        int bw = (i + 1) * 800 / COUNT - bx;
        lv_obj_t* btn = mk_panel(scr, bx, NAV_Y, bw, NAV_H, C_HDR);
        lv_obj_set_style_bg_color(btn, lv_color_hex(C_SURF2), LV_STATE_PRESSED);
        lv_obj_set_style_border_side(btn, (lv_border_side_t)(LV_BORDER_SIDE_TOP |
                                     (i < COUNT - 1 ? LV_BORDER_SIDE_RIGHT : 0)), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(C_DIV), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, nav_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        nav_btns[i] = btn;

        // Child 0: indicator bar, child 1: label (order used by updateNavHighlight)
        nav_ind[i] = mk_panel(btn, 0, 0, bw, NAV_IND_H, C_CYAN);
        lv_obj_add_flag(nav_ind[i], LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LABELS[i]);
        lv_obj_set_style_text_font(lbl, UI_F_NAV, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_TXT2), 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 1);
    }

    // ── Start ────────────────────────────────────────────────────────────────
    switchTo(TRACKER);
    lv_timer_create(timer_cb, 1000, nullptr);
    timer_cb(nullptr);   // immediate first fill
}

} // namespace ScreenManager
