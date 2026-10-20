// BLE HID output (HID over GATT): the board is the phone's Bluetooth mouse + keyboard instead of a USB
// one. Build with -DBLE_HID=1; the sketch then uses these objects in place of the TinyUSB ones. The
// method surface mirrors USBHIDMouse / USBHIDKeyboard / USBHIDConsumerControl, so run() is unchanged.
// The command service (ble_link) and this HID service live on the same NimBLE server; the phone
// bonds to the HID service, the Mac bridge or the app keeps using the command service.
#pragma once
#include <Arduino.h>

// Same values as the Arduino USB keyboard library, so keyCode() in the sketch needs no change:
// >= 0x88 is a raw HID keycode + 0x88, 0x80..0x87 a modifier, below that ASCII.
#define KEY_LEFT_CTRL 0x80
#define KEY_LEFT_SHIFT 0x81
#define KEY_LEFT_ALT 0x82
#define KEY_LEFT_GUI 0x83
#define KEY_UP_ARROW 0xDA
#define KEY_DOWN_ARROW 0xD9
#define KEY_LEFT_ARROW 0xD8
#define KEY_RIGHT_ARROW 0xD7
#define KEY_BACKSPACE 0xB2
#define KEY_TAB 0xB3
#define KEY_RETURN 0xB0
#define KEY_ESC 0xB1
#define MOUSE_LEFT 1

void ble_hid_begin();          // after ble_begin(): adds the HID service and re-advertises as a HID device
bool ble_hid_ready();          // a bonded central is subscribed to the input reports
String ble_hid_info();         // link + report counters, for the `hid` debug command

class BleMouse {
 public:
  void move(int8_t x, int8_t y, int8_t wheel = 0, int8_t pan = 0);
  void press(uint8_t b = MOUSE_LEFT);
  void release(uint8_t b = MOUSE_LEFT);
 private:
  uint8_t buttons = 0;
};

class BleKeyboard {
 public:
  size_t press(uint8_t k);
  size_t release(uint8_t k);
  void releaseAll();
  size_t write(uint8_t c);
  size_t print(const String &s);
 private:
  uint8_t mods = 0, keys[6] = {0};
  void send();
};

class BleConsumer {
 public:
  void press(uint16_t usage);
  void release();
};
