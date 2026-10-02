#!/usr/bin/env python3
"""Engendre board_pins.h depuis une declaration de carte.

    python3 tools/gen_board.py boards/isno-super.json build/board/board_pins.h

Meme format de declaration que isno-launcher et OpenRFTest : les broches, les
radios et, par radio, la plage autorisee. Le firmware LIT ces valeurs, il n'en
connait aucune. Appele par CMake a la configuration ; rien d'engendre n'est
commis.

OpenProfalux ne pilote qu'UNE radio CC1101, en 868 (Profalux emet a 868,425 MHz,
voir PROFALUX_FREQ_HZ). Sur une carte multi-radio comme la Super, on retient la
radio 868 ; sur une carte mono-radio, la seule.

Les noms de macros sont ceux QUE LE CODE d'OpenProfalux lit deja
(CC1101_PIN_*, LED_*, BTN_PIN_DEBUG_*), pas ceux d'OpenRFTest : chaque firmware
garde son vocabulaire, seul le format de la declaration est partage.
"""
import json
import sys


def pick_radio(d):
    """La radio 868 si la carte en a une, sinon la premiere declaree."""
    radios = d["radios"]
    for r in radios:
        if str(r.get("band")) == "868":
            return r
    return radios[0]


def header(d):
    ui = d.get("ui", {})
    radio = pick_radio(d)
    pins = radio["pins"]
    bands = {str(b["radio"]): b for b in d.get("bands_allowed_khz", [])}
    band = str(radio.get("band"))
    if band not in bands:
        raise SystemExit("radio %s sans plage declaree dans %s" % (band, d["id"]))
    b = bands[band]
    # SPI2_HOST existe sur l'ESP32 comme sur l'ESP32-S3 ; VSPI/HSPI_HOST ont
    # disparu sur le S3. Un seul hote, valable partout, evite le #if par puce.
    freq = int(d.get("spi_freq_hz", 6_000_000))
    ws2812 = 1 if ui.get("led_ws2812", False) else 0
    L = [
        "/* ENGENDRE par tools/gen_board.py depuis boards/%s.json. Ne pas modifier. */" % d["id"],
        "#ifndef OPENPROFALUX_BOARD_PINS_H",
        "#define OPENPROFALUX_BOARD_PINS_H",
        "",
        '#define TARGET_NAME          "%s"' % d["id"],
        '#define BOARD_NAME           "%s"' % d.get("name", d["id"]),
        "",
        "/* Radio CC1101 (%s MHz) sur le bus SPI de la carte. */" % band,
        "#define CC1101_SPI_HOST      SPI2_HOST",
        "#define CC1101_PIN_SCK       %d" % pins["sck"],
        "#define CC1101_PIN_MISO      %d" % pins["miso"],
        "#define CC1101_PIN_MOSI      %d" % pins["mosi"],
        "#define CC1101_PIN_CS        %d" % pins["cs"],
        "#define CC1101_PIN_GDO0      %d" % pins["gdo0"],
        "#define CC1101_PIN_GDO2      %d" % pins["gdo2"],
        "#define CC1101_SPI_FREQ_HZ   %d" % freq,
        "",
        "/* Plage autorisee de la radio retenue, bornes incluses (kHz). */",
        "#define CC1101_BAND_LOW_KHZ  %d" % b["low"],
        "#define CC1101_BAND_HIGH_KHZ %d" % b["high"],
        "#define CC1101_MAX_DBM       %d" % b.get("max_dbm", 10),
        "",
        "/* Interface. Les boutons de debogage restent desactives : tout passe par MQTT/HA. */",
        "#define LED_PIN              %d" % ui.get("led", -1),
        "#define LED_ACTIVE_HIGH      %d" % (1 if ui.get("led_active_high", True) else 0),
        "#define LED_IS_WS2812        %d" % ws2812,
        "#define BTN_PIN_DEBUG_UP     -1",
        "#define BTN_PIN_DEBUG_STOP   -1",
        "#define BTN_PIN_DEBUG_DOWN   -1",
        "",
        "#endif",
    ]
    return "\n".join(L) + "\n"


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    d = json.load(open(sys.argv[1], encoding="utf-8"))
    text = header(d)
    try:
        if open(sys.argv[2], encoding="utf-8").read() == text:
            sys.exit(0)  # inchange : ne pas toucher au fichier, sinon tout recompile
    except FileNotFoundError:
        pass
    open(sys.argv[2], "w", encoding="utf-8").write(text)
