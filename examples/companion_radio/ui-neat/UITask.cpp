#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#ifdef WIFI_SSID
  #include <WiFi.h>
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS    10000   // 10 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds
#define TIMEZONE_OFFSET      10800  // UTC+3 in seconds
#define DISPLAY_CONTRAST     255
#define SCREENSAVER_CONTRAST 1

#ifdef  PIN_STATUS_LED
  #define LED_ON_MILLIS     20
  #define LED_ON_MSG_MILLIS 200
  #define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   5000
#define HIBERNATE_CANCEL_MILLIS 3000

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash by changing dash to null terminator
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    // meshcore logo
    display.setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    display.drawXbm((display.width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // version info
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(2);
    display.drawTextCentered(display.width()/2, 22, _version_info);

    display.setTextSize(1);
    #ifdef OLED_RU
        char filtered_date[sizeof(FIRMWARE_BUILD_DATE)];
        display.translateUTF8ToBlocks(filtered_date, FIRMWARE_BUILD_DATE, sizeof(filtered_date));
        display.drawTextCentered(display.width()/2, 42, filtered_date);
    #else
        display.drawTextCentered(display.width()/2, 42, FIRMWARE_BUILD_DATE);
    #endif

    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

class HomeScreen : public UIScreen {
  enum HomePage {
    FIRST,
    RECENT,
    RADIO,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
    TIME,  
    Count,         // keep last
    SCREENSAVER    // reserved for screensaver
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  AdvertPath recent[UI_RECENT_LIST_SIZE];


  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
    // Convert millivolts to percentage
    #ifndef BATT_MIN_MILLIVOLTS
      #define BATT_MIN_MILLIVOLTS 3300
    #endif
    #ifndef BATT_MAX_MILLIVOLTS
      #define BATT_MAX_MILLIVOLTS 4200
    #endif
    const int minMilliVolts = BATT_MIN_MILLIVOLTS;
    const int maxMilliVolts = BATT_MAX_MILLIVOLTS;
    int batteryPercentage = ((batteryMilliVolts - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
    if (batteryPercentage < 0) batteryPercentage = 0;      // Clamp to 0%
    if (batteryPercentage > 100) batteryPercentage = 100;  // Clamp to 100%

    // battery icon
    int iconWidth = 24;
    int iconHeight = 7;
    int iconX = display.width() - iconWidth - 2; // Position the icon near the top-right corner
    int iconY = 0;
    display.setContrast(DISPLAY_CONTRAST);
    display.setColor(DisplayDriver::GREEN);

    // battery outline
    display.drawXbm(iconX, iconY, batt_outline, iconWidth, iconHeight);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 2)) / 100;
    display.fillRect(iconX + 1, iconY + 1, fillWidth, iconHeight - 2);

    // show muted icon if buzzer is muted
#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(DisplayDriver::RED);
      display.drawXbm(iconX - 9, iconY + 1, muted_icon, 8, 8);
    }
#endif

    // battery percent and voltage readings in DEBUG 
    #ifdef BATTERY_DEBUG
      char tmp[80];
      if (!show_volt) sprintf(tmp, "%d%%", batteryPercentage);
      else sprintf(tmp, "%.2f", batteryMilliVolts/1000.0f);
      #ifdef ST7789
        display.drawTextCentered(114, 7, tmp);
      #else
        display.drawTextCentered(115, 9, tmp);
      #endif

    #endif
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;
  
  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  uint8_t _page;
  uint8_t _page_before_screensaver = HomePage::FIRST;
  bool show_volt = false;
  
  void activateScreensaver() {
    if (_page != HomePage::SCREENSAVER) {
      _page_before_screensaver = _page;
    }
    _page = HomePage::SCREENSAVER;
  }

  void deactivateScreensaver() {
    _page = _page_before_screensaver;  
  }
  
  bool isScreensaverActive() const {
    return _page == HomePage::SCREENSAVER;
  }

  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0), sensors_lpp(200) {  }

  int render(DisplayDriver& display) override {
    char tmp[80];

    if (_page == HomePage::SCREENSAVER) {
      display.setColor(DisplayDriver::LIGHT);
      
      if (_node_prefs->screensaver_dimmed) {
        display.setContrast(SCREENSAVER_CONTRAST);
      } else {
        display.setContrast(DISPLAY_CONTRAST);
      }
      
      uint32_t current_time = _rtc->getCurrentTime() + TIMEZONE_OFFSET;
      DateTime dt(current_time);
      sprintf(tmp, "%02d:%02d", dt.hour(), dt.minute());
      #ifdef ST7789
      display.setTextSize(2);
      display.drawTextCentered(display.width()/2, 13, tmp);
      #else
      display.setTextSize(4);
      display.drawTextCentered(display.width()/2, 11, tmp);
      #endif
      display.setTextSize(1);
      sprintf(tmp, "%02d.%02d.%04d", dt.day(), dt.month(), dt.year());
      #ifdef ST7789
      display.drawTextCentered(display.width()/2, 41, tmp);
      #else:
      display.drawTextCentered(display.width()/2, 48, tmp);
      #endif
      return 1000;  // refresh every second
    }

    // node name
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    int iconWidth = 24;                       // Must match iconWidth in renderBatteryIndicator
    int max_name_width = display.width() - iconWidth - 4;
    display.drawTextEllipsized(2, 0, max_name_width, filtered_name);
    // battery voltage
    renderBatteryIndicator(display, _task->getBattMilliVolts());

    // curr page indicator
    int y = 14;
    int x = display.width() / 2 - 5 * (HomePage::Count-1);
    for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
      if (i == _page) {
        display.fillRect(x-1, y-1, 3, 3);
      } else {
        display.fillRect(x, y, 1, 1);
      }
    }

    if (_page == HomePage::FIRST) {
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(2);
      sprintf(tmp, "MSG:%d", _task->getMsgCount());
      #ifdef ST7789
        display.drawTextCentered(display.width()/2, 26, tmp);
      #else
        display.drawTextCentered(display.width() / 2, 20, tmp);
      #endif
      #ifdef WIFI_SSID
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 54, tmp); 
      #endif
      if (_task->hasConnection()) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 43, "< Connected >");

      } else if (the_mesh.getBLEPin() != 0) { // BT pin
        display.setColor(DisplayDriver::RED);
        display.setTextSize(2);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, 43, tmp);
      }
    } else if (_page == HomePage::RECENT) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      if(recent[0].name[0] == NULL) {
        display.setColor(DisplayDriver::RED);
        display.setTextSize(1);
        display.drawTextCentered(display.width()/2, 32, "No adverts received");
        return 1000;
      }
      display.setColor(DisplayDriver::GREEN);
      int y = 20;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++, y += 11) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(tmp, "%ds", secs);
        } else if (secs < 60*60) {
          sprintf(tmp, "%dm", secs / 60);
        } else {
          sprintf(tmp, "%dh", secs / (60*60));
        }
        
        int timestamp_width = display.getTextWidth(tmp);
        int max_name_width = display.width() - timestamp_width - 1;
        
        char filtered_recent_name[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_recent_name, a->name, sizeof(filtered_recent_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_recent_name);
        display.setCursor(display.width() - timestamp_width - 1, y);
        display.print(tmp);
      }
    } else if (_page == HomePage::RADIO) {
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(1);
      // freq / sf
      display.setCursor(0, 20);
      sprintf(tmp, "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
      display.print(tmp);

      display.setCursor(0, 31);
      sprintf(tmp, "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
      display.print(tmp);

      // tx power,  noise floor
      display.setCursor(0, 42);
      sprintf(tmp, "TX: %ddBm", _node_prefs->tx_power_dbm);
      display.print(tmp);
      display.setCursor(0, 53);
      sprintf(tmp, "Noise floor: %ddB", radio_driver.getNoiseFloor());
      display.print(tmp);

#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = 18;
      bool gps_state = _task->getGPSState();
#ifdef PIN_GPS_SWITCH
      bool hw_gps_state = digitalRead(PIN_GPS_SWITCH);
      if (gps_state != hw_gps_state) {
        strcpy(buf, gps_state ? "gps off(hw)" : "gps off(sw)");
      } else {
        strcpy(buf, gps_state ? "gps on" : "gps off");
      }
#else
      strcpy(buf, gps_state ? "gps on" : "gps off");
#endif
      display.drawTextLeftAlign(0, y, buf);
      if (nmea == NULL) {
        y = y + 12;
        display.drawTextLeftAlign(0, y, "Can't access GPS");
      } else {
        strcpy(buf, nmea->isValid()?"fix":"no fix");
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "sat");
        sprintf(buf, "%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "pos");
        sprintf(buf, "%.4f %.4f", 
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "alt");
        sprintf(buf, "%.2f", nmea->getAltitude()/1000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
      }
#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      int y = 18;
      refresh_sensors();
      char buf[30];
      char name[30];
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

      for (int i = 0; i < sensors_scroll_offset; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      for (int i = 0; i < (sensors_scroll?UI_RECENT_LIST_SIZE:sensors_nb); i++) {
        uint8_t channel, type;
        if (!r.readHeader(channel, type)) { // reached end, reset
          r.reset();
          r.readHeader(channel, type);
        }

        display.setCursor(0, y);
        float v;
        switch (type) {
          case LPP_GPS: // GPS
            float lat, lon, alt;
            r.readGPS(lat, lon, alt);
            strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
            break;
          case LPP_VOLTAGE:
            r.readVoltage(v);
            strcpy(name, "voltage"); sprintf(buf, "%6.2f", v);
            break;
          case LPP_CURRENT:
            r.readCurrent(v);
            strcpy(name, "current"); sprintf(buf, "%.3f", v);
            break;
          case LPP_TEMPERATURE:
            r.readTemperature(v);
            strcpy(name, "temperature"); sprintf(buf, "%.2f", v);
            break;
          case LPP_RELATIVE_HUMIDITY:
            r.readRelativeHumidity(v);
            strcpy(name, "humidity"); sprintf(buf, "%.2f", v);
            break;
          case LPP_BAROMETRIC_PRESSURE:
            r.readPressure(v);
            strcpy(name, "pressure"); sprintf(buf, "%.2f", v);
            break;
          case LPP_ALTITUDE:
            r.readAltitude(v);
            strcpy(name, "altitude"); sprintf(buf, "%.0f", v);
            break;
          case LPP_POWER:
            r.readPower(v);
            strcpy(name, "power"); sprintf(buf, "%6.2f", v);
            break;
          default:
            r.skipData(type);
            strcpy(name, "unk"); sprintf(buf, "");
        }
        display.setCursor(0, y);
        display.print(name);
        display.setCursor(
          display.width()-display.getTextWidth(buf)-1, y
        );
        display.print(buf);
        y = y + 12;
      }
      if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset+1)%sensors_nb;
      else sensors_scroll_offset = 0;
      #endif
    } else if (_page == HomePage::TIME) {
    uint32_t current_time = rtc_clock.getCurrentTime() + TIMEZONE_OFFSET;
    DateTime dt(current_time);
    sprintf(tmp, "%02d:%02d", dt.hour(), dt.minute());
  #ifdef ST7789
    display.setTextSize(2);
    display.drawTextCentered(display.width()/2, 26, tmp);
  #else
    display.setTextSize(4);
    display.drawTextCentered(display.width()/2, 21, tmp);
  #endif
    display.setTextSize(1);
    sprintf(tmp, "%02d.%02d.%04d", dt.day(), dt.month(), dt.year());
    display.drawTextCentered(display.width()/2, 54, tmp);
    }
    return 5000;   // next render after 5000 ms
  }

  bool handleInput(char c) override {
   
    if (c == KEY_ENTER && display.isOn() && _page == HomePage::FIRST) {
      show_volt = !show_volt;
      return true;
    }

    if (_page == HomePage::SCREENSAVER) {
      if (c == KEY_ENTER) {
        _node_prefs->screensaver_dimmed = !_node_prefs->screensaver_dimmed;
        the_mesh.savePrefs();
        c = 0;
        return true;
      }
        _page = _page_before_screensaver;  // go back to previous page
        c = 0;
      return true;
    }

    if (c == KEY_LEFT || c == KEY_PREV) {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _page = (_page + 1) % HomePage::Count;
      if (_page == HomePage::RECENT) {
        _task->showAlert("Send: long press", 1000);
      }
      return true;
    }
    if (c == KEY_ENTER && display.isOn() && _page == HomePage::RADIO) {
      if (_task->isSerialEnabled()) {  // toggle Bluetooth on/off
        _task->disableSerial();
        _task->showAlert("Bluetooth OFF", 1000);
      } else {
        _task->enableSerial();
        _task->showAlert("Bluetooth ON", 1000);
      }
      return true;
    }
    if (c == KEY_ENTER && display.isOn() && _page == HomePage::RECENT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert("Advert sent!", 1000);
      } else {
        _task->showAlert("Advert failed..", 1000);
      }
      return true;
    }
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && display.isOn() && _page == HomePage::GPS) {
      _task->toggleGPS();
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && display.isOn() && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
      return true;
    }
#endif

    if (c == KEY_ENTER && display.isOn() && _page == HomePage::TIME) {
      _task->toggleScreensaver();
      return true;
    }
    return false;
  }
};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[78];
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  int head = MAX_UNREAD_MSGS - 1; // index of latest unread message
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    head = (head + 1) % MAX_UNREAD_MSGS;
    if (num_unread < MAX_UNREAD_MSGS) num_unread++;

    auto p = &unread[head];
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    char tmp[16];
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    sprintf(tmp, "Unread: %d", num_unread);
    display.print(tmp);

    auto p = &unread[head];

    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(tmp, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(tmp, "%dm", secs / 60);
    } else {
      sprintf(tmp, "%dh", secs / (60*60));
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);  // horiz line

    display.setCursor(0, 14);
    display.setColor(DisplayDriver::YELLOW);
    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));
    display.print(filtered_origin);

    display.setCursor(0, 25);
    display.setColor(DisplayDriver::LIGHT);
    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    display.printWordWrap(filtered_msg, display.width());

