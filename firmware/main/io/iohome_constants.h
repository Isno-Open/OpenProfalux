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

// Frequency Configuration
constexpr float FREQUENCY_CHANNEL_1 = 868.25f;  // MHz (2W only)
constexpr float FREQUENCY_CHANNEL_2 = 868.95f;  // MHz (1W/2W primary)
constexpr float FREQUENCY_CHANNEL_3 = 869.85f;  // MHz (2W only)

// Modulation Parameters
constexpr float BIT_RATE = 38.4f;              // kbps
constexpr float FREQ_DEVIATION = 19.2f;        // kHz
constexpr uint16_t PREAMBLE_LENGTH = 512;      // bits

// Sync Word
constexpr uint32_t SYNC_WORD = 0xFF33;
constexpr uint8_t SYNC_WORD_LEN = 3;

// Frequency Hopping
constexpr float CHANNEL_HOP_TIME_MS = 2.7f;    // milliseconds per channel

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

// Control Byte 0 Masks
constexpr uint8_t CTRL0_ORDER_MASK = 0xC0;      // bits 7-6
constexpr uint8_t CTRL0_PROTOCOL_MASK = 0x20;   // bit 5 (1W/2W)
constexpr uint8_t CTRL0_LENGTH_MASK = 0x1F;     // bits 4-0

// Control Byte 1 Masks
constexpr uint8_t CTRL1_USE_BEACON = 0x80;      // bit 7
constexpr uint8_t CTRL1_ROUTED = 0x40;          // bit 6
constexpr uint8_t CTRL1_LOW_POWER = 0x20;       // bit 5
constexpr uint8_t CTRL1_ACK = 0x10;             // bit 4
constexpr uint8_t CTRL1_PROTOCOL_VERSION = 0x0F; // bits 3-0

// Protocol Modes
constexpr uint8_t MODE_1W = 0x00;
constexpr uint8_t MODE_2W = 0x01;

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
constexpr uint8_t CMD_KEY_TRANSFER_2W = 0x31;

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

// Marqueur fabricant, 2e octet de la charge utile (samr037 tx_runner.h).
constexpr uint8_t MANUFACTURER_SOMFY = 0x43;
constexpr uint8_t MANUFACTURER_VELUX = 0x61;

// 1er octet de la charge utile ("data"/origin), observe a 0x01 sur Somfy io.
constexpr uint8_t BUTTON_ORIGIN = 0x01;

// Code bouton (1 octet, 3e octet de la charge utile), mesure chez samr037 (tx_runner.h).
constexpr uint8_t BTN_UP   = 0x00;              // monter
constexpr uint8_t BTN_DOWN = 0xC8;              // descendre
constexpr uint8_t BTN_STOP = 0xD2;              // stop

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

constexpr uint8_t BROADCAST_ADDRESS[NODE_ID_SIZE] = {0x00, 0x00, 0x00};

} // namespace iohome
