/*
  SALARINI X - DroneLink.h  (V26 DUO - TX/RX LINK + AUTO-PIN DISCOVERY + IN-WINDOW SWEEP)
  ---------------------------------------------------------------------------
  Command link between the SALARINI X board (screen, commander) and a second
  ESP32-C5 running the SALARINI DRONE firmware (headless attack radio).

  V25 - THE LINK FINDS ITS OWN PINS. Nothing to wire "correctly" anymore:
  ---------------------------------------------------------------------------
  The V24.x field reports kept coming back with the same story: three jumper
  wires, "wired correctly", and still no link. Wrong pins, swapped direction,
  a USB-bridge holding a leg - every one of those looks identical from the
  user's chair. So this version stops guessing:

    1. The COMMANDER sweeps its own TX pins and rotates its RX pin at the
       same time, PINGing continuously on every combination:
       TX candidates x RX candidates (10 x 10 ordered pairs).
    2. The DRONE rapidly rotates ITS rx pin too, listening for a PING. Whose
       wire goes where does not matter - the first complete "PING" tells the
       drone which of its legs the commander is talking into, and it locks.
    3. The drone then CYCLES its tx pin broadcasting PONG while listening for
       "ACK". The commander hears the PONG on one of its rotating RX pins,
       locks that pair, floods ACK, and the drone locks its tx leg.
    4. Both boards SAVE the discovered pair in NVS. Every later boot links
       in under a second on the saved pair (FAST path) - the full sweep only
       reruns when the saved pair stops answering (e.g. you re-wired).

  Result: plug the two boards together with ANY two GPIOs + GND from the
  safe lists below, power both, wait - the link comes up on its own.
  A NO-SUCCESS result now means exactly one thing: check GND (or the drone
  firmware is not V25).

  SAFE PINS (both sides are from this set; never flash/USB/strapping/LCD/SD):
    commander (Waveshare LCD-3.5): 11, 12, 15, 16, 17, 18, 20, 21, 22, 4
    drone (any C5 board):          16, 15, 17, 18, 20, 21, 22, 4, 11, 12
  (11/12 are the Waveshare exposed UART header - first choice there; on
  plain dev boards they are UART0/bridge so they stay LAST on the drone.)

  Protocol (unchanged from V24.x): the DRONE pushes "STAT:ST=..;CH=..;..."
  every 0.5 s - that line IS the heartbeat. The commander sends text
  commands (TCLR/T+ch:mac/GO/SW/BC/STOP/PING...). Jobs are MIRRORED locally
  and REPLAYED automatically if the wire pops mid-attack.

  NO wireless stack is used for the link, so NO NimBLE version can break it,
  and the BLE radio stays 100% free for the B.T ADV spam module.
  ---------------------------------------------------------------------------
*/
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "AttackEngine.h"   // ae:: keeps include order happy

