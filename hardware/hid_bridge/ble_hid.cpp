#if BLE_HID  // compiled only into the Bluetooth HID builds (-DBLE_HID=1)
#include "ble_hid.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "ble_link.h"

#define UUID(s) "6e400001-a9e8-4b1b-9c39-6d6f8a3c" s  // the command service, kept in the advertisement
enum { RID_MOUSE = 1, RID_KEYBOARD = 2, RID_CONSUMER = 3 };
const uint16_t APPEARANCE_HID = 0x03C2;  // HID mouse, as the working iPhone mouse firmware advertises

// Plain mouse only, byte for byte the map of a firmware known to drive an iPhone pointer
// (dovydaskel/Iphone-control-for-social-media). Keyboard and consumer collections come back once the
// pointer is proven; their reports go to characteristics iOS does not read until then.
static const uint8_t REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, RID_MOUSE, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,
};

// ASCII -> HID keycode, bit 7 = shift (the Arduino Keyboard library's table, US layout).
#define SHIFT 0x80
static const uint8_t ASCII_MAP[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0x2a, 0x2b, 0x28, 0, 0, 0, 0, 0,  // BS, TAB, LF
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x2c, 0x1e | SHIFT, 0x34 | SHIFT, 0x20 | SHIFT, 0x21 | SHIFT, 0x22 | SHIFT, 0x24 | SHIFT, 0x34,  // space ! " # $ % & '
    0x26 | SHIFT, 0x27 | SHIFT, 0x25 | SHIFT, 0x2e | SHIFT, 0x36, 0x2d, 0x37, 0x38,                // ( ) * + , - . /
    0x27, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,                                      // 0-9
    0x33 | SHIFT, 0x33, 0x36 | SHIFT, 0x2e, 0x37 | SHIFT, 0x38 | SHIFT, 0x1f | SHIFT,                // : ; < = > ? @
    0x04 | SHIFT, 0x05 | SHIFT, 0x06 | SHIFT, 0x07 | SHIFT, 0x08 | SHIFT, 0x09 | SHIFT, 0x0a | SHIFT, 0x0b | SHIFT,
    0x0c | SHIFT, 0x0d | SHIFT, 0x0e | SHIFT, 0x0f | SHIFT, 0x10 | SHIFT, 0x11 | SHIFT, 0x12 | SHIFT, 0x13 | SHIFT,
    0x14 | SHIFT, 0x15 | SHIFT, 0x16 | SHIFT, 0x17 | SHIFT, 0x18 | SHIFT, 0x19 | SHIFT, 0x1a | SHIFT, 0x1b | SHIFT,
    0x1c | SHIFT, 0x1d | SHIFT,                                                                     // A-Z
    0x2f, 0x31, 0x30, 0x23 | SHIFT, 0x2d | SHIFT, 0x35,                                             // [ \ ] ^ _ `
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,                                      // a-z
    0x2f | SHIFT, 0x31 | SHIFT, 0x30 | SHIFT, 0x35 | SHIFT, 0x4c,                                    // { | } ~ DEL
};

static NimBLEHIDDevice *hid;
static NimBLECharacteristic *inMouse, *inKeyboard, *inConsumer;
static volatile bool subscribed;
static volatile int hidConn = -1;  // the HID host (the phone): reports go only here, never to a Mac command link
static uint32_t sent, failed;
static int subConn = -1, mapReads;  // which connection subscribed to the mouse report; report-map reads (iOS parses it on connect)  // the phone enabled notifications on the mouse report: HID link is up

class SubCb : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *c, NimBLEConnInfo &ci, uint16_t v) override {
    if (ble_is_cmd_peer(ci.getConnHandle())) return;  // a Mac's own HID stack must not get the phone's pointer
    subscribed = v != 0;
    subConn = subscribed ? ci.getConnHandle() : -1;
    hidConn = subConn;
    Serial.printf("hid: subscribe %u on %s conn %u enc %d bond %d mtu %u\n", v, c->getUUID().toString().c_str(),
                  ci.getConnHandle(), ci.isEncrypted(), ci.isBonded(), ci.getMTU());
  }
};
static SubCb subCb;
class MapCb : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *, NimBLEConnInfo &ci) override {
    mapReads++;
    Serial.printf("hid: report map read by conn %u enc %d\n", ci.getConnHandle(), ci.isEncrypted());
  }
};
static MapCb mapCb;
const int KEY_MS = 12;  // between key down and key up; iOS drops reports that follow too fast

static void notify(NimBLECharacteristic *c, const uint8_t *p, size_t n) {
  c->setValue(p, n);
  if (hidConn >= 0 && c->notify((uint16_t)hidConn)) sent++;
  else { failed++; Serial.printf("hid: notify failed (%lu ok, %lu failed)\n", (unsigned long)sent, (unsigned long)failed); }
}

