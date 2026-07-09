#pragma once
// Auto-detect state machine for protocol version discovery.
// Only compiled when MIDEA_PROTOCOL_AUTO is defined (protocol_version: 0 in YAML).
// When not compiled, all functions are no-op stubs so the main code can call
// them unconditionally without #ifdef guards.

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent;

#ifdef MIDEA_PROTOCOL_AUTO

struct MideaAutoDetect {
  bool active       = false;
  uint8_t round     = 0;
  bool got_response = false;
};

// Called once from set_protocol_version(0) to enable auto-detect.
void ad_init(MideaDehumComponent* self);

// Called on each auto-detect cycle round. Alternates V1/V2 with exponential
// backoff until a status response is received.
void ad_next(MideaDehumComponent* self);

// Called from processPacket(). Sets got_response if the frame is a status
// response for the current protocol.
void ad_on_packet(MideaDehumComponent* self, bool is_status);

// Called from switch_protocol() to clear auto-detect state on protocol change.
void ad_reset(MideaDehumComponent* self);

// Returns true if auto-detect is currently active.
bool ad_is_active(const MideaDehumComponent* self);

// Called from setup(). If auto-detect is active, starts the detection cycle.
// Returns true if it took over handshake scheduling (caller should not proceed).
bool ad_try_start(MideaDehumComponent* self);

#else  // !MIDEA_PROTOCOL_AUTO

struct MideaAutoDetect {};

inline void ad_init(MideaDehumComponent*) {}
inline void ad_next(MideaDehumComponent*) {}
inline void ad_on_packet(MideaDehumComponent*, bool) {}
inline void ad_reset(MideaDehumComponent*) {}
inline bool ad_is_active(const MideaDehumComponent*) {
  return false;
}
inline bool ad_try_start(MideaDehumComponent*) {
  return false;
}

#endif  // MIDEA_PROTOCOL_AUTO

}  // namespace midea_dehum
}  // namespace esphome
