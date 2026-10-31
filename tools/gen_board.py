#!/usr/bin/env python3
"""Engendre board_pins.h depuis une declaration de carte.

    python3 tools/gen_board.py boards/isno-super.json build/board/board_pins.h

Meme format de declaration que isno-launcher et OpenRFTest : les broches, les
radios et, par radio, la plage autorisee. Le firmware LIT ces valeurs, il n'en
connait aucune. Appele par CMake a la configuration ; rien d'engendre n'est
commis.

La radio PRIMAIRE est la 868 (Profalux emet a 868,425 MHz, voir PROFALUX_FREQ_HZ) :
c'est elle que portent les macros CC1101_PIN_* / CC1101_BAND_* historiques, et c'est
l'index 0. Mais TOUTES les radios declarees par la carte sont emises, dans la table
CC1101_RADIO_TABLE_INIT (voir cc1101.c), pour qu'une carte bi-bande comme la Super
pilote aussi sa radio 433. Le firmware ne retient plus une radio en jetant les autres :
il lit la declaration entiere.

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


# Sens de l'horloge RMII, lu du JSON (jamais en dur dans le C). "out" : l'ESP32
# genere l'horloge 50 MHz et la sort (cartes Olimex POE). "ext_in" : horloge
# fournie par un oscillateur externe (ESP32-GATEWAY/EVB).
_ETH_CLK_MODE = {"out": "EMAC_CLK_OUT", "ext_in": "EMAC_CLK_EXT_IN"}


def ethernet_lines(d):
    """Bloc Ethernet filaire, present seulement si la carte declare "ethernet".
    Le code ne lit ces valeurs que sous la garde BOARD_HAS_ETHERNET."""
    eth = d.get("ethernet")
    if not eth:
        return ["", "#define BOARD_HAS_ETHERNET   0"]
    mode = str(eth.get("clk_mode", "out"))
    if mode not in _ETH_CLK_MODE:
        raise SystemExit("ethernet.clk_mode %r inconnu dans %s (attendu out|ext_in)" % (mode, d["id"]))
    return [
        "",
        "/* Ethernet filaire (voir boards/%s.json). BOARD_HAS_ETHERNET garde le code reseau. */" % d["id"],
        "#define BOARD_HAS_ETHERNET   1",
        '#define ETH_PHY_MODEL        "%s"' % eth.get("phy", "LAN8720"),
        "#define ETH_MDC_GPIO         %d" % int(eth.get("mdc", 23)),
        "#define ETH_MDIO_GPIO        %d" % int(eth.get("mdio", 18)),
        "#define ETH_PHY_POWER_GPIO   %d" % int(eth.get("phy_power", -1)),
        "#define ETH_PHY_ADDR         %d" % int(eth.get("phy_addr", 0)),
        "#define ETH_CLK_GPIO         %d" % int(eth.get("clk_gpio", 0)),
        "/* Sens de l'horloge RMII : la POE n'a pas d'oscillateur externe, l'ESP32 la SORT. */",
        "#define ETH_CLK_MODE         %s" % _ETH_CLK_MODE[mode],
    ]


def init_gpio_lines(d):
    """Broches menees a un niveau fixe au tout debut de cc1101_init, AVANT le SPI :
    alimentation de la radio, selection de bande, CS des peripheriques du bus partage
    tenus hauts. Present seulement si la carte declare "init_gpio" ; le code ne les
    pilote que sous la garde BOARD_HAS_INIT_GPIO (la Super n'en declare aucune)."""
    gpios = d.get("init_gpio")
    if not gpios:
        return ["", "#define BOARD_HAS_INIT_GPIO  0"]
    rows = [(int(g["pin"]), 1 if int(g["level"]) else 0, str(g.get("note", ""))) for g in gpios]
    L = [
        "",
        "/* GPIO menes a un niveau fixe au tout debut de cc1101_init (voir boards/%s.json) :" % d["id"],
        " * alimentation radio, selection de bande, CS des peripheriques du SPI partage.",
    ]
    L += [" *   GPIO%-3d = %d%s" % (p, l, ("  (%s)" % n) if n else "") for p, l, n in rows]
    L += [
        " * Lu seulement sous BOARD_HAS_INIT_GPIO. */",
        "#define BOARD_HAS_INIT_GPIO   1",
        "#define BOARD_INIT_GPIO_COUNT %d" % len(rows),
        "#define BOARD_INIT_GPIO { \\",
    ]
    L += ["  { %d, %d }%s \\" % (p, l, "," if i + 1 < len(rows) else "") for i, (p, l, n) in enumerate(rows)]
    L += ["}"]
    return L


def radio_table_lines(d, primary, bands):
    """Table de TOUTES les radios declarees, la primaire (868) en index 0.
    Chaque entree : {bande, CS, GDO0, GDO2, bande basse kHz, bande haute kHz, porteuse kHz}.
    La bande est l'etiquette declaree ("868", "433") : l'UI s'en sert pour nommer chaque
    radio sans la deviner. cc1101.c lit la table pour piloter plusieurs radios sur le bus
    SPI partage."""
    radios = d["radios"]
    ordered = [primary] + [r for r in radios if r is not primary]
    rows = []
    for r in ordered:
        band = str(r.get("band"))
        if band not in bands:
            raise SystemExit("radio %s sans plage declaree dans %s" % (band, d["id"]))
        b = bands[band]
        p = r["pins"]
        rows.append("  { \"%s\", %d, %d, %d, %d, %d, %d }" % (
            band, p["cs"], p["gdo0"], p["gdo2"], b["low"], b["high"], int(b.get("carrier_khz", 0))))
    L = [
        "/* Toutes les radios declarees par la carte (index 0 = primaire 868). */",
        "#define CC1101_COUNT         %d" % len(ordered),
        "/* {bande, CS, GDO0, GDO2, bande_basse_kHz, bande_haute_kHz, porteuse_kHz} par radio. */",
        "#define CC1101_RADIO_TABLE_INIT { \\",
    ]
    L += [row + (", \\" if i + 1 < len(rows) else " \\") for i, row in enumerate(rows)]
    L += ["}"]
    return L


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
        "/* Plage autorisee de la radio PRIMAIRE (868), bornes incluses (kHz). */",
        "#define CC1101_BAND_LOW_KHZ  %d" % b["low"],
        "#define CC1101_BAND_HIGH_KHZ %d" % b["high"],
        "#define CC1101_MAX_DBM       %d" % b.get("max_dbm", 10),
        "",
    ]
    L += radio_table_lines(d, radio, bands)
    L += [
        "",
        "/* Interface. Les boutons de debogage restent desactives : tout passe par MQTT/HA. */",
        "#define LED_PIN              %d" % ui.get("led", -1),
        "#define LED_ACTIVE_HIGH      %d" % (1 if ui.get("led_active_high", True) else 0),
        "#define LED_IS_WS2812        %d" % ws2812,
        "#define BTN_PIN_DEBUG_UP     -1",
        "#define BTN_PIN_DEBUG_STOP   -1",
        "#define BTN_PIN_DEBUG_DOWN   -1",
    ]
    L += ethernet_lines(d)
    L += init_gpio_lines(d)
    L += ["", "#endif"]
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
