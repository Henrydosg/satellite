#include <Arduino.h>
#include <LittleFS.h>
#include <lvgl.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "HB9IIUdisplayInit.h"
#include "lv_driver.h"
#include "boot_manager.h"
#include "sat_tracker.h"
#include "screen_manager.h"
#include "myconfig.h"
#include "LvglScreenshot.h"
#include "orientation_check.h"


LGFX tft;
static LvglScreenshot screenshot;
static WebServer httpServer(8080);

static void serveTracking() {
    const auto& state = SatTracker::getState();
    httpServer.sendHeader("Cache-Control", "no-store");

    if (!state.tleLoaded || !state.telemetryValid || !state.observerValid || state.name[0] == '\0' ||
        !isfinite(state.azimuth) || !isfinite(state.elevation)) {
        httpServer.send(503, "application/json", "{\"error\":\"Tracking data unavailable\"}");
        return;
    }

    JsonDocument response;
    response["satellite"] = state.name;
    response["azimuth"] = state.azimuth;
    response["elevation"] = state.elevation;

    String body;
    serializeJson(response, body);
    httpServer.send(200, "application/json", body);
}

static void show_splash() {
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    Serial.println("[splash] LittleFS root:");
    while (f) {
        Serial.printf("  %s  (%u bytes)\n", f.name(), f.size());
        f = root.openNextFile();
    }
    if (!LittleFS.exists("/splash.jpg")) {
        BootDiagnostics::log("[splash] /splash.jpg not found; continuing");
        return;
    }
    bool ok = tft.drawJpgFile(LittleFS, "/splash.jpg", 0, 0, 800, 480);
    BootDiagnostics::log("[splash] JPEG framebuffer drawing: %s", ok ? "OK" : "FAILED");
    if (ok) {
        tft.setTextDatum(lgfx::datum_t::bottom_right);
        tft.setFont(&fonts::DejaVu18);
        tft.setTextColor(0xCCCCCC);
        tft.drawString("v" FIRMWARE_VERSION, 790, 472);
    }
}


void setup() {
    uint32_t t_setup0 = millis();
    BootDiagnostics::begin();
    delay(500);
    BootDiagnostics::log("=== ESP32-S3 Satellite Tracker v%s ===", FIRMWARE_VERSION);
    BootDiagnostics::log("[boot] Diagnostics: UART0/CH343 + Serial; reset reason=%d, flash=%u bytes",
                         (int)esp_reset_reason(), ESP.getFlashChipSize());
    BootDiagnostics::memory("application startup");
    if (!psramFound()) BootDiagnostics::halt("PSRAM detection");

    if (!initTFT()) BootDiagnostics::halt("LCD initialization");
    runOrientationCheck();
    BootDiagnostics::log("[screen] Orientation check complete");

    bool filesystemReady = LittleFS.begin(false, "/littlefs", 10, "littlefs");
    TLEManager::setFilesystemReady(filesystemReady);
    if (!filesystemReady) {
        BootDiagnostics::log("[filesystem] LittleFS mount FAILED; continuing without assets");
    } else {
        BootDiagnostics::log("[filesystem] LittleFS mounted");
        BootDiagnostics::log("[filesystem] total=%u used=%u splash=%s worldmap=%s",
                             LittleFS.totalBytes(), LittleFS.usedBytes(),
                             LittleFS.exists("/splash.jpg") ? "present" : "MISSING",
                             LittleFS.exists("/worldmap.jpg") ? "present" : "MISSING");
        show_splash();
    }

    uint32_t t_boot0 = millis();
    BootManager::run();
    Serial.printf("[perf] BootManager::run()            %lu ms\n", millis() - t_boot0);

    // ArduinoOTA — allows "Upload" from PlatformIO env:DISPLAY_OTA over WiFi
    ArduinoOTA.setHostname(WIFI_HOSTNAME);  // must match boot_manager MDNS.begin() hostname
    // ArduinoOTA.setPassword("secret");  // uncomment to require a password
    ArduinoOTA.begin();
    Serial.printf("[ota] ArduinoOTA ready  (%s.local:3232)\n", WIFI_HOSTNAME);

    uint32_t t_lv0 = millis();
    BootDiagnostics::memory("before LVGL init");
    BootDiagnostics::log("[lvgl] Initializing main interface");
    lv_init();
    if (!lvgl_setup()) BootDiagnostics::halt("LVGL driver initialization");
    BootDiagnostics::log("[lvgl] Main display/input drivers ready");
    Serial.printf("[perf] LVGL init + driver setup       %lu ms\n", millis() - t_lv0);

    if (screenshot.begin(800, 480, &httpServer)) {
        screenshot.setPreServeFn([](uint16_t* buf, uint16_t w, uint16_t h) {
            tft.readRect(0, 0, w, h, buf);
        });
    }
    httpServer.on("/api/tracking", HTTP_GET, serveTracking);
    httpServer.begin();
    Serial.printf("[http] Tracking API ready at http://%s.local:8080/api/tracking\n", WIFI_HOSTNAME);

    uint32_t satId = NVSConfig::loadSelectedSat(DEFAULT_SAT_ID);
    BootManager::drawStatus("Computing orbits...   Please wait...", 0x4A9ECC);
    uint32_t t_sat0 = millis();
    SatTracker::begin(satId);
    Serial.printf("[perf] SatTracker::begin()            %lu ms\n", millis() - t_sat0);
    BootManager::drawStatus("Building interface...   Please wait...", 0x4A9ECC);

    uint32_t t_ui0 = millis();
    BootDiagnostics::memory("before interface build");
    ScreenManager::build(lv_scr_act());
    Serial.printf("[perf] ScreenManager::build()         %lu ms\n", millis() - t_ui0);
    // UI construction loads the map JPEG. Start background cache reads only
    // after those asset file handles have been closed.
    SatTracker::startSkyTask();

    Serial.printf("[perf] setup() total                  %lu ms\n", millis() - t_setup0);
    BootDiagnostics::log("[screen] UI built; entering LVGL loop");
}

void loop() {
    ArduinoOTA.handle();
    uint32_t handlerStarted = micros();
    lv_timer_handler();
    lvgl_record_handler(micros() - handlerStarted);
    ScreenTracker::serviceTleRequest();
    ScreenSetup::serviceTleRequest();
    SatTracker::runPassCompute();      // next-pass search for active sat
    httpServer.handleClient();
    lvgl_diagnostics_poll();

    delay(5);
}
