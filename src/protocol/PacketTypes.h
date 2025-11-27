#ifndef PACKET_TYPES_H
#define PACKET_TYPES_H

#include <cstdint>

enum PacketType : uint8_t {
	PKT_BIND_REQ = 0x20,
	PKT_BIND_RESP = 0x21,
	PKT_BIND_CONFIRM = 0x22,
	PKT_DATA = 0x10,
	PKT_BEACON = 0x30,
	PKT_ACK = 0x11,
	PKT_HEARTBEAT = 0x31
};

#endif // PACKET_TYPES_H

