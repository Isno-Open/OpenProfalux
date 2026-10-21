/**
 * @file iohome_crypto.cpp
 * @brief io-homecontrol Cryptographic Functions Implementation
 * @author iown-homecontrol project
 */

#include "iohome_crypto.h"
#include <string.h>

// Include mbedTLS for AES (available in ESP32 Arduino Core)
#ifdef ARDUINO
  #include <mbedtls/aes.h>
#else
  #include <mbedtls/aes.h>
#endif

namespace iohome {
namespace crypto {

// ============================================================================
// CRC-16/KERMIT Implementation
// ============================================================================

uint16_t compute_crc16_byte(uint8_t data, uint16_t crc) {
  crc ^= data;
  for (int i = 0; i < 8; i++) {
    uint16_t remainder = (crc & 1) ? CRC_POLYNOMIAL : 0;
    crc = (crc >> 1) ^ remainder;
  }
  return crc;
}

uint16_t compute_crc16(const uint8_t* data, size_t length, uint16_t crc) {
  for (size_t i = 0; i < length; i++) {
    crc = compute_crc16_byte(data[i], crc);
  }
  return crc;
}

bool verify_crc16(const uint8_t* frame, size_t length) {
  if (length < CRC_SIZE) {
    return false;
  }

  // Calculate CRC for all bytes except the last 2 (which are the CRC itself)
  uint16_t calculated_crc = compute_crc16(frame, length - CRC_SIZE);

  // Extract received CRC (LSB first)
  uint16_t received_crc = frame[length - 2] | (frame[length - 1] << 8);

  return calculated_crc == received_crc;
}

// ============================================================================
// Checksum Functions (for IV construction)
// ============================================================================

void compute_checksum(uint8_t frame_byte, uint8_t& chksum1, uint8_t& chksum2) {
  uint8_t tmpchksum = frame_byte ^ chksum2;
  chksum2 = ((chksum1 & 0x7F) << 1) & 0xFF;

  if ((chksum1 & 0x80) == 0) {
    if (tmpchksum >= 128) {
      chksum2 |= 1;
    }
    chksum1 = chksum2;
    chksum2 = (tmpchksum << 1) & 0xFF;
    return;
  }

  if (tmpchksum >= 128) {
    chksum2 |= 1;
  }

  chksum1 = chksum2 ^ 0x55;
  chksum2 = ((tmpchksum << 1) ^ 0x5B) & 0xFF;
}

// ============================================================================
// Initial Value (IV) Construction
// ============================================================================

void construct_iv_1w(
  const uint8_t* frame_data,
  size_t data_len,
  const uint8_t sequence_number[ROLLING_CODE_SIZE],
  uint8_t iv_out[IV_SIZE]
) {
  // Initialize IV
  memset(iv_out, 0, IV_SIZE);

  // Initialize checksums
  uint8_t chksum1 = 0;
  uint8_t chksum2 = 0;

  // Process frame data
  for (size_t i = 0; i < data_len; i++) {
    compute_checksum(frame_data[i], chksum1, chksum2);
    if (i < 8) {
      iv_out[i] = frame_data[i];
    }
  }

  // Pad bytes 0-7 with 0x55 if data is shorter than 8 bytes
  for (size_t i = data_len; i < 8; i++) {
    iv_out[i] = IV_PADDING;
  }

  // Set checksums (bytes 8-9)
  iv_out[8] = chksum1;
  iv_out[9] = chksum2;

  // Set sequence number (bytes 10-11)
  iv_out[10] = sequence_number[0];
  iv_out[11] = sequence_number[1];

  // Pad bytes 12-15 with 0x55
  for (int i = 12; i < 16; i++) {
    iv_out[i] = IV_PADDING;
  }
}

void construct_iv_2w(
  const uint8_t* frame_data,
  size_t data_len,
  const uint8_t challenge[HMAC_SIZE],
  uint8_t iv_out[IV_SIZE]
) {
  // Initialize IV
  memset(iv_out, 0, IV_SIZE);

  // Initialize checksums
  uint8_t chksum1 = 0;
  uint8_t chksum2 = 0;

  // Process frame data
  for (size_t i = 0; i < data_len; i++) {
    compute_checksum(frame_data[i], chksum1, chksum2);
    if (i < 8) {
      iv_out[i] = frame_data[i];
    }
  }

  // Pad bytes 0-7 with 0x55 if data is shorter than 8 bytes
  for (size_t i = data_len; i < 8; i++) {
    iv_out[i] = IV_PADDING;
  }

  // Set checksums (bytes 8-9)
  iv_out[8] = chksum1;
  iv_out[9] = chksum2;

  // Set challenge (bytes 10-15)
  memcpy(&iv_out[10], challenge, HMAC_SIZE);
}

// ============================================================================
// AES-128 Encryption/Decryption
// ============================================================================

bool aes128_encrypt(
  const uint8_t input[AES_BLOCK_SIZE],
  const uint8_t key[AES_KEY_SIZE],
  uint8_t output[AES_BLOCK_SIZE]
) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  int ret = mbedtls_aes_setkey_enc(&aes, key, 128);
  if (ret != 0) {
    mbedtls_aes_free(&aes);
    return false;
  }

  ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input, output);

