/*
 * hardware_config.h - brochage et constantes Profalux.
 *
 * Le BROCHAGE ne vit plus ici : il est declare par carte dans boards/<carte>.json
 * et lu depuis l'en-tete engendre board_pins.h (voir plus bas). Ce fichier ne
 * porte que les constantes Profalux, independantes de la carte.
 */
#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

/* sdkconfig.h reste inclus : la construction ESP-IDF l'attend en tete des
 * fichiers qui incluent celui-ci, et la simulation sur poste en fournit un. */
#include "sdkconfig.h"

/* ═══════════════════════════════════════════════════════════════
 * Le brochage vient de la CARTE choisie a la construction.
 *
 * -DBOARD=isno-super | external | m5-atom-lite selectionne boards/<BOARD>.json,
 * et tools/gen_board.py en engendre board_pins.h a la configuration CMake. Ce
 * firmware ne code AUCUNE broche : il les LIT. Ajouter une carte, c'est ajouter
 * un boards/<nom>.json, pas editer ce fichier.
 * ═══════════════════════════════════════════════════════════════ */

#if defined(SIM_MODE)

    /* ─── Simulation sur poste, aucun materiel ─── */
    #define TARGET_NAME               "sim"
    #define CC1101_SPI_HOST           SPI2_HOST
    #define CC1101_PIN_MISO           (-1)
    #define CC1101_PIN_MOSI           (-1)
    #define CC1101_PIN_SCK            (-1)
    #define CC1101_PIN_CS             (-1)
    #define CC1101_PIN_GDO0           (-1)
    #define CC1101_PIN_GDO2           (-1)
    #define CC1101_SPI_FREQ_HZ  1000000
    #define BTN_PIN_DEBUG_UP          -1
    #define BTN_PIN_DEBUG_STOP        -1
    #define BTN_PIN_DEBUG_DOWN        -1
    #define LED_PIN                   (-1)
    #define LED_ACTIVE_HIGH           1
    #define LED_IS_WS2812             0

#else

    /* Engendre dans build/board/board_pins.h depuis boards/${BOARD}.json. */
    #include "board_pins.h"

#endif

/* ═══════════════════════════════════════════════════════════════
 * KEELOQ / Profalux constants (=target-independent)
 * ═══════════════════════════════════════════════════════════════ */

/* Valeurs MESUREES sur l'air le 2026-08-10 (captures/2026-08-10-profalux-868). */
#define PROFALUX_FREQ_HZ          868425000UL  /* 868,425 MHz (etait 868,350 = hors bande) */
#define PROFALUX_TE_US                  455    /* element de base T_E mesure */
#define PROFALUX_BIT_TIME_US           1365    /* periode de bit = 3*Te */
#define PROFALUX_PREAMBLE_ELEMENTS       23    /* 23 alternances de Te */
#define PROFALUX_PREAMBLE_US            455    /* = Te */
#define PROFALUX_HEADER_US             4450    /* silence mesure 4420-4491 us */
#define PROFALUX_REPEAT_COUNT            10  /* HCS301 typical */
#define PROFALUX_PAIR_FRAMES             60  /* 1 par seconde */
#define PROFALUX_PAIR_DELAY_MS         1000

#endif /* HARDWARE_CONFIG_H */
