/**
 * @file iohome_2w.h
 * @brief io-homecontrol 2-Way Mode Features
 * @author iown-homecontrol project
 *
 * Advanced features for 2-Way mode including:
 * - Frequency hopping (FHSS)
 * - Challenge-response authentication
 * - Beacon handling
 * - Discovery and pairing
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "iohome_constants.h"
#include "iohome_frame.h"

namespace iohome {
namespace mode2w {

// ============================================================================
// Frequency Hopping (FHSS)
// ============================================================================

/**
 * @brief Channel hopping state
 */
enum class ChannelState : uint8_t {
  CHANNEL_1 = 0,  // 868.25 MHz
  CHANNEL_2 = 1,  // 868.95 MHz (primary)
  CHANNEL_3 = 2   // 869.85 MHz
};

/**
 * @brief Frequency Hopping State Machine
 *
 * Manages channel switching for 2W mode with precise timing (2.7ms per channel).
 */
class ChannelHopper {
public:
  ChannelHopper();

  /**
   * @brief Initialize channel hopper
   *
   * @param hop_interval_ms Channel hop interval in milliseconds (default: 2.7)
   */
  void begin(float hop_interval_ms = CHANNEL_HOP_TIME_MS);

  /**
   * @brief Update state machine (call this frequently in loop)
   *
   * @param current_time_ms Current time in milliseconds
   * @return true if channel changed, false otherwise
   */
  bool update(unsigned long current_time_ms);

  /**
   * @brief Get current channel state
   *
   * @return Current ChannelState
   */
  ChannelState get_current_channel() const { return current_channel_; }

  /**
   * @brief Get current frequency in MHz
   *
   * @return Frequency in MHz
   */
  float get_current_frequency() const;

  /**
   * @brief Reset to initial channel
   */
  void reset();

  /**
   * @brief Enable/disable hopping
   *
   * @param enabled true to enable, false to disable
   */
  void set_enabled(bool enabled) { enabled_ = enabled; }

  /**
   * @brief Check if hopping is enabled
   *
   * @return true if enabled
   */
  bool is_enabled() const { return enabled_; }

  /**
   * @brief Get time until next hop in microseconds
   *
   * @param current_time_ms Current time in milliseconds
   * @return Time until next hop in microseconds
   */
  unsigned long time_until_next_hop_us(unsigned long current_time_ms) const;

protected:
  ChannelState current_channel_;
  unsigned long last_hop_time_ms_;
  unsigned long hop_interval_us_;  // in microseconds for precision
  bool enabled_;

  /**
   * @brief Switch to next channel
   */
  void next_channel();
};

// ============================================================================
// Challenge-Response Authentication
// ============================================================================

/**
 * @brief Challenge-Response state
 */
enum class ChallengeState : uint8_t {
  IDLE,             // No authentication in progress
  CHALLENGE_SENT,   // Waiting for challenge response
  AUTHENTICATED     // Successfully authenticated
};

/**
 * @brief Challenge-Response Authentication Manager
 *
 * Handles 2W authentication using challenge-response mechanism.
 */
class AuthenticationManager {
public:
  AuthenticationManager();

  /**
   * @brief Initialize authentication manager
   *
   * @param system_key System key for HMAC calculation (16 bytes)
   */
  void begin(const uint8_t system_key[AES_KEY_SIZE]);

  /**
   * @brief Generate a new challenge
   *
   * @param challenge_out Output buffer (6 bytes)
   */
  void generate_challenge(uint8_t challenge_out[HMAC_SIZE]);

  /**
   * @brief Create challenge request frame
   *
   * @param frame Output IoFrame structure
   * @param dest_node Destination node ID (3 bytes)
   * @param src_node Source node ID (3 bytes)
   * @return true on success
   */
  bool create_challenge_request(
    frame::IoFrame* frame,
    const uint8_t dest_node[NODE_ID_SIZE],
    const uint8_t src_node[NODE_ID_SIZE]
  );

  /**
   * @brief Create challenge response frame
   *
   * @param frame Output IoFrame structure
   * @param dest_node Destination node ID (3 bytes)
   * @param src_node Source node ID (3 bytes)
   * @param received_challenge Challenge from request (6 bytes)
   * @return true on success
   */
  bool create_challenge_response(
    frame::IoFrame* frame,
    const uint8_t dest_node[NODE_ID_SIZE],
    const uint8_t src_node[NODE_ID_SIZE],
    const uint8_t received_challenge[HMAC_SIZE]
  );

  /**
   * @brief Verify challenge response
   *
   * @param frame Received response frame
   * @return true if valid
   */
  bool verify_challenge_response(const frame::IoFrame* frame);

  /**
   * @brief Get current challenge
   *
   * @return Pointer to current challenge (6 bytes)
   */
  const uint8_t* get_current_challenge() const { return current_challenge_; }

  /**
   * @brief Get authentication state
   *
   * @return Current ChallengeState
   */
  ChallengeState get_state() const { return state_; }

  /**
   * @brief Reset authentication state
   */
  void reset();

protected:
  uint8_t system_key_[AES_KEY_SIZE];
  uint8_t current_challenge_[HMAC_SIZE];
  ChallengeState state_;
  unsigned long challenge_timestamp_;
  uint32_t challenge_timeout_ms_;
};

// ============================================================================
// Beacon Handling
// ============================================================================

/**
 * @brief Beacon type
 */
enum class BeaconType : uint8_t {
  SYNC_BEACON = 0x00,      // Synchronization beacon
  DISCOVERY_BEACON = 0x01,  // Discovery beacon
  SYSTEM_BEACON = 0x02      // System announcement
};

