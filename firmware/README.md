# Firmware úlové váhy

Deska LaskaKit Meteo Mini v4.1 (ESP32-C3). Hotový soubor je v `server/static/flash/firmware/ulova_vaha.bin` a nahrává se z prohlížeče na stránce `/flash/` serveru. Tady je zdrojový kód pro úpravy.

## Překlad

**Arduino IDE:** deska *ESP32C3 Dev Module* (jádro Arduino-ESP32 3.2.x), *USB CDC On Boot: Enabled*, *Partition Scheme: No FS 4MB (2MB APP x2)*. Knihovny ze správce knihoven: *SparkFun Qwiic Scale NAU7802 Arduino Library*, *OneWire*, *DallasTemperature*. Otevřete `ulova_vaha/ulova_vaha.ino` (kód je v souborech `.cpp` vedle).

**Příkazová řádka:** `./build.sh` (arduino-cli). Výsledek zkopíruje do instalátoru na serveru a do `manifest.json` zapíše verzi z `config.h`. Stejně překládá i GitHub Actions při každé změně ve složce `firmware/`.

## Soubory

| Soubor | Co dělá |
|---|---|
| `main.cpp` | cyklus probuzení → měření → uložení do RTC paměti → občas odeslání → hluboký spánek |
| `sensors.cpp` | NAU7802 (zesílení 128, 10 vzorků/s, medián), DS18B20, napětí baterie |
| `net.cpp` | Wi-Fi s heslem nebo PEAP, HTTPS s kořenovými certifikáty Let's Encrypt (`roots.h`) |
| `console.cpp` | sériová konzole pro nastavení a kalibraci (115200 Bd) |
| `config.cpp` | nastavení uložené ve flash (NVS) |

## Piny

| GPIO | Funkce |
|---|---|
| IO4 | zapíná 3,3 V do konektorů µŠup (převodník a teploměry), ve spánku vypnuto |
| IO19 / IO18 | I²C SDA / SCL, NAU7802 na adrese 0x2A |
| IO10 | 1-Wire, 2× DS18B20 |
| IO0 | napětí baterie přes dělič (koeficient `vbat_k`, výchozí 2,3/1,3 – ověřte multimetrem) |
| IO5 | tlačítko: držené při zapnutí otevře konzoli i u nastavené váhy |

## Konzole

Po zapnutí (ne po probuzení ze spánku) čeká konzole 20 s na příkaz, nenastavená váha čeká 10 minut. Řádky začínající `@` obsahují JSON pro webovou stránku.

```
show                    nastavení
set ssid SiliconHill    uložení hodnoty (prázdná hodnota: set eap_user -)
read                    jedno měření
tare                    tára prázdné váhy
cal 20.15               kalibrace závažím 20,15 kg
ds / dsfind             teploměry / určení teploměru na tenzometru
wifi                    test Wi-Fi (signál, doba připojení)
upload                  změří a hned odešle
mac                     MAC adresa
run                     začne měřit
```

Klíče pro `set`: `ssid pass eap_user eap_pass eap_identity eap_ca url scale_id token tls ds_cell ds_box interval interval_cold cold_below upload_every cal_offset cal_factor temp_coef cal_temp vbat_k`.

## Výpočet hmotnosti

```
kg = (raw − cal_offset) / cal_factor − temp_coef × (t_tenzometru − cal_temp)
```

`temp_coef` je zpočátku 0. Po 1–2 týdnech provozu ho spočítejte z nočních dat (stálá zátěž, mění se jen teplota) jako sklon závislosti hmotnosti na teplotě tenzometru a nastavte `set temp_coef 0.012`. Server ukládá i surovou hodnotu `raw`, takže jde přepočítat i zpětně.

## Spotřeba (odhad)

Spánek desky ≈ 23 µA, měření ≈ 3 s, odeslání přes Wi-Fi ≈ 3–5 s (s PEAP déle). S měřením každých 15 minut a odesláním jednou za hodinu vychází zhruba 6–8 mAh za den. Plná baterie 6400 mAh by tak teoreticky vydržela přes dva roky bez slunce, v praxi ji omezí samovybíjení a ztráta kapacity v mrazu. Ověřte měřením.
