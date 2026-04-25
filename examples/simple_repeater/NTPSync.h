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

      const unsigned long kFirstRunDelayMs  = 5000UL;           // 5s после загрузки
      const unsigned long kRepeatIntervalMs = prefs->ntp_interval * 60 * 60 * 1000; // интервал между синхронизациями, из настроек (часы -> мс)
      const unsigned long kMinValidEpoch    = 1704067200UL;     // 2024-01-01 00:00:00 UTC
      const unsigned long kWiFiTimeoutMs    = 5000UL;
      const int           kMaxRetries       = 3;

      static unsigned long lastSyncAt = 0;  // 0 = никогда не синхронизировались
      unsigned long now = millis();

      // Первый запуск: ждём kFirstRunDelayMs после загрузки.
      // Последующие: повторяем каждые kRepeatIntervalMs.
      bool due = (lastSyncAt == 0)
                   ? (now >= kFirstRunDelayMs)
                   : ((unsigned long)(now - lastSyncAt) >= kRepeatIntervalMs);
      if (!due) return;

      // Проверка учётных данных: пропускаем, если не настроены или оставлены по умолчанию
      if (!prefs
          || strlen(prefs->wifi_ssid)     == 0 || strcmp(prefs->wifi_ssid,     "ssid_here")     == 0
          || strlen(prefs->wifi_password) == 0 || strcmp(prefs->wifi_password, "password_here") == 0) {
        lastSyncAt = now;  // отмечаем как попытку, чтобы таймер продолжал идти
        return;
      }

      WiFi.mode(WIFI_STA);
      WiFi.begin(prefs->wifi_ssid, prefs->wifi_password);

      unsigned long t0 = millis();
      while (WiFi.status() != WL_CONNECTED &&
             (unsigned long)(millis() - t0) < kWiFiTimeoutMs) {
        delay(100);
      }

      if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        lastSyncAt = millis();
        return;
      }

      bool          ntp_ok    = false;
      unsigned long epochTime = 0;
      unsigned long fetchedAt = 0;  // момент получения времени — для компенсации задержки

      // ── Основной метод: NTPClient ─────────────────────────────────────────────
      _detail::client.begin();
      for (int attempt = 1; attempt <= kMaxRetries && !ntp_ok; attempt++) {
        if (attempt > 1) {
          delay(500);
        }
        if (_detail::client.forceUpdate()) {
          epochTime = _detail::client.getEpochTime();
          if (epochTime >= kMinValidEpoch) {
            fetchedAt = millis();  // фиксируем момент получения
            ntp_ok = true;
          }
        }
      }
      _detail::client.end();

      // ── Резервный метод: встроенный SNTP ESP32 ────────────────────────────────
      if (!ntp_ok) {
        configTime(0, 0, "pool.ntp.org");
        for (int i = 0; i < 20 && !ntp_ok; i++) {
          delay(500);
          epochTime = (unsigned long)time(nullptr);
          if (epochTime >= kMinValidEpoch) {
            fetchedAt = millis();  // фиксируем момент получения
            ntp_ok = true;
          }
        }
      }

      // ── Применяем результат ───────────────────────────────────────────────────
      if (ntp_ok && rtc) {
        // Компенсируем время, прошедшее между получением epochTime и записью в RTC.
        // Без этой поправки каждая синхронизация даёт ошибку ~+60 с.
        unsigned long elapsedSec = (millis() - fetchedAt) / 1000UL;
        rtc->setCurrentTime(epochTime + elapsedSec);
      }

      // ── Отключаем WiFi ────────────────────────────────────────────────────────
      lastSyncAt = millis();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }

  } // namespace NTPSync

#endif // ESP32