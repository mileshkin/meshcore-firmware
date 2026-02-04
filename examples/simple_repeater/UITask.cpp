#include "UITask.h"
#include "MyMesh.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>
#include "icons.h"
#include <MeshCore.h>


#define AUTO_OFF_MILLIS      15000  // 15 seconds

#ifdef HELTEC_LORA_V4
  #define BOOT_SCREEN_MILLIS   8000   // 8 seconds
#else
  #define BOOT_SCREEN_MILLIS   4000   // 4 seconds
#endif

#define TIMEZONE_OFFSET       10800  // UTC+3 in seconds
#define LONG_PRESS_MILLIS     5000   // 5 seconds for long press

uint16_t _battery_mv = 0;
int batteryPercentage = 0;
uint32_t _next_batt_read = 0;

const int minMilliVolts = 3300; // Minimum voltage (e.g., 3.3V)
const int maxMilliVolts = 4200; // Maximum voltage (e.g., 4.2V)

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version, mesh::RTCClock* rtc, mesh::MainBoard* board) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  _rtc = rtc;
  _board = board;
  _press_start = 0;
  _display->turnOn();
  
   // version info trimming
  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if(dash){
    *dash = 0;
  }
  sprintf(_version_info, "%s", version);
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){
  if (restart) {
    _board->reboot();
  } else {
    _display->turnOff();
    radio_driver.powerOff();
    _board->powerOff();
  }
}

void UITask::renderCurrScreen() {
  char tmp[80];
  if (millis() < BOOT_SCREEN_MILLIS) { // boot screen
    // meshcore logo
    _display->setColor(DisplayDriver::BLUE);
    _display->drawXbm(1, 5, meshcore_logo, 127, 13);

    // node type
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width()/2, 24, "< Repeater >");
    
    // version info
    _display->setTextSize(2);
    _display->setColor(DisplayDriver::LIGHT);
    _display->drawTextCentered(_display->width()/2, 36, _version_info);

    // build date
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width()/2, 56, FIRMWARE_BUILD_DATE);

  } else {  // home screen

    // date/time
    uint32_t now = _rtc->getCurrentTime() + TIMEZONE_OFFSET;
    DateTime dt(now);
    sprintf(tmp, "%02d:%02d %02d.%02d.%04d",
            dt.hour(), dt.minute(),
            dt.day(), dt.month(), dt.year());
    _display->drawTextLeftAlign(2, 3, tmp);

    // node name
    _display->drawXbm(1, 13, horizontal_line, 126, 1);
    _display->setColor(DisplayDriver::GREEN);
    _display->drawTextCentered(_display->width()/2, 17, _node_prefs->node_name);
    _display->drawXbm(1, 27, horizontal_line, 126, 1);

    // freq / sf
    _display->setColor(DisplayDriver::YELLOW);
    sprintf(tmp, "FREQ:%06.3f SF:%d", _node_prefs->freq, _node_prefs->sf);
    _display->drawTextCentered(_display->width()/2, 33, tmp);

    // bw / cr
    sprintf(tmp, "BW:%03.2f CR:%d", _node_prefs->bw, _node_prefs->cr);
    _display->drawTextCentered(_display->width()/2, 43, tmp);

    // Noise Floor
    sprintf(tmp, "Noise floor: %ddB", radio_driver.getNoiseFloor());
    _display->drawTextCentered(_display->width()/2, 53, tmp);

    // battery math
    _battery_mv = _board->getBattMilliVolts();
    batteryPercentage = (int)(((_battery_mv - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts));
          if (batteryPercentage < 0) batteryPercentage = 0; // Clamp to 0%
          if (batteryPercentage > 100) batteryPercentage = 100; // Clamp to 100%

    // battery icon
    int iconWidth = 24;
    int iconHeight = 7;
    int iconX = display.width() - iconWidth - 2; // Position the icon near the top-right corner
    int iconY = 3;
    display.setColor(DisplayDriver::GREEN);

    // battery outline
    display.drawXbm(iconX, iconY, batt_outline, iconWidth, iconHeight);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 2)) / 100;
    display.fillRect(iconX + 1, iconY + 1, fillWidth, iconHeight - 2);

    // battery readings
    //sprintf(tmp, "Bat: %.2fV (%d%%),(%d)", _battery_mv / 1000.0f, batteryPercentage, fillWidth);
    //_display->drawTextCentered(_display->width()/2, 53, tmp);
    
  }
}

void UITask::loop() {
#ifdef PIN_USER_BTN
  if (millis() >= _next_read) {
    int btnState = digitalRead(PIN_USER_BTN);
    if (btnState != _prevBtnState) {
      if (btnState == LOW) {  // pressed
        _press_start = millis();
        if (!_display->isOn()) {
          _display->turnOn();
        }
        _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
      } else {  // released
        if (_press_start > 0 && (millis() - _press_start) >= LONG_PRESS_MILLIS) {
          // long press detected, shutdown
          _display->turnOff();
          radio_driver.powerOff();
          _board->powerOff();
        }
        _press_start = 0;
      }
      _prevBtnState = btnState;
    }
    _next_read = millis() + 200;  // 5 reads per second
  }
#endif

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

      _next_refresh = millis() + 1000;   // refresh every second
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }

  #ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = _board->getBattMilliVolts();
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
