#pragma once
#ifdef ESP32

  #include <Arduino.h>
  #include <WiFi.h>
  #include <NTPClient.h>
  #include <WiFiUdp.h>
  #include <time.h>

  namespace NTPSync {

    namespace _detail {
      static WiFiUDP   udp;
      static NTPClient client(udp, "pool.ntp.org", 0, 60000);
    }

    template <typename TPrefs, typename TRtc>
    static void sync(const TPrefs* prefs, TRtc* rtc) {

      const unsigned long kFirstRunDelayMs  = 5000UL;           //  5s after boot
      const unsigned long kRepeatIntervalMs = 24UL*60*60*1000;  //  24h

      static unsigned long lastSyncAt = 0;  // 0 = never synced
      unsigned long now = millis();

      // First run: wait kFirstRunDelayMs after boot.
      // Subsequent runs: repeat every kRepeatIntervalMs.
      bool due = (lastSyncAt == 0)
                   ? (now >= kFirstRunDelayMs)
                   : ((unsigned long)(now - lastSyncAt) >= kRepeatIntervalMs);
      if (!due) return;

      // Credentials guard: skip if not configured or still at defaults
      if (!prefs
          || strlen(prefs->wifi_ssid)     == 0 || strcmp(prefs->wifi_ssid,     "ssid_here")     == 0
          || strlen(prefs->wifi_password) == 0 || strcmp(prefs->wifi_password, "password_here") == 0) {
        lastSyncAt = now;  // mark as attempted so timer keeps ticking
        return;
      }

      WiFi.mode(WIFI_STA);
      WiFi.begin(prefs->wifi_ssid, prefs->wifi_password);

      const unsigned long kWiFiTimeoutMs = 5000UL;
      unsigned long t0 = millis();
      while (WiFi.status() != WL_CONNECTED &&
            (unsigned long)(millis() - t0) < kWiFiTimeoutMs) {
        delay(100);
      }

      if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
      }

      const unsigned long kMinValidEpoch = 1704067200UL; // 2024-01-01 00:00:00 UTC
      const int           kMaxRetries    = 3;
      bool          ntp_ok    = false;
      unsigned long epochTime = 0;

      IPAddress resolved;

      _detail::client.begin();
      for (int attempt = 1; attempt <= kMaxRetries && !ntp_ok; attempt++) {
        if (attempt > 1) {
          delay(500);
        }
        if (_detail::client.forceUpdate()) {
          epochTime = _detail::client.getEpochTime();
          if (epochTime >= kMinValidEpoch) {
            ntp_ok = true;
          }
        }
      }
      _detail::client.end();

      // ── Fallback: ESP32 built-in SNTP ────────────────────────────────────────
      if (!ntp_ok) {
        configTime(0, 0, "pool.ntp.org");
        for (int i = 0; i < 20 && !ntp_ok; i++) {
          delay(500);
          epochTime = (unsigned long)time(nullptr);
          if (epochTime >= kMinValidEpoch) {
            ntp_ok = true;
          }
        }
      }

      // ── Apply result ──────────────────────────────────────────────────────────
      if (ntp_ok) {
        configTime(0, 0, "pool.ntp.org"); // keep system clock in UTC
        if (rtc) {
          rtc->setCurrentTime(epochTime);
        }
      }

      // ── Disconnect WiFi ───────────────────────────────────────────────────────
      lastSyncAt = millis();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
  }
#endif // ESP32