  mbedtls_aes_free(&aes);
  return (ret == 0);
}

bool aes128_decrypt(
  const uint8_t input[AES_BLOCK_SIZE],
  const uint8_t key[AES_KEY_SIZE],
  uint8_t output[AES_BLOCK_SIZE]
) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  int ret = mbedtls_aes_setkey_dec(&aes, key, 128);
  if (ret != 0) {
    mbedtls_aes_free(&aes);
    return false;
  }

  ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input, output);

  mbedtls_aes_free(&aes);
  return (ret == 0);
}

// ============================================================================
// Key Encryption (for pairing)
// ============================================================================

bool encrypt_1w_key(
  const uint8_t system_key[AES_KEY_SIZE],
  const uint8_t node_address[NODE_ID_SIZE],
  uint8_t encrypted_out[AES_KEY_SIZE]
) {
  // Construct IV from node address (repeated pattern)
  // IV = ABCDEF ABCDEF ABCDEF ABCDEF ABCDEF AB
  uint8_t iv[IV_SIZE];
  for (int i = 0; i < 16; i++) {
    iv[i] = node_address[i % NODE_ID_SIZE];
  }

  // Encrypt IV with transfer key
  uint8_t encrypted_iv[AES_BLOCK_SIZE];
  if (!aes128_encrypt(iv, TRANSFER_KEY, encrypted_iv)) {
    return false;
  }

  // XOR system key with encrypted IV
  for (int i = 0; i < AES_KEY_SIZE; i++) {
    encrypted_out[i] = system_key[i] ^ encrypted_iv[i];
  }

  return true;
}

bool encrypt_2w_key(
  const uint8_t system_key[AES_KEY_SIZE],
  const uint8_t challenge[HMAC_SIZE],
  uint8_t encrypted_out[AES_KEY_SIZE]
) {
  // Construct IV with challenge
  uint8_t iv[IV_SIZE];
  memset(iv, IV_PADDING, IV_SIZE);
  memcpy(&iv[10], challenge, HMAC_SIZE);

  // Encrypt IV with transfer key
  uint8_t encrypted_iv[AES_BLOCK_SIZE];
  if (!aes128_encrypt(iv, TRANSFER_KEY, encrypted_iv)) {
    return false;
  }

  // XOR system key with encrypted IV
  for (int i = 0; i < AES_KEY_SIZE; i++) {
    encrypted_out[i] = system_key[i] ^ encrypted_iv[i];
  }

  return true;
}

// ============================================================================
// HMAC/MAC Generation
// ============================================================================

bool create_1w_hmac(
  const uint8_t* frame_data,
  size_t data_len,
  const uint8_t sequence_number[ROLLING_CODE_SIZE],
  const uint8_t system_key[AES_KEY_SIZE],
  uint8_t hmac_out[HMAC_SIZE]
) {
  // Construct IV
  uint8_t iv[IV_SIZE];
  construct_iv_1w(frame_data, data_len, sequence_number, iv);

  // Encrypt IV with system key
  uint8_t encrypted_iv[AES_BLOCK_SIZE];
  if (!aes128_encrypt(iv, system_key, encrypted_iv)) {
    return false;
  }

  // Truncate to 6 bytes for HMAC
  memcpy(hmac_out, encrypted_iv, HMAC_SIZE);

  return true;
}

bool create_2w_hmac(
  const uint8_t* frame_data,
  size_t data_len,
  const uint8_t challenge[HMAC_SIZE],
  const uint8_t system_key[AES_KEY_SIZE],
  uint8_t hmac_out[HMAC_SIZE]
) {
  // Construct IV
  uint8_t iv[IV_SIZE];
  construct_iv_2w(frame_data, data_len, challenge, iv);

  // Encrypt IV with system key
  uint8_t encrypted_iv[AES_BLOCK_SIZE];
  if (!aes128_encrypt(iv, system_key, encrypted_iv)) {
    return false;
  }

  // Truncate to 6 bytes for HMAC
  memcpy(hmac_out, encrypted_iv, HMAC_SIZE);

  return true;
}

bool verify_hmac(
  const uint8_t* frame_data,
  size_t data_len,
  const uint8_t received_hmac[HMAC_SIZE],
  const uint8_t* sequence_or_challenge,
  const uint8_t system_key[AES_KEY_SIZE],
  bool is_2w
) {
  uint8_t calculated_hmac[HMAC_SIZE];
  bool success;

  if (is_2w) {
    success = create_2w_hmac(frame_data, data_len, sequence_or_challenge, system_key, calculated_hmac);
  } else {
    success = create_1w_hmac(frame_data, data_len, sequence_or_challenge, system_key, calculated_hmac);
  }

  if (!success) {
    return false;
  }

  // Compare HMACs (constant-time comparison for security)
  uint8_t diff = 0;
  for (int i = 0; i < HMAC_SIZE; i++) {
    diff |= calculated_hmac[i] ^ received_hmac[i];
  }

  return (diff == 0);
}

} // namespace crypto
} // namespace iohome
