#include "UITask.h"
#include "../MyMesh.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>
#include "icons.h"
#include <MeshCore.h>
#ifdef WITH_MQTT_BRIDGE
  #include <WiFi.h>
#endif

#define AUTO_OFF_MILLIS      15000  // 15 seconds

#ifdef HELTEC_LORA_V4
  #define BOOT_SCREEN_MILLIS   7000   // 7 seconds
#else
  #define BOOT_SCREEN_MILLIS   4000   // 4 seconds
#endif

#define TIMEZONE_OFFSET       10800  // UTC+3 in seconds
#define LONG_PRESS_MILLIS     5000   // 5 seconds for long press
#define HIBERNATE_CANCEL_MILLIS 3000 // 3 seconds to cancel hibernation
#define DISPLAY_SWITCH_MILLIS 5000   // 5 seconds for display switch

uint16_t _battery_mv = 0;
int batteryPercentage = 0;
uint32_t _next_batt_read = 0;
uint32_t _next_display_switch = 0;  // Timer for switching display
bool _show_ip = false;  // Flag to alternate between node name and IP

const int minMilliVolts = 3300; // Minimum voltage (e.g., 3.3V)
const int maxMilliVolts = 4200; // Maximum voltage (e.g., 4.2V)

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version, mesh::RTCClock* rtc, mesh::MainBoard* board) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  _rtc = rtc;
  _board = board;
  _press_start = 0;
  _button_was_pressed = false;
  _button_press_start = 0;
  _hibernation_pending = false;
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

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return digitalRead(PIN_USER_BTN) == LOW;
#else
  return false;
#endif
}

void UITask::handleHibernation() {
  bool button_pressed = isButtonPressed();
  
  // Detect button press start
  if (button_pressed && !_button_was_pressed) {
    _button_press_start = millis();
    _button_was_pressed = true;
    
    // Turn on display and extend auto-off timer
    if (!_display->isOn()) {
      _display->turnOn();
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;
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
    if (press_duration >= LONG_PRESS_MILLIS && !_hibernation_pending) {
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
      
      // Refresh display to clear hibernation message
      _next_refresh = 0;
    }
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

              #ifdef WITH_MQTT_BRIDGE
              // Check if it's time to switch display
              if (millis() >= _next_display_switch) {
              _show_ip = !_show_ip;  // Toggle between node name and IP
              _next_display_switch = millis() + DISPLAY_SWITCH_MILLIS;
              }
              if (_show_ip && WiFi.status() == WL_CONNECTED) {
              // Display IP address
              IPAddress ip = WiFi.localIP();
              snprintf(tmp, sizeof(tmp), "IP:%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
              _display->drawTextCentered(_display->width()/2, 17, tmp);
              } else if (_show_ip && WiFi.status() != WL_CONNECTED) {
              // Display WiFi disconnected message
              _display->drawTextCentered(_display->width()/2, 17, "WiFi Disconnected");
              } else {
              // Display node name
              _display->drawTextCentered(_display->width()/2, 17, _node_prefs->node_name);
              }
              #else
              // If MQTT bridge is not enabled, always show node name
              _display->drawTextCentered(_display->width()/2, 17, _node_prefs->node_name);
              #endif
              
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
  
  handleHibernation();
  
  // If hibernation is pending, only process buzzer/vibration loops
  if (_hibernation_pending) {
    #ifdef PIN_BUZZER
    if (buzzer.isPlaying()) buzzer.loop();
    #endif
    #ifdef PIN_VIBRATION
    vibration.loop();
    #endif
    return;
  }

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