namespace drone {

// ---- candidate pins + timing ------------------------------------------------
// Commander (this board). Waveshare LCD-3.5 free GPIOs, most-likely first:
// 11/12 = the exposed UART header, then the plain header pins.
#ifndef SALARINI_DL_PINS
#define SALARINI_DL_PINS { 11, 12, 15, 16, 17, 18, 20, 21, 22, 4 }
#endif
static const uint8_t DL_PIN_LIST[] = SALARINI_DL_PINS;
static const uint8_t DL_PIN_COUNT  = (uint8_t)(sizeof(DL_PIN_LIST)/sizeof(DL_PIN_LIST[0]));

#ifndef SALARINI_DL_BAUD
#define SALARINI_DL_BAUD   115200
#endif
static const uint32_t DL_BAUD            = SALARINI_DL_BAUD;
static const uint32_t DL_TIMEOUT_MS      = 3000;  // heartbeat older than this = link down
static const uint32_t DL_CONNECT_WAIT_MS = 3400;  // linkStart() blocking window (FAST path)
static const uint32_t DL_FAST_MS         = 3000;  // saved-pair window before the full sweep
static const uint32_t DL_ACK_MS          = 2500;  // wait for the drone to lock its TX leg
static const uint32_t DL_PING_FAST_MS    = 25;    // PING pace on the saved pair
static const uint32_t DL_PING_SCAN_MS    = 12;    // dense PINGs while sweeping (drone RX rotates fast)
static const uint32_t DL_RX_ROTATE_MS    = 45;    // our RX rotation step while sweeping
static const uint32_t DL_PING_MS         = 700;   // keep-poke interval once linked

enum LinkState : uint8_t { DL_OFF = 0, DL_CONNECTING, DL_LINKED };
enum ProbePhase : uint8_t { PH_FAST = 0, PH_SCAN, PH_ACK };

// ---- state ------------------------------------------------------------------
static bool      sUartUp   = false;    // Serial1 opened
static int       sCurRx    = -1;       // pins currently routed to Serial1
static int       sCurTx    = -1;
static LinkState sState    = DL_OFF;
static bool      sWant     = false;    // keep the link (manual link or running attack)
static bool      sAuto     = false;    // link was opened automatically by an attack
static bool      sPersistent = false;  // never give the retry loop up (attack running)
static ProbePhase sPhase   = PH_SCAN;
static uint8_t   sTxIdx    = 0;        // sweep cursor: index into DL_PIN_LIST
static uint8_t   sRxIdx    = 0;
static int       sLockTx   = -1;       // pair locked by a PONG (waiting for ACK)
static int       sLockRx   = -1;
static uint32_t  sPhaseT0  = 0;        // phase start (millis)
static uint32_t  sLastRxMs   = 0;      // last line received from the drone
static uint32_t  sLastPingMs = 0;
static uint32_t  sLastAckMs  = 0;
static uint32_t  sSent     = 0;
static uint32_t  sRxBytes  = 0;        // diagnostics: raw bytes heard on RX
static uint32_t  sRxLines  = 0;        // diagnostics: valid lines assembled
static String    sStat     = "";       // last STAT payload (prefix stripped)
static bool      sStatFresh = false;   // one-shot flag for the UI
static String    sRxBuf    = "";       // UART line assembler
static Preferences sLinkPrefs;         // NVS: "sx-link" / tx, rx, ok
static bool      sPrefsOK  = false;
static int       sSavedTx  = -1;
static int       sSavedRx  = -1;
static bool      sEverLinked = false;  // at least one successful discovery

// Local MIRROR of the job the drone is running. If the link dies and comes
// back mid-attack, replayJob() re-sends the whole job automatically.
struct JobMirror {
  bool     valid;
  uint8_t  rate;
  uint8_t  n;
  uint8_t  ch[8];
  uint8_t  mac[8][6];
};
static JobMirror sJob = { false, 3, 0, {0}, {{0}} };
static bool     sSweepOn = false;      // mirrored SWEEP job (drone sweeps all channels)
static uint8_t  sSweepRate = 2;
static bool     sBcOn = false;         // mirrored BEACON CLONE job (drone beacons an SSID)
static uint8_t  sBcCh = 1;
static char     sBcSsid[33] = {0};
static bool     sBcEnc = false;

// forward declaration (defined below, after the job mirrors).
static void replayJob();

// ---- NVS persistence ---------------------------------------------------------
static void pinsLoad(){
  sPrefsOK = sLinkPrefs.begin("sx-link", true);
  if(!sPrefsOK) return;
  sSavedTx = (int)sLinkPrefs.getUChar("tx", 255);
  sSavedRx = (int)sLinkPrefs.getUChar("rx", 255);
  if(!sLinkPrefs.getBool("ok", false)){ sSavedTx = -1; sSavedRx = -1; }
  sLinkPrefs.end();
}
static void pinsSave(int tx, int rx){
  if(!sLinkPrefs.begin("sx-link", false)) return;
  sLinkPrefs.putUChar("tx", (uint8_t)tx);
  sLinkPrefs.putUChar("rx", (uint8_t)rx);
  sLinkPrefs.putBool("ok", true);
  sLinkPrefs.end();
  sSavedTx = tx; sSavedRx = rx;
}

// ---- transport ---------------------------------------------------------------
// Route Serial1 to (rx,tx). First call opens the port; later calls re-pin and
// flush whatever the previous pin had left in the buffer.
static bool uartRoute(int rx, int tx){
  if(rx < 0 || tx < 0) return false;
  if(!sUartUp){
    Serial1.setRxBufferSize(256);
    Serial1.begin(DL_BAUD, SERIAL_8N1, rx, tx);
    sUartUp = true;
    sLastRxMs = 0;
  } else if(rx != sCurRx || tx != sCurTx){
    Serial1.setPins(rx, tx);
    while(Serial1.available()) Serial1.read();   // bytes from the old pin are garbage now
    sRxBuf = "";
  } else {
    return true;
  }
  sCurRx = rx; sCurTx = tx;
  pinMode(rx, INPUT_PULLUP);   // floating RX reads noise; pulled up it sits clean HIGH
  return true;
}

// Ingest one line from the drone. ANY line refreshes the heartbeat timestamp;
// "STAT:..." additionally feeds the telemetry parser. Returns a classification:
// 0 = noise/irrelevant (do NOT lock a pair on it), 1 = PONG (drone heard us,
// its TX leg still unknown), 2 = fully-up traffic (STAT/OK/SYNC/INFO/RDY).
static int handleLine(String line){
  line.trim();
  if(line.length() == 0) return 0;
  sRxLines++;
  sLastRxMs = millis();
  if(line.startsWith("PONG")) return 1;         // heartbeat answer / probe reply
  if(line.startsWith("STAT:")){
    String st = line.substring(5);
    st.trim();
    if(st != sStat){ sStat = st; sStatFresh = true; }
    return 2;
  }
  if(line.startsWith("SYNC") || line.startsWith("INFO") || line.startsWith("OK") || line.startsWith("RDY")) return 2;
  Serial.println("[DRONE<-] " + line);         // ERR ... / unknown - logged, not trusted
  return 0;
}

// ---- probe state machine helpers ----------------------------------------------
static void enterFast(){
  sPhase = PH_FAST;
  sPhaseT0 = millis();
  if(sSavedTx >= 0 && sSavedRx >= 0) uartRoute(sSavedRx, sSavedTx);
  else { sTxIdx = 0; sRxIdx = 0; uartRoute(DL_PIN_LIST[0], DL_PIN_LIST[0]); sPhase = PH_SCAN; }
}
static void enterScan(){
  sPhase = PH_SCAN;
  sPhaseT0 = millis();
  sTxIdx = 0; sRxIdx = 0;
  uartRoute(DL_PIN_LIST[sRxIdx], DL_PIN_LIST[sTxIdx]);
}
static void enterAck(int tx, int rx){
  sPhase = PH_ACK;
  sPhaseT0 = millis();
  sLockTx = tx; sLockRx = rx;
  uartRoute(rx, tx);
  sLastAckMs = 0;
  Serial.printf("[DRONE] pair heard: TX=GPIO%d RX=GPIO%d - waiting for the drone to lock its TX leg\n", tx, rx);
}

// ---- public API ------------------------------------------------------------

// Boot the transport early (called from setup(); service() would lazy-init
// it anyway). Loads the saved pin pair from NVS and opens the port on it.
static void begin(){
  if(sPrefsOK) return;                 // idempotent
  pinsLoad();
  if(sSavedTx >= 0 && sSavedRx >= 0){
    uartRoute(sSavedRx, sSavedTx);
    Serial.printf("[DRONE] saved pair from last run: TX=GPIO%d RX=GPIO%d\n", sSavedTx, sSavedRx);
  } else {
    Serial.println("[DRONE] no saved pair - AUTO-PIN discovery will sweep on first want");
  }
}

static bool available(){ return sUartUp; }

static LinkState state(){ return sState; }
static bool linked(){
  if(!sUartUp) return false;
  return (sLastRxMs != 0) && ((millis() - sLastRxMs) < DL_TIMEOUT_MS);
}
static const char* stateName(){
  if(linked()) return "LINKED";
  if(sWant)    return "PROBING";
  return "OFFLINE";
}
// UI helpers for the probe strip / popups.
static bool probing(){ return sWant && !linked(); }
static int probeTxPin(){ return (sPhase==PH_ACK) ? sLockTx : ((sPhase==PH_FAST)?sSavedTx:DL_PIN_LIST[sTxIdx]); }
static int probeRxPin(){ return (sPhase==PH_ACK) ? sLockRx : ((sPhase==PH_FAST)?sSavedRx:DL_PIN_LIST[sRxIdx]); }
static int savedTx(){ return sSavedTx; }
static int savedRx(){ return sSavedRx; }
static const uint8_t* pinList(){ return DL_PIN_LIST; }
static uint8_t pinCount(){ return DL_PIN_COUNT; }

static bool autoLink(){ return sAuto; }
static void setAuto(bool a){ sAuto = a; }
// While an attack is running the retry loop never gives up - unplug and
// re-plug the drone mid-attack and it rejoins + resumes the job by itself.
static void setPersistent(bool p){ sPersistent = p; if(p){ sWant = true; } }
static uint32_t sentCount(){ return sSent; }
// Wiring diagnostics: 0 bytes heard = power/wiring/GND problem; bytes but no
// lines = baud/level problem; lines = the link itself.
static uint32_t rxBytes(){ return sRxBytes; }
static uint32_t rxLines(){ return sRxLines; }
static const String& lastStat(){ return sStat; }
static bool statNew(){ bool f = sStatFresh; sStatFresh = false; return f; }

// ---- telemetry: parsed fields from the last "STAT:..." payload -------------
// Stored payload format: ST=JAM;CH=6;TG=2;TX=1234;RPS=88;T=42;U=99
static String statField(const char* key){
  if(sStat.length()==0 || !key || !key[0]) return String("");
  String s = ";" + sStat;                  // anchor every search at a ';'
  String k = String(";") + key + "=";
  int i = s.indexOf(k);
  if(i < 0) return String("");
  i += k.length();
  int j = s.indexOf(';', i);
  if(j < 0) j = s.length();
  return s.substring(i, j);
}
static String statMode(){ return statField("ST"); }   // IDLE/JAM/SWEEP/BCON/COMBO
static String statCh(){ return statField("CH"); }     // current radio channel
static String statTx(){ return statField("TX"); }     // total TX frames
static String statRps(){ return statField("RPS"); }   // frames per second
static String statTemp(){ return statField("T"); }    // chip temperature (C)
static String statUp(){ return statField("U"); }      // uptime seconds

// Opens (or confirms) the link. Blocking for up to ~3.4 s on the FAST (saved
// pair) path; the full sweep keeps running in the background via service().
static bool linkStart(){
  if(linked()) return true;
  sWant = true;
  sState = DL_CONNECTING;
  if(!sUartUp) begin();
  if(sSavedTx < 0 || sSavedRx < 0){ enterScan(); }
  else enterFast();
  // service() is normally driven by loop(); while parked here we run the
  // same pump by hand so the first heartbeat is caught immediately.
  uint32_t t0 = millis();
  while(millis() - t0 < DL_CONNECT_WAIT_MS){
    // 1) drain
    while(Serial1.available()){
      char c = (char)Serial1.read();
      sRxBytes++;
      if(c=='\n' || c=='\r'){
        if(sRxBuf.length()){
          int cls = handleLine(sRxBuf);
          sRxBuf = "";
          if(cls == 2){ sState = DL_LINKED; sEverLinked = true; return true; }
          if(cls == 1 && sPhase != PH_ACK){
            int tx = probeTxPin(), rx = probeRxPin();
            if(tx >= 0 && rx >= 0){ pinsSave(tx, rx); enterAck(tx, rx); }
          }
        }
        sRxBuf = "";
      }
      else if(sRxBuf.length() < 120) sRxBuf += c;
      else sRxBuf = "";
    }
    // 2) poke
    uint32_t now = millis();
    uint32_t pace = (sPhase==PH_SCAN)?DL_PING_SCAN_MS:((sPhase==PH_FAST)?DL_PING_FAST_MS:DL_PING_MS);
    if(now - sLastPingMs >= pace){
      sLastPingMs = now;
      if(sPhase != PH_ACK) Serial1.print("PING\n");
      else                 Serial1.print("ACK\n");
    }
    // V26: rotate the sweep INSIDE the blocking window too. service() only
    // rotates between loop() passes - without this, a first-ever discovery
    // spent the whole popup PINGing one single pin pair.
    if(sPhase == PH_SCAN && now - sPhaseT0 >= DL_RX_ROTATE_MS){
      sPhaseT0 = now;
      sRxIdx++;
      if(sRxIdx >= DL_PIN_COUNT){
        sRxIdx = 0;
        sTxIdx++;
        if(sTxIdx >= DL_PIN_COUNT) sTxIdx = 0;
      }
      uartRoute(DL_PIN_LIST[sRxIdx], DL_PIN_LIST[sTxIdx]);
    }
    if(linked()) { sState = DL_LINKED; sEverLinked = true; return true; }
    delay(2);
  }
  // Not up yet - keep the sweep alive in the background (service() drives it).
  return false;
}

// Best-effort command write. Returns false when the link is down.
static bool sendCmd(const char* cmd){
  if(!sUartUp || !linked()) return false;
  size_t len = strlen(cmd);
  if(len == 0 || len > 120) return false;
  Serial1.print(cmd);
  Serial1.print('\n');
  sSent++;
  return true;
}

// Sends STOP (best effort) without tearing the link down.
static void jobStop(){
  sendCmd("STOP");
  sJob.valid = false; sJob.n = 0;
  sSweepOn = false;
  sBcOn = false;
}

// Full release: STOP + drop the link (the drone keeps beaconing heartbeats,
// the commander just stops wanting it). Used when an auto link ends with the
// attack - the wireless radios were never shared, so nothing else to free.
static void linkStop(){
  jobStop();
  sWant = false; sAuto = false;
  sPersistent = false;
  sState = DL_OFF;
  Serial.println("[DRONE] link released");
}

// ---- job builders (attack integration helpers) ------------------------------
static void jobBegin(){ sJob.n = 0; sJob.valid = false; sendCmd("TCLR"); }
static void jobAdd(uint8_t ch, const uint8_t* bssid){
  if(!bssid || ch < 1) return;
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "T+%u:%02x:%02x:%02x:%02x:%02x:%02x",
           (unsigned)ch, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  sendCmd(cmd);
  if(sJob.n < 8){
    sJob.ch[sJob.n] = ch;
    memcpy(sJob.mac[sJob.n], bssid, 6);
    sJob.n++;
  }
}
static void jobRun(uint8_t rate = 3){
  sJob.rate = rate;
  char c[8]; snprintf(c, sizeof(c), "GO:%u", (unsigned)rate);
  sendCmd(c);
  // V25: the mirror is valid as soon as targets were recorded - a job pushed
  // while the AUTO-PIN sweep is still running gets REPLAYED the moment the
  // link comes up, so "attack solo -> drone joins mid-attack" just works.
  if(sJob.n > 0) sJob.valid = true;
}

// SWEEP job - the drone hops every non-DFS 2.4 + 5 GHz channel broadcasting
// deauth bursts. Full-spectrum pressure from one command.
static bool sweep(uint8_t rate = 2){
  if(rate < 1 || rate > 3) rate = 2;
  sSweepOn = true; sSweepRate = rate;
  char c[8]; snprintf(c, sizeof(c), "SW:%u", (unsigned)rate);
  bool ok = sendCmd(c);
  return ok;
}

// BEACON CLONE job - the drone builds real 802.11 beacons for the given SSID
// (privacy bit mirrored so secured networks look secured) and transmits them
// on the given channel. The SSID is hex-encoded so ':', spaces and any byte
// survive the text transport.
static bool beaconStart(uint8_t ch, const char* ssid, bool privacy){
  if(!ssid || ch < 1) return false;
  size_t n = strlen(ssid);
  if(n > 32) n = 32;
  static const char* HX = "0123456789abcdef";
  char hex[65];
  for(size_t i=0;i<n;i++){
    hex[i*2]   = HX[(ssid[i] >> 4) & 0xF];
    hex[i*2+1] = HX[ssid[i] & 0xF];
  }
  hex[n*2] = 0;
  char cmd[96];
  snprintf(cmd, sizeof(cmd), "BC:%u:%u:%s", (unsigned)ch, (unsigned)(privacy?1:0), hex);
  sBcOn = true; sBcCh = ch; sBcEnc = privacy;
  memset(sBcSsid, 0, sizeof(sBcSsid));
  memcpy(sBcSsid, ssid, n);
  return sendCmd(cmd);
}
static bool beaconStop(){ sBcOn = false; return sendCmd("BCSTOP"); }

// Replay the mirrored job after a mid-attack re-link (auto links only).
static void replayJob(){
  if(!sAuto || !sJob.valid) return;
  Serial.printf("[DRONE] replaying job: %u targets R%u%s%s\n",
                (unsigned)sJob.n, (unsigned)sJob.rate,
                sSweepOn?" +SWEEP":"", sBcOn?" +BCON":"");
  sendCmd("TCLR");
  for(uint8_t i=0;i<sJob.n;i++){
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "T+%u:%02x:%02x:%02x:%02x:%02x:%02x",
             (unsigned)sJob.ch[i], sJob.mac[i][0], sJob.mac[i][1],
             sJob.mac[i][2], sJob.mac[i][3], sJob.mac[i][4], sJob.mac[i][5]);
    sendCmd(cmd);
  }
  char g[8]; snprintf(g, sizeof(g), "GO:%u", (unsigned)sJob.rate);
  sendCmd(g);
  if(sSweepOn){
    char c[8]; snprintf(c, sizeof(c), "SW:%u", (unsigned)sSweepRate);
    sendCmd(c);
  }
  if(sBcOn) beaconStart(sBcCh, sBcSsid, sBcEnc);
}

