/**
 * @file iohome_constants.h
 * @brief io-homecontrol Protocol Constants and Definitions
 * @author iown-homecontrol project
 *
 * Constants and definitions for the io-homecontrol protocol
 * including frame sizes, sync words, keys, and command IDs.
 */

#pragma once

#include <stdint.h>

namespace iohome {

// ============================================================================
// Physical Layer Constants
// ============================================================================

// Porteuse. ⚠️ UNE SEULE : le 2W est retire, et les canaux 868,25 / 869,85 avec lui.
// Mesure du 2026-10-28 : le binaire d'une passerelle tierce ne contient QUE 868,95 (6 occurrences), zero
// occurrence de 868,25 et de 869,85. Un produit du commerce qui marche sur le terrain ne
// saute donc pas de frequence.
constexpr float FREQUENCY_CHANNEL_2 = 868.95f;  // MHz, canal 1W

// Modulation Parameters
constexpr float BIT_RATE = 38.4f;              // kbps
constexpr float FREQ_DEVIATION = 19.2f;        // kHz

// ⚠️ PAS de constante de mot de synchronisation ici. La valeur LOGIQUE 0xFF33 n'apparait
// JAMAIS sur l'air : l'io encapsule chaque octet en UART 8N1, et ce que le CC1101 doit
// reellement programmer est 0x57FD (mesure le 2026-10-28, voir firmware/test/io-framing).
// C'est io_radio.c qui decide, et lui seul. Une seconde definition ici se lirait comme la
// verite et ne le serait pas.


// ============================================================================
// Data Link Layer Constants
// ============================================================================

// Frame Sizes
constexpr uint8_t FRAME_MIN_SIZE = 11;         // bytes (without data)
constexpr uint8_t FRAME_MAX_SIZE = 32;         // bytes (max 21 bytes data)
constexpr uint8_t FRAME_MAX_DATA_SIZE = 21;    // maximum parameter bytes

// Frame Field Sizes
constexpr uint8_t CTRL_BYTE_SIZE = 2;
constexpr uint8_t NODE_ID_SIZE = 3;
constexpr uint8_t COMMAND_ID_SIZE = 1;
constexpr uint8_t ROLLING_CODE_SIZE = 2;       // 1W only
constexpr uint8_t HMAC_SIZE = 6;
constexpr uint8_t CRC_SIZE = 2;

// Frame Field Offsets
constexpr uint8_t OFFSET_CTRL_BYTE_0 = 0;
constexpr uint8_t OFFSET_CTRL_BYTE_1 = 1;
constexpr uint8_t OFFSET_DEST_NODE = 2;
constexpr uint8_t OFFSET_SRC_NODE = 5;
constexpr uint8_t OFFSET_COMMAND_ID = 8;
constexpr uint8_t OFFSET_DATA = 9;
constexpr uint8_t OFFSET_PAIR_MAN_ID = 25;   // trame 0x30 : octet fabricant (capte)
constexpr uint8_t OFFSET_PAIR_STATUS = 26;   // trame 0x30 : octet de statut d'appairage (capte)

// Control Byte 0 Masks
constexpr uint8_t CTRL0_ORDER_MASK = 0xC0;      // bits 7-6 : debut (0x40) + fin (0x80) de message
constexpr uint8_t CTRL0_PROTOCOL_MASK = 0x20;   // bit 5 : pose a 1 sur une vraie trame 1W Somfy
constexpr uint8_t CTRL0_LENGTH_MASK = 0x1F;     // bits 4-0 : longueur = total - CTRL0_LEN_BASE

// Base du champ longueur de l'octet 0 : longueur = total - 3 (tout sauf l'octet 0 et les
// 2 octets de CRC ; couvre de ctrl_byte_1 au dernier octet avant le CRC). Mesure sur une
// vraie trame 1W Somfy : ctrl0 0xF8 -> longueur 24 = 27 - 3.
constexpr uint8_t CTRL0_LEN_BASE = 3;

// Control Byte 1 Masks
constexpr uint8_t CTRL1_USE_BEACON = 0x80;      // bit 7
constexpr uint8_t CTRL1_ROUTED = 0x40;          // bit 6
constexpr uint8_t CTRL1_LOW_POWER = 0x20;       // bit 5
constexpr uint8_t CTRL1_ACK = 0x10;             // bit 4
constexpr uint8_t CTRL1_PROTOCOL_VERSION = 0x0F; // bits 3-0

// Protocol Modes
constexpr uint8_t MODE_1W = 0x00;

// ============================================================================
// Cryptography Constants
// ============================================================================

// AES-128 Parameters
constexpr uint8_t AES_KEY_SIZE = 16;            // 128 bits
constexpr uint8_t AES_BLOCK_SIZE = 16;          // 128 bits
constexpr uint8_t IV_SIZE = 16;                 // 128 bits

// Transfer Key (hardcoded, used for key obfuscation during pairing)
constexpr uint8_t TRANSFER_KEY[AES_KEY_SIZE] = {
  0x34, 0xC3, 0x46, 0x6E, 0xD8, 0x8F, 0x4E, 0x8E,
  0x16, 0xAA, 0x47, 0x39, 0x49, 0x88, 0x43, 0x73
};

// IV Padding Value
constexpr uint8_t IV_PADDING = 0x55;

// CRC-16/KERMIT
constexpr uint16_t CRC_POLYNOMIAL = 0x8408;
constexpr uint16_t CRC_INITIAL = 0x0000;

// ============================================================================
// Command IDs
// ============================================================================

// Discovery Commands (0x28-0x2D)
constexpr uint8_t CMD_DISCOVER_ACTUATOR = 0x28;
constexpr uint8_t CMD_DISCOVER_SENSOR = 0x29;
constexpr uint8_t CMD_DISCOVER_BEACON = 0x2A;
constexpr uint8_t CMD_DISCOVER_CONTROLLER = 0x2B;

// Key Exchange Commands (0x30-0x39)
constexpr uint8_t CMD_KEY_TRANSFER_1W = 0x30;
// Ceremonie d'appairage 1W Somfy, calquee sur une capture Situo reelle
// (danielpetrovic/somfy-io-bridge iohc_remote1w.cpp) : 0x30 (cle) puis une RAFALE de
// 0x20 (private-write) puis 0x2e (confirmation). Un 0x30 seul fait tressaillir le moteur
// mais ne le bonde pas. 0x39 n'est PAS une annonce : c'est le DEPAIRAGE (retrait de cle 1W).
constexpr uint8_t CMD_PRIVATE_WRITE = 0x20;     // "private write" de la ceremonie / trame compagnon
constexpr uint8_t CMD_PAIR_CONFIRM  = 0x2e;     // confirmation de pairage (close de la ceremonie)
constexpr uint8_t CMD_REMOVE_1W     = 0x39;     // depairage (retrait d'une cle 1W)

// Marqueur fabricant dans la trame d'appairage 0x30 (different de l'octet bouton) :
// 0x02 pour Somfy, 0x01 pour Velux (samr037 frame_build.h, danielpetrovic manufacturer_).
constexpr uint8_t MANUFACTURER_SOMFY_PAIR = 0x02;
constexpr uint8_t MANUFACTURER_VELUX_PAIR = 0x01;
constexpr uint8_t PAIR_STATUS_BYTE        = 0x01;   // 2e octet de statut du 0x30, observe a 0x01

// man_id du 0x30 D'ENROLEMENT que NOUS emettons = marque de l'ENROLEUR (la passerelle qui ajoute
// une identite), PAS du moteur. Verite terrain (capture d'un enrolement reel par une passerelle de reference le 2026-11-08,
// + banc vodhash) : la reference enrole un moteur SOMFY et vodhash un VELUX avec la MEME valeur 0x01. Les
// MANUFACTURER_*_PAIR ci-dessus sont la marque que les TELECOMMANDES mettent dans LEUR ceremonie
// (Somfy 0x02) : a NE PAS reemettre comme notre man_id (bug io12g/h : on reemettait le 0x02 capte).
constexpr uint8_t IO_ENROLL_MAN_ID        = 0x01;

// Rythme d'emission de l'appairage, RELEVE sur l'enrolement reel d'une passerelle tierce du 2026-10-24 (issue #87,
// journal 6, 48 emissions chronometrees) : chaque trame est emise 4 FOIS, octet pour octet
// IDENTIQUES, CRC compris, et le code roulant NE BOUGE PAS entre les repetitions ; repetitions
// espacees de 25 ms (min 23, median 25).
// Les commandes gardent l'espacement de 40 ms valide sur un Velux (io_send.cpp).
constexpr uint8_t  IO_TX_REPEATS          = 4;
constexpr uint16_t IO_TX_REPEAT_GAP_MS    = 25;

// Ecole passerelle de reference (capture Somfy de Pascal, 2026-11-08) : l'enrolement 0x30 reussi n'est PAS une
// rafale identique. C'est DEUX copies a seq CONSTANTE : la 1re avec le bit 0x20 de ctrl1 (FC20),
// la 2e sans (FC00), espacees d'environ 143 ms debut-a-debut (W77472826 -> W77472969). Le CRC
// couvre ctrl1, donc recalcule par variante. (L'ecole vodhash/Velux = 4x FC20 identiques : autre
// motif prouve, non retenu ici car la cible est un Somfy.)
constexpr uint16_t IO_ENROLL_CTRL1_GAP_MS = 143;

// Ceremonie d'ajout d'un emetteur 1W, recette de samr037/iohc-flipper (CC1101 comme nous, validee sur
// du vrai materiel Somfy ET Velux avec une seule et meme ceremonie, tx_runner.c send_pair_with_identity) :
//   1. 0x39 signe a NOTRE adresse vers 00003F : n'efface que notre propre entree eventuelle ;
//   2. 0x30 (cle enveloppee, man_id capte) vers les 4 destinations d'enrolement, meme code roulant ;
//      chaque moteur ne retient que la destination de sa classe ;
//   3. STOP puis DESCENTE signes avec la nouvelle cle : exiges par Velux (manuel KLI 313, p. 10,
//      etape 5, dans les 3 s), sans effet genant sur Somfy (court arret puis debut de descente).
// Chaque trame part en IO_PAIR_COPIES copies : la 1re avec le bit basse conso de ctrl1, les autres
// sans, a IO_PAIR_COPY_GAP_MS d'ecart, comme une vraie telecommande.
constexpr uint8_t  IO_PAIR_BROADCAST[3]      = {0x00, 0x00, 0x3F};   // tous les appareils
constexpr uint8_t  IO_PAIR_DESTS[4][3]       = {
    {0x00, 0x00, 0x3F},   // tous les appareils (Somfy)
    {0x00, 0x00, 0xBF},   // Velux fenetre (GGL, GGU, CVP)
    {0x00, 0x00, 0xFF},   // Velux volet (SSL, SST)
    {0x00, 0x03, 0x7F},   // Velux store et autres variantes
};
constexpr uint8_t  IO_PAIR_REMOVE_DATA[1]    = {0x00};
constexpr uint8_t  IO_PAIR_COPIES            = 4;
constexpr uint16_t IO_PAIR_COPY_GAP_MS       = 14;
constexpr uint16_t IO_PAIR_DEST_GAP_MS       = 20;
constexpr uint16_t IO_PAIR_STEP_GAP_MS       = 40;

// NOTE 2026-10-28 : les adresses d'ecriture de la rafale 0x20 (0x0C00, 0x05XX) ont ete
// RETIREES. C'etait du REGLAGE DE FINS DE COURSE (doc io : enter_settings_mode,
// save_lower/upper_end_limit, save_settings), emis par la telecommande MAITRE qui parraine,
// pas par la nouvelle telecommande qui s'enrole. L'emettre sur un moteur deja regle le
// dereglerait. L'enrolement « nouvelle telecommande » = un seul 0x30 (voir io_pair.cpp).

// Authentication Commands (0x3C-0x3D)
constexpr uint8_t CMD_CHALLENGE_REQUEST = 0x3C;
constexpr uint8_t CMD_CHALLENGE_RESPONSE = 0x3D;

// Configuration Commands (0x50-0x57)
constexpr uint8_t CMD_GET_NAME = 0x50;
constexpr uint8_t CMD_SET_NAME = 0x51;
constexpr uint8_t CMD_GET_INFO = 0x52;
constexpr uint8_t CMD_SET_INFO = 0x53;

// Actuator Control Commands
//
// Correction 2026-10-22 : monter/descendre/stop ne sont PAS des octets de commande.
// Les anciens 0x61/0x62/0x63 etaient des placeholders "TBD" herites de
// rspaargaren/iown-home (qui emet aussi en aveugle : stop = 0x61, open/close =
// set_position). Le seul referentiel MESURE est samr037/iohc-flipper, qui capture et
// re-emet de vraies telecommandes Somfy io.
//
// Forme reelle (samr037, trames courtes "p0x00_14" de 25 octets vers le broadcast
// 00 00 3F, cf. docs/phase2_updown_capture.csv + src/state/tx_runner.c) :
//   cmd = CMD_EXECUTE (0x00), puis charge utile = [origin, fabricant, code_bouton, 0, 0, 0]
// Le sens est donc un unique "code bouton", pas un parametre de 2 octets.
constexpr uint8_t CMD_EXECUTE  = 0x00;          // "Activate/Execute function" (appui bouton)
constexpr uint8_t CMD_SET_MODE = 0x01;          // "Activate mode"

// DEUX marqueurs de marque, a deux endroits differents, et il ne faut pas les confondre.
// Source : samr037/iohc-flipper, projet VALIDE SUR MATERIEL REEL (appairage Velux et Somfy),
// tableau « Vendor-specific protocol differences » de son CLAUDE.md.
//
//   | champ                                   | Somfy | Velux |
//   | octet fabricant d'une commande 0x00     | 0x43  | 0x61  |   <- payload[1]
//   | man_id d'une trame d'appairage 0x30     | 0x02  | 0x01  |   <- octet 25
//
// ⚠️ J'avais retire 0x43 le 2026-10-28 en lisant la charge utile 0x00 comme
// { origin, acei, main[2], ... } (struct de rspaargaren), ce qui en faisait un champ ACEI.
// C'etait FAUX : un champ de protocole ne changerait pas avec la marque, or 0x43 et 0x61
// distinguent Somfy de Velux. La lecture de samr037 est la bonne, et elle est eprouvee.
constexpr uint8_t MANUFACTURER_SOMFY = 0x43;   // octet fabricant d'une commande 0x00
constexpr uint8_t MANUFACTURER_VELUX = 0x61;

// L'origine, le fabricant et le code bouton d'une commande 0x00 ne sont PAS des constantes :
// ils sont CAPTES de la vraie telecommande, une capture 0x00 par action (voir io_pair.cpp).

// Bootloader Commands (0xE0-0xE1)
constexpr uint8_t CMD_BOOTLOADER_START = 0xE0;
constexpr uint8_t CMD_BOOTLOADER_DATA = 0xE1;

// Service Commands (0xF0-0xF3)
constexpr uint8_t CMD_SERVICE_PING = 0xF0;
constexpr uint8_t CMD_SERVICE_RESET = 0xF1;

// ============================================================================
// Device Types (Actuator Subtypes)
// ============================================================================

enum class DeviceType : uint8_t {
  ROLLER_SHUTTER = 0x00,
  ADJUSTABLE_SLAT_SHUTTER = 0x01,
  SCREEN = 0x02,
  WINDOW_OPENER = 0x03,
  VENETIAN_BLIND = 0x04,
  EXTERIOR_BLIND = 0x05,
  DUAL_SHUTTER = 0x06,
  GARAGE_DOOR = 0x07,
  AWNING = 0x08,
  CURTAIN = 0x09,
  PERGOLA = 0x0A,
  HORIZONTAL_AWNING = 0x0B,
  EXTERIOR_SCREEN = 0x0C,
  LIGHT = 0x0D,
  LOCK = 0x0E,
  HEATING = 0x0F,
  GATE = 0x10,
  BEACON = 0x11,
  SENSOR = 0x12
};

// ============================================================================
// Broadcast Addresses
// ============================================================================

// Les adresses de diffusion ne sont PAS codees ici : elles sont CAPTEES des trames de la vraie
// telecommande (dest de commande dans le 0x00, dest d'enrolement dans le 0x30). Une dest en dur
// melangeait les marques : Velux emet commande ET enrolement vers 00 00 3F, mais Somfy emet ses
// commandes vers 00 01 BF et son enrolement vers 00 00 3F. Voir io_pair.cpp.

} // namespace iohome
