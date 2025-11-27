#ifndef HEARTBEAT_MANAGER_H
#define HEARTBEAT_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <cstdint>
#include "../Config.h"
#include "../protocol/PacketTypes.h"
#include "../security/SecurityManager.h"
#include "../lora/LoRaModule.h"

class HeartbeatManager {
public:
	// Utilise les constantes de Config.h : HEARTBEAT_INTERVAL_MS et HEARTBEAT_TIMEOUT_MS
	static const unsigned long STATUS_UPDATE_INTERVAL_MS = 500;
	
	HeartbeatManager(SecurityManager* security, LoRaModule* lora);
	
	// Envoi de heartbeat
	void sendHeartbeatIfDue(uint32_t deviceId, const uint8_t* sessionKey, bool isPaired, bool isTransmitting);
	
	// Réception de heartbeat
	bool handleHeartbeat(const std::vector<uint8_t>& packet, const uint8_t* sessionKey, uint32_t deviceId, uint32_t& pairedDeviceId);
	
	// Vérification de l'état en ligne
	bool isPairedDeviceOnline() const;
	
	// Mise à jour de l'état
	void updateAndSendOnlineStatus(bool isPaired, uint32_t pairedDeviceId);
	
private:
	SecurityManager* security;
	LoRaModule* lora;
	
	unsigned long lastHeartbeatSentMs;
	unsigned long lastHeartbeatReceivedMs;
	unsigned long lastStatusUpdateMs;
	bool lastOnlineStateSent;
	
	void sendHeartbeat(uint32_t deviceId, const uint8_t* sessionKey);
};

#endif // HEARTBEAT_MANAGER_H

