#ifndef PACKET_HANDLER_H
#define PACKET_HANDLER_H

#include <Arduino.h>
#include <vector>
#include "../protocol/PacketTypes.h"
#include "../security/PairingManager.h"
#include "../protocol/FragmentManager.h"
#include "../utils/HeartbeatManager.h"
#include "../security/DiscoveryManager.h"

class PacketHandler {
public:
	PacketHandler(PairingManager* pairing, FragmentManager* fragment, 
	             HeartbeatManager* heartbeat, DiscoveryManager* discovery);
	
	// Traitement d'un paquet reçu
	bool handlePacket(const std::vector<uint8_t>& packet, uint32_t deviceId, 
	                 bool isPaired, const uint8_t* sessionKey, PairingManager* pairing);
	
private:
	PairingManager* pairing;
	FragmentManager* fragment;
	HeartbeatManager* heartbeat;
	DiscoveryManager* discovery;
	
	// Trouver le type de paquet dans le buffer (peut être décalé)
	uint8_t findPacketType(const std::vector<uint8_t>& buffer, size_t& typeOffset);
};

#endif // PACKET_HANDLER_H