bool ble_hid_ready() { return subscribed; }

String ble_hid_info() {
  NimBLEServer *server = NimBLEDevice::getServer();
  String out = String("sub ") + subscribed + " by " + subConn + " mapreads " + mapReads + " sent " + sent + " failed " + failed + " peers " + server->getConnectedCount();
  for (int i = 0; i < server->getConnectedCount(); i++) {
    NimBLEConnInfo ci = server->getPeerInfo(i);
    out += String(" [") + ci.getConnHandle() + " " + ci.getAddress().toString().c_str() + " enc " + ci.isEncrypted() + " bond " + ci.isBonded() + " auth " + ci.isAuthenticated() +
           " mtu " + ci.getMTU() + " int " + ci.getConnInterval() + "]";
  }
  return out;
}

void ble_hid_begin() {
  NimBLEServer *server = NimBLEDevice::getServer();
  hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("Mimir");
  hid->setPnp(0x02, 0x303a, 0x4000, 0x0100);  // USB-IF source, Espressif VID
  hid->setHidInfo(0x00, 0x02);                // country 0, normally connectable
  hid->setReportMap((uint8_t *)REPORT_MAP, sizeof REPORT_MAP);
  inMouse = hid->getInputReport(RID_MOUSE);
  inKeyboard = hid->getInputReport(RID_KEYBOARD);
  inConsumer = hid->getInputReport(RID_CONSUMER);
  hid->setBatteryLevel(100);
  inMouse->setCallbacks(&subCb);
  hid->getReportMap()->setCallbacks(&mapCb);
  server->start();  // re-registers the GATT table with the new services (nobody is connected yet at boot)

  // Advertise like the s3watch, which iOS Settings lists: flags, tx power, the 16-bit HID UUID and the
  // generic-HID appearance in the advertisement; the name in the scan response (set by ble_begin).
  // The 128-bit command UUID stays out of the advertisement; centrals find the board by name.
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->stop();
  NimBLEAdvertisementData data;
  data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  data.addTxPower();
  data.setCompleteServices16({NimBLEUUID((uint16_t)0x1812)});
  data.setAppearance(APPEARANCE_HID);
  adv->setAdvertisementData(data);
  adv->setMinInterval(160);  // 100-150 ms: slow advertisers drop out of the iOS Settings list
  adv->setMaxInterval(240);
  adv->start();
}

void BleMouse::move(int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  uint8_t r[4] = {buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel};  // no pan: not in the map
  notify(inMouse, r, sizeof r);
}
void BleMouse::press(uint8_t b) { buttons |= b; move(0, 0); }
void BleMouse::release(uint8_t b) { buttons &= ~b; move(0, 0); }

void BleKeyboard::send() {
  uint8_t r[8] = {mods, 0, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]};
  notify(inKeyboard, r, sizeof r);
}
size_t BleKeyboard::press(uint8_t k) {
  if (k >= 0x88) k -= 0x88;
  else if (k >= 0x80) { mods |= 1 << (k - 0x80); send(); return 1; }
  else {
    k = ASCII_MAP[k & 0x7f];
    if (!k) return 0;
    if (k & SHIFT) { mods |= 0x02; k &= 0x7f; }
  }
  for (auto &slot : keys) if (slot == k) { send(); return 1; }
  for (auto &slot : keys) if (!slot) { slot = k; send(); return 1; }
  return 0;
}
size_t BleKeyboard::release(uint8_t k) {
  if (k >= 0x88) k -= 0x88;
  else if (k >= 0x80) { mods &= ~(1 << (k - 0x80)); send(); return 1; }
  else {
    k = ASCII_MAP[k & 0x7f];
    if (!k) return 0;
    if (k & SHIFT) { mods &= ~0x02; k &= 0x7f; }
  }
  for (auto &slot : keys) if (slot == k) slot = 0;
  send();
  return 1;
}
void BleKeyboard::releaseAll() { mods = 0; memset(keys, 0, sizeof keys); send(); }
size_t BleKeyboard::write(uint8_t c) {
  size_t n = press(c);
  delay(KEY_MS);
  release(c);
  delay(KEY_MS);
  return n;
}
size_t BleKeyboard::print(const String &s) {
  size_t n = 0;
  for (char c : s) n += write(c);
  return n;
}

void BleConsumer::press(uint16_t usage) {
  uint8_t r[2] = {(uint8_t)usage, (uint8_t)(usage >> 8)};
  notify(inConsumer, r, sizeof r);
}
void BleConsumer::release() { press(0); }
#endif  // BLE_HID