#if AUTO_OFF_MILLIS==0 // probably e-ink
    return 10000; // 10 s
#else
    return 1000;  // next render after 1000 ms
#endif
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
      num_unread--;
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      }
      return true;
    }
    if (c == KEY_ENTER) {
      num_unread = 0;  // clear unread queue
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
  _analogue_pin_read_millis = millis();
#endif

  _node_prefs = node_prefs;

#if ENV_INCLUDE_GPS == 1
  // Initialize GPS hardware pins
  #ifdef PIN_GPS_EN
    pinMode(PIN_GPS_EN, OUTPUT);
    // Immediately set to OFF state to prevent power drain
    #ifdef PIN_GPS_EN_ACTIVE
      digitalWrite(PIN_GPS_EN, !PIN_GPS_EN_ACTIVE);  // Disable GPS power
    #else
      digitalWrite(PIN_GPS_EN, LOW);  // Default: disable GPS power
    #endif
  #endif
  #ifdef PIN_GPS_STANDBY
    pinMode(PIN_GPS_STANDBY, OUTPUT);
    // Immediately set to standby/sleep state
    #ifdef PIN_GPS_STANDBY_ACTIVE
      digitalWrite(PIN_GPS_STANDBY, PIN_GPS_STANDBY_ACTIVE);  // Put GPS to sleep
    #else
      digitalWrite(PIN_GPS_STANDBY, LOW);  // Default: put GPS to sleep
    #endif
  #endif
  #ifdef PIN_GPS_RESET
    pinMode(PIN_GPS_RESET, OUTPUT);
    #ifdef PIN_GPS_RESET_ACTIVE
      digitalWrite(PIN_GPS_RESET, !PIN_GPS_RESET_ACTIVE);  // Keep GPS out of reset
    #else
      digitalWrite(PIN_GPS_RESET, HIGH);  // Default: keep GPS out of reset
    #endif
  #endif

  // Apply GPS preferences from stored prefs
  if (_sensors != NULL && _node_prefs != NULL) {
    _sensors->setSettingValue("gps", _node_prefs->gps_enabled ? "1" : "0");
    
    // Set hardware GPS state based on preferences
    setGPSHardwareState(_node_prefs->gps_enabled);
    
    if (_node_prefs->gps_interval > 0) {
      char interval_str[12];  // Max: 24 hours = 86400 seconds (5 digits + null)
      sprintf(interval_str, "%u", _node_prefs->gps_interval);
      _sensors->setSettingValue("gps_interval", interval_str);
    }
  }
#endif

  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  screensaver_on = _node_prefs->screensaver_enabled;
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  setCurrScreen(splash);
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}


