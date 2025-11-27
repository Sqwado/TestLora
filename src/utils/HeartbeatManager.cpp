#include "HeartbeatManager.h"
#include <cstring>

HeartbeatManager::HeartbeatManager(SecurityManager* security, LoRaModule* lora)
	: security(security), lora(lora), lastHeartbeatSentMs(0), lastHeartbeatReceivedMs(0),
	  lastStatusUpdateMs(0), lastOnlineStateSent(false) {
}

void HeartbeatManager::sendHeartbeat(uint32_t deviceId, const uint8_t* sessionKey) {
	std::vector<uint8_t> pkt;
	pkt.reserve(1 + 4 + 16);
	pkt.push_back((uint8_t)PKT_HEARTBEAT);
	pkt.push_back((deviceId >> 24) & 0xFF);
	pkt.push_back((deviceId >> 16) & 0xFF);
	pkt.push_back((deviceId >> 8) & 0xFF);
	pkt.push_back(deviceId & 0xFF);
	
	uint8_t mac16[16];
	security->hmacSha256Trunc16(sessionKey, 16, pkt.data(), pkt.size(), mac16);
	pkt.insert(pkt.end(), mac16, mac16 + 16);
	
	lora->sendPacket(pkt);
}

void HeartbeatManager::sendHeartbeatIfDue(uint32_t deviceId, const uint8_t* sessionKey, 
                                          bool isPaired, bool isTransmitting) {
	if (!isPaired) return;
	
	// Ne bloquer les heartbeats que si une transmission est réellement en cours
	// Les messages en attente d'ACK ne bloquent plus les heartbeats
	// car les heartbeats sont très courts et n'interfèrent pas avec la transmission
	if (isTransmitting) {
		return; // Transmission en cours, attendre qu'elle se termine
	}
	
	const unsigned long now = millis();
	if (now - lastHeartbeatSentMs < HEARTBEAT_INTERVAL_MS) return;
	
	lastHeartbeatSentMs = now;
	sendHeartbeat(deviceId, sessionKey);
}

bool HeartbeatManager::handleHeartbeat(const std::vector<uint8_t>& packet, 
                                      const uint8_t* sessionKey, uint32_t deviceId, 
                                      uint32_t& pairedDeviceId) {
	if (packet.size() < 1 + 4 + 16) {
		return false;
	}
	
	uint32_t senderId = ((uint32_t)packet[1] << 24) | ((uint32_t)packet[2] << 16) | 
	                    ((uint32_t)packet[3] << 8) | packet[4];
	
	if (senderId == deviceId) {
		return false; // Heartbeat de nous-même, ignorer
	}
	
	uint8_t macRx[16];
	memcpy(macRx, &packet[packet.size() - 16], 16);
	uint8_t macCalc[16];
	security->hmacSha256Trunc16(sessionKey, 16, packet.data(), packet.size() - 16, macCalc);
	
	if (memcmp(macRx, macCalc, 16) != 0) {
		Serial.println("[HEARTBEAT] MAC invalide, heartbeat rejeté");
		return false;
	}
	
	lastHeartbeatReceivedMs = millis();
	bool wasOnline = (lastOnlineStateSent == true);
	
	if (pairedDeviceId == 0 || pairedDeviceId != senderId) {
		pairedDeviceId = senderId;
		Serial.print("[HEARTBEAT] Device appairé détecté: 0x");
		Serial.println(senderId, HEX);
	}
	
	if (!wasOnline) {
		lastOnlineStateSent = true;
		Serial.print("[STATUS] Device appairé en ligne: OUI");
		if (pairedDeviceId != 0) {
			Serial.print(" (ID: 0x");
			Serial.print(pairedDeviceId, HEX);
			Serial.print(")");
		}
		Serial.println();
	}
	
	return true;
}

bool HeartbeatManager::isPairedDeviceOnline() const {
	if (lastHeartbeatReceivedMs == 0) return false;
	const unsigned long now = millis();
	return (now - lastHeartbeatReceivedMs) < HEARTBEAT_TIMEOUT_MS;
}

void HeartbeatManager::updateAndSendOnlineStatus(bool isPaired, uint32_t pairedDeviceId) {
	const unsigned long now = millis();
	if (now - lastStatusUpdateMs < STATUS_UPDATE_INTERVAL_MS) {
		return;
	}
	lastStatusUpdateMs = now;
	
	if (!isPaired) {
		if (lastOnlineStateSent != false) {
			lastOnlineStateSent = false;
		}
		return;
	}
	
	bool online = isPairedDeviceOnline();
	
	if (online != lastOnlineStateSent) {
		lastOnlineStateSent = online;
		Serial.print("[STATUS] Device appairé en ligne: ");
		Serial.println(online ? "OUI" : "NON");
		if (pairedDeviceId != 0) {
			Serial.print("[STATUS] Device appairé ID: 0x");
			Serial.println(pairedDeviceId, HEX);
		}
	}
}

