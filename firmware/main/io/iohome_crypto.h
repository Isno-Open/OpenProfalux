/**
 * @file iohome_crypto.h
 * @brief io-homecontrol Cryptographic Functions
 * @author iown-homecontrol project
 *
 * Cryptographic functions for io-homecontrol protocol including:
 * - CRC-16/KERMIT calculation
 * - AES-128 encryption/decryption
 * - IV (Initial Value) construction
 * - HMAC generation for 1W and 2W modes
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "iohome_constants.h"

namespace iohome {
namespace crypto {

// ============================================================================
// CRC-16/KERMIT Functions
// ============================================================================

/**
 * @brief Compute CRC-16/KERMIT for a single byte
 *
 * @param data The byte to process
 * @param crc Current CRC value (default: 0)
 * @return Updated CRC value
 */
uint16_t compute_crc16_byte(uint8_t data, uint16_t crc = CRC_INITIAL);

/**
 * @brief Compute CRC-16/KERMIT for a buffer
 *
 * @param data Pointer to data buffer
 * @param length Number of bytes to process
 * @param crc Initial CRC value (default: 0)
 * @return Final CRC value
 */
uint16_t compute_crc16(const uint8_t* data, size_t length, uint16_t crc = CRC_INITIAL);

/**
 * @brief Verify CRC-16 of a frame (last 2 bytes are CRC)
 *
 * @param frame Pointer to complete frame including CRC
 * @param length Total frame length (including CRC)
 * @return true if CRC is valid, false otherwise
 */
bool verify_crc16(const uint8_t* frame, size_t length);

// ============================================================================
// Checksum Functions (for IV construction)
// ============================================================================

/**
 * @brief Compute custom checksum for IV construction
 *
 * This is a proprietary checksum algorithm used in the IV generation.
 *
 * @param frame_byte Input byte from frame data
 * @param chksum1 First checksum accumulator (in/out)
 * @param chksum2 Second checksum accumulator (in/out)
 */
void compute_checksum(uint8_t frame_byte, uint8_t& chksum1, uint8_t& chksum2);

// ============================================================================
// Initial Value (IV) Construction
// ============================================================================

/**
 * @brief Construct Initial Value for 1-Way mode encryption
 *
 * The IV is constructed from:
 * - Bytes 0-7: Frame data (or 0x55 padding)
 * - Bytes 8-9: Custom checksum
 * - Bytes 10-11: Sequence number (rolling code)
 * - Bytes 12-15: Padding (0x55)
 *
 * @param frame_data Pointer to frame data (command ID + parameters)
 * @param data_len Length of frame data
 * @param sequence_number Rolling code (2 bytes)
 * @param iv_out Output buffer for IV (16 bytes)
 */
void construct_iv_1w(
  const uint8_t* frame_data,
  size_t data_len,
  const uint8_t sequence_number[ROLLING_CODE_SIZE],
  uint8_t iv_out[IV_SIZE]
);

/**
 * @brief Construct Initial Value for 2-Way mode encryption
 *
 * The IV is constructed from:
 * - Bytes 0-7: Frame data (or 0x55 padding)
 * - Bytes 8-9: Custom checksum
 * - Bytes 10-15: Challenge (6 bytes)
 *
 * @param frame_data Pointer to frame data (command ID + parameters)
 * @param data_len Length of frame data
 * @param challenge Challenge bytes (6 bytes)
 * @param iv_out Output buffer for IV (16 bytes)
 */
void construct_iv_2w(
  const uint8_t* frame_data,
  size_t data_len,
  const uint8_t challenge[HMAC_SIZE],
  uint8_t iv_out[IV_SIZE]
);

// ============================================================================
// AES-128 Encryption/Decryption
// ============================================================================

/**
 * @brief Encrypt a 16-byte block using AES-128 ECB
 *
 * @param input Input data (16 bytes)
 * @param key AES key (16 bytes)
 * @param output Output buffer (16 bytes)
 * @return true on success, false on error
 */
