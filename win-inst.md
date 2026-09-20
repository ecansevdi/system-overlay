# stat-win — Windows proje yapısı ve davranış kuralları

> Bu belge, Windows 11 için yazılmış **stat-win** (v1.2.0) projesinin genel
> yapısını ve davranış kurallarını tarif eder. Linux/CachyOS karşılığı
> `linux-inst.md` dosyasındadır.
>
> Tüm kod C++20'dir. Windows'a özgü kısımlar her bölümde belirtilmiştir.

---

## 1. Proje nedir?

Ekranda seçilen köşede (varsayılan **sağ alt**) dikey bir gösterge yığını
olarak duran, hafif bir performans HUD'u:

```
CPU:  10% 49°C
GPU:  14% 47°C
RAM:  5.4/31.3 GiB
VRAM: 0.5/8.0 GiB
up:   12.3 MB/s
down: 73.2 MB/s
FPS:  60
```

Özellikler:

- **Her zaman en üstte**, çerçevesiz, **tıklamaları geçen (click-through)**,
  giriş almayan, asla odak çalmayan bir pencere. Tam ekran oyunlarda bile
  görünür; tıklamalar alttaki uygulamaya gider.
- Kontrol tamamen **sistem tepsisi** üzerinden: sağ tık menüsü
  (Gizle/Göster, Duraklat, satır aç/kapat, Renk, Credit, Çıkış).
- Dinamik hizalama: tüm `ETİKET:` önekleri, görünen en geniş etikete göre
  pad'lenir (monospace font → değerler aynı x'te başlar).
