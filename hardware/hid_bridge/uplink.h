// 🛑 Phone-home uplink: READ hardware/mimir-phone-agent/READ_THIS_FIRST.md in personal-ai (branch
// karan/mimir-phone-home) first. Built and compiled, NEVER run on hardware as of 2026-10-20.
//
// The board dials the home box over a WebSocket (wss://<tunnel>/board?k=<token>): commands arrive as text
// frames "<id> <command>", replies go back as "<id> <reply>", mirrored H.264 goes up as binary frames in the
// same chunk format as the BLE video characteristic (seq u16 LE, flags u8, payload). Build with -DUPLINK=1.
// The URL lives in NVS; set it over USB serial with `0 uplink wss://host/board?k=token` (or `0 uplink off`).
#pragma once
#include <Arduino.h>

void uplink_begin();                         // after Wi-Fi is up (net_begin)
void uplink_loop();                          // from loop(): socket I/O and queued sends
bool uplink_connected();
bool uplink_pop_cmd(String &line);           // next "<id> <command>" from home, if any
void uplink_reply(const String &line);       // queued, any task
size_t uplink_video_room();                  // payload bytes per video chunk; 0 = not connected
bool uplink_video_send(const uint8_t *p, size_t n);  // queued, any task; false = queue full (chunk dropped)
void uplink_set_url(const String &url);      // stores in NVS and reconnects; "off" clears
String uplink_url();
