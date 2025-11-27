#include "../lora/PacketHandler.h"

PacketHandler::PacketHandler(PairingManager* pairing, FragmentManager* fragment,
                             HeartbeatManager* heartbeat, DiscoveryManager* discovery)
	: pairing(pairing), fragment(fragment), heartbeat(heartbeat), discovery(discovery) {
}

uint8_t PacketHandler::findPacketType(const std::vector<uint8_t>& buffer, size_t& typeOffset) {
	typeOffset = 0;
	
	// Chercher le type de paquet dans les premiers octets
	for (size_t i = 0; i < buffer.size() && i < 5; i++) {
		uint8_t candidate = buffer[i];
		if (candidate == PKT_BIND_REQ || candidate == PKT_BIND_RESP || 
		    candidate == PKT_BIND_CONFIRM || candidate == PKT_DATA || 
		    candidate == PKT_BEACON || candidate == PKT_ACK || 
		    candidate == PKT_HEARTBEAT) {
			typeOffset = i;
			return candidate;
		}
	}
	
	return 0;
}

bool PacketHandler::handlePacket(const std::vector<uint8_t>& packet, uint32_t deviceId,
                                bool isPaired, const uint8_t* sessionKey, PairingManager* pairingMgr) {
	if (packet.empty()) return false;
	
	size_t typeOffset = 0;
	uint8_t type = findPacketType(packet, typeOffset);
	
	if (type == 0) {
		return false; // Type invalide
	}
	
	// Ajuster le buffer pour ignorer les octets avant le type
	std::vector<uint8_t> adjustedPacket;
	if (typeOffset > 0) {
		adjustedPacket.assign(packet.begin() + typeOffset, packet.end());
	} else {
		adjustedPacket = packet;
	}
	
	switch (type) {
		case PKT_BIND_REQ:
			return pairing->handleBindRequest(adjustedPacket);
			
		case PKT_BIND_RESP:
			return pairing->handleBindResponse(adjustedPacket);
			
		case PKT_BIND_CONFIRM:
			return pairing->handleBindConfirm(adjustedPacket);
			
		case PKT_BEACON:
			return discovery->handleBeacon(adjustedPacket, deviceId);
			
		case PKT_HEARTBEAT:
			if (!isPaired) {
				Serial.println("[HEARTBEAT] Heartbeat reçu mais non appairé, ignoré");
				return false;
			}
			{
				// Note: pairedDeviceId est géré par PairingManager, mais HeartbeatManager
				// peut le mettre à jour temporairement pour la détection
				uint32_t tempPairedDeviceId = pairingMgr->getPairedDeviceId();
				bool result = heartbeat->handleHeartbeat(adjustedPacket, sessionKey, deviceId, tempPairedDeviceId);
				// Le pairedDeviceId sera mis à jour dans PairingManager lors de l'appairage
				return result;
			}
			
		case PKT_DATA:
			if (!isPaired) {
				Serial.println("[SEC] Données reçues alors que non appairé, ignoré");
				return false;
			}
			return fragment->handleDataPacket(adjustedPacket, sessionKey);
			
		case PKT_ACK:
			if (!isPaired) {
				return false; // Ignorer si non appairé
			}
			return fragment->handleAck(adjustedPacket, sessionKey);
			
		default:
			return false;
	}
}

