#pragma once
// Protocol vtable — keeps protocol-specific behavior in separate version files.
// Included by midea_dehum.h, implemented by midea_dehum_protocol_v{1,2}.cpp.

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent;

struct ProtocolVTable {
  uint8_t version;

  // Advance the handshake state machine. Called from performHandshakeStep().
  void (*start_handshake)(MideaDehumComponent* self);

  // Returns true if data[] is a status response frame for this protocol variant.
  bool (*is_status_response)(uint8_t* data, size_t len);

  // Handle a non-status MCU message (ACK 0x07, queries E1/0xA0/0x63, ping 0x05).
  // RX counterpart to sendMessage(). Returns true if handled.
  bool (*on_message)(MideaDehumComponent* self, uint8_t* data, size_t len);

  // Send a status poll query to the MCU.  Uses sendMessage() so the frame
  // header carries the negotiated mcu_protocol_version_ from the handshake
  // (byte 7) and the caller-passed agreement version (byte 8).  This makes
  // the query adaptive to the MCU that answered the handshake instead of
  // burning a hard-coded frame that the MCU may reject.
  void (*get_status_query)(MideaDehumComponent* self);

  // Send a control command to the MCU.
  // V1 builds its payload inline via component accessors; V2 builds its own payload.
  // Both protocols provide this via their vtable — sendSetStatus() is a thin
  // dispatcher with no fallback logic.
  void (*send_set_status)(MideaDehumComponent* self);

  // Delay before first handshake attempt (ms)
  uint32_t startup_delay_ms;
};

// Defined in midea_dehum_protocol_v{1,2}.cpp (guarded by MIDEA_PROTOCOL_V{1,2})
#ifdef MIDEA_PROTOCOL_V1
extern const ProtocolVTable PROTOCOL_V1;
#endif
#ifdef MIDEA_PROTOCOL_V2
extern const ProtocolVTable PROTOCOL_V2;
#endif

}  // namespace midea_dehum
}  // namespace esphome
