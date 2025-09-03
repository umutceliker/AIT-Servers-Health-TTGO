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
  ensurePinInit(gpio);
  if(!pinSupportsPwm(gpio)){ applyDigital(gpio, value0_1023>=512); return; }
  uint8_t duty = (uint8_t)map((int)value0_1023,0,1023,0,255);
  allocChannelFor(gpio); // LEDC channel'ı hazırla
  states[gpio].isPwm=true; states[gpio].duty=duty;
  ledcWrite(gpio, duty);
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
<style>body{background:#0e1117;color:#e6edf3}.card{background:#161b22;border-color:#30363d}.form-select,.form-control{background:#0d1117;color:#e6edf3;border-color:#30363d}.btn-primary{background:#238636;border-color:#238636}.btn-outline-secondary{color:#c9d1d9;border-color:#30363d}code{color:#abb2bf}</style>
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
    bool ok=matchCondition(cond,ctx);
    if(ok){ if(mode=="digital") applyDigital(gpio, val!=0); else applyPwm(gpio, (uint16_t)constrain(val,0,1023)); }
    else  { applyElse(gpio, mode, ebeh, eval); }
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
  StaticJsonDocument<4096> d;
  if(deserializeJson(d, server.arg("plain"))){ server.send(400,"text/plain","Bad JSON"); return; }
  KumaCtx c;
  c.status = d["heartbeat"]["status"].is<int>()? d["heartbeat"]["status"].as<int>() : -1;
  c.monitorId = (const char*)(d["monitor"]["id"] | "");
  c.monitorName= (const char*)(d["monitor"]["name"] | "");
  evaluateRules(c);
  server.send(200,"application/json","{\"ok\":true}");
  #if USE_TFT
    if (displayOn) {
      // Kuma durumunu ekranda göster (geçici olarak)
      tft.fillRect(0, 140, 240, 25, TFT_BLACK);
      tft.setCursor(0, 140);
      tft.setTextSize(1);
      tft.setTextColor((c.status==0)?TFT_RED:(c.status==1)?TFT_GREEN:TFT_YELLOW);
      tft.print("Kuma: ");
      tft.print(c.status==0?"DOWN":c.status==1?"UP":c.status==2?"PEND":"MAINT");
      tft.print(" - ");
      tft.print(c.monitorName.c_str());
      
      // 3 saniye sonra normal ekranı göster
      delay(3000);
      updateDisplay();
    }
  #endif
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
  return tr;
}

function renderRules(){ 
  const tb=document.querySelector('#rulesTable tbody'); 
  tb.innerHTML=''; 
  config.rules.forEach((r,i)=>tb.appendChild(ruleRow(r,i))); 
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
    action:{pin:pins[0]?.label||'IO2',mode:'digital',value:1}, 
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
  
  Serial.println("Setup completed - entering loop()");
  Serial.println("=================================");
}

void loop(){ 
  // İlk loop çağrısını göster
  static bool firstLoop = true;
  if (firstLoop) {
    Serial.println("*** FIRST LOOP CALL - Loop is working! ***");
    firstLoop = false;
  }
  
  #if USE_TFT
    // Butonları kontrol et
    handleButtons();
    
    // Debug: Her 10 saniyede loop durumunu yazdır (azaltıldı)
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 10000) {
      Serial.print("System OK - Uptime: ");
      Serial.print(millis() / 1000);
      Serial.print("s, WiFi: ");
      Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
      lastDebug = millis();
    }
    
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
