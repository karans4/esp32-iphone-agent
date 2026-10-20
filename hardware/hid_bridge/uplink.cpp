// See uplink.h. 🛑 Unverified on hardware: READ_THIS_FIRST.md (personal-ai, branch karan/mimir-phone-home).
#if UPLINK  // compiled only into the phone-home build (-DUPLINK=1)
#include "uplink.h"
#include <Preferences.h>
#include <WebSocketsClient.h>
#include <freertos/ringbuf.h>

static WebSocketsClient ws;
static Preferences prefs;
static String url;
static volatile bool up;
static QueueHandle_t cmdQ;
static RingbufHandle_t txText, txVideo;  // sends from any task; drained by uplink_loop() on the loop task
const size_t CMD_MAX = 512;
const size_t VIDEO_RING = 192 * 1024;    // in PSRAM: ~1.5 s of a 1 Mbit/s mirror
const size_t CHUNK = 1024;               // video payload per binary frame

static void onEvent(WStype_t type, uint8_t *data, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      up = true;
      Serial.printf("uplink: connected to %s\n", url.c_str());
      break;
    case WStype_DISCONNECTED:
      if (up) Serial.println("uplink: disconnected");
      up = false;
      break;
    case WStype_TEXT: {
      char line[CMD_MAX] = {0};
      memcpy(line, data, min(len, CMD_MAX - 1));
      xQueueSend(cmdQ, line, 0);  // full: home times out and retries
      break;
    }
    default:
      break;
  }
}

// wss://host[:port]/path?query  ->  host, port, path (ws:// for a LAN test without TLS)
static bool parse(const String &u, bool &tls, String &host, uint16_t &port, String &path) {
  int s = u.indexOf("://");
  if (s < 0) return false;
  tls = u.startsWith("wss");
  String rest = u.substring(s + 3);
  int slash = rest.indexOf('/');
  String hp = slash < 0 ? rest : rest.substring(0, slash);
  path = slash < 0 ? "/" : rest.substring(slash);
  int colon = hp.indexOf(':');
  host = colon < 0 ? hp : hp.substring(0, colon);
  port = colon < 0 ? (tls ? 443 : 80) : hp.substring(colon + 1).toInt();
  return host.length() > 0;
}

static void connect() {
  ws.disconnect();
  up = false;
  bool tls;
  String host, path;
  uint16_t port;
  if (url.isEmpty() || !parse(url, tls, host, port, path)) return;
  // TODO before real use: verify the server (beginSslWithBundle + the core's x509 bundle). Today TLS is
  // encrypted but unauthenticated; the ?k= token in the URL is what keeps strangers off the home bridge.
  if (tls) ws.beginSSL(host.c_str(), port, path.c_str(), "", "");
  else ws.begin(host.c_str(), port, path.c_str(), "");
  ws.onEvent(onEvent);
  ws.setReconnectInterval(3000);
  ws.enableHeartbeat(10000, 4000, 2);  // ping every 10 s, drop after 2 missed pongs (tunnels idle out)
}

void uplink_begin() {
  cmdQ = xQueueCreate(8, CMD_MAX);
  txText = xRingbufferCreate(4096, RINGBUF_TYPE_NOSPLIT);
  txVideo = xRingbufferCreateWithCaps(VIDEO_RING, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_SPIRAM);
  prefs.begin("uplink", false);
  url = prefs.getString("url", "");
#ifdef UPLINK_URL
  if (url.isEmpty()) url = UPLINK_URL;
#endif
  connect();
}

void uplink_loop() {
  if (url.isEmpty()) return;
  ws.loop();
  size_t n;
  // text first: replies must not queue behind seconds of video
  while (void *p = xRingbufferReceive(txText, &n, 0)) {
    if (up) ws.sendTXT((uint8_t *)p, n);
    vRingbufferReturnItem(txText, p);
  }
  for (int i = 0; i < 16; i++) {  // bounded per loop pass so commands keep flowing
    void *p = xRingbufferReceive(txVideo, &n, 0);
    if (!p) break;
    if (up) ws.sendBIN((uint8_t *)p, n);
    vRingbufferReturnItem(txVideo, p);
  }
}

bool uplink_connected() { return up; }

bool uplink_pop_cmd(String &line) {
  char buf[CMD_MAX];
  if (!cmdQ || xQueueReceive(cmdQ, buf, 0) != pdTRUE) return false;
  line = buf;
  line.trim();
  return line.length() > 0;
}

void uplink_reply(const String &line) {
  if (up && txText) xRingbufferSend(txText, line.c_str(), line.length(), pdMS_TO_TICKS(100));
}

size_t uplink_video_room() { return up && txVideo ? CHUNK : 0; }

bool uplink_video_send(const uint8_t *p, size_t n) {
  return up && xRingbufferSend(txVideo, p, n, pdMS_TO_TICKS(200)) == pdTRUE;
}

void uplink_set_url(const String &u) {
  url = u == "off" ? "" : u;
  prefs.putString("url", url);
  connect();
}

String uplink_url() { return url; }
#endif  // UPLINK