void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) {
    gotoHomeScreen();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;

  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from_name, text);
  
  bool on_screensaver = (curr == home && ((HomeScreen*)home)->isScreensaverActive());
  if (!on_screensaver) { setCurrScreen(msg_preview); }

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    bool on_screensaver = (curr == home && ((HomeScreen*)home)->isScreensaverActive());
    if (_display->isOn() && !on_screensaver) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
      _next_refresh = 100;
    }
  }
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    // Turn off GPS if it's enabled before shutdown to save power
    if (getGPSState()) {
      setGPSHardwareState(false);
    }
    
    _display->turnOff();
    radio_driver.powerOff();
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {

  handleHibernation();
    if (_hibernation_pending) {
      #ifdef PIN_BUZZER
      if (buzzer.isPlaying()) buzzer.loop();
      #endif
      #ifdef PIN_VIBRATION
      vibration.loop();
      #endif
      return;
  }
  char c = 0;
  #if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);  // REVISIT: could be mapped to different key code
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  } else if (ev == BUTTON_EVENT_QUADRUPLE_CLICK) {
    c = handleQuadrupleClick(KEY_SELECT);
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(DisplayDriver::LIGHT);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
    if (millis() > _auto_off && !_forceBacklight) {
      if (screensaver_on && curr == home) {
        ((HomeScreen*)home)->activateScreensaver();
        _next_refresh = 0;
      } else {
        _display->turnOff();
      }
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {

      // show low battery shutdown alert
      // we should only do this for eink displays, which will persist after power loss
      #if defined(THINKNODE_M1) || defined(LILYGO_TECHO)
      if (_display != NULL) {
        _display->startFrame();
        _display->setTextSize(2);
        _display->setColor(DisplayDriver::RED);
        _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
        _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
        _display->endFrame();
      }
      #endif

      shutdown();

    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (_display->isOn() && curr == home && ((HomeScreen*)home)->isScreensaverActive()) {
      ((HomeScreen*)home)->deactivateScreensaver(); 
      c = 0;  // consume event
    }
        if (!_display->isOn()) {
      _display->turnOn();
      c = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 0;
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
    c = 0;   // consume event
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

char UITask::handleQuadrupleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: quadruple click triggered");
  checkDisplayOn(c);
  toggleBacklight();
  c = 0;
  return c;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  } 
  return false;
}

void UITask::toggleScreensaver() {
  screensaver_on = !screensaver_on;
  _node_prefs->screensaver_enabled = screensaver_on;
  the_mesh.savePrefs();
  showAlert(screensaver_on ? "Screensaver: ON" : "Screensaver: OFF", 1000);
  _next_refresh = 0;
}

void UITask::toggleBacklight() {
    _forceBacklight = !_forceBacklight;
    showAlert(_forceBacklight ? "Backlight: ALWAYS" : "Backlight: BUTTON ", 1000);
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 0;
}

void UITask::setGPSHardwareState(bool enabled) {
  if (enabled) {
    // Hardware GPS control - turn ON
    #ifdef PIN_GPS_EN
      #ifdef PIN_GPS_EN_ACTIVE
        digitalWrite(PIN_GPS_EN, PIN_GPS_EN_ACTIVE);  // Enable GPS power
      #else
        digitalWrite(PIN_GPS_EN, HIGH);  // Default: enable GPS power
      #endif
    #endif
    #ifdef PIN_GPS_STANDBY
      delay(100);  // Wait for power stabilization
      #ifdef PIN_GPS_STANDBY_ACTIVE
        digitalWrite(PIN_GPS_STANDBY, !PIN_GPS_STANDBY_ACTIVE);  // Wake GPS from sleep (inverse of active)
      #else
        digitalWrite(PIN_GPS_STANDBY, HIGH);  // Default: wake GPS from sleep
      #endif
    #endif
  } else {
    // Hardware GPS control - turn OFF
    #ifdef PIN_GPS_EN
      #ifdef PIN_GPS_EN_ACTIVE
        digitalWrite(PIN_GPS_EN, !PIN_GPS_EN_ACTIVE);  // Disable GPS power
      #else
        digitalWrite(PIN_GPS_EN, LOW);  // Default: disable GPS power
      #endif
    #endif
    #ifdef PIN_GPS_STANDBY
      #ifdef PIN_GPS_STANDBY_ACTIVE
        digitalWrite(PIN_GPS_STANDBY, PIN_GPS_STANDBY_ACTIVE);  // Put GPS to sleep
      #else
        digitalWrite(PIN_GPS_STANDBY, LOW);  // Default: put GPS to sleep
      #endif
    #endif
  }
}

void UITask::toggleGPS() {
  if (_sensors != NULL) {
    // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        bool current_state = strcmp(_sensors->getSettingValue(i), "1") == 0;
        bool new_state = !current_state;
        
        // Update software state
        _sensors->setSettingValue("gps", new_state ? "1" : "0");
        _node_prefs->gps_enabled = new_state ? 1 : 0;
        
        // Update hardware state
        setGPSHardwareState(new_state);
        
        notify(UIEventType::ack);
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;  // trigger refresh
  #else
    showAlert("Buzzer N/A", 1000);
  #endif
}

void UITask::handleHibernation() {
  
    if (curr != home || ((HomeScreen*)home)->_page != 0) {
      return;
    }
  
    bool button_pressed = isButtonPressed();
    
    // Detect button press start
    if (button_pressed && !_button_was_pressed) {
        _button_press_start = millis();
        _button_was_pressed = true;
    }
    
    // Detect button release
    if (!button_pressed && _button_was_pressed) {
        _button_was_pressed = false;
        _button_press_start = 0;
        
        // If hibernation was pending and button released, proceed to hibernate
        if (_hibernation_pending) {
            _hibernation_pending = false;
            shutdown(false); // false = hibernate, true = restart
        }
    }
    
    // Check for long press
    if (button_pressed && _button_was_pressed) {
        unsigned long press_duration = millis() - _button_press_start;
        
        // Initial long press (5 seconds) - show warning
        if (press_duration >= LONG_PRESS_MILLIS && !_hibernation_pending && millis() - ui_started_at > 10000) {
            _hibernation_pending = true;
            _auto_off = millis() + AUTO_OFF_MILLIS + HIBERNATE_CANCEL_MILLIS;
            
            if (_display != NULL) {
                _display->startFrame();
                _display->setTextSize(1);
                _display->setColor(DisplayDriver::YELLOW);
                _display->drawTextCentered(_display->width() / 2, 15, "Release to POWER OFF");
                _display->drawTextCentered(_display->width() / 2, 26, "...");
                _display->drawTextCentered(_display->width() / 2, 40, "or hold to CANCEL");

                _display->endFrame();
            }
            
            #ifdef PIN_BUZZER
            buzzer.play("hibernate:d=8,o=6,b=180:c,e,g");
            #endif
            
            #ifdef PIN_VIBRATION
            vibration.trigger();
            #endif
        }
        
        // If still holding after warning - CANCEL hibernation
        if (_hibernation_pending && press_duration >= (LONG_PRESS_MILLIS + HIBERNATE_CANCEL_MILLIS)) {
            _hibernation_pending = false;
            _button_was_pressed = false;  // Reset button state
            _button_press_start = 0;
                        
            #ifdef PIN_BUZZER
            buzzer.play("cancel:d=8,o=6,b=180:g,e,c");  // reverse melody
            #endif
            
            #ifdef PIN_VIBRATION
            vibration.trigger();
            #endif
            
            // Show alert in UI
            showAlert("Power off cancelled", 1500);
            _next_refresh = 0;
        }
    }
}