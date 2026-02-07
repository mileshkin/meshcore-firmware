#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

class UITask : public AbstractUITask {
private:
  // Hardware components
  DisplayDriver* _display;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;

#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif

  // UI screens
  UIScreen* splash;
  UIScreen* home;
  UIScreen* msg_preview;
  UIScreen* curr;

  // Timing and state variables
  unsigned long _next_refresh;
  unsigned long _auto_off;
  unsigned long _alert_expiry;
  unsigned long ui_started_at;
  unsigned long next_batt_chck;
  unsigned long _button_press_start;
  
#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis;
#endif

  // UI state
  char _alert[80];
  int _msgcount;
  int next_backlight_btn_check;
  
  // State flags
  bool screensaver_on;
  bool _hibernation_pending;
  bool _button_was_pressed;

#ifdef PIN_STATUS_LED
  int led_state;
  int next_led_change;
  int last_led_increment;
#endif

  // Helper methods
  void setGPSHardwareState(bool enabled);
  void userLedHandler();
  void setCurrScreen(UIScreen* c);

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

public:
  UITask(mesh::MainBoard* board, BaseSerialInterface* serial) 
    : AbstractUITask(board, serial)
    , _display(NULL)
    , _sensors(NULL)
    , _node_prefs(NULL)
    , splash(NULL)
    , home(NULL)
    , msg_preview(NULL)
    , curr(NULL)
    , _next_refresh(0)
    , _auto_off(0)
    , _alert_expiry(0)
    , ui_started_at(0)
    , next_batt_chck(0)
    , _button_press_start(0)
#ifdef PIN_USER_BTN_ANA
    , _analogue_pin_read_millis(0)
#endif
    , _msgcount(0)
    , next_backlight_btn_check(0)
    , screensaver_on(false)
    , _hibernation_pending(false)
    , _button_was_pressed(false)
#ifdef PIN_STATUS_LED
    , led_state(0)
    , next_led_change(0)
    , last_led_increment(0)
#endif
  {
  }

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // UI control
  void gotoHomeScreen() { setCurrScreen(home); }
  void showAlert(const char* text, int duration_millis);
  
  // Getters
  int  getMsgCount() const { return _msgcount; }
  bool hasDisplay() const { return _display != NULL; }
  bool isButtonPressed() const;
  bool getGPSState();
  bool isScreensaverEnabled() const { return screensaver_on; }

  // User actions
  void handleHibernation();
  void toggleBuzzer();
  void toggleGPS();
  void toggleScreensaver();

  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};