/**
 * @brief Beacon information
 */
struct BeaconInfo {
  uint8_t node_id[NODE_ID_SIZE];
  BeaconType type;
  uint8_t data[FRAME_MAX_DATA_SIZE];
  uint8_t data_len;
  int16_t rssi;
  float snr;
  unsigned long timestamp_ms;
};

/**
 * @brief Beacon Handler
 *
 * Manages beacon reception and processing for 2W synchronization.
 */
class BeaconHandler {
public:
  BeaconHandler();

  /**
   * @brief Initialize beacon handler
   */
  void begin();

  /**
   * @brief Process received beacon frame
   *
   * @param frame Received frame
   * @param rssi RSSI value
   * @param snr SNR value
   * @return true if beacon was processed
   */
  bool process_beacon(const frame::IoFrame* frame, int16_t rssi, float snr);

  /**
   * @brief Get last received beacon info
   *
   * @param info Output BeaconInfo structure
   * @return true if beacon info is available
   */
  bool get_last_beacon(BeaconInfo* info);

  /**
   * @brief Check if beacon was received recently
   *
   * @param timeout_ms Timeout in milliseconds (default: 5000)
   * @return true if beacon was received within timeout
   */
  bool has_recent_beacon(unsigned long timeout_ms = 5000);

  /**
   * @brief Get time since last beacon in milliseconds
   *
   * @param current_time_ms Current time in milliseconds
   * @return Time since last beacon
   */
  unsigned long time_since_last_beacon(unsigned long current_time_ms);

protected:
  BeaconInfo last_beacon_;
  bool beacon_received_;
};

// ============================================================================
// Discovery and Pairing
// ============================================================================

/**
 * @brief Device discovery state
 */
enum class DiscoveryState : uint8_t {
  IDLE,           // Not discovering
  DISCOVERING,    // Discovery in progress
  FOUND           // Device found
};

/**
 * @brief Discovered device information
 */
struct DiscoveredDevice {
  uint8_t node_id[NODE_ID_SIZE];
  DeviceType device_type;
  uint8_t manufacturer;
  uint8_t protocol_version;
  int16_t rssi;
  unsigned long timestamp_ms;
};

/**
 * @brief Discovery Manager
 *
 * Handles device discovery and pairing workflows.
 */
class DiscoveryManager {
public:
  DiscoveryManager();

  /**
   * @brief Initialize discovery manager
   *
   * @param own_node_id This controller's node ID (3 bytes)
   */
  void begin(const uint8_t own_node_id[NODE_ID_SIZE]);

  /**
   * @brief Start device discovery
   *
   * @param device_type Type of device to discover (0xFF for all)
   * @param timeout_ms Discovery timeout in milliseconds
   */
  void start_discovery(uint8_t device_type = 0xFF, unsigned long timeout_ms = 10000);

  /**
   * @brief Stop device discovery
   */
  void stop_discovery();

  /**
   * @brief Create discovery request frame
   *
   * @param frame Output IoFrame structure
   * @param device_type Type of device to discover
   * @return true on success
   */
  bool create_discovery_request(frame::IoFrame* frame, uint8_t device_type);

  /**
   * @brief Process discovery response
   *
   * @param frame Received frame
   * @param rssi RSSI value
   * @return true if response was valid
   */
  bool process_discovery_response(const frame::IoFrame* frame, int16_t rssi);

  /**
   * @brief Get number of discovered devices
   *
   * @return Number of devices
   */
  size_t get_discovered_count() const { return discovered_count_; }

  /**
   * @brief Get discovered device by index
   *
   * @param index Device index (0 to discovered_count-1)
   * @param device Output DiscoveredDevice structure
   * @return true if device exists
   */
  bool get_discovered_device(size_t index, DiscoveredDevice* device);

  /**
   * @brief Create key transfer frame for pairing (1W mode)
   *
   * @param frame Output IoFrame structure
   * @param dest_node Destination node ID (3 bytes)
   * @param src_node Source node ID (3 bytes)
   * @param system_key System key to transfer (16 bytes)
   * @return true on success
   */
  bool create_key_transfer_1w(
    frame::IoFrame* frame,
    const uint8_t dest_node[NODE_ID_SIZE],
    const uint8_t src_node[NODE_ID_SIZE],
    const uint8_t system_key[AES_KEY_SIZE]
  );

  /**
   * @brief Create key transfer frame for pairing (2W mode)
   *
   * @param frame Output IoFrame structure
   * @param dest_node Destination node ID (3 bytes)
   * @param src_node Source node ID (3 bytes)
   * @param system_key System key to transfer (16 bytes)
   * @param challenge Challenge bytes (6 bytes)
   * @return true on success
   */
  bool create_key_transfer_2w(
    frame::IoFrame* frame,
    const uint8_t dest_node[NODE_ID_SIZE],
    const uint8_t src_node[NODE_ID_SIZE],
    const uint8_t system_key[AES_KEY_SIZE],
    const uint8_t challenge[HMAC_SIZE]
  );

protected:
  uint8_t own_node_id_[NODE_ID_SIZE];
  DiscoveryState state_;
  unsigned long discovery_start_time_;
  unsigned long discovery_timeout_;
  uint8_t discovery_device_type_;

  static constexpr size_t MAX_DISCOVERED_DEVICES = 32;
  DiscoveredDevice discovered_devices_[MAX_DISCOVERED_DEVICES];
  size_t discovered_count_;
};

} // namespace mode2w
} // namespace iohome
