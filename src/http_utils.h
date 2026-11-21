#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <atomic>
#include <utility>
#include "HB9IIUdisplayInit.h"

namespace HttpUtils {

inline String get(const char* url, bool secure = false, int retries = 3,
                  int* finalCode = nullptr) {
    String body;
    int lastCode = -1;
    if (finalCode) *finalCode = lastCode;
    if (WiFi.status() != WL_CONNECTED) {
        BootDiagnostics::log("[http] No WiFi connection: %s", url);
        return body;
    }
    String host = url;
    int schemeEnd = host.indexOf("://");
    if (schemeEnd >= 0) host = host.substring(schemeEnd + 3);
    int slash = host.indexOf('/');
    if (slash >= 0) host = host.substring(0, slash);
    IPAddress resolved;
    if (!WiFi.hostByName(host.c_str(), resolved)) {
        BootDiagnostics::log("[http] DNS FAILED: %s", host.c_str());
        return body;
    }
    BootDiagnostics::log("[http] DNS %s -> %s; RSSI=%d", host.c_str(), resolved.toString().c_str(), WiFi.RSSI());
    for (int attempt = 1; attempt <= retries; attempt++) {
        uint32_t started = millis();
        BootDiagnostics::log("[http] GET %s (attempt %d/%d)", url, attempt, retries);

        WiFiClientSecure* secClient = nullptr;
        WiFiClient        plainClient;
        int               code      = -1;

        {   // Inner scope: HTTPClient is destroyed here, BEFORE secClient is deleted.
            // Deleting secClient while HTTPClient still holds a reference to it causes
            // a crash in ~HTTPClient() when it tries to close the connection.
            HTTPClient http;
            http.setConnectTimeout(15000);
            http.setTimeout(10000);
            http.setUserAgent("ESP32-SatTracker/1.0");
            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            http.setRedirectLimit(3);
            const char* responseHeaders[] = {"Content-Type"};
            http.collectHeaders(responseHeaders, 1);

            bool begun = false;
            if (secure) {
                secClient = new WiFiClientSecure();
                secClient->setInsecure();
                secClient->setHandshakeTimeout(12);
                begun = http.begin(*secClient, url);
            } else {
                begun = http.begin(plainClient, url);
            }

            if (!begun) {
                BootDiagnostics::log("[http] ERROR: begin() failed");
            } else {
                code = http.GET();
                lastCode = code;
                BootDiagnostics::log("[http] Status=%d content-type=%s elapsed=%lu ms", code,
                                     http.header("Content-Type").c_str(), (unsigned long)(millis() - started));
                if (code == HTTP_CODE_OK) {
                    body = http.getString();
                    BootDiagnostics::log("[http] %u bytes received", (unsigned)body.length());
                } else if (code < 0) {
                    BootDiagnostics::log("[http] Transport error: %s", HTTPClient::errorToString(code).c_str());
                    if (secClient) {
                        char error[160] = {};
                        int tlsCode = secClient->lastError(error, sizeof(error));
                        BootDiagnostics::log("[http] TLS error: %d %s; free heap=%u", tlsCode, error, ESP.getFreeHeap());
                    }
                } else {
                    // Keep the diagnostic bounded, including for large HTML errors.
                    char preview[257] = {};
                    WiFiClient* stream = http.getStreamPtr();
                    size_t limit = sizeof(preview) - 1;
                    if (http.getSize() >= 0 && (size_t)http.getSize() < limit)
                        limit = (size_t)http.getSize();
                    size_t received = 0;
                    if (stream) {
                        stream->setTimeout(1000);
                        received = stream->readBytes(preview, limit);
                    }
                    preview[received] = '\0';
                    BootDiagnostics::log("[http] Server response: %.180s", preview);
                }
                http.end();
            }
        }   // ~HTTPClient() runs here — secClient is still valid

        if (secClient) { delete secClient; secClient = nullptr; }

        if (code == HTTP_CODE_OK) {
            if (finalCode) *finalCode = code;
            return body;
        }

        BootDiagnostics::log("[http] ERROR: code %d", code);
        // Repeating HTTP errors can trigger CelesTrak's IP blocking. Only
        // retry transport failures; let callers apply their normal cooldown.
        if (code > 0) break;
        if (attempt < retries) {
            BootDiagnostics::log("[http] Retrying in 2 s...");
            delay(2000);
        }
    }
    if (finalCode) *finalCode = lastCode;
    BootDiagnostics::log("[http] Request failed; final status=%d", lastCode);
    return body;
}

// One bounded network job. The worker never accesses LVGL or LittleFS; callers
// consume its result on the UI task before updating widgets or the TLE cache.
static std::atomic<bool> _asyncBusy{false};
static std::atomic<bool> _asyncReady{false};
static char _asyncUrl[224];
static String _asyncBody;
static int _asyncCode = -1;
static void _getTask(void*) {
    _asyncBody = get(_asyncUrl, true, 1, &_asyncCode);
    _asyncReady.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
}
inline bool startAsyncGet(const char* url) {
    if (!url || strlen(url) >= sizeof(_asyncUrl) || _asyncBusy.exchange(true)) return false;
    snprintf(_asyncUrl, sizeof(_asyncUrl), "%s", url);
    _asyncReady.store(false);
    if (xTaskCreate(_getTask, "tle_http", 12288, nullptr, 1, nullptr) != pdPASS) {
        _asyncBusy.store(false);
        BootDiagnostics::log("[http] Cannot allocate network task");
        return false;
    }
    return true;
}
inline bool takeAsyncResult(String& body, int& code) {
    if (!_asyncReady.load(std::memory_order_acquire)) return false;
    body = std::move(_asyncBody);
    code = _asyncCode;
    _asyncReady.store(false);
    _asyncBusy.store(false);
    return true;
}

} // namespace HttpUtils
