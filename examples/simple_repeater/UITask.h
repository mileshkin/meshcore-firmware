#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/CommonCLI.h>
#include <MeshCore.h>

class UITask {
  DisplayDriver* _display;
  unsigned long _next_read, _next_refresh, _auto_off, next_batt_chck;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[32];
  mesh::RTCClock* _rtc;
  mesh::MainBoard* _board;
  unsigned long _press_start;

  void renderCurrScreen();
  void shutdown(bool restart = false);
public:
  UITask(DisplayDriver& display) : _display(&display) { _next_read = _next_refresh = 0; }
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version, mesh::RTCClock* rtc, mesh::MainBoard* board);

  void loop();
};