// ---- V28 AIR PAIR: radio-direct pairing (no BLE stack on the drone) --------
// When DUAL MODE is switched ON the commander broadcasts marker beacons with
// SSID "SXP:<code>" on channel 1 for ~12 s while a promiscuous listener waits
// for the drone's answer "SXY:<code>".
//   first pairing  -> the request is the wildcard SXP:FFFF (any drone answers)
//   later pairings -> the request carries the saved code (only THAT drone
//                     answers - "hasta conseguir la correcta")
//   nothing heard for half the window -> fall back to the wildcard once
// On success the code is saved to NVS ("sx-air") and the caller normally
// kicks linkStart() so the 3-wire AUTO-PIN transport comes up right away.
// The whole thing lives on channel 1 and ends with the attack radios clean;
// during the window the main radio does nothing else (guarded by the caller).
static Preferences sAirPrefs;
static uint16_t   sAirCode    = 0;    // saved drone code (0 = never paired)
static bool       sAirPairOn  = false;
static bool       sAirFound   = false;
static bool       sAirWild    = false;// currently requesting the wildcard
static uint32_t   sAirT0      = 0;
static uint32_t   sAirLastTx  = 0;
static int        sAirResult  = 0;    // 0 running, 1 paired, 2 timeout (UI reads once)

