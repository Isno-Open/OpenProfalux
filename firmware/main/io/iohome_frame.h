/**
 * @file iohome_frame.h
 * @brief io-homecontrol Frame Construction and Parsing
 * @author iown-homecontrol project
 *
 * Functions for constructing and parsing io-homecontrol protocol frames.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "iohome_constants.h"

namespace iohome {
namespace frame {

// ============================================================================
// Frame Structure
// ============================================================================

/**
 * @brief io-homecontrol Frame structure
 */
struct IoFrame {
  // Control Bytes
  uint8_t ctrl_byte_0;              // Order, Protocol Mode, Frame Length
  uint8_t ctrl_byte_1;              // Beacon, Routed, Low Power, ACK, Version

  // Addresses
  uint8_t dest_node[NODE_ID_SIZE];  // Destination Node ID (3 bytes)
  uint8_t src_node[NODE_ID_SIZE];   // Source Node ID (3 bytes)

  // Command and Data
  uint8_t command_id;               // Command ID (1 byte)
  uint8_t data[FRAME_MAX_DATA_SIZE]; // Parameters (0-21 bytes)
  uint8_t data_len;                 // Actual length of data

  // 1W Mode only
  uint8_t rolling_code[ROLLING_CODE_SIZE]; // Sequence number (2 bytes)

  // Authentication
  uint8_t hmac[HMAC_SIZE];          // HMAC/MAC (6 bytes)

  // CRC
  uint8_t crc[CRC_SIZE];            // CRC-16 (2 bytes, LSB first)

  // Metadata
  bool is_1w_mode;                  // true = 1W, false = 2W
  uint8_t frame_length;             // Total frame length
};

// ============================================================================
// Frame Construction
// ============================================================================

/**
 * @brief Initialize a frame with default values
 *
 * @param frame Pointer to IoFrame structure
 * @param is_1w true for 1W mode, false for 2W mode
 */
void init_frame(IoFrame* frame, bool is_1w = true);

/**
 * @brief Set destination node address
 *
 * @param frame Pointer to IoFrame structure
 * @param node_id Node ID (3 bytes)
 */
void set_destination(IoFrame* frame, const uint8_t node_id[NODE_ID_SIZE]);

/**
 * @brief Set source node address
 *
 * @param frame Pointer to IoFrame structure
 * @param node_id Node ID (3 bytes)
 */
void set_source(IoFrame* frame, const uint8_t node_id[NODE_ID_SIZE]);

/**
 * @brief Set command ID and parameters
 *
 * @param frame Pointer to IoFrame structure
 * @param cmd_id Command ID
 * @param params Pointer to parameter data (can be nullptr)
 * @param params_len Length of parameters
 * @return true on success, false if params_len too large
 */
bool set_command(IoFrame* frame, uint8_t cmd_id, const uint8_t* params = nullptr, size_t params_len = 0);

/**
 * @brief Set rolling code (1W mode only)
 *
 * @param frame Pointer to IoFrame structure
 * @param code Rolling code value (0-65535)
 */
void set_rolling_code(IoFrame* frame, uint16_t code);

/**
 * @brief Finalize frame by calculating HMAC and CRC
 *
 * This must be called after all frame fields are set.
 *
 * @param frame Pointer to IoFrame structure
 * @param system_key System key for HMAC calculation (16 bytes)
 * @param challenge Challenge for 2W mode (nullptr for 1W mode)
 * @return true on success, false on error
 */
bool finalize_frame(IoFrame* frame, const uint8_t system_key[AES_KEY_SIZE], const uint8_t* challenge = nullptr);

/**
 * @brief Serialize frame to byte buffer
 *
 * @param frame Pointer to IoFrame structure
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of bytes written, or 0 on error
 */
size_t serialize_frame(const IoFrame* frame, uint8_t* buffer, size_t buffer_size);

// ============================================================================
// Frame Parsing
// ============================================================================

/**
 * @brief Parse a received frame from byte buffer
 *
 * @param buffer Input buffer
 * @param buffer_len Length of input buffer
 * @param frame Output IoFrame structure
 * @return true on success, false on parse error
 */
bool parse_frame(const uint8_t* buffer, size_t buffer_len, IoFrame* frame);

/**
 * @brief Validate frame (CRC and optionally HMAC)
 *
 * @param frame Pointer to IoFrame structure
 * @param system_key System key for HMAC verification (nullptr to skip HMAC check)
 * @param challenge Challenge for 2W mode (nullptr for 1W mode)
 * @return true if frame is valid, false otherwise
 */
bool validate_frame(const IoFrame* frame, const uint8_t* system_key = nullptr, const uint8_t* challenge = nullptr);

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Get protocol mode from control byte
 *
 * @param ctrl_byte_0 Control byte 0
 * @return true if 2W mode, false if 1W mode
 */
inline bool is_2w_mode(uint8_t ctrl_byte_0) {
  return (ctrl_byte_0 & CTRL0_PROTOCOL_MASK) != 0;
}

/**
 * @brief Get frame length from control byte
 *
 * @param ctrl_byte_0 Control byte 0
 * @return Frame length (11-32 bytes)
 */
inline uint8_t get_frame_length(uint8_t ctrl_byte_0) {
  return (ctrl_byte_0 & CTRL0_LENGTH_MASK) + FRAME_MIN_SIZE;
}

/**
 * @brief Check if address is broadcast
 *
 * @param node_id Node ID (3 bytes)
 * @return true if broadcast address
 */
bool is_broadcast(const uint8_t node_id[NODE_ID_SIZE]);

/**
 * @brief Print frame in human-readable format (for debugging)
 *
 * @param frame Pointer to IoFrame structure
 * @param print_func Function pointer for printing (e.g., Serial.println)
 */
void print_frame(const IoFrame* frame, void (*print_func)(const char*));

} // namespace frame
} // namespace iohome
