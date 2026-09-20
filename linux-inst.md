# system-overlay — Linux (CachyOS / KDE Plasma) proje yapısı

> Bu belge, CachyOS + KDE Plasma 6 / Wayland üzerindeki **system-overlay**
> (v1.4.1) projesinin yapısını ve davranışını tarif eder. Windows karşılığı
> `win-inst.md` (stat-win v1.2.0).
>
> Tüm kod C++20 + Qt6'dır.

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

- **Her zaman en üstte** (KWin layer-shell `overlay`), çerçevesiz,
  **tıklamaları geçen**, giriş almayan, odak çalmayan. Tam ekran oyunlarda
  bile görünür.
- Kontrol **sistem tepsisi** (StatusNotifierItem): Gizle/Göster, Duraklat,
  satır aç/kapat, Renk, Credit, Çıkış.
- Dinamik hizalama: `ETİKET:` önekleri en geniş görünen etikete pad'lenir.
- Yüzde değerleri: normal yeşil, **≥ %75 sarı**, **≥ %90 kırmızı**.
- Eksik sensör ölümcül değildir: `--%` / `--°C` / `-- MB/s` / `FPS: --`.
- Tek süreç, ana thread, 1 saniyelik QTimer. İş parçacığı yok.

## 2. Teknoloji ve yapı

- **C++20 + Qt6 (Core, Gui, Widgets, DBus) + CMake + LayerShellQt.**

```
src/
  main.cpp                     CLI, tek-instance (QLockFile), QApplication
  config/Config.{h,cpp}        ~/.config/system-overlay/config.ini
  metrics/
    HudRow.h                   satır + enum + alignHudRows
    CpuMetrics.{h,cpp}         /proc/stat delta
    MemoryMetrics.{h,cpp}      /proc/meminfo
    NetMetrics.{h,cpp}         /proc/net/dev — up/down AYRI, tek örnek
    FpsTracker.{h,cpp}         KWin Window.damaged (en hızlı istemci)
    MetricManager.{h,cpp}      QTimer, satırlar, renk, görünürlük
  gpu/
    GpuBackend.h, GpuMetrics, GpuDiscovery
    NvidiaGpuBackend           NVML (dlopen) / nvidia-smi yedek
    AmdGpuBackend              sysfs gpu_busy_percent + VRAM + hwmon
    IntelGpuBackend            fdinfo engine busy + VRAM + hwmon
  sensors/
    HwmonScanner.{h,cpp}       CPU sıcaklığı (k10temp Tctl, coretemp Package)
  overlay/
    OverlayWindow.{h,cpp}      QWindow + QBackingStore
    OverlayController.{h,cpp}  ekranlar, tepsi, HUD park/geri-kur
    WaylandOverlay.{h,cpp}     LayerShellQt (zwlr_layer_shell_v1)
    X11Overlay.{h,cpp}         StaysOnTop + boş input shape
    TrayIcon.{h,cpp}           SNI menü (DBus menu)
setup.sh                       kullanıcı kurulumu (~/.local + masaüstü)
resources/system-overlay.desktop.in
contrib/system-overlay.service
```

Windows'taki `HelperTempReader`, PawnIO, `Win32Overlay`, IGCL/ADLX/PDH
backend'leri **yoktur**.

## 3. Satırlar ve veri kaynakları

| Satır | Etiket | Kaynak | Notlar |
|---|---|---|---|
| CPU kullanımı | `CPU:` | `/proc/stat` ilk satır | `busy = total − idle − iowait`. |
| CPU sıcaklığı | `CPU: …°C` | `/sys/class/hwmon/*/temp*_input` | AMD `k10temp` **Tctl**; Intel `coretemp` **Package id 0**. Milli-°C / 1000. Aralık −60…+250 °C dışı yok sayılır. Helper gerekmez. |
| RAM | `RAM:` | `/proc/meminfo` | `used = MemTotal − MemAvailable`. |
| VRAM | `VRAM:` | GPU backend | AMD sysfs; NVIDIA NVML; Intel PCI BAR toplam + fdinfo kullanım. |
| GPU | `GPU:` | GPU backend | AMD `gpu_busy_percent` + hwmon; NVIDIA NVML; Intel fdinfo. |
| Yükleme | `up:` | `/proc/net/dev` `tx_bytes` | `lo` hariç; tun/VPN dahil. |
| İndirme | `down:` | `/proc/net/dev` `rx_bytes` | **Tek `/proc/net/dev` okuması iki satırı besler.** |
| FPS | `FPS:` | KWin `Window.damaged` | En çok hasarlanan (present) istemci. KWin yoksa `--`. Bar yok; 55 altında warning. |

MB/s = bayt/sn / 1024². `net_link_mbit=1000` → 125 MB/s tam bar. `0` → çubuksuz.

### 3.1 Örneklem

1. `prime()` — ilk görüntü.
2. `sample()` — delta; iki örnek öncesi `nullopt`.
3. Ağ satırı kapanıp açılırsa `prime()`.

## 4. Satır modeli

- `HudRow { text, label, value, fraction, color }`; `fraction < 0` → çubuksuz.
- Enum: `RowCpu … RowNetUp, RowNetDown, RowFps, RowCount=7`.
- `alignHudRows` en geniş `ETİKET: ` alanına pad'ler.

## 5. Tepsi menüsü

```
Gizle / Göster
Duraklat / Devam et
────────────
CPU ☑  GPU ☑  RAM ☑  VRAM ☑  up ☑  down ☑  FPS ☑
────────────
Renk ▸  (9 palet + "RGB gir…" + "Renk penceresi…")
────────────
Credit: ecansevdi    (gri, tıklanmaz)
Çıkış
```

