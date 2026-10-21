/**
 * @file iohome_frame.cpp
 * @brief io-homecontrol Frame Construction and Parsing Implementation
 * @author iown-homecontrol project
 */

#include "iohome_frame.h"
#include "iohome_crypto.h"
#include <string.h>
#include <stdio.h>

namespace iohome {
namespace frame {

// ============================================================================
// Frame Construction
// ============================================================================

void init_frame(IoFrame* frame, bool is_1w) {
  memset(frame, 0, sizeof(IoFrame));
  frame->is_1w_mode = is_1w;

  // Set default control bytes
  frame->ctrl_byte_0 = 0x00; // Will be updated when data is set
  frame->ctrl_byte_1 = 0x00; // Default: no beacon, not routed, normal power

  if (!is_1w) {
    frame->ctrl_byte_0 |= CTRL0_PROTOCOL_MASK; // Set 2W bit
  }
}

void set_destination(IoFrame* frame, const uint8_t node_id[NODE_ID_SIZE]) {
  memcpy(frame->dest_node, node_id, NODE_ID_SIZE);
}

void set_source(IoFrame* frame, const uint8_t node_id[NODE_ID_SIZE]) {
  memcpy(frame->src_node, node_id, NODE_ID_SIZE);
}

bool set_command(IoFrame* frame, uint8_t cmd_id, const uint8_t* params, size_t params_len) {
  if (params_len > FRAME_MAX_DATA_SIZE) {
    return false; // Parameters too large
  }

  frame->command_id = cmd_id;
  frame->data_len = params_len;

  if (params != nullptr && params_len > 0) {
    memcpy(frame->data, params, params_len);
  }

  // Update control byte 0 with frame length
  // Frame length field = (total_length - 11)
  // total_length = 9 (header) + 1 (cmd) + data_len + 2 (rolling code for 1W) + 6 (hmac) + 2 (crc)
  uint8_t total_length;
  if (frame->is_1w_mode) {
    total_length = 9 + 1 + frame->data_len + ROLLING_CODE_SIZE + HMAC_SIZE + CRC_SIZE;
  } else {
    total_length = 9 + 1 + frame->data_len + HMAC_SIZE + CRC_SIZE;
  }

  frame->frame_length = total_length;

  // Update ctrl_byte_0 length field
  frame->ctrl_byte_0 = (frame->ctrl_byte_0 & ~CTRL0_LENGTH_MASK) | ((total_length - FRAME_MIN_SIZE) & CTRL0_LENGTH_MASK);

  return true;
}

void set_rolling_code(IoFrame* frame, uint16_t code) {
  frame->rolling_code[0] = code & 0xFF;         // LSB first
  frame->rolling_code[1] = (code >> 8) & 0xFF;
}

bool finalize_frame(IoFrame* frame, const uint8_t system_key[AES_KEY_SIZE], const uint8_t* challenge) {
  // Prepare frame data for HMAC (command ID + parameters)
  uint8_t frame_data[1 + FRAME_MAX_DATA_SIZE];
  frame_data[0] = frame->command_id;
  memcpy(&frame_data[1], frame->data, frame->data_len);
  size_t frame_data_len = 1 + frame->data_len;

  // Calculate HMAC
  bool hmac_success;
  if (frame->is_1w_mode) {
    hmac_success = crypto::create_1w_hmac(
      frame_data,
      frame_data_len,
      frame->rolling_code,
      system_key,
      frame->hmac
    );
  } else {
    if (challenge == nullptr) {
      return false; // 2W mode requires challenge
    }
    hmac_success = crypto::create_2w_hmac(
      frame_data,
      frame_data_len,
      challenge,
      system_key,
      frame->hmac
    );
  }

  if (!hmac_success) {
    return false;
  }

  // Serialize frame to temporary buffer for CRC calculation
  uint8_t temp_buffer[FRAME_MAX_SIZE];
  size_t crc_len = serialize_frame(frame, temp_buffer, sizeof(temp_buffer));
  if (crc_len == 0) {
    return false;
  }

  // Calculate CRC (exclude CRC bytes themselves)
  uint16_t crc_value = crypto::compute_crc16(temp_buffer, crc_len - CRC_SIZE);

  // Store CRC (LSB first)
  frame->crc[0] = crc_value & 0xFF;
  frame->crc[1] = (crc_value >> 8) & 0xFF;

  return true;
}

size_t serialize_frame(const IoFrame* frame, uint8_t* buffer, size_t buffer_size) {
  if (buffer_size < frame->frame_length) {
    return 0; // Buffer too small
  }

  size_t offset = 0;

  // Control Bytes
  buffer[offset++] = frame->ctrl_byte_0;
  buffer[offset++] = frame->ctrl_byte_1;

  // Destination Node
  memcpy(&buffer[offset], frame->dest_node, NODE_ID_SIZE);
  offset += NODE_ID_SIZE;

  // Source Node
  memcpy(&buffer[offset], frame->src_node, NODE_ID_SIZE);
  offset += NODE_ID_SIZE;

  // Command ID
  buffer[offset++] = frame->command_id;

  // Parameters
  memcpy(&buffer[offset], frame->data, frame->data_len);
  offset += frame->data_len;

  // Rolling Code (1W mode only)
  if (frame->is_1w_mode) {
    memcpy(&buffer[offset], frame->rolling_code, ROLLING_CODE_SIZE);
    offset += ROLLING_CODE_SIZE;
  }

  // HMAC
  memcpy(&buffer[offset], frame->hmac, HMAC_SIZE);
  offset += HMAC_SIZE;

  // CRC
  memcpy(&buffer[offset], frame->crc, CRC_SIZE);
  offset += CRC_SIZE;

  return offset;
}

// ============================================================================
// Frame Parsing
// ============================================================================

bool parse_frame(const uint8_t* buffer, size_t buffer_len, IoFrame* frame) {
  if (buffer_len < FRAME_MIN_SIZE) {
    return false; // Frame too short
  }

  memset(frame, 0, sizeof(IoFrame));

  size_t offset = 0;

  // Parse Control Bytes
  frame->ctrl_byte_0 = buffer[offset++];
  frame->ctrl_byte_1 = buffer[offset++];

  // Determine mode
  frame->is_1w_mode = !is_2w_mode(frame->ctrl_byte_0);

  // Get frame length
  frame->frame_length = get_frame_length(frame->ctrl_byte_0);

  if (buffer_len < frame->frame_length) {
    return false; // Buffer doesn't contain complete frame
  }

  // Parse Destination Node
  memcpy(frame->dest_node, &buffer[offset], NODE_ID_SIZE);
  offset += NODE_ID_SIZE;

  // Parse Source Node
  memcpy(frame->src_node, &buffer[offset], NODE_ID_SIZE);
  offset += NODE_ID_SIZE;

  // Parse Command ID
  frame->command_id = buffer[offset++];

  // Calculate data length
  if (frame->is_1w_mode) {
    frame->data_len = frame->frame_length - (9 + 1 + ROLLING_CODE_SIZE + HMAC_SIZE + CRC_SIZE);
  } else {
    frame->data_len = frame->frame_length - (9 + 1 + HMAC_SIZE + CRC_SIZE);
  }

  if (frame->data_len > FRAME_MAX_DATA_SIZE) {
    return false; // Invalid data length
  }

  // Parse Parameters
  memcpy(frame->data, &buffer[offset], frame->data_len);
  offset += frame->data_len;

  // Parse Rolling Code (1W mode only)
  if (frame->is_1w_mode) {
    memcpy(frame->rolling_code, &buffer[offset], ROLLING_CODE_SIZE);
    offset += ROLLING_CODE_SIZE;
  }

  // Parse HMAC
  memcpy(frame->hmac, &buffer[offset], HMAC_SIZE);
  offset += HMAC_SIZE;

  // Parse CRC
  memcpy(frame->crc, &buffer[offset], CRC_SIZE);
  offset += CRC_SIZE;

  return true;
}

bool validate_frame(const IoFrame* frame, const uint8_t* system_key, const uint8_t* challenge) {
  // Serialize frame for CRC check
  uint8_t temp_buffer[FRAME_MAX_SIZE];
  size_t frame_len = serialize_frame(frame, temp_buffer, sizeof(temp_buffer));
  if (frame_len == 0) {
    return false;
  }

  // Verify CRC
  if (!crypto::verify_crc16(temp_buffer, frame_len)) {
    return false;
  }

  // Optionally verify HMAC
  if (system_key != nullptr) {
    uint8_t frame_data[1 + FRAME_MAX_DATA_SIZE];
    frame_data[0] = frame->command_id;
    memcpy(&frame_data[1], frame->data, frame->data_len);
    size_t frame_data_len = 1 + frame->data_len;

    const uint8_t* seq_or_challenge = frame->is_1w_mode ? frame->rolling_code : challenge;

    if (!crypto::verify_hmac(
        frame_data,
        frame_data_len,
        frame->hmac,
        seq_or_challenge,
        system_key,
        !frame->is_1w_mode
      )) {
      return false;
    }
  }

  return true;
}

// ============================================================================
// Helper Functions
// ============================================================================

bool is_broadcast(const uint8_t node_id[NODE_ID_SIZE]) {
  return (node_id[0] == 0x00 && node_id[1] == 0x00 && node_id[2] == 0x00);
}

void print_frame(const IoFrame* frame, void (*print_func)(const char*)) {
  char buf[128];

  snprintf(buf, sizeof(buf), "Frame: %s Mode", frame->is_1w_mode ? "1W" : "2W");
  print_func(buf);

  snprintf(buf, sizeof(buf), "  Length: %d bytes", frame->frame_length);
  print_func(buf);

  snprintf(buf, sizeof(buf), "  Dest: %02X %02X %02X",
           frame->dest_node[0], frame->dest_node[1], frame->dest_node[2]);
  print_func(buf);

  snprintf(buf, sizeof(buf), "  Src:  %02X %02X %02X",
           frame->src_node[0], frame->src_node[1], frame->src_node[2]);
  print_func(buf);

  snprintf(buf, sizeof(buf), "  Cmd:  0x%02X", frame->command_id);
  print_func(buf);

  if (frame->data_len > 0) {
    char hex[64] = {0};
    for (size_t i = 0; i < frame->data_len && i < 20; i++) {
      snprintf(hex + strlen(hex), sizeof(hex) - strlen(hex), "%02X ", frame->data[i]);
    }
    snprintf(buf, sizeof(buf), "  Data: %s", hex);
    print_func(buf);
  }

  if (frame->is_1w_mode) {
    snprintf(buf, sizeof(buf), "  Rolling Code: %02X %02X",
             frame->rolling_code[0], frame->rolling_code[1]);
    print_func(buf);
  }

  char hmac_str[24] = {0};
  for (int i = 0; i < HMAC_SIZE; i++) {
    snprintf(hmac_str + strlen(hmac_str), sizeof(hmac_str) - strlen(hmac_str),
             "%02X ", frame->hmac[i]);
  }
  snprintf(buf, sizeof(buf), "  HMAC: %s", hmac_str);
  print_func(buf);

  snprintf(buf, sizeof(buf), "  CRC:  %02X %02X", frame->crc[0], frame->crc[1]);
  print_func(buf);
}

} // namespace frame
} // namespace iohome