static const uint32_t AP_WINDOW_MS  = 12000;
static const uint32_t AP_TX_PACE_MS = 120;
static const uint8_t  AP_PAIR_CH    = 1;

// Beacon template (same shape the drone echoes back).
static const uint8_t AIR_BEACON_T[38] = {
  0x80,0x00, 0x00,0x00,
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
  0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xE8,0x03,
  0x01,0x04
};
static const uint8_t AIR_RATES_T[10] = { 0x01,0x08,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24 };

static void airLoad(){
  if(!sAirPrefs.begin("sx-air", true)) return;
  sAirCode = sAirPrefs.getUShort("code", 0);
  sAirPrefs.end();
}
static void airSaveCode(uint16_t c){
  if(!sAirPrefs.begin("sx-air", false)) return;
  sAirPrefs.putUShort("code", c);
  sAirPrefs.putBool("on", true);
  sAirPrefs.end();
  sAirCode = c;
}

// SSID of a received beacon frame (tag-0 IE). "" when none/invalid.
static String airBeaconSSID(const uint8_t* d, uint16_t len){
  if(len < 38 || (d[0] & 0x0F) != 0x80) return "";
  const uint8_t* p   = d + 36;                 // 24 mgmt hdr + 12 fixed params
  const uint8_t* end = d + len - 4;            // FCS at the tail
  while(p + 2 <= end){
    uint8_t tag = p[0], tl = p[1];
    if(tag == 0xFF) break;
    if(tag == 0x00){
      if(tl == 0 || tl > 32 || p + 2 + tl > end) return "";
      String s;
      for(uint8_t i=0;i<tl;i++){
        char c = (char)p[2+i];
        if(c < 32 || c > 126) return "";
        s += c;
      }
      return s;
    }
    p += 2 + tl;
  }
  return "";
}

