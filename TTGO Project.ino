/* Kuma → (Webhook) → ESP32 (TTGO/ESP32 Dev) → GPIO (Digital/LEDC PWM)
 * - WiFiManager ile Captive Portal (AP: Kuma-Relay-XXXX / pass: 12345678)
 * - Web UI: /  (kurallar, test, Wi-Fi portal/reset)
 * - /kuma: Uptime Kuma Webhook (JSON) {heartbeat.status:0..3}
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <FS.h>
#include <LittleFS.h>
#include <driver/ledc.h>  // ESP32 LEDC PWM kütüphanesi

// LCD kullanımı (T-Display için aç)
#define USE_TFT 1
#if USE_TFT
  // TFT_eSPI pin tanımları (User_Setup.h'a alternatif)
  #define ST7789_DRIVER
  #define TFT_WIDTH  135
  #define TFT_HEIGHT 240
  #define TFT_MOSI 19
  #define TFT_SCLK 18
  #define TFT_CS   5
  #define TFT_DC   16
  #define TFT_RST  23
  #define TFT_BL   4
  #define SPI_FREQUENCY 27000000
  
  #include <TFT_eSPI.h>
  
  // TTGO T-Display için TFT_eSPI kütüphanesi
  TFT_eSPI tft = TFT_eSPI();
  #define TFT_BL_PIN 4
  
  // TTGO T-Display butonları
  #define BUTTON_1 35  // Sol buton (WiFi Manager)
  #define BUTTON_2 0   // Sağ buton (Ekran açma/kapama) - BOOT button
  
  bool displayOn = true;
  bool button1Pressed = false;
  unsigned long button1PressTime = 0;
  bool button2Pressed = false;
  unsigned long lastDisplayUpdate = 0;
  const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // 1 saniyede bir güncelle
#endif

// AP portal parolası
#define WIFI_PORTAL_AP_PASS "12345678"

// Debug log seviyeleri
#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1  
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_DEBUG 3

// Mevcut log seviyesi (değiştirilebilir)
#define CURRENT_LOG_LEVEL LOG_LEVEL_INFO

// Log makroları
#define LOG_ERROR(msg) if(CURRENT_LOG_LEVEL >= LOG_LEVEL_ERROR) Serial.print("[ERROR] "), Serial.println(msg)
#define LOG_INFO(msg) if(CURRENT_LOG_LEVEL >= LOG_LEVEL_INFO) Serial.print("[INFO] "), Serial.println(msg)
#define LOG_DEBUG(msg) if(CURRENT_LOG_LEVEL >= LOG_LEVEL_DEBUG) Serial.print("[DEBUG] "), Serial.println(msg)

String apName() {
  char b[32];
  sprintf(b,"Kuma-Relay-%06X",(uint32_t)(ESP.getEfuseMac() & 0xFFFFFF));
  return String(b);
}

#if USE_TFT
// Ekran fonksiyonları
void initDisplay() {
  Serial.println("TFT init starting...");
  
  // Backlight ayarla
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);
  Serial.println("Backlight ON");
  
  // TFT'yi başlat
  tft.init();
  tft.setRotation(1); // Landscape
  Serial.println("TFT initialized");
  
  // İlk test - ekranı siyah yap
  tft.fillScreen(TFT_BLACK);
  Serial.println("Screen filled black");
  
  // Basit bir test yazısı
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("TTGO READY");
  Serial.println("Initial text drawn");
  
  displayOn = true;
  delay(2000); // 2 saniye göster
  
  Serial.println("TFT display initialization completed");
}

void toggleDisplay() {
  displayOn = !displayOn;
  Serial.print("Display toggled, now: ");
  Serial.println(displayOn ? "ON" : "OFF");
  
  digitalWrite(TFT_BL_PIN, displayOn ? HIGH : LOW);
  
  if (displayOn) {
    Serial.println("Turning display ON and updating content...");
    // Ekran açıldığında içerik göster
    delay(200); // Arka ışığın açılmasını bekle
    updateDisplay();
  } else {
    Serial.println("Turning display OFF");
    // Ekranı siyah yap
    tft.fillScreen(TFT_BLACK);
  }
}

void updateDisplay() {
  // Serial.println("updateDisplay() called"); // Debug kapatıldı
  
  if (!displayOn) {
    // Serial.println("Display is OFF, returning"); // Debug kapatıldı
    return;
  }
  
  // Serial.println("Drawing main screen..."); // Debug kapatıldı
  
  // Ekranı temizle
  tft.fillScreen(TFT_BLACK);
  
  // Başlık
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("AIT Servers Health");
  
  // WiFi durumu
  tft.setTextSize(1);
  tft.setCursor(0, 25);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.print("WiFi: ");
    tft.println(WiFi.SSID());
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print("IP: ");
    tft.println(WiFi.localIP());
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("WiFi: Disconnected");
  }
  
  // Sistem bilgisi
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(0, 70);
  tft.print("Uptime: ");
  tft.print(millis() / 1000);
  tft.println(" sec");
  
  // Buton bilgisi
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.setCursor(0, 120);
  tft.println("Left: WiFi (3s)");
  tft.println("Right: Screen");
  
  // Serial.println("Main screen drawn successfully"); // Debug kapatıldı
}

void handleButtons() {
  // Sol buton - WiFi Manager (3 saniye basılı tutma)
  bool btn1State = digitalRead(BUTTON_1) == LOW;
  if (btn1State && !button1Pressed) {
    button1Pressed = true;
    button1PressTime = millis();
  } else if (!btn1State && button1Pressed) {
    button1Pressed = false;
    unsigned long pressDuration = millis() - button1PressTime;
    if (pressDuration >= 3000) {
      // 3 saniye basılı tutuldu - WiFi Manager başlat
      startWifiManagerFromButton();
    }
  }
  
  // Sağ buton - Ekran açma/kapama + Force Refresh
  bool btn2State = digitalRead(BUTTON_2) == LOW;
  if (btn2State && !button2Pressed) {
    button2Pressed = true;
    Serial.println("Right button pressed - toggling display and force refresh");
    toggleDisplay();
    
    // Force refresh ekle
    if (displayOn) {
      Serial.println("Force refreshing display...");
      delay(500);
      updateDisplay();
      Serial.println("Force refresh completed");
    }
    
    delay(200); // Debounce
  } else if (!btn2State && button2Pressed) {
    button2Pressed = false;
  }
}
#endif

WebServer server(80);

// LEDC (PWM)
const int LEDC_TIMER_BITS = 8;     // 0-255 duty
int ledcFreq = 1000;               // Hz
int ledcChannelsUsed = 0;

// Uygun çıkışlar (TTGO/ESP32 güvenli seçim)
// TFT pinleri (4,5,16,18,19,23) ve buton pinleri (0,35) hariç tutuldu
struct PinMap { const char* label; uint8_t gpio; bool pwm; };
PinMap PINS[] = {
  {"IO2",2,true},{"IO13",13,true},{"IO14",14,true},{"IO17",17,true},
  {"IO21",21,true},{"IO22",22,true},{"IO25",25,true},{"IO26",26,true},
  {"IO27",27,true},{"IO32",32,true},{"IO33",33,true}
};
const size_t PIN_COUNT = sizeof(PINS)/sizeof(PinMap);

struct OutputState {
  bool initialized=false;
  bool isPwm=false;
  uint8_t channel=255;
  uint8_t duty=0;
  bool digitalHigh=false;
  // Animasyon özellikleri
  bool isAnimating=false;
  String animationType="none"; // "blink", "fade", "pulse"
  unsigned long animationInterval=1000; // ms
  unsigned long lastAnimationUpdate=0;
  bool animationDirection=true; // true=yukarı, false=aşağı
  uint16_t animationMin=0;
  uint16_t animationMax=1023;
  uint16_t animationStep=50;
  uint16_t currentAnimationValue=0;
};
OutputState states[40];

int gpioFromLabel(const String& label){
  for(size_t i=0;i<PIN_COUNT;i++) if(label==PINS[i].label) return PINS[i].gpio;
  return -1;
}
bool pinSupportsPwm(uint8_t gpio){
  for(size_t i=0;i<PIN_COUNT;i++) if(gpio==PINS[i].gpio) return PINS[i].pwm;
  return false;
}
uint8_t allocChannelFor(uint8_t gpio){
  if(states[gpio].channel!=255) return states[gpio].channel;
  // Yeni API'de channel otomatik atanır
  ledcAttach(gpio, ledcFreq, LEDC_TIMER_BITS);
  states[gpio].channel = gpio; // GPIO numarasını channel olarak kullan
  return gpio;
}
void ensurePinInit(uint8_t gpio){
  if(gpio>39) return;
  if(!states[gpio].initialized){
    pinMode(gpio, OUTPUT);
    digitalWrite(gpio, LOW);
    states[gpio].initialized=true;
  }
}
void applyDigital(uint8_t gpio, bool high){
  // Sadece önemli değişikliklerde log
  static bool lastStates[40] = {false};
  if (lastStates[gpio] != high) {
    LOG_DEBUG("Digital GPIO" + String(gpio) + " -> " + (high ? "HIGH" : "LOW"));
    lastStates[gpio] = high;
  }
  
  ensurePinInit(gpio);
  if(states[gpio].channel!=255){
    ledcDetach(gpio);
    states[gpio].channel=255;
  }
  states[gpio].isPwm=false;
  states[gpio].digitalHigh=high;
  digitalWrite(gpio, high?HIGH:LOW);
}
void applyPwm(uint8_t gpio, uint16_t value0_1023){
  // Sadece önemli değişikliklerde log (>10% değişim)
  static uint16_t lastValues[40] = {9999}; // Başlangıç değeri geçersiz
  uint16_t diff = abs((int)lastValues[gpio] - (int)value0_1023);
  if (lastValues[gpio] == 9999 || diff > 100) { // >10% değişim
    LOG_DEBUG("PWM GPIO" + String(gpio) + " -> " + String(value0_1023) + "/1023");
    lastValues[gpio] = value0_1023;
  }
  
  ensurePinInit(gpio);
  if(!pinSupportsPwm(gpio)){ 
    LOG_DEBUG("Pin doesn't support PWM, falling back to digital");
    applyDigital(gpio, value0_1023>=512); 
    return; 
  }
  uint8_t duty = (uint8_t)map((int)value0_1023,0,1023,0,255);
  allocChannelFor(gpio); // LEDC channel'ı hazırla
  states[gpio].isPwm=true; states[gpio].duty=duty;
  ledcWrite(gpio, duty);
}

// Animasyon fonksiyonları
void startAnimation(uint8_t gpio, const String& type, unsigned long interval, uint16_t minVal, uint16_t maxVal, uint16_t step){
  if(gpio>39) return;
  states[gpio].isAnimating = true;
  states[gpio].animationType = type;
  states[gpio].animationInterval = interval;
  states[gpio].animationMin = minVal;
  states[gpio].animationMax = maxVal;
  states[gpio].animationStep = step;
  states[gpio].currentAnimationValue = minVal;
  states[gpio].animationDirection = true;
  states[gpio].lastAnimationUpdate = millis();
  
  LOG_INFO("Animation started GPIO" + String(gpio) + " - " + type + ", " + String(interval) + "ms");
}

void stopAnimation(uint8_t gpio){
  if(gpio>39) return;
  if(states[gpio].isAnimating) { // Sadece çalışıyorsa log
    LOG_INFO("Animation stopped GPIO" + String(gpio));
  }
  states[gpio].isAnimating = false;
  states[gpio].animationType = "none";
}

void processAnimations(){
  unsigned long currentTime = millis();
  
  for(uint8_t gpio=0; gpio<40; gpio++){
    if(!states[gpio].isAnimating) continue;
    
    if(currentTime - states[gpio].lastAnimationUpdate >= states[gpio].animationInterval){
      states[gpio].lastAnimationUpdate = currentTime;
      
      if(states[gpio].animationType == "blink"){
        // Digital blink (0/1 arası geçiş)
        states[gpio].digitalHigh = !states[gpio].digitalHigh;
        applyDigital(gpio, states[gpio].digitalHigh);
        
      } else if(states[gpio].animationType == "fade"){
        // PWM fade (smooth geçiş)
        if(states[gpio].animationDirection){
          states[gpio].currentAnimationValue += states[gpio].animationStep;
          if(states[gpio].currentAnimationValue >= states[gpio].animationMax){
            states[gpio].currentAnimationValue = states[gpio].animationMax;
            states[gpio].animationDirection = false;
          }
        } else {
          if(states[gpio].currentAnimationValue >= states[gpio].animationStep){
            states[gpio].currentAnimationValue -= states[gpio].animationStep;
          } else {
            states[gpio].currentAnimationValue = states[gpio].animationMin;
          }
          if(states[gpio].currentAnimationValue <= states[gpio].animationMin){
            states[gpio].currentAnimationValue = states[gpio].animationMin;
            states[gpio].animationDirection = true;
          }
        }
        applyPwm(gpio, states[gpio].currentAnimationValue);
        
      } else if(states[gpio].animationType == "pulse"){
        // PWM pulse (hızlı fade)
        if(states[gpio].animationDirection){
          states[gpio].currentAnimationValue = states[gpio].animationMax;
          states[gpio].animationDirection = false;
        } else {
          states[gpio].currentAnimationValue = states[gpio].animationMin;
          states[gpio].animationDirection = true;
        }
        applyPwm(gpio, states[gpio].currentAnimationValue);
      }
    }
  }
}

// Config (LittleFS:/config.json)
String configJson;

bool loadConfig(){
  if(!LittleFS.exists("/config.json")){
    StaticJsonDocument<1536> d;
    JsonArray rules = d.createNestedArray("rules");
    JsonObject r1 = rules.createNestedObject();
    r1["condition"]["type"]="status"; r1["condition"]["value"]="down";
    r1["action"]["pin"]="IO2"; r1["action"]["mode"]="digital"; r1["action"]["value"]=1;
    r1["else"]["behavior"]="nochange";
    JsonObject r2 = rules.createNestedObject();
    r2["condition"]["type"]="status"; r2["condition"]["value"]="up";
    r2["action"]["pin"]="IO2"; r2["action"]["mode"]="digital"; r2["action"]["value"]=0;
    r2["else"]["behavior"]="nochange";
    d["pwm"]["freq"]=1000;
    File f=LittleFS.open("/config.json","w"); if(!f) return false;
    serializeJsonPretty(d, f); f.close();
  }
  File f=LittleFS.open("/config.json","r"); if(!f) return false;
  configJson=f.readString(); f.close();

  StaticJsonDocument<1024> d;
  if(!deserializeJson(d,configJson)){
    if(d["pwm"]["freq"].is<int>()){
      ledcFreq = d["pwm"]["freq"].as<int>();
      // LEDC frekansı yeniden ayarlanacak (yeni API'de otomatik)
    }
  }
  return true;
}
bool saveConfig(const String& body){
  StaticJsonDocument<4096> d; if(deserializeJson(d, body)) return false;
  File f=LittleFS.open("/config.json","w"); if(!f) return false;
  f.print(body); f.close(); configJson=body;
  if(d["pwm"]["freq"].is<int>()){
    ledcFreq=d["pwm"]["freq"].as<int>();
    // LEDC frekansı yeniden ayarlanacak (yeni API'de otomatik)
  }
  return true;
}

// WiFi Manager
bool startWifiPortalBlocking(bool reset=false){
  WiFiManager wm;
  wm.setConfigPortalBlocking(true);
  wm.setConfigPortalTimeout(30); // 30 saniye timeout (600→30)
  if(reset) wm.resetSettings();
  Serial.println("Starting WiFiManager autoConnect...");
  bool result = wm.autoConnect(apName().c_str(), WIFI_PORTAL_AP_PASS);
  Serial.print("WiFiManager result: ");
  Serial.println(result);
  return result;
}
bool tryWifiAuto(uint32_t ms=8000){
  WiFi.mode(WIFI_STA); WiFi.begin();
  uint32_t t=millis(); while(WiFi.status()!=WL_CONNECTED && millis()-t<ms) delay(200);
  return WiFi.status()==WL_CONNECTED;
}
void ensureWifiConnectedOrPortal(){
  Serial.println("Trying auto WiFi connection...");
  if(tryWifiAuto()) {
    Serial.println("WiFi auto-connected successfully");
    return;
  }
  
  Serial.println("Auto WiFi failed, starting WiFiManager portal...");
  if(!startWifiPortalBlocking(false)) {
    Serial.println("WiFiManager failed, continuing without WiFi...");
    // ESP.restart(); // Restart yerine WiFi olmadan devam et
  } else {
    Serial.println("WiFiManager portal completed successfully");
  }
}

// Buton ile WiFi Manager başlatma
void startWifiManagerFromButton(){
  #if USE_TFT
    Serial.println("WiFi Manager starting - TFT display skipped");
    // TFT komutları geçici olarak kapalı
    // tft.fillScreen(TFT_BLACK);
    // tft.setTextSize(2);
    // tft.setTextColor(TFT_YELLOW);
    // tft.setCursor(0, 40);
    // tft.println("WiFi Portal");
  #endif
  
  WiFi.disconnect(true, true);
  delay(1000);
  startWifiPortalBlocking(false);
}

// ---------------- HTML (dikkat: doğru kapanış) ----------------
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="tr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kuma → ESP32 GPIO</title>
<link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
<link href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.11.3/font/bootstrap-icons.min.css" rel="stylesheet">
<style>body{background:#0e1117;color:#e6edf3}.card{background:#161b22;border-color:#30363d;color: cornsilk;}.form-select,.form-control{background:#0d1117;color:#e6edf3;border-color:#30363d}.form-text{color: inherit;}.btn-primary{background:#238636;border-color:#238636}.btn-outline-secondary{color:#c9d1d9;border-color:#30363d}code{color:#abb2bf}.table{font-size:0.85rem}.table-responsive{max-height:600px;overflow-y:auto}.form-control-sm,.form-select-sm{font-size:0.75rem;padding:0.2rem 0.4rem}.small{font-size:0.7rem}.text-muted{color:#8b949e!important}.animation-row{background:#1c2128!important;border-top:1px solid #30363d}.animation-controls{padding:0.5rem;font-size:0.75rem}</style>
</head><body><div class="container py-4">
<div class="d-flex align-items-center justify-content-between mb-3">
  <h1 class="h5 m-0"><i class="bi bi-hdd-network"></i> Kuma → ESP32 GPIO</h1>
  <span id="ip" class="badge text-bg-dark"></span>
</div>
<div class="row g-3">
  <div class="col-lg-8">
    <div class="card">
      <div class="card-header d-flex justify-content-between align-items-center">
        <strong>Kurallar</strong>
        <div><button class="btn btn-sm btn-outline-secondary me-2" id="btnAdd">Kural Ekle</button>
             <button class="btn btn-sm btn-primary" id="btnSave">Kaydet</button></div>
      </div>
      <div class="card-body p-0">
        <div class="table-responsive">
          <table class="table table-dark table-striped table-hover m-0" id="rulesTable">
            <thead><tr><th>#</th><th>Koşul</th><th>Değer</th><th>Pin</th><th>Mod</th><th>Değer</th><th>Else</th><th></th></tr></thead>
            <tbody></tbody>
          </table>
        </div>
      </div>
      <div class="card-footer small text-secondary">Sıralı çalışır, <b>son eşleşen</b> aynı pini ezer.</div>
    </div>
    <div class="card mt-3"><div class="card-header"><strong>Hızlı Test</strong></div>
      <div class="card-body">
        <div class="row g-2 align-items-end">
          <div class="col-3"><label class="form-label">Pin</label><select class="form-select" id="tPin"></select></div>
          <div class="col-3"><label class="form-label">Mod</label><select class="form-select" id="tMode"><option value="digital">Digital</option><option value="pwm">PWM</option></select></div>
          <div class="col-3"><label class="form-label">Değer</label><input class="form-control" id="tVal" placeholder="digital:0/1, pwm:0-1023"></div>
          <div class="col-3"><button class="btn btn-outline-secondary w-100" id="btnTest">Uygula</button></div>
        </div>
        <div class="mt-3">
          <button id="btnSimDown" class="btn btn-sm btn-outline-secondary me-2">Simüle: DOWN</button>
          <button id="btnSimUp" class="btn btn-sm btn-outline-secondary me-2">Simüle: UP</button>
          <button id="btnSimMaint" class="btn btn-sm btn-outline-secondary">Simüle: MAINT</button>
        </div>
      </div>
    </div>
  </div>
  <div class="col-lg-4">
    <div class="card"><div class="card-header"><strong>Wi-Fi Ayarları</strong></div>
      <div class="card-body">
        <p class="small m-0">Ağ: <code id="ssid"></code></p>
        <ol class="small mt-2 mb-3"><li><b>Konfigürasyon Portalını Başlat</b></li><li>Wi-Fi listesinden <code id="apname"></code> (şifre: <code>12345678</code>)</li><li>Yeni SSID/şifreyi kaydet</li></ol>
        <div class="d-flex gap-2">
          <button class="btn btn-sm btn-primary" id="btnPortal">Konfigürasyon Portalını Başlat</button>
          <button class="btn btn-sm btn-outline-danger" id="btnWifiReset">Wi-Fi Ayarlarını Sıfırla</button>
        </div>
      </div>
    </div>
    <div class="card mt-3"><div class="card-header"><strong>PWM Ayarları</strong></div>
      <div class="card-body">
        <label class="form-label">LEDC Frekansı (Hz)</label>
        <input type="number" min="100" max="20000" step="100" class="form-control" id="pwmFreq">
        <div class="form-text">0–255 duty (8-bit) kullanılır.</div>
      </div>
    </div>
    <div class="card mt-3"><div class="card-header"><strong>Webhook</strong></div>
      <div class="card-body small">
        <div><b>URL:</b> <code id="webhookUrl"></code></div>
        <div><b>CT:</b> <code>application/json</code></div>
<pre class="mb-0"><code>{
  "msg": "{{msg}}",
  "monitor": {{monitorJSON}},
  "heartbeat": {{heartbeatJSON}}
}</code></pre>
      </div>
    </div>
    <div class="card mt-3"><div class="card-header"><strong>📋 Animasyon Kullanım Örnekleri</strong></div>
      <div class="card-body small">
        <div class="mb-3">
          <strong>🔴 Yanıp Sönen LED (Blink):</strong><br>
          ✅ Anim işaretli<br>
          Type: <code>Blink</code><br>
          ms: <code>1000</code> (1 saniyede bir)<br>
          Min: <code>0</code>, Max: <code>1</code>, Step: <code>1</code>
        </div>
        <div class="mb-3">
          <strong>🌅 Fade Efekti (PWM):</strong><br>
          ✅ Anim işaretli<br>
          Type: <code>Fade</code><br>
          ms: <code>50</code> (yumuşak geçiş)<br>
          Min: <code>0</code>, Max: <code>1023</code>, Step: <code>20</code>
        </div>
        <div class="mb-0">
          <strong>⚡ Hızlı Pulse (Uyarı):</strong><br>
          ✅ Anim işaretli<br>
          Type: <code>Pulse</code><br>
          ms: <code>300</code> (hızlı nabız)<br>
          Min: <code>0</code>, Max: <code>800</code>, Step: <code>100</code>
        </div>
      </div>
    </div>
  </div>
</div>
</div>
<script src="/script.js"></script>
</body></html>
)HTML";  // <— ÖNEMLİ: HTML gömülü string burada kapanıyor

// ----------- Kural motoru -----------
struct KumaCtx{ int status; String monitorId; String monitorName; };
String toLowerS(String s){ s.toLowerCase(); return s; }
void applyElse(uint8_t gpio, const String& mode, const String& behavior, int value){
  if(behavior=="nochange") return;
  if(mode=="digital"){ applyDigital(gpio, (behavior=="off")?false:(value!=0)); }
  else { applyPwm(gpio, (behavior=="off")?0:(uint16_t)constrain(value,0,1023)); }
}
bool matchCondition(JsonObject cond, const KumaCtx& c){
  String type=cond["type"]|"status"; String val=cond["value"]|""; String vlow=toLowerS(val);
  if(type=="always") return true;
  if(type=="status"){
    if(vlow=="down") return c.status==0;
    if(vlow=="up") return c.status==1;
    if(vlow=="pending") return c.status==2;
    if(vlow=="maintenance") return c.status==3;
    return c.status==val.toInt();
  }
  if(type=="monitorId") return c.monitorId==val;
  if(type=="monitorNameContains"){ String n=c.monitorName; n.toLowerCase(); return n.indexOf(vlow)>=0; }
  return false;
}
void evaluateRules(const KumaCtx& ctx){
  StaticJsonDocument<8192> d; if(deserializeJson(d, configJson)) return;
  JsonArray rules=d["rules"].as<JsonArray>(); if(rules.isNull()) return;
  for(JsonObject r:rules){
    JsonObject cond=r["condition"], act=r["action"], els=r["else"];
    String label=act["pin"]|"IO2"; int gpio=gpioFromLabel(label); if(gpio<0) continue;
    String mode=act["mode"]|"digital";
    int val=act["value"].is<int>()?act["value"].as<int>():1;
    String ebeh=els["behavior"]|"nochange";
    int eval=els["value"].is<int>()?els["value"].as<int>():0;
    
    // Debug JSON parsing (sadece DEBUG seviyesinde)
    LOG_DEBUG("Rule parsing - Pin: " + label + ", GPIO: " + String(gpio) + 
              ", Mode: " + mode + ", Value: " + String(val));
    
    // Animasyon parametreleri
    JsonObject anim = act["animation"];
    bool hasAnimation = false;
    
    // Animasyon enable kontrolü - sadece animation objesi varsa ve enable true ise
    if(!anim.isNull() && anim.containsKey("enable") && anim["enable"].as<bool>()) {
      hasAnimation = true;
    }
    
    LOG_DEBUG("GPIO" + String(gpio) + " Animation check: hasAnimation=" + (hasAnimation ? "true" : "false"));
    
    bool ok=matchCondition(cond,ctx);
    if(ok){ 
      if(hasAnimation){
        // Animasyon modunda
        String animType = anim["type"].as<String>();
        unsigned long animInterval = anim["interval"].as<unsigned long>();
        uint16_t animMin = anim["minValue"].as<uint16_t>();
        uint16_t animMax = anim["maxValue"].as<uint16_t>();
        uint16_t animStep = anim["step"].as<uint16_t>();
        
        if(animType == "" || animInterval == 0) animType = "blink", animInterval = 1000;
        if(animMin == animMax) animMin = 0, animMax = (mode=="digital") ? 1 : 1023;
        if(animStep == 0) animStep = (mode=="digital") ? 1 : 50;
        
        LOG_DEBUG("Starting animation - Type: " + animType + ", Interval: " + 
                 String(animInterval) + "ms, Range: " + String(animMin) + "-" + String(animMax));
        
        startAnimation(gpio, animType, animInterval, animMin, animMax, animStep);
      } else {
        // Normal mod - animasyonu durdur
        LOG_INFO("Normal mode GPIO" + String(gpio) + " - " + mode + ", Value: " + String(val));
        
        stopAnimation(gpio);
        if(mode=="digital") {
          LOG_INFO("Applying Digital: GPIO" + String(gpio) + " = " + (val!=0 ? "HIGH" : "LOW"));
          applyDigital(gpio, val!=0);
        } else {
          uint16_t pwmValue = (uint16_t)constrain(val,0,1023);
          LOG_INFO("Applying PWM: GPIO" + String(gpio) + " = " + String(pwmValue) + "/1023");
          applyPwm(gpio, pwmValue);
        }
      }
    } else {
      // Else durumu - animasyonu durdur
      LOG_DEBUG("Else condition GPIO" + String(gpio) + " - Behavior: " + ebeh);
      
      stopAnimation(gpio);
      applyElse(gpio, mode, ebeh, eval);
    }
  }
}

// ----------- HTTP handlers -----------
void handleIndex(){ server.send_P(200,"text/html; charset=utf-8", INDEX_HTML); }
void handlePins(){
  StaticJsonDocument<1024> d; JsonArray a=d.to<JsonArray>();
  for(size_t i=0;i<PIN_COUNT;i++){ JsonObject o=a.createNestedObject(); o["label"]=PINS[i].label; o["gpio"]=PINS[i].gpio; o["pwm"]=PINS[i].pwm; }
  String s; serializeJson(a,s); server.send(200,"application/json",s);
}
void handleGetConfig(){ server.send(200,"application/json",configJson); }
void handlePostConfig(){
  if(!server.hasArg("plain")){ server.send(400,"text/plain","No body"); return; }
  if(!saveConfig(server.arg("plain"))){ server.send(400,"text/plain","Invalid JSON"); return; }
  server.send(200,"application/json","{\"ok\":true}");
}
void handleTest(){
  String pin=server.arg("pin"), mode=server.arg("mode");
  int gpio=gpioFromLabel(pin); if(gpio<0){ server.send(400,"text/plain","Bad pin"); return; }
  int val=server.hasArg("value")?server.arg("value").toInt():1;
  if(mode=="digital") applyDigital(gpio, val!=0);
  else applyPwm(gpio, (uint16_t)constrain(val,0,1023));
  server.send(200,"application/json","{\"ok\":true}");
}
void handleKuma(){
  if(server.method()!=HTTP_POST){ server.send(405,"text/plain","Method Not Allowed"); return; }
  if(!server.hasArg("plain")){ server.send(400,"text/plain","No body"); return; }
  
  // Webhook geldi
  String webhookBody = server.arg("plain");
  LOG_INFO("=== KUMA WEBHOOK RECEIVED ===");
  LOG_DEBUG("Raw JSON: " + webhookBody);
  
  StaticJsonDocument<4096> d;
  if(deserializeJson(d, webhookBody)){ 
    LOG_ERROR("Bad JSON received!");
    server.send(400,"text/plain","Bad JSON"); 
    return; 
  }
  
  KumaCtx c;
  c.status = d["heartbeat"]["status"].is<int>()? d["heartbeat"]["status"].as<int>() : -1;
  c.monitorId = (const char*)(d["monitor"]["id"] | "");
  c.monitorName= (const char*)(d["monitor"]["name"] | "");
  
  // Ana bilgileri logla
  String statusStr = (c.status==0?"DOWN":c.status==1?"UP":c.status==2?"PENDING":"MAINTENANCE");
  LOG_INFO("Monitor: " + c.monitorName + " [" + c.monitorId + "] -> " + statusStr);
  
  // Ek bilgiler DEBUG seviyesinde
  if(d["msg"]) {
    LOG_DEBUG("Message: " + String((const char*)d["msg"]));
  }
  if(d["heartbeat"]["time"]) {
    LOG_DEBUG("Heartbeat Time: " + String((const char*)d["heartbeat"]["time"]));
  }
  
  // Kuralları uygula
  LOG_DEBUG("Applying rules...");
  evaluateRules(c);
  LOG_DEBUG("Rules applied successfully");
  
  server.send(200,"application/json","{\"ok\":true}");
  
  #if USE_TFT
    if (displayOn) {
      // Ekranı temizle ve webhook bilgilerini göster
      tft.fillScreen(TFT_BLACK);
      
      // Başlık
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.setTextSize(2);
      tft.setCursor(0, 0);
      tft.println("WEBHOOK RECEIVED");
      
      // Monitor bilgileri
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(0, 25);
      tft.print("Monitor: ");
      tft.println(c.monitorName.c_str());
      
      tft.setCursor(0, 40);
      tft.print("ID: ");
      tft.println(c.monitorId.c_str());
      
      // Status bilgisi (renkli)
      tft.setCursor(0, 55);
      tft.setTextColor((c.status==0)?TFT_RED:(c.status==1)?TFT_GREEN:TFT_YELLOW, TFT_BLACK);
      tft.setTextSize(2);
      tft.print("STATUS: ");
      tft.println(c.status==0?"DOWN":c.status==1?"UP":c.status==2?"PEND":"MAINT");
      
      // Zaman bilgisi
      tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
      tft.setTextSize(1);
      tft.setCursor(0, 85);
      tft.print("Time: ");
      tft.print(millis() / 1000);
      tft.println("s");
      
      // Mesaj varsa göster
      if(d["msg"]) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(0, 100);
        tft.print("Msg: ");
        tft.println((const char*)d["msg"]);
      }
      
      // Alt bilgi
      tft.setTextColor(0x7BEF, TFT_BLACK); // Gri renk (RGB565 format)
      tft.setCursor(0, 120);
      tft.println("Returning to main in 5s...");
      
      // 5 saniye bekle sonra normal ekranı göster
      delay(5000);
      updateDisplay();
    }
  #endif
  
  LOG_INFO("=== WEBHOOK PROCESSING COMPLETE ===");
}
// Wi-Fi yardımcıları
void handleWifiPortal(){ server.send(200,"text/plain","Reboot + Portal..."); delay(300); WiFi.disconnect(true,true); ESP.restart(); }
void handleWifiReset(){ WiFiManager wm; wm.resetSettings(); server.send(200,"text/plain","Wi-Fi reset, reboot..."); delay(300); ESP.restart(); }
void handleWifiSsid(){ server.send(200,"text/plain", WiFi.SSID()); }
void handleWifiApName(){ server.send(200,"text/plain", apName()); }
void handleScript(){ 
  server.sendHeader("Content-Type", "application/javascript");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/javascript", R"JS(
let pins=[], config={rules:[], pwm:{freq:1000}};

function el(t,a={},c=[]){
  const e=document.createElement(t);
  Object.entries(a).forEach(([k,v])=>{
    if(k==='class')e.className=v;
    else if(k==='html')e.innerHTML=v;
    else e.setAttribute(k,v)
  });
  c.forEach(ch=>e.appendChild(ch));
  return e;
}

async function loadPins(){
  const r=await fetch('/api/pins'); 
  pins=await r.json(); 
  const tPin=document.getElementById('tPin'); 
  pins.forEach(p=>{
    const o=el('option',{value:p.label}); 
    o.textContent=`${p.label} (GPIO${p.gpio})`; 
    tPin.appendChild(o);
  });
}

function ruleRow(rule,idx){
  const tr=el('tr',{},[]), selCond=el('select',{class:'form-select form-select-sm'});
  ['status','monitorId','monitorNameContains','always'].forEach(t=>{
    const o=el('option',{value:t});o.textContent=t;selCond.appendChild(o);
  });
  selCond.value=rule.condition?.type||'status';
  
  const inpCondVal=el('input',{class:'form-control form-control-sm',placeholder:'down|up|pending|maintenance / id / name'}); 
  inpCondVal.value=rule.condition?.value||'down';
  
  const selPin=el('select',{class:'form-select form-select-sm'}); 
  pins.forEach(p=>{
    const o=el('option',{value:p.label});o.textContent=p.label;selPin.appendChild(o);
  }); 
  selPin.value=rule.action?.pin||pins[0]?.label||'IO2';
  
  const selMode=el('select',{class:'form-select form-select-sm'}); 
  [['digital','Digital'],['pwm','PWM']].forEach(([v,t])=>{
    const o=el('option',{value:v});o.textContent=t;selMode.appendChild(o);
  }); 
  selMode.value=rule.action?.mode||'digital';
  
  const inpVal=el('input',{class:'form-control form-control-sm',placeholder:'digital:0/1, pwm:0-1023'}); 
  inpVal.value=rule.action?.value??1;
  
  // Animasyon seçenekleri - detaylı
  const animDiv=el('div',{class:'d-flex flex-column gap-1',style:'min-width:200px'});
  const chkAnim=el('input',{type:'checkbox',class:'form-check-input',id:`anim_${idx}`});
  chkAnim.checked=rule.action?.animation?.enable||false;
  
  const selAnimType=el('select',{class:'form-select form-select-sm',style:'width:80px'});
  [['blink','Blink'],['fade','Fade'],['pulse','Pulse']].forEach(([v,t])=>{
    const o=el('option',{value:v});o.textContent=t;selAnimType.appendChild(o);
  });
  selAnimType.value=rule.action?.animation?.type||'blink';
  selAnimType.disabled=!chkAnim.checked;
  
  const inpAnimInterval=el('input',{type:'number',class:'form-control form-control-sm',placeholder:'ms',style:'width:70px',min:'100',max:'10000'});
  inpAnimInterval.value=rule.action?.animation?.interval||1000;
  inpAnimInterval.disabled=!chkAnim.checked;
  
  const inpAnimMin=el('input',{type:'number',class:'form-control form-control-sm',placeholder:'Min',style:'width:60px',min:'0',max:'1023'});
  inpAnimMin.value=rule.action?.animation?.minValue||0;
  inpAnimMin.disabled=!chkAnim.checked;
  
  const inpAnimMax=el('input',{type:'number',class:'form-control form-control-sm',placeholder:'Max',style:'width:60px',min:'0',max:'1023'});
  inpAnimMax.value=rule.action?.animation?.maxValue||(rule.action?.mode==='digital'?1:1023);
  inpAnimMax.disabled=!chkAnim.checked;
  
  const inpAnimStep=el('input',{type:'number',class:'form-control form-control-sm',placeholder:'Step',style:'width:60px',min:'1',max:'100'});
  inpAnimStep.value=rule.action?.animation?.step||(rule.action?.mode==='digital'?1:50);
  inpAnimStep.disabled=!chkAnim.checked;
  
  // İlk satır - Enable ve Type
  const animRow1=el('div',{class:'d-flex gap-1 align-items-center'},[
    chkAnim,
    el('label',{class:'form-check-label small',for:`anim_${idx}`,style:'width:35px'},[document.createTextNode('Anim')]),
    selAnimType
  ]);
  
  // İkinci satır - Interval, Min, Max, Step
  const animRow2=el('div',{class:'d-flex gap-1 align-items-center'},[
    el('span',{class:'small text-muted',style:'width:35px'},[document.createTextNode('ms:')]),
    inpAnimInterval,
    el('span',{class:'small text-muted'},[document.createTextNode('Min:')]),
    inpAnimMin,
    el('span',{class:'small text-muted'},[document.createTextNode('Max:')]),
    inpAnimMax,
    el('span',{class:'small text-muted'},[document.createTextNode('Step:')]),
    inpAnimStep
  ]);
  
  animDiv.appendChild(animRow1);
  animDiv.appendChild(animRow2);
  
  const selElse=el('select',{class:'form-select form-select-sm'}); 
  [['nochange','NoChange'],['off','Off'],['value','Value']].forEach(([v,t])=>{
    const o=el('option',{value:v});o.textContent=t;selElse.appendChild(o);
  }); 
  selElse.value=rule.else?.behavior||'nochange';
  
  const inpElseVal=el('input',{class:'form-control form-control-sm',placeholder:'Else value'}); 
  if(rule.else&&rule.else.value!==undefined) inpElseVal.value=rule.else.value;
  
  const btnUp=el('button',{class:'btn btn-sm btn-outline-secondary me-1'},[document.createTextNode('↑')]);
  const btnDn=el('button',{class:'btn btn-sm btn-outline-secondary me-1'},[document.createTextNode('↓')]);
  const btnDel=el('button',{class:'btn btn-sm btn-outline-danger'},[document.createTextNode('Sil')]);
  
  btnUp.onclick=()=>{
    if(idx>0){
      const r=config.rules.splice(idx,1)[0];
      config.rules.splice(idx-1,0,r);
      renderRules();
    }
  };
  btnDn.onclick=()=>{
    if(idx<config.rules.length-1){
      const r=config.rules.splice(idx,1)[0];
      config.rules.splice(idx+1,0,r);
      renderRules();
    }
  };
  btnDel.onclick=()=>{
    config.rules.splice(idx,1);
    renderRules();
  };
  
  selCond.onchange=()=>{rule.condition=rule.condition||{}; rule.condition.type=selCond.value;};
  inpCondVal.oninput=()=>{rule.condition=rule.condition||{}; rule.condition.value=inpCondVal.value;};
  selPin.onchange=()=>{rule.action=rule.action||{}; rule.action.pin=selPin.value;};
  selMode.onchange=()=>{rule.action=rule.action||{}; rule.action.mode=selMode.value;};
  inpVal.oninput=()=>{rule.action=rule.action||{}; rule.action.value=Number(inpVal.value);};
  
  // Animasyon event handlers
  chkAnim.onchange=()=>{
    rule.action=rule.action||{}; rule.action.animation=rule.action.animation||{};
    rule.action.animation.enable=chkAnim.checked;
    selAnimType.disabled=!chkAnim.checked;
    inpAnimInterval.disabled=!chkAnim.checked;
    inpAnimMin.disabled=!chkAnim.checked;
    inpAnimMax.disabled=!chkAnim.checked;
    inpAnimStep.disabled=!chkAnim.checked;
  };
  selAnimType.onchange=()=>{rule.action=rule.action||{}; rule.action.animation=rule.action.animation||{}; rule.action.animation.type=selAnimType.value;};
  inpAnimInterval.oninput=()=>{rule.action=rule.action||{}; rule.action.animation=rule.action.animation||{}; rule.action.animation.interval=Number(inpAnimInterval.value);};
  inpAnimMin.oninput=()=>{rule.action=rule.action||{}; rule.action.animation=rule.action.animation||{}; rule.action.animation.minValue=Number(inpAnimMin.value);};
  inpAnimMax.oninput=()=>{rule.action=rule.action||{}; rule.action.animation=rule.action.animation||{}; rule.action.animation.maxValue=Number(inpAnimMax.value);};
  inpAnimStep.oninput=()=>{rule.action=rule.action||{}; rule.action.animation=rule.action.animation||{}; rule.action.animation.step=Number(inpAnimStep.value);};
  
  selElse.onchange=()=>{rule.else=rule.else||{}; rule.else.behavior=selElse.value;};
  inpElseVal.oninput=()=>{rule.else=rule.else||{}; rule.else.value=Number(inpElseVal.value);};
  
  tr.append(
    el('td',{html:String(idx+1)}), 
    el('td',{},[selCond]), 
    el('td',{},[inpCondVal]), 
    el('td',{},[selPin]), 
    el('td',{},[selMode]), 
    el('td',{},[inpVal]), 
    el('td',{},[el('div',{class:'d-flex gap-1'},[selElse,inpElseVal])]), 
    el('td',{},[el('div',{class:'d-flex'},[btnUp,btnDn,btnDel])])
  );
  
  // Animasyon satırı (ayrı tr)
  const animTr = el('tr',{class:'animation-row'});
  const animTd = el('td',{colspan:'8',class:'animation-controls'});
  animTd.appendChild(animDiv);
  animTr.appendChild(animTd);
  
  return [tr, animTr];
}

function renderRules(){ 
  const tb=document.querySelector('#rulesTable tbody'); 
  tb.innerHTML=''; 
  config.rules.forEach((r,i)=>{
    const [mainRow, animRow] = ruleRow(r,i);
    tb.appendChild(mainRow);
    tb.appendChild(animRow);
  }); 
  document.getElementById('pwmFreq').value=config.pwm?.freq??1000; 
}

async function loadConfig(){ 
  const r=await fetch('/api/config'); 
  config=await r.json(); 
  if(!config.rules)config.rules=[]; 
  if(!config.pwm)config.pwm={freq:1000}; 
  renderRules(); 
}

function addRule(){ 
  config.rules.push({
    condition:{type:'status',value:'down'}, 
    action:{
      pin:pins[0]?.label||'IO2',
      mode:'digital',
      value:1,
      animation:{
        enable:false,
        type:'blink',
        interval:1000,
        minValue:0,
        maxValue:1,
        step:1
      }
    }, 
    else:{behavior:'nochange'}
  }); 
  renderRules(); 
}

async function saveConfig(){ 
  const freq=Number(document.getElementById('pwmFreq').value||1000); 
  config.pwm={freq}; 
  const r=await fetch('/api/config',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify(config)
  }); 
  alert(r.ok?'Kaydedildi':'Hata'); 
}

async function testApply(){ 
  const pin=document.getElementById('tPin').value, 
        mode=document.getElementById('tMode').value, 
        val=document.getElementById('tVal').value; 
  await fetch(`/test?pin=${encodeURIComponent(pin)}&mode=${mode}&value=${encodeURIComponent(val)}`); 
}

async function simulate(s){ 
  const b={
    msg:`Sim ${s}`,
    monitor:{id:"demo",name:"Demo"},
    heartbeat:{status:s==='down'?0:s==='up'?1:3}
  }; 
  await fetch('/kuma',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify(b)
  }); 
  alert('Simülasyon gönderildi'); 
}

async function wifiPortal(){ 
  const r=await fetch('/wifi/portal'); 
  alert(await r.text()); 
}

async function wifiReset(){ 
  if(!confirm('Wi-Fi ayarları silinsin ve yeniden başlatılsın mı?'))return; 
  const r=await fetch('/wifi/reset'); 
  alert(await r.text()); 
}

// Ana başlatma fonksiyonu
(async ()=>{
  await loadPins(); 
  
  document.getElementById('btnAdd').onclick=addRule; 
  document.getElementById('btnSave').onclick=saveConfig; 
  document.getElementById('btnTest').onclick=testApply; 
  document.getElementById('btnSimDown').onclick=()=>simulate('down'); 
  document.getElementById('btnSimUp').onclick=()=>simulate('up'); 
  document.getElementById('btnSimMaint').onclick=()=>simulate('maintenance'); 
  document.getElementById('btnPortal').onclick=wifiPortal; 
  document.getElementById('btnWifiReset').onclick=wifiReset; 
  
  const ip=location.host; 
  document.getElementById('ip').textContent=ip; 
  document.getElementById('webhookUrl').textContent=`http://${ip}/kuma`; 
  document.getElementById('ssid').textContent=(await (await fetch('/wifi/ssid')).text()); 
  document.getElementById('apname').textContent=(await (await fetch('/wifi/apname')).text()); 
  
  const tPin=document.getElementById('tPin'); 
  pins.forEach(pi=>{
    const o=document.createElement('option'); 
    o.value=pi.label; 
    o.textContent=`${pi.label} (GPIO${pi.gpio})`; 
    tPin.appendChild(o);
  }); 
  
  await loadConfig(); 
})();
)JS");
}

// ----------- setup / loop -----------
void setup(){
  Serial.begin(115200);
  Serial.println("Setup starting...");
  
  #if USE_TFT
    Serial.println("Initializing display...");
    // Ekran ve butonları başlat
    initDisplay();
    pinMode(BUTTON_1, INPUT_PULLUP);
    pinMode(BUTTON_2, INPUT_PULLUP);
    
    // Başlangıç mesajı
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(0, 60);
    tft.println("Starting...");
    delay(1000);
  #endif

  LittleFS.begin(true);
  loadConfig();
  
  #if USE_TFT
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(0, 60);
    tft.println("WiFi...");
  #endif
  
  Serial.println("Starting WiFi connection...");
  ensureWifiConnectedOrPortal();
  Serial.println("WiFi connection completed");

  #if USE_TFT
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(0, 60);
    tft.println("WiFi OK!");
    delay(2000);
  #endif

  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/pins", HTTP_GET, handlePins);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/test", HTTP_GET, handleTest);
  server.on("/kuma", HTTP_POST, handleKuma);
  server.on("/wifi/portal", HTTP_GET, handleWifiPortal);
  server.on("/wifi/reset",  HTTP_GET, handleWifiReset);
  server.on("/wifi/ssid",   HTTP_GET, handleWifiSsid);
  server.on("/wifi/apname", HTTP_GET, handleWifiApName);
  server.on("/script.js",   HTTP_GET, handleScript);
  
  Serial.println("Starting web server...");
  server.begin();
  Serial.println("Web server started");

  Serial.println("Initializing GPIO pins...");
  for(size_t i=0;i<PIN_COUNT;i++){ pinMode(PINS[i].gpio, OUTPUT); digitalWrite(PINS[i].gpio, LOW); }
  Serial.println("GPIO pins initialized");

  #if USE_TFT
    Serial.println("Setup completed, showing main display...");
    // İlk ekran güncellemesi (sistem tam hazır olduktan sonra)
    delay(500); // Tüm servisler başlasın
    Serial.print("WiFi Status: ");
    Serial.println(WiFi.status());
    Serial.print("WiFi IP: ");
    Serial.println(WiFi.localIP());
    
    Serial.println("Calling updateDisplay() from setup...");
    updateDisplay();
    lastDisplayUpdate = millis();
    Serial.println("Main display update completed");
    
    // Test: Hemen kontrol et
    delay(1000);
    Serial.println("1 second after updateDisplay - checking if still visible");
    
    // Force bir test daha yap
    Serial.println("Drawing test text on top...");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(0, 0);
    tft.println("IDLE TEST");
    Serial.println("Test text drawn");
  #endif
  
  Serial.println("Setup completed - listening for webhooks");
  Serial.println("=================================");
}

void loop(){ 
  // İlk loop çağrısını göster
  static bool firstLoop = true;
  if (firstLoop) {
    Serial.println("*** Listening for webhooks ***");
    firstLoop = false;
  }
  
  // Animasyonları işle (en üstte olmalı)
  processAnimations();
  
  #if USE_TFT
    // Butonları kontrol et
    handleButtons();
    
    // Debug: Her 10 saniyede loop durumunu yazdır (azaltıldı)
    // static unsigned long lastDebug = 0;
    // if (millis() - lastDebug > 10000) {
    //   Serial.print("System OK - Uptime: ");
    //   Serial.print(millis() / 1000);
    //   Serial.print("s, WiFi: ");
    //   Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    //   lastDebug = millis();
    // }
    
    // Ekranı belirli aralıklarla güncelle
    if (displayOn && millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
      // Serial.println("Auto updating display..."); // Debug mesajı kapatıldı
      updateDisplay();
      lastDisplayUpdate = millis();
    }
  #endif
  
  // Server handling'i sonda yap
  server.handleClient();
}