bool aes128_encrypt(
  const uint8_t input[AES_BLOCK_SIZE],
  const uint8_t key[AES_KEY_SIZE],
  uint8_t output[AES_BLOCK_SIZE]
);

/**
 * @brief Decrypt a 16-byte block using AES-128 ECB
 *
 * @param input Input data (16 bytes)
 * @param key AES key (16 bytes)
 * @param output Output buffer (16 bytes)
 * @return true on success, false on error
 */
bool aes128_decrypt(
  const uint8_t input[AES_BLOCK_SIZE],
  const uint8_t key[AES_KEY_SIZE],
  uint8_t output[AES_BLOCK_SIZE]
);

// ============================================================================
// Key Encryption (for pairing)
// ============================================================================

/**
 * @brief Encrypt system key for 1-Way mode transfer
 *
 * The key is encrypted using AES-128 with an IV constructed from
 * the node address and the transfer key.
 *
 * @param system_key System key to encrypt (16 bytes)
 * @param node_address Node address (3 bytes)
 * @param encrypted_out Output buffer (16 bytes)
 * @return true on success, false on error
 */
bool encrypt_1w_key(
  const uint8_t system_key[AES_KEY_SIZE],
  const uint8_t node_address[NODE_ID_SIZE],
  uint8_t encrypted_out[AES_KEY_SIZE]
);

/**
 * @brief Encrypt system key for 2-Way mode transfer
 *
 * @param system_key System key to encrypt (16 bytes)
 * @param challenge Challenge bytes (6 bytes)
 * @param encrypted_out Output buffer (16 bytes)
 * @return true on success, false on error
 */
bool encrypt_2w_key(
  const uint8_t system_key[AES_KEY_SIZE],
  const uint8_t challenge[HMAC_SIZE],
  uint8_t encrypted_out[AES_KEY_SIZE]
);

// ============================================================================
// HMAC/MAC Generation
// ============================================================================

/**
 * @brief Generate HMAC for 1-Way mode
 *
 * The HMAC is a 6-byte truncated AES output used for authentication.
 *
 * @param frame_data Complete frame data (command ID + parameters)
 * @param data_len Length of frame data
 * @param sequence_number Rolling code (2 bytes)
 * @param system_key System key (16 bytes)
 * @param hmac_out Output buffer (6 bytes)
 * @return true on success, false on error
 */
bool create_1w_hmac(
  const uint8_t* frame_data,
  size_t data_len,
  const uint8_t sequence_number[ROLLING_CODE_SIZE],
  const uint8_t system_key[AES_KEY_SIZE],
  uint8_t hmac_out[HMAC_SIZE]
);

/**
 * @brief Generate HMAC for 2-Way mode
 *
 * @param frame_data Complete frame data (command ID + parameters)
 * @param data_len Length of frame data
 * @param challenge Challenge bytes (6 bytes)
 * @param system_key System key (16 bytes)
 * @param hmac_out Output buffer (6 bytes)
 * @return true on success, false on error
 */
bool create_2w_hmac(
  const uint8_t* frame_data,
  size_t data_len,
  const uint8_t challenge[HMAC_SIZE],
  const uint8_t system_key[AES_KEY_SIZE],
  uint8_t hmac_out[HMAC_SIZE]
);

/**
 * @brief Verify HMAC of a received frame
 *
 * @param frame_data Complete frame data
 * @param data_len Length of frame data
 * @param received_hmac HMAC from frame (6 bytes)
 * @param sequence_or_challenge Sequence number (1W) or challenge (2W)
 * @param system_key System key (16 bytes)
 * @param is_2w true for 2W mode, false for 1W mode
 * @return true if HMAC is valid, false otherwise
 */
bool verify_hmac(
  const uint8_t* frame_data,
  size_t data_len,
  const uint8_t received_hmac[HMAC_SIZE],
  const uint8_t* sequence_or_challenge,
  const uint8_t system_key[AES_KEY_SIZE],
  bool is_2w
);

} // namespace crypto
} // namespace iohome
