/**
 * @file time_sync.cpp
 * @brief WiFi time sync implementation. See time_sync.h.
 */
#include "time_sync.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "persist.h"

/* The firmware keeps no TZ set (newlib default = UTC), so localtime() is the
 * identity on the epoch and the clock holds local wall-clock directly. We match
 * that: system time and RTC both store local wall-clock, timezone folded in. */

static DEVICES* s_dev = nullptr;

void time_sync_attach(DEVICES* dev) { s_dev = dev; }

/* Join the WiFi saved in Settings if not already connected. */
static bool ensure_wifi(uint32_t timeout_ms)
{
    if (WiFi.status() == WL_CONNECTED) return true;

    char ssid[33] = {}, pass[65] = {};
    persist_get_str(PKEY_WIFI_SSID, ssid, sizeof(ssid), "");
    persist_get_str(PKEY_WIFI_PASS, pass, sizeof(pass), "");
    if (ssid[0] == '\0') return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeout_ms) delay(100);
    return WiFi.status() == WL_CONNECTED;
}

/* NTP → UTC epoch. Stops the SNTP updater afterwards so it can't later
 * overwrite the local time we set. */
static bool ntp_utc(time_t* out, uint32_t timeout_ms)
{
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms) {
        time_t now = time(nullptr);
        if (now > 1700000000) {          /* 2023-11 — clearly a real timestamp */
            sntp_stop();
            *out = now;
            return true;
        }
        delay(100);
    }
    sntp_stop();
    return false;
}

/* IP-geolocation → this connection's UTC offset in seconds. Plain HTTP (no TLS
 * stack pulled in). ip-api returns e.g. {"status":"success","offset":7200}. */
static bool ip_offset(long* out)
{
    HTTPClient http;
    http.setConnectTimeout(6000);
    http.setTimeout(6000);
    if (!http.begin("http://ip-api.com/json/?fields=status,offset")) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return false; }
    String body = http.getString();
    http.end();

    if (body.indexOf("\"success\"") < 0) return false;
    int k = body.indexOf("\"offset\"");
    if (k < 0) return false;
    k = body.indexOf(':', k);
    if (k < 0) return false;
    *out = strtol(body.c_str() + k + 1, nullptr, 10);
    return true;
}

/* Write a local wall-clock epoch to both the system clock and the RTC. */
static void apply_local(time_t local_epoch)
{
    struct timeval tv = { .tv_sec = local_epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);

    if (s_dev) {
        struct tm lt;
        gmtime_r(&local_epoch, &lt);     /* TZ=UTC → fields are the local w-clock */
        RTC_Time rt;
        rt.year    = (uint16_t)(lt.tm_year + 1900);
        rt.month   = (uint8_t)(lt.tm_mon + 1);
        rt.day     = (uint8_t)lt.tm_mday;
        rt.weekday = (uint8_t)lt.tm_wday;
        rt.hour    = (uint8_t)lt.tm_hour;
        rt.min     = (uint8_t)lt.tm_min;
        rt.sec     = (uint8_t)lt.tm_sec;
        s_dev->rtc.setTime(rt);
    }
}

bool time_sync_now(char* status, int status_len)
{
    if (!ensure_wifi(8000)) {
        snprintf(status, status_len, "No WiFi — connect in Settings");
        return false;
    }

    time_t utc = 0;
    if (!ntp_utc(&utc, 8000)) {
        snprintf(status, status_len, "NTP unreachable");
        return false;
    }

    long offset = 0;
    if (!ip_offset(&offset)) {
        snprintf(status, status_len, "Timezone lookup failed");
        return false;
    }

    time_t local = utc + offset;
    apply_local(local);

    struct tm lt;
    gmtime_r(&local, &lt);
    long oh = offset / 3600;
    long om = labs(offset % 3600) / 60;
    snprintf(status, status_len, "%02d:%02d  (UTC%+ld:%02ld)",
             lt.tm_hour, lt.tm_min, oh, om);
    return true;
}

void time_sync_restore_from_rtc(void)
{
    if (!s_dev) return;
    RTC_Time t;
    if (!s_dev->rtc.getTime(t)) return;
    if (t.year < 2020) return;           /* RTC never set — nothing to restore */

    struct tm lt = {};
    lt.tm_year  = t.year - 1900;
    lt.tm_mon   = t.month - 1;
    lt.tm_mday  = t.day;
    lt.tm_hour  = t.hour;
    lt.tm_min   = t.min;
    lt.tm_sec   = t.sec;
    lt.tm_isdst = 0;
    time_t e = mktime(&lt);              /* TZ=UTC → treats fields as wall-clock */
    if (e <= 0) return;
    struct timeval tv = { .tv_sec = e, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
}
