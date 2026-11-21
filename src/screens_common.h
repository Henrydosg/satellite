#pragma once

#include <lvgl.h>
#include <string.h>

// ═══════════════════════════════════════════════════════════════════════════════
// Satellite Mission Control UI v2.0 — shared design system
// ═══════════════════════════════════════════════════════════════════════════════
//
// Geometry is unchanged from v1 on purpose: every page, the 800×400 world map
// and the 45 px pass tables are laid out against these values.

// ── Shared layout constants ───────────────────────────────────────────────────
#define HEADER_H    40    // persistent header height (y = 0..40)
#define CONTENT_Y   40    // content panels start here
#define CONTENT_H   400   // content panel height  (y = 40..440)
#define CONTENT_W   800
#define NAV_Y       440   // nav bar starts here
#define NAV_H       40    // nav bar height        (y = 440..480)

// ── Palette (v2.0 — dark navy mission-control) ────────────────────────────────
// Names are kept from v1 so every page picks up the new palette automatically.
#define C_BG       0x07111F   // background
#define C_HDR      0x101F32   // surface: header, nav, table stripes, cards
#define C_SURF2    0x172B44   // raised surface: active nav tile, pressed state
#define C_BORDER   0x2A4462   // card / button outline
#define C_SEC      0x5BB8E8   // section headings, secondary accent values
#define C_VAL      0xF2F6FC   // primary text / key values
#define C_TXT2     0xC4D0DE   // secondary text
#define C_DIM      0x98A8BE   // field labels, inactive items
#define C_CYAN     0x37D5FF   // primary accent: satellite name, marker, active nav
#define C_GREEN    0x2EE87A   // success / above horizon / good
#define C_RED      0xFF4A3D   // error / below horizon / LOS
#define C_BORDEAUX 0xAA2244
#define C_GOLD     0xFFBF69   // warning / TCA / highlights
#define C_DIV      0x1F3249   // hairline dividers

// ── Typography — Inter (UI text) + IBM Plex Mono (live numbers) ───────────────
// All seven fonts are 4 bpp subsets of ASCII + ° ± · × – — … → −.
// Glyphs missing from a subset (LV_SYMBOL_* icons) fall back to Montserrat 16.
LV_FONT_DECLARE(ui_inter_500_14);
LV_FONT_DECLARE(ui_inter_400_16);
LV_FONT_DECLARE(ui_inter_500_16);
LV_FONT_DECLARE(ui_inter_600_18);
LV_FONT_DECLARE(ui_inter_600_24);
LV_FONT_DECLARE(ui_plex_500_18);
LV_FONT_DECLARE(ui_plex_600_26);

#define UI_F_TITLE   (&ui_inter_600_24)   // page / satellite title
#define UI_F_HEAD    (&ui_inter_600_18)   // section headings, buttons
#define UI_F_NAV     (&ui_inter_500_16)   // bottom navigation
#define UI_F_BODY    (&ui_inter_400_16)   // body text, messages
#define UI_F_LABEL   (&ui_inter_500_14)   // field labels, status chips
#define UI_F_VAL     (&ui_plex_500_18)    // telemetry values (monospace, Medium)
#define UI_F_BIG     (&ui_plex_600_26)    // key values: AZ / EL (monospace, SemiBold)

// ── Widget factory helpers ────────────────────────────────────────────────────

static lv_obj_t* mk_panel(lv_obj_t* parent,
                            int x, int y, int w, int h,
                            uint32_t bg_hex) {
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_style_bg_color(p, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static lv_obj_t* mk_label(lv_obj_t* parent,
                            const lv_font_t* font, uint32_t color_hex,
                            int x, int y,
                            const char* text = "") {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color_hex), 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

// ── Redraw-friendly setters ───────────────────────────────────────────────────
// lv_label_set_text() always invalidates and re-lays out the label, and every
// lv_obj_set_style_*() call refreshes the style cache and invalidates the
// object. Pages refresh once per second, so these guards skip the call when
// nothing changed and only the widgets whose value moved get redrawn.

static inline void ui_set_text(lv_obj_t* label, const char* text) {
    if (!label || !text) return;
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static inline void ui_set_text_color(lv_obj_t* obj, uint32_t hex) {
    if (!obj) return;
    lv_color_t c = lv_color_hex(hex);
    if (lv_obj_get_style_text_color(obj, LV_PART_MAIN).full != c.full)
        lv_obj_set_style_text_color(obj, c, 0);
}

static inline void ui_set_bg_color(lv_obj_t* obj, uint32_t hex) {
    if (!obj) return;
    lv_color_t c = lv_color_hex(hex);
    if (lv_obj_get_style_bg_color(obj, LV_PART_MAIN).full != c.full)
        lv_obj_set_style_bg_color(obj, c, 0);
}

static inline void ui_set_hidden(lv_obj_t* obj, bool hidden) {
    if (!obj) return;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != hidden) {
        if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// Rounded "card" container used by v2 dashboards.
static lv_obj_t* mk_card(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* c = mk_panel(parent, x, y, w, h, C_HDR);
    lv_obj_set_style_radius(c, 6, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    return c;
}