- Plasma SNI **DBus menü** kullanır (`setContextMenu`). Wayland'de `QMenu::popup`
  ebeveyn + giriş serisi olmadan `grabbing popup` hatasıyla düşer; bu yüzden
  menü panel tarafından çizilir.
- Layer-shell Overlay, Plasma menülerinin **üstüne** biner. Menü / Renk
  alt menüsü / renk diyalogları açıkken HUD **park edilir** (büyük layer-shell
  margin + `LayerBackground`). Kapanınca overlay **yeniden oluşturulur**
  (aynı yüzeyi unpark etmek KWin'de görünür geri getirmez).
- İmleç menü bölgesinden (ekranın alt ~%55'i) ayrılırsa HUD geri gelir.
- Sol tık tepsi ikonuna da HUD'u geri getirir.
- `show_net` hem `up` hem `down` başlangıç durumudur; tepside ayrı kapatılır.
- FPS anahtarı KWin izleme betiğinin ömrünü yönetir (`start`/`stop`).
- İkon kodla çizilir: koyu kare, üç yeşil çubuk (#a6f28f).

## 6. MetricManager

- Görünürlük `show_*` anahtarlarından; FPS açıksa `FpsTracker::start()`.
- `sampleAndFormat`: GPU ve ağ **en fazla birer** kez.
- `--debug`: hwmon taraması, seçilen CPU temp, GPU özeti, FPS kaynağı,
  satır görünürlüğü, ilk HUD satırları.

## 7. FPS (Linux)

Windows ETW'nin karşılığı: kısa ömürlü bir **KWin betiği** her pencerenin
`damaged` sinyalini sayar, saniyede bir en yüksek değeri
`local.systemoverlay /Fps report(int)` ile bildirir.

- Masaüstü / dock / tooltip / bildirim / overlay'in kendisi sayılmaz.
- Boşta `FPS: --` (0 present); video/oyun açılınca sayı gelir.
- KWin Scripting yoksa satır `--` kalır (ölümcül değil).
- MangoHud enjeksiyonu yoktur; `FpsTracker` arayüzünün arkasına eklenebilir.

## 8. Overlay kabuğu

- **Wayland:** LayerShellQt, `layer=Overlay`, köşe anchor + `offset_x/y`,
  exclusive zone alt köşelerde 0 (panelden kaçınır), klavye yok, boş input
  region (click-through).
- **X11:** `Qt::Tool | WindowStaysOnTopHint | WindowTransparentForInput`.
- Ekran: `primary` / `all` / çıktı adı (`DP-1`).
- `HWND_TOPMOST` afirmasyonu yok.

## 9. Tek instance

`$XDG_RUNTIME_DIR/system-overlay.lock` (`QLockFile`). İkinci başlatış
uyarı kutusu + stderr, çıkış kodu 1. `--help` / `--version` kilitten önce işlenir.

## 10. Yapılandırma

`~/.config/system-overlay/config.ini` (XDG). Eksik anahtar varsayılana düşer.
`position` hem `bottom-right` hem `bottomright` kabul eder.

```ini
[general]
refresh_interval=1000

[display]
screen=primary
position=bottom-right
offset_x=10
offset_y=10
font_size=14
font_family=monospace
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
```

CLI: `--interval <ms>`, `--screen primary|all|<ad>`, `--debug`, `--version`, `--help`.

## 11. Dağıtım ve çalıştırma

### 11.1 AppImage (birincil)

Qt6, Wayland eklentileri ve LayerShellQt paketin içindedir. Çalıştırmak için
sisteme Qt kurulması gerekmez:

```bash
chmod +x pack-appimage.sh
./pack-appimage.sh
chmod +x dist/system-overlay-*-x86_64.AppImage
./dist/system-overlay-1.4.1-x86_64.AppImage
```

`pack-appimage.sh` derleme makinesinde `qt6-base`, `qt6-wayland`,
`layer-shell-qt`, `cmake`, `imagemagick` ister; üretilen AppImage başka
CachyOS/Arch/KDE kutularında bağımsız çalışır. NVIDIA ölçümü hâlâ sistemdeki
`libnvidia-ml.so.1` dosyasını `dlopen` eder (sürücüyle eşleşmeli).

### 11.2 Kaynaktan kurulum (ikinci seçenek)

```bash
chmod +x setup.sh
./setup.sh
# → ~/.local/bin/system-overlay
# → uygulama menüsü + masaüstü kısayolu
```

Elle:

```bash
sudo pacman -S --needed qt6-base qt6-wayland layer-shell-qt cmake gcc
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/system-overlay --debug
```

## 12. Kabul kriterleri

- [x] HUD sağ altta; tam ekran üstünde; tıklamalar geçer; odak yok; görev çubuğunda yok.
- [x] 7 satır hizalı; eksik sensör `--`.
- [x] `up`/`down` ayrı; tek `/proc/net/dev` örneği.
- [x] Tepsi satır anahtarları; FPS KWin betiğini başlatır/durdurur.
- [x] Renk paleti/RGB/diyalog; `text_color` kalıcı.
- [x] ≥ %75 sarı, ≥ %90 kırmızı.
- [x] Menü / Renk açıkken HUD gizli; kapanınca (veya imleç uzaklaşınca) geri.
- [x] `Credit: ecansevdi`.
- [x] `--debug` hwmon + GPU + FPS kaynağı + satırlar.
- [x] Tek instance.
- [x] CPU sıcaklığı hwmon `k10temp/Tctl` veya `coretemp/Package` (PawnIO yok).