- Her yüzdeye dayalı değer tek renk fonksiyonu kullanır: normal yeşil,
  **≥ %75 sarı (warning)**, **≥ %90 kırmızı (critical)** (eşikler config'ten).
- Eksik/ölçülemeyen sensör **asla ölümcül değildir**: satır `--%` / `--°C` /
  `-- MB/s` gösterir ve uygulama çalışmaya devam eder.
- Tek örneklemli, iş parçacıcısız tasarım: tüm ölçümler ana thread'de, saniyede
  bir QTimer ile.

## 2. Teknoloji ve yapı

- **C++20 + Qt6 (Core, Gui, Widgets) + CMake + Ninja.** Tek süreç, GUI app.

```
src/
  main.cpp                     giriş, tek-instance, CLI (--interval --screen --debug)
  config/Config.{h,cpp}        INI yapılandırma (QSettings), CLI override'ları
  metrics/
    HudRow.h                   satır yapısı + satır enum'u + hizalama algoritması
    CpuMetrics.{h,cpp}         CPU kullanımı (delta örneklemeli)
    MemoryMetrics.{h,cpp}      RAM used/total
    NetMetrics.{h,cpp}         ağ hızı — up/down AYRI (delta örneklemeli)
    FpsTracker.{h,cpp}         FPS (ETW present olayları)
    MetricManager.{h,cpp}      orkestratör: QTimer, satır üretimi, renkler, görünürlük
  gpu/
    GpuMetrics.h               GpuSample (util%, temp, vram used/total) + arayüz
    GpuBackend.h / GpuDiscovery.cpp
    NvidiaGpuBackend / IntelGclBackend / AmdAdlBackend / PdhGpuBackend
  sensors/
    ThermalScanner.{h,cpp}     CPU sıcaklığı keşfi + okuma
    HelperTempReader.{h,cpp}   LibreHardwareMonitor + PawnIO yardımcı süreci
  overlay/
    OverlayWindow.{h,cpp}      çizim: QWindow + QBackingStore
    OverlayController.{h,cpp}  ekran modları, zamanlayıcılar, tepsi
    Win32Overlay.{h,cpp}       HWND_TOPMOST, tool-window, click-through
    TrayIcon.{h,cpp}           tepsi ikonu + menü
assets/                        ikon üretimi + exe/rc/qrc
helper/                        sıcaklık yardımcı süreci
installer/stat-win.iss         Inno Setup
```

## 3. Satırlar ve veri kaynakları (Windows)

| Satır | Etiket | Windows kaynağı | Notlar |
|---|---|---|---|
| CPU kullanımı | `CPU:` | PDH / `GetSystemTimes` delta | busy = total − idle; Görev Yöneticisi toplamına denk. |
| CPU sıcaklığı | `CPU: …°C` | HelperTempReader → LibreHardwareMonitor / PawnIO | Kullanıcı-modu WMI çoğu çipte paket sıcaklığı vermez; helper süreci gerekir. Sağlık aralığı **−60…+250 °C**. |
| RAM | `RAM:` | `GlobalMemoryStatusEx` | used/total; bar `used/total`. |
| VRAM | `VRAM:` | GPU backend | NVML / IGCL / ADLX. |
| GPU kullanım+sıcaklık | `GPU:` | GPU backend | keşif: NVML → IGCL → ADL → PDH engine. |
| Yükleme | `up:` | `GetIfTable2` / IP Helper | **up/down AYRI satırlar.** Loopback hariç. Tek örnek iki satırı besler. |
| İndirme | `down:` | aynı örnek | `InOctets` delta → MB/s. |
| FPS | `FPS:` | ETW present olayları | En çok kare üreten süreç. Yükseltilmiş hak; oturum açılamazsa `--`. |

Birim: MB/s = `bayt/sn / 1024²`. Bar ölçeği: `net_link_mbit` (1000 → 125 MB/s tam ölçek). `0` → çubuksuz.

**Değişmez:** ağ tek seferde örneklenir; **tek örnek iki satırı (up+down) besler.**

### 3.1 Örneklem disiplini (CpuMetrics/NetMetrics)

1. `prime()` — ilk anlık görüntüyü sakla.
2. `sample()` — delta hesapla; iki örneklem dolmadan `nullopt` (HUD `--`).
3. Görünürlük kapanıp açılırsa `prime()` ile taban tazelenir.

## 4. Satır modeli ve hizalama

- `HudRow { text, label, value, fraction, color }` — `fraction < 0` → çubuksuz (FPS; net ölçeği 0 ise up/down).
- `alignHudRows`: `ETİKET: ` alanı en geniş görünen etikete pad'lenir.
- Enum: `RowCpu, RowGpu, RowRam, RowVram, RowNetUp, RowNetDown, RowFps, RowCount=7`.
- FPS: metin satırı; 55 FPS altında warning rengi.

## 5. Tepsi ikonu ve menü

```
Gizle / Göster
Duraklat / Devam et
────────────
CPU ☑  GPU ☑  RAM ☑  VRAM ☑  up ☑  down ☑  FPS ☑
────────────
Renk ▸  (9 hazır palet + "RGB gir…" + "Renk penceresi…")
────────────
Credit: ecansevdi    (gri, tıklanmaz)
Çıkış
```

- Renk `setBaseColor` → HUD + `display/text_color`.
- FPS anahtarı ETW oturumunun ömrünü yönetir.
- Menü imleçte açılmaz; HUD'un olduğu ekran yarısının **karşı kenarında** (12 px pay).
- Menü açıkken HUD gizlenir, kapanınca geri gelir (HUD `HWND_TOPMOST` menünün üstüne biner).
- İkon: koyu yuvarlak kare (rgb 24,24,24, α230) içinde üç yeşil çubuk (#a6f28f).

## 6. MetricManager

- Kurucu: `show_*`, sensör keşfi, `prime()`.
- `sampleAndFormat()`: GPU varsa **tam bir** backend örneği; ağ varsa **tam bir** ağ örneği.
- `setRowVisible`: ağ açılırsa `prime()`; FPS aç/kapa oturumu başlat/durdur.
- `--debug`: sıcaklık kaynağı, GPU özeti, eşikler, güncel HUD satırları.

## 7. FPS

ETW izleme oturumu, görüntü sunumu (present) olaylarını sayar, en hızlı çizen
sürecin FPS'ini verir. Yükseltilmiş hak ister; oturum açılamazsa `--`.

## 8. Overlay penceresi (Win32)

- `Win32Overlay`: `HWND_TOPMOST`, tool-window, skip-taskbar, boş input region
  (click-through), odak yok.
- 5 saniyede bir `HWND_TOPMOST` yeniden afirmasyonu (tam ekran oyunlar
  z-order'ı çalabilir).
- Yerleşim: `availableGeometry` + köşe + `offset_x/offset_y`.
- Ekran: `primary` / `all` / ekran adı.

## 9. Windows'a özgü katmanlar

- `sensors/HelperTempReader.*` + `helper/` — LibreHardwareMonitor + PawnIO.
- GPU: NVML (Windows), IGCL, ADLX/ADL, PDH GPU engine sayaçları.
- `overlay/Win32Overlay.*`, `assets/stat-win.rc`, `installer/stat-win.iss`.

## 10. Yapılandırma

Windows: QSettings, tipik olarak `%APPDATA%/system-overlay/config.ini`
(veya organizasyon yoluna göre). Eksik anahtar güvenli varsayılana düşer.

```ini
[display]
position=bottomright        ; topleft|topright|bottomleft|bottomright
offset_x=10
offset_y=10
font_size=14
font_family=Consolas
show_background=false
show_tray=true
text_color=#a6f28f
outline_color=#000000

[colors]
warning_pct=75
critical_pct=90
warning_color=#f2d24f
critical_color=#f25d5d

[metrics]
show_cpu_usage=true
show_cpu_temp=true
show_gpu_usage=true
show_gpu_temp=true
show_ram=true
show_vram=true
show_net=true
show_fps=true
net_link_mbit=1000

[general]
refresh_interval=1000
```

CLI: `--interval <ms>`, `--screen primary|all|<ad>`, `--debug`, `--version`, `--help`.

## 11. Kabul kriterleri

- [ ] HUD seçili köşede; tam ekran üstünde; tıklamalar geçer; odak yok; görev çubuğunda yok.
- [ ] 7 satır hizalı; eksik sensör `--`.
- [ ] `up`/`down` ayrı ve doğru.
- [ ] Tepsi satır anahtarları; FPS oturum ömrünü yönetir.
- [ ] Renk paleti kalıcı.
- [ ] ≥ %75 sarı, ≥ %90 kırmızı.
- [ ] Menü karşı kenarda; menü açıkken HUD gizli.
- [ ] `Credit: ecansevdi`.
- [ ] `--debug` kaynakları yazar.
- [ ] Tek instance.