// RX callback during the pair window: watch for the drone's SXY answer.
static void airSniffCb(void* buf, wifi_promiscuous_pkt_type_t type){
  if(!sAirPairOn || sAirFound || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pp = (const wifi_promiscuous_pkt_t*)buf;
  String ssid = airBeaconSSID(pp->payload, pp->rx_ctrl.sig_len);
  if(!ssid.startsWith("SXY:")) return;
  uint16_t code = (uint16_t)strtol(ssid.substring(4).c_str(), nullptr, 16);
  if(code == 0) return;
  if(!sAirWild && code != sAirCode) return;            // targeted run: wrong drone stays silent
  sAirFound  = true;
  sAirResult = 1;
  sAirCode   = code;                                   // adopt on discovery runs
  Serial.printf("[DRONE] AIR PAIR confirmed: code %04X\n", (unsigned)code);
}

// One marker beacon with an arbitrary SSID on the pair channel.
static void airTxMarker(const char* ssid){
  uint8_t pkt[96];
  memcpy(pkt, AIR_BEACON_T, 38);
  uint16_t mc = sAirWild ? 0xFFFF : sAirCode;
  uint8_t b[6] = { 0xC6, 0xD2, 0xBB, (uint8_t)((mc >> 8) & 0xFF), (uint8_t)(mc & 0xFF), 0x01 };
  memcpy(&pkt[10], b, 6);
  memcpy(&pkt[16], b, 6);
  uint8_t sl = (uint8_t)strlen(ssid);
  if(sl > 32) sl = 32;
  int idx = 38;
  pkt[idx++] = 0x00; pkt[idx++] = sl;
  memcpy(&pkt[idx], ssid, sl); idx += sl;
  memcpy(&pkt[idx], AIR_RATES_T, 10); idx += 10;
  pkt[idx++] = 0x03; pkt[idx++] = 0x01; pkt[idx++] = AP_PAIR_CH;
  esp_wifi_80211_tx(WIFI_IF_STA, pkt, idx, false);
}

// Starts the pair window. The CALLER guarantees the radios are idle (no
// attack / scan / evil twin running) - the pair owns the radio for 12 s.
static bool airPairStart(){
  if(linked()) return true;                 // wire link already beats any pairing
  if(sAirPairOn) return true;
  if(sAirCode == 0) airLoad();
  esp_wifi_set_promiscuous_rx_cb(&airSniffCb);
  if(esp_wifi_set_promiscuous(true) != ESP_OK) return false;
  if(esp_wifi_set_channel(AP_PAIR_CH, WIFI_SECOND_CHAN_NONE) != ESP_OK){
    esp_wifi_set_promiscuous(false);
    return false;
  }
  sAirPairOn = true; sAirFound = false; sAirResult = 0;
  sAirWild   = (sAirCode == 0);            // no saved drone -> discover
  sAirT0     = millis(); sAirLastTx = 0;
  Serial.printf("[DRONE] AIR PAIR window open on ch %u - requesting code %s\n",
                (unsigned)AP_PAIR_CH, sAirWild ? "FFFF (any drone)" : "saved code");
  return true;
}
static void airPairStop(bool success){
  if(!sAirPairOn) return;
  sAirPairOn = false;
  esp_wifi_set_promiscuous(false);
  if(success && sAirCode != 0){ airSaveCode(sAirCode); }
  else if(!success){ sAirResult = 2; }
}
// Drive from loop(); non-blocking. On success call linkStart() to bring the
// 3-wire AUTO-PIN transport up right away.
static void airPairService(){
  if(!sAirPairOn) return;
  uint32_t now = millis();
  if(sAirFound){ airPairStop(true); return; }
  if(!sAirWild && !sAirFound && (now - sAirT0) >= AP_WINDOW_MS/2){
    sAirWild = true;                       // half the window gone - widen search
    Serial.println("[DRONE] AIR PAIR: saved code silent - trying discovery FFFF");
  }
  if(now - sAirT0 >= AP_WINDOW_MS){ airPairStop(false); return; }
  if(now - sAirLastTx >= AP_TX_PACE_MS){
    sAirLastTx = now;
    char ss[12];
    snprintf(ss, sizeof(ss), "SXP:%04X", (unsigned)(sAirWild ? 0xFFFF : sAirCode));
    airTxMarker(ss);
  }
}
// UI helpers.
static bool airPairing(){ return sAirPairOn; }
static int  airPairResult(){ int r = sAirResult; sAirResult = 0; return r; }
static uint16_t airCode(){ return sAirCode; }

// ---- background service (AUTO-PIN discovery + heartbeat watch + re-link) ----
static void service(){
  if(!sUartUp){
    if(sWant) begin();
    else return;
  }
  // 1) drain everything the drone sent
  while(Serial1.available()){
    char c = (char)Serial1.read();
    sRxBytes++;
    if(c=='\n' || c=='\r'){
      if(sRxBuf.length()){
        int cls = handleLine(sRxBuf);
        sRxBuf = "";
        if(sState != DL_LINKED){
          if(cls == 2){
            sState = DL_LINKED; sEverLinked = true;
            if(sPhase == PH_SCAN || sPhase == PH_FAST){
              int tx = (sPhase==PH_FAST)?sSavedTx:DL_PIN_LIST[sTxIdx];
              int rx = (sPhase==PH_FAST)?sSavedRx:DL_PIN_LIST[sRxIdx];
              pinsSave(tx, rx);
            }
            Serial.println("[DRONE] TX/RX link established (auto-pins)");
            if(sAuto) replayJob();          // mid-attack re-link -> resume the job
          } else if(cls == 1 && sPhase != PH_ACK){
            int tx = probeTxPin(), rx = probeRxPin();
            if(tx >= 0 && rx >= 0){ pinsSave(tx, rx); enterAck(tx, rx); }
          }
        }
      }
      sRxBuf = "";
    } else if(sRxBuf.length() < 120){
      sRxBuf += c;
    } else {
      sRxBuf = "";                        // overrun guard
    }
  }

  // 2) state machine
  uint32_t now = millis();
  if(linked()){
    if(sState != DL_LINKED){
      sState = DL_LINKED; sEverLinked = true;
      Serial.println("[DRONE] TX/RX link established");
      if(sAuto) replayJob();
    }
  } else {
    if(sState == DL_LINKED){
      Serial.println("[DRONE] link lost - AUTO-PIN rediscovery");
      sState = sWant ? DL_CONNECTING : DL_OFF;
      if(sWant){ if(sSavedTx >= 0 && sSavedRx >= 0) enterFast(); else enterScan(); }
    } else if(sWant && sState == DL_OFF){
      sState = DL_CONNECTING;
      if(sSavedTx >= 0 && sSavedRx >= 0) enterFast(); else enterScan();
    }
  }

  if(!sWant || sState == DL_LINKED){
    // linked (or not wanted): gentle keep-poke, nothing else
    if(sWant && (now - sLastPingMs >= DL_PING_MS)){
      sLastPingMs = now;
      Serial1.print("PING\n");
    }
    return;
  }

  // 3) AUTO-PIN probe phases (only while the link is wanted and down)
  if(sPhase == PH_FAST){
    if(now - sLastPingMs >= DL_PING_FAST_MS){ sLastPingMs = now; Serial1.print("PING\n"); }
    if(now - sPhaseT0 >= DL_FAST_MS) enterScan();          // saved pair is dead - sweep
  }
  else if(sPhase == PH_SCAN){
    if(now - sLastPingMs >= DL_PING_SCAN_MS){ sLastPingMs = now; Serial1.print("PING\n"); }
    if(now - sPhaseT0 >= DL_RX_ROTATE_MS){
      sPhaseT0 = now;
      // rotate the RX leg; when one full sweep is done, move to the next TX
      sRxIdx++;
      if(sRxIdx >= DL_PIN_COUNT){
        sRxIdx = 0;
        sTxIdx++;
        if(sTxIdx >= DL_PIN_COUNT) sTxIdx = 0;   // sweep restarts (drone may still be syncing)
      }
      uartRoute(DL_PIN_LIST[sRxIdx], DL_PIN_LIST[sTxIdx]);
    }
  }
  else if(sPhase == PH_ACK){
    if(now - sLastAckMs >= 40){ sLastAckMs = now; Serial1.print("ACK\n"); }
    if(now - sPhaseT0 >= DL_ACK_MS){ Serial.println("[DRONE] ACK window timed out - resuming sweep"); enterScan(); }
  }
}

} // namespace drone
