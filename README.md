# Chytré úly pro Silicon Hill

Otevřená úlová váha na solární napájení: měří hmotnost úlu a teplotu, posílá data přes Wi-Fi a ukazuje je na veřejném přehledu. Vznikla na FEL ČVUT pro včelaře Silicon Hill na Strahově v rámci Mikrograntů na podporu trvalé udržitelnosti ČVUT 2026.

*English: an open, solar-powered beehive scale (ESP32-C3 + NAU7802) with a small self-hosted server and a public dashboard. Firmware is flashed and configured from the browser. MIT licensed.*

## Co je v repozitáři

| Složka | Obsah |
|---|---|
| `firmware/` | Program pro desku LaskaKit Meteo Mini (ESP32-C3): měření, hluboký spánek, odesílání po dávkách, nastavení přes USB |
| `server/` | Server v Pythonu (FastAPI + SQLite): příjem dat, veřejný přehled s grafy, export CSV, instalátor firmwaru v prohlížeči |
| `docs/` | Montážní návod (HTML a PDF, barevně i černobíle), zapojení tenzometru |
| `firmware/tools/ds18b20_test/` | Program na otestování teploměrů DS18B20 před montáží |

## Jak to funguje

```
tenzometr ─ NAU7802 ─┐
2× DS18B20 ──────────┼─ ESP32-C3 (Meteo Mini) ── Wi-Fi ── HTTPS ── server ── veřejný přehled
baterie + panel 6 W ─┘   měří každých 15 min,                    SQLite     grafy, CSV
                         posílá jednou za hodinu
```

- Váha se každých 15 minut probudí, změří hmotnost (medián 16 vzorků), teplotu tenzometru a krabice a napětí baterie, uloží měření do paměti a znovu usne. V zimě, když je v krabici pod 3 °C, měří jednou za hodinu.
- Jednou za hodinu se připojí k Wi-Fi a pošle všechna uložená měření najednou. Když síť nejde, data počkají (až 96 měření).
- Když se hmotnost změní o víc než 0,5 kg, měří 30 minut každou minutu. Při změně nad 1 kg pošle data hned, aby šlo rychle poznat třeba roj.
- Wi-Fi s heslem (WPA2-PSK) i podniková síť s přihlášením (WPA2-Enterprise PEAP, např. SiliconHill).
- Server ukládá naměřená data beze změny. Na přehledu ukazuje i **čistou hmotnost**, ze které jsou odečtené skoky způsobené včelařem (nástavek, medobraní, kontrola), takže je vidět jen práce včel.

## Rychlý start

### 1. Server

Nejjednodušší je Docker. V Coolify vytvořte zdroj z tohoto repozitáře s build packem *Docker Compose*, jako *Base Directory* nastavte `/server` a službě `vahy` přidejte doménu (port 8000). Data jsou ve svazku `vahy-data`. HTTPS je potřeba, jinak nebude fungovat instalace firmwaru z prohlížeče.

Bez Coolify:

```bash
cd server
docker compose up -d          # v docker-compose.yml odkomentujte "ports"
```

Nebo bez Dockeru:

```bash
cd server
pip install -r requirements.txt
uvicorn app.main:app --port 8000
```

Nastavení přes proměnné prostředí: `SITE_TITLE`, `REPO_URL`, `TZ_NAME` (výchozí Europe/Prague), `DB_PATH` (výchozí `/data/vahy.db` v Dockeru), `STALE_AFTER_S` (po kolika sekundách bez dat je váha „nehlásí se“, výchozí 3 h).

### 2. Přidat váhy

Každá váha má své ID a tajný token. Na serveru (v Coolify přes *Terminal*):

```bash
python -m app.admin add vaha-1 "Úl 1" --location "Strahov"
python -m app.admin list
python -m app.admin token vaha-1      # nový token, starý přestane platit
python -m app.admin hide vaha-1       # skrýt z veřejného přehledu
```

Pro vyzkoušení přehledu bez skutečných vah: `python -m tools.simulate --url http://localhost:8000` (vytvoří váhy `demo-1` až `demo-3` s ukázkovými daty, smažete je `python -m app.admin delete demo-1`).

### 3. Firmware a nastavení váhy

Otevřete `https://váš-server/flash/` v Chrome nebo Edge na počítači a připojte Meteo Mini kabelem USB-C:

1. **Nahrát firmware.** Při první instalaci nechte smazat paměť.
2. **Připojit** a vyplnit ID váhy, token, adresu serveru a Wi-Fi. Tlačítkem **Test Wi-Fi** ověříte signál, **Odeslat zkušební měření** ověří celou cestu až na přehled.
3. **Kalibrace:** Tára prázdné váhy, pak závaží se známou hmotností a Kalibrovat. **Určit teploměry**: zahřejte v dlani ten na tenzometru.
4. **Spustit měření.**

Stránka zobrazí i MAC adresu desky, kterou potřebují správci sítě SiliconHill pro registraci.

### 4. Montáž

Postup je v [docs/Navod_sestaveni_ulove_vahy.pdf](docs/Navod_sestaveni_ulove_vahy.pdf) (černobílá verze pro tisk: [docs/Navod_sestaveni_ulove_vahy_cernobily.pdf](docs/Navod_sestaveni_ulove_vahy_cernobily.pdf)).

## Otevřená data

Data jsou veřejná:

- `GET /api/v1/scales` – přehled vah a poslední hodnoty
- `GET /api/v1/scales/{id}/series?days=7` – časová řada (hmotnost, čistá hmotnost, teploty, baterie, události)
- `GET /api/v1/scales/{id}/csv?days=365` – export CSV
- `GET /api/docs` – popis API

Váhy posílají data na `POST /api/v1/ingest` s hlavičkou `Authorization: Bearer <token>`.

## Hardware

LaskaKit Meteo Mini v4.1 (ESP32-C3), MIKROE Load Cell 2 Click (NAU7802), plošinová váha Steinberg SBS-PF-150/10A (rám a tenzometr), 2× DS18B20, Li-Ion 2×18650 6400 mAh, solární panel 6 V / 6 W, krabice IP65. Úplný seznam dílů je v montážním návodu.

## Licence

Kód a dokumentace: [MIT](LICENSE).

Přibalené cizí části mají vlastní licence: [uPlot](https://github.com/leeoniya/uPlot) (MIT), [ESP Web Tools](https://github.com/esphome/esp-web-tools) (Apache 2.0), písmo IBM Plex (SIL OFL 1.1), knihovny pro firmware SparkFun NAU7802 (MIT), OneWire a DallasTemperature (MIT/LGPL), jádro Arduino-ESP32 (LGPL 2.1).
