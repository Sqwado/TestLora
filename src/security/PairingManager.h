#ifndef PAIRING_MANAGER_H
#define PAIRING_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <cstdint>
#include "PacketTypes.h"
#include "SecurityManager.h"
#include "LoRaModule.h"
#include "NVSManager.h"

class PairingManager {
public:
	PairingManager(SecurityManager* security, LoRaModule* lora, NVSManager* nvs);
	
	// Gestion de l'état d'appairage
	bool isPaired() const { return paired; }
	uint32_t getPairedDeviceId() const { return pairedDeviceId; }
	const uint8_t* getSessionKey() const { return sessionKey; }
	
	// Charger/sauvegarder l'état
	bool loadPairingState();
	bool savePairingState();
	bool clearPairingState();
	
	// Initier une demande d'appairage
	bool sendBindRequest(uint32_t targetId);
	
	// Accepter une demande d'appairage
	bool acceptPendingBind();
	void cancelPendingBind();
	
	// Vérifier s'il y a une demande en attente
	bool hasPendingBind() const { return pendingBind; }
	uint32_t getPendingInitiatorId() const { return pendingInitiatorId; }
	
	// Traitement des paquets d'appairage
	bool handleBindRequest(const std::vector<uint8_t>& packet);
	bool handleBindResponse(const std::vector<uint8_t>& packet);
	bool handleBindConfirm(const std::vector<uint8_t>& packet);
	
	// Configuration
	void setDeviceId(uint32_t id) { deviceId = id; }
	
private:
	SecurityManager* security;
	LoRaModule* lora;
	NVSManager* nvs;
	
	bool paired;
	uint8_t sessionKey[16];
	uint32_t pairedDeviceId;
	
	// État d'appairage en cours
	bool pendingBind;
	uint32_t pendingInitiatorId;
	std::vector<uint8_t> pendingPubI;
	uint8_t pendingNonceI[16];
	
	// Nonces pour l'appairage courant
	uint8_t nonceInitiator[16];
	uint8_t nonceResponder[16];
	
	// Clés ECDH pour l'appairage courant
	std::vector<uint8_t> currentPubI;
	std::vector<uint8_t> currentPubR;
	
	uint32_t deviceId;
	
	void sendBindResponse(uint32_t initiatorId, const std::vector<uint8_t>& pubI);
	void sendBindConfirm(const std::vector<uint8_t>& pubI, const std::vector<uint8_t>& pubR);
};

#endif // PAIRING_MANAGER_H

