# AIT Servers Health TTGO

TTGO T-Display ESP32 tabanlı Uptime Kuma webhook relay sistemi. Uptime Kuma'dan gelen webhook'ları GPIO çıkışlarına yönlendirerek fiziksel sinyaller oluşturur.

## 🚀 Özellikler

- **Uptime Kuma Entegrasyonu**: Webhook'lar ile durum izleme
- **TTGO T-Display Desteği**: Renkli LCD ekran ile görsel durum
- **WiFi Manager**: Kolay ağ konfigürasyonu
- **GPIO Kontrolü**: Digital ve PWM çıkışlar
- **Web Arayüzü**: Kural yönetimi ve test araçları
- **Gerçek Zamanlı Monitoring**: Anlık durum gösterimi

## 📱 Desteklenen Cihazlar

- **TTGO T-Display** (ESP32 + ST7789 LCD)
- Standart ESP32 geliştirme kartları

## 🔧 Kurulum

### Gereksinimler

#### Arduino IDE Kütüphaneleri:
```
- WiFi (ESP32 yerleşik)
- WebServer (ESP32 yerleşik)
- ArduinoJson (v6.x)
- WiFiManager (v2.x)
- LittleFS (ESP32 yerleşik)
- TFT_eSPI (TTGO için)
```

#### Donanım Bağlantıları:
```
TTGO T-Display Pin Haritası:
- TFT_MOSI: 19
- TFT_SCLK: 18
- TFT_CS: 5
- TFT_DC: 16
- TFT_RST: 23
- TFT_BL: 4
- BUTTON_1: 35 (Sol)
- BUTTON_2: 0 (Sağ/BOOT)
```

### Kurulum Adımları

1. **Arduino IDE'yi hazırlayın**:
   - ESP32 board paketini yükleyin
   - Gerekli kütüphaneleri yükleyin

2. **TFT_eSPI konfigürasyonu**:
   - `TFT_eSPI` kütüphanesinin `User_Setup.h` dosyasını düzenleyin
   - Veya kodda tanımlı pinleri kullanın

3. **Kodu yükleyin**:
   - `TTGO Project.ino` dosyasını Arduino IDE'de açın
   - Board: "ESP32 Dev Module" seçin
   - Kodu derleyip yükleyin

## 🌐 WiFi Konfigürasyonu

### İlk Kurulum:
1. Cihaz açıldığında AP moduna geçer
2. WiFi listesinde `Kuma-Relay-XXXXXX` ağını bulun
3. Şifre: `12345678`
4. Tarayıcıda konfigürasyon sayfası açılır
5. WiFi bilgilerinizi girin

### Manuel WiFi Reset:
- Sol butonu **3 saniye** basılı tutun
- Veya web arayüzünden "Wi-Fi Ayarlarını Sıfırla"

## 🎮 Kullanım

### Web Arayüzü
ESP32'nin IP adresini tarayıcıda açın:
```
http://[ESP32_IP]/
```

#### Ana Özellikler:
- **Kurallar**: Uptime Kuma durumlarını GPIO aksiyonlarına bağlama
- **Hızlı Test**: GPIO pinlerini manuel test etme
- **WiFi Ayarları**: Ağ konfigürasyonu
- **PWM Ayarları**: Frekans kontrolü

### TTGO T-Display Kontrolü
- **Sol Buton (3s)**: WiFi Manager başlat
- **Sağ Buton**: Ekran açma/kapama

### Webhook Entegrasyonu

#### Uptime Kuma'da Webhook URL:
```
http://[ESP32_IP]/kuma
```

#### Webhook Formatı:
```json
{
  "msg": "{{msg}}",
  "monitor": {{monitorJSON}},
  "heartbeat": {{heartbeatJSON}}
}
```

#### Durum Kodları:
- `0`: DOWN (Çevrimdışı)
- `1`: UP (Çevrimiçi)
- `2`: PENDING (Beklemede)
- `3`: MAINTENANCE (Bakım)

## ⚙️ GPIO Pin Haritası

### Kullanılabilir Pinler:
```
IO2, IO13, IO14, IO17, IO21, IO22, IO25, IO26, IO27, IO32, IO33
```

### Rezerve Pinler (Kullanılmaz):
```
- TFT pinleri: 4, 5, 16, 18, 19, 23
- Buton pinleri: 0, 35
- Diğer sistem pinleri
```

## 📋 Kural Yapısı

### Kural Örneği:
```json
{
  "condition": {
    "type": "status",
    "value": "down"
  },
  "action": {
    "pin": "IO2",
    "mode": "digital",
    "value": 1
  },
  "else": {
    "behavior": "nochange"
  }
}
```

### Koşul Türleri:
- `status`: Durum kontrolü (down/up/pending/maintenance)
- `monitorId`: Monitor ID eşleşmesi
- `monitorNameContains`: Monitor isim içerik kontrolü
- `always`: Her zaman aktif

### Aksiyon Modları:
- `digital`: ON/OFF (0 veya 1)
- `pwm`: PWM sinyal (0-1023)

### Else Davranışları:
- `nochange`: Değişiklik yok
- `off`: Kapat (0 değeri)
- `value`: Belirtilen değeri uygula

## 🔧 Teknik Detaylar

### Dosya Sistemi:
- **LittleFS** kullanılır
- Konfigürasyon: `/config.json`

### PWM Ayarları:
- **LEDC Timer**: 8-bit (0-255)
- **Varsayılan Frekans**: 1000 Hz
- **Ayarlanabilir**: 100-20000 Hz

### Bellek Kullanımı:
- **JSON Buffer**: 8KB (kurallar için)
- **Web Server**: Minimal bellek kullanımı

## 🚨 Sorun Giderme

### WiFi Bağlantı Sorunları:
1. Sol butonu 3 saniye basılı tutun
2. AP moduna geçmesini bekleyin
3. Yeniden konfigüre edin

### Ekran Sorunları:
1. Sağ buton ile ekranı açmayı deneyin
2. TFT_eSPI kütüphane ayarlarını kontrol edin
3. Pin bağlantılarını doğrulayın

### GPIO Sorunları:
1. Web arayüzünden "Hızlı Test" kullanın
2. Pin haritasını kontrol edin
3. PWM/Digital mod uyumluluğunu doğrulayın

## 📄 Lisans

Bu proje MIT lisansı altında yayınlanmıştır.

## 🤝 Katkıda Bulunma

1. Fork edin
2. Feature branch oluşturun (`git checkout -b feature/amazing-feature`)
3. Commit edin (`git commit -m 'Add amazing feature'`)
4. Push edin (`git push origin feature/amazing-feature`)
5. Pull Request açın

## 📞 İletişim

Sorularınız için issue açabilirsiniz.

---

**Not**: Bu proje AIT (Advanced Information Technologies) sunucu altyapısı izleme sistemi için geliştirilmiştir.
