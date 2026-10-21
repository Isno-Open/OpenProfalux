/**
 * @file iohome_2w.cpp
 * @brief io-homecontrol 2-Way Mode Features Implementation
 * @author iown-homecontrol project
 */

#include "iohome_2w.h"
#include "iohome_crypto.h"
#include <string.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #define GET_TIME_MS() millis()
  #define GET_TIME_US() micros()
  #define RANDOM_BYTE() random(256)
#else
  #include <time.h>
  #include <stdlib.h>
  #define GET_TIME_MS() (clock() * 1000 / CLOCKS_PER_SEC)
  #define GET_TIME_US() (clock() * 1000000 / CLOCKS_PER_SEC)
  #define RANDOM_BYTE() (rand() % 256)
#endif

namespace iohome {
namespace mode2w {

// ============================================================================
// Frequency Hopping Implementation
// ============================================================================

ChannelHopper::ChannelHopper()
  : current_channel_(ChannelState::CHANNEL_2),
    last_hop_time_ms_(0),
    hop_interval_us_(2700),  // 2.7ms in microseconds
    enabled_(false)
{
}

void ChannelHopper::begin(float hop_interval_ms) {
  hop_interval_us_ = (unsigned long)(hop_interval_ms * 1000.0f);
  last_hop_time_ms_ = GET_TIME_MS();
  current_channel_ = ChannelState::CHANNEL_2;  // Start with primary channel
  enabled_ = false;
}

bool ChannelHopper::update(unsigned long current_time_ms) {
  if (!enabled_) {
    return false;
  }

  // Convert to microseconds for precision
  unsigned long current_time_us = current_time_ms * 1000UL;
  unsigned long last_hop_time_us = last_hop_time_ms_ * 1000UL;
  unsigned long elapsed_us = current_time_us - last_hop_time_us;

  if (elapsed_us >= hop_interval_us_) {
    next_channel();
    last_hop_time_ms_ = current_time_ms;
    return true;
  }

  return false;
}

float ChannelHopper::get_current_frequency() const {
  switch (current_channel_) {
    case ChannelState::CHANNEL_1:
      return FREQUENCY_CHANNEL_1;
    case ChannelState::CHANNEL_2:
      return FREQUENCY_CHANNEL_2;
    case ChannelState::CHANNEL_3:
      return FREQUENCY_CHANNEL_3;
    default:
      return FREQUENCY_CHANNEL_2;
  }
}

void ChannelHopper::reset() {
  current_channel_ = ChannelState::CHANNEL_2;
  last_hop_time_ms_ = GET_TIME_MS();
}

unsigned long ChannelHopper::time_until_next_hop_us(unsigned long current_time_ms) const {
  unsigned long current_time_us = current_time_ms * 1000UL;
  unsigned long last_hop_time_us = last_hop_time_ms_ * 1000UL;
  unsigned long elapsed_us = current_time_us - last_hop_time_us;

  if (elapsed_us >= hop_interval_us_) {
    return 0;
  }

  return hop_interval_us_ - elapsed_us;
}

void ChannelHopper::next_channel() {
  switch (current_channel_) {
    case ChannelState::CHANNEL_1:
      current_channel_ = ChannelState::CHANNEL_2;
      break;
    case ChannelState::CHANNEL_2:
      current_channel_ = ChannelState::CHANNEL_3;
      break;
    case ChannelState::CHANNEL_3:
      current_channel_ = ChannelState::CHANNEL_1;
      break;
  }
}

// ============================================================================
// Challenge-Response Authentication Implementation
// ============================================================================

AuthenticationManager::AuthenticationManager()
  : state_(ChallengeState::IDLE),
    challenge_timestamp_(0),
    challenge_timeout_ms_(5000)
{
  memset(system_key_, 0, AES_KEY_SIZE);
  memset(current_challenge_, 0, HMAC_SIZE);
}

void AuthenticationManager::begin(const uint8_t system_key[AES_KEY_SIZE]) {
  memcpy(system_key_, system_key, AES_KEY_SIZE);
  state_ = ChallengeState::IDLE;
}

void AuthenticationManager::generate_challenge(uint8_t challenge_out[HMAC_SIZE]) {
  // Generate random 6-byte challenge
  for (int i = 0; i < HMAC_SIZE; i++) {
    challenge_out[i] = RANDOM_BYTE();
  }

  // Store current challenge
  memcpy(current_challenge_, challenge_out, HMAC_SIZE);
  challenge_timestamp_ = GET_TIME_MS();
  state_ = ChallengeState::CHALLENGE_SENT;
}

bool AuthenticationManager::create_challenge_request(
  frame::IoFrame* frame,
  const uint8_t dest_node[NODE_ID_SIZE],
  const uint8_t src_node[NODE_ID_SIZE]
) {
  // Initialize frame for 2W mode
  frame::init_frame(frame, false);  // false = 2W mode
  frame::set_destination(frame, dest_node);
  frame::set_source(frame, src_node);

  // Generate new challenge
  uint8_t challenge[HMAC_SIZE];
  generate_challenge(challenge);

  // Set command 0x3C with challenge as parameters
  if (!frame::set_command(frame, CMD_CHALLENGE_REQUEST, challenge, HMAC_SIZE)) {
    return false;
  }

  // Finalize frame (HMAC and CRC)
  return frame::finalize_frame(frame, system_key_, challenge);
}

bool AuthenticationManager::create_challenge_response(
  frame::IoFrame* frame,
  const uint8_t dest_node[NODE_ID_SIZE],
  const uint8_t src_node[NODE_ID_SIZE],
  const uint8_t received_challenge[HMAC_SIZE]
) {
  // Initialize frame for 2W mode
  frame::init_frame(frame, false);  // false = 2W mode
  frame::set_destination(frame, dest_node);
  frame::set_source(frame, src_node);

  // Set command 0x3D with challenge as parameters
  if (!frame::set_command(frame, CMD_CHALLENGE_RESPONSE, received_challenge, HMAC_SIZE)) {
    return false;
  }

  // Finalize frame with challenge
  return frame::finalize_frame(frame, system_key_, received_challenge);
}

bool AuthenticationManager::verify_challenge_response(const frame::IoFrame* frame) {
  if (state_ != ChallengeState::CHALLENGE_SENT) {
    return false;  // No challenge was sent
  }

  // Check timeout
  unsigned long current_time = GET_TIME_MS();
  if (current_time - challenge_timestamp_ > challenge_timeout_ms_) {
    state_ = ChallengeState::IDLE;
    return false;  // Timeout
  }

  // Verify command ID
  if (frame->command_id != CMD_CHALLENGE_RESPONSE) {
    return false;
  }

  // Verify HMAC with current challenge
  if (!frame::validate_frame(frame, system_key_, current_challenge_)) {
    return false;
  }

  state_ = ChallengeState::AUTHENTICATED;
  return true;
}

void AuthenticationManager::reset() {
  state_ = ChallengeState::IDLE;
  memset(current_challenge_, 0, HMAC_SIZE);
  challenge_timestamp_ = 0;
}

// ============================================================================
// Beacon Handling Implementation
// ============================================================================

BeaconHandler::BeaconHandler()
  : beacon_received_(false)
{
  memset(&last_beacon_, 0, sizeof(BeaconInfo));
}

void BeaconHandler::begin() {
  beacon_received_ = false;
}

bool BeaconHandler::process_beacon(const frame::IoFrame* frame, int16_t rssi, float snr) {
  // Check if frame has beacon flag set
  if (!(frame->ctrl_byte_1 & CTRL1_USE_BEACON)) {
    return false;  // Not a beacon frame
  }

  // Store beacon information
  memcpy(last_beacon_.node_id, frame->src_node, NODE_ID_SIZE);

  // Determine beacon type from command or data
  if (frame->data_len > 0) {
    last_beacon_.type = static_cast<BeaconType>(frame->data[0]);
  } else {
    last_beacon_.type = BeaconType::SYNC_BEACON;
  }

  // Copy beacon data
  last_beacon_.data_len = frame->data_len;
  if (frame->data_len > 0) {
    size_t copy_len = frame->data_len < FRAME_MAX_DATA_SIZE ? frame->data_len : FRAME_MAX_DATA_SIZE;
    memcpy(last_beacon_.data, frame->data, copy_len);
  }

  last_beacon_.rssi = rssi;
  last_beacon_.snr = snr;
  last_beacon_.timestamp_ms = GET_TIME_MS();
  beacon_received_ = true;

  return true;
}

bool BeaconHandler::get_last_beacon(BeaconInfo* info) {
  if (!beacon_received_) {
    return false;
  }

  memcpy(info, &last_beacon_, sizeof(BeaconInfo));
  return true;
}

bool BeaconHandler::has_recent_beacon(unsigned long timeout_ms) {
  if (!beacon_received_) {
    return false;
  }

  unsigned long current_time = GET_TIME_MS();
  return (current_time - last_beacon_.timestamp_ms) <= timeout_ms;
}

unsigned long BeaconHandler::time_since_last_beacon(unsigned long current_time_ms) {
  if (!beacon_received_) {
    return 0xFFFFFFFF;  // Max value if no beacon received
  }

  return current_time_ms - last_beacon_.timestamp_ms;
}

// ============================================================================
// Discovery and Pairing Implementation
// ============================================================================

DiscoveryManager::DiscoveryManager()
  : state_(DiscoveryState::IDLE),
    discovery_start_time_(0),
    discovery_timeout_(0),
    discovery_device_type_(0xFF),
    discovered_count_(0)
{
  memset(own_node_id_, 0, NODE_ID_SIZE);
  memset(discovered_devices_, 0, sizeof(discovered_devices_));
}

void DiscoveryManager::begin(const uint8_t own_node_id[NODE_ID_SIZE]) {
  memcpy(own_node_id_, own_node_id, NODE_ID_SIZE);
  state_ = DiscoveryState::IDLE;
  discovered_count_ = 0;
}

void DiscoveryManager::start_discovery(uint8_t device_type, unsigned long timeout_ms) {
  state_ = DiscoveryState::DISCOVERING;
  discovery_start_time_ = GET_TIME_MS();
  discovery_timeout_ = timeout_ms;
  discovery_device_type_ = device_type;
  discovered_count_ = 0;
}

void DiscoveryManager::stop_discovery() {
  state_ = DiscoveryState::IDLE;
}

bool DiscoveryManager::create_discovery_request(frame::IoFrame* frame, uint8_t device_type) {
  // Initialize frame for broadcast
  frame::init_frame(frame, true);  // Use 1W for discovery

  // Set broadcast destination
  frame::set_destination(frame, BROADCAST_ADDRESS);
  frame::set_source(frame, own_node_id_);

  // Determine command based on device type
  uint8_t cmd_id;
  switch (device_type) {
    case 0x00: // Actuator
      cmd_id = CMD_DISCOVER_ACTUATOR;
      break;
    case 0x12: // Sensor
      cmd_id = CMD_DISCOVER_SENSOR;
      break;
    case 0x11: // Beacon
      cmd_id = CMD_DISCOVER_BEACON;
      break;
    default:
      cmd_id = CMD_DISCOVER_ACTUATOR;
      break;
  }

  // Set command with device type as parameter
  uint8_t params[1] = {device_type};
  return frame::set_command(frame, cmd_id, params, 1);
}

bool DiscoveryManager::process_discovery_response(const frame::IoFrame* frame, int16_t rssi) {
  if (state_ != DiscoveryState::DISCOVERING) {
    return false;
  }

  // Check if we have space for more devices
  if (discovered_count_ >= MAX_DISCOVERED_DEVICES) {
    return false;
  }

  // Check if device was already discovered (avoid duplicates)
  for (size_t i = 0; i < discovered_count_; i++) {
    if (memcmp(discovered_devices_[i].node_id, frame->src_node, NODE_ID_SIZE) == 0) {
      return false;  // Already in list
    }
  }

  // Add to discovered devices
  DiscoveredDevice* device = &discovered_devices_[discovered_count_];
  memcpy(device->node_id, frame->src_node, NODE_ID_SIZE);

  // Extract device info from frame data
  if (frame->data_len >= 2) {
    device->device_type = static_cast<DeviceType>(frame->data[0]);
    device->manufacturer = frame->data[1];
  } else {
    device->device_type = DeviceType::ROLLER_SHUTTER;
    device->manufacturer = 0;
  }

  if (frame->data_len >= 3) {
    device->protocol_version = frame->data[2];
  } else {
    device->protocol_version = 0;
  }

  device->rssi = rssi;
  device->timestamp_ms = GET_TIME_MS();

  discovered_count_++;
  state_ = DiscoveryState::FOUND;

  return true;
}

bool DiscoveryManager::get_discovered_device(size_t index, DiscoveredDevice* device) {
  if (index >= discovered_count_) {
    return false;
  }

  memcpy(device, &discovered_devices_[index], sizeof(DiscoveredDevice));
  return true;
}

bool DiscoveryManager::create_key_transfer_1w(
  frame::IoFrame* frame,
  const uint8_t dest_node[NODE_ID_SIZE],
  const uint8_t src_node[NODE_ID_SIZE],
  const uint8_t system_key[AES_KEY_SIZE]
) {
  // Initialize frame for 1W mode
  frame::init_frame(frame, true);
  frame::set_destination(frame, dest_node);
  frame::set_source(frame, src_node);

  // Encrypt system key with transfer key
  uint8_t encrypted_key[AES_KEY_SIZE];
  if (!crypto::encrypt_1w_key(system_key, dest_node, encrypted_key)) {
    return false;
  }

  // Set command 0x30 with encrypted key
  return frame::set_command(frame, CMD_KEY_TRANSFER_1W, encrypted_key, AES_KEY_SIZE);
}

bool DiscoveryManager::create_key_transfer_2w(
  frame::IoFrame* frame,
  const uint8_t dest_node[NODE_ID_SIZE],
  const uint8_t src_node[NODE_ID_SIZE],
  const uint8_t system_key[AES_KEY_SIZE],
  const uint8_t challenge[HMAC_SIZE]
) {
  // Initialize frame for 2W mode
  frame::init_frame(frame, false);
  frame::set_destination(frame, dest_node);
  frame::set_source(frame, src_node);

  // Encrypt system key with transfer key and challenge
  uint8_t encrypted_key[AES_KEY_SIZE];
  if (!crypto::encrypt_2w_key(system_key, challenge, encrypted_key)) {
    return false;
  }

  // Set command 0x31 with encrypted key
  return frame::set_command(frame, CMD_KEY_TRANSFER_2W, encrypted_key, AES_KEY_SIZE);
}

} // namespace mode2w
} // namespace iohome
