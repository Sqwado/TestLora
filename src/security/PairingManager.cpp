#include "PairingManager.h"

PairingManager::PairingManager(SecurityManager* security, LoRaModule* lora, NVSManager* nvs)
	: security(security), lora(lora), nvs(nvs), paired(false), pairedDeviceId(0),
	  pendingBind(false), pendingInitiatorId(0), deviceId(0) {
	memset(sessionKey, 0, 16);
	memset(nonceInitiator, 0, 16);
	memset(nonceResponder, 0, 16);
	memset(pendingNonceI, 0, 16);
}

bool PairingManager::loadPairingState() {
	return nvs->loadPairingState(sessionKey, 16, paired);
}

bool PairingManager::savePairingState() {
	return nvs->savePairingState(sessionKey, 16, paired);
}

bool PairingManager::clearPairingState() {
	paired = false;
	memset(sessionKey, 0, 16);
	pairedDeviceId = 0;
	return nvs->clearPairingState();
}

bool PairingManager::sendBindRequest(uint32_t targetId) {
	security->generateRandomBytes(nonceInitiator, 16);
	
	std::vector<uint8_t> pubI;
	if (!security->generateKeypair(pubI)) {
		Serial.println("[BIND] Echec gen clé");
		return false;
	}
	
	currentPubI = pubI;
	
	std::vector<uint8_t> pkt;
	pkt.reserve(1 + 4 + 4 + 16 + 1 + pubI.size());
	pkt.push_back((uint8_t)PKT_BIND_REQ);
	pkt.push_back((targetId >> 24) & 0xFF);
	pkt.push_back((targetId >> 16) & 0xFF);
	pkt.push_back((targetId >> 8) & 0xFF);
	pkt.push_back(targetId & 0xFF);
	pkt.push_back((deviceId >> 24) & 0xFF);
	pkt.push_back((deviceId >> 16) & 0xFF);
	pkt.push_back((deviceId >> 8) & 0xFF);
	pkt.push_back(deviceId & 0xFF);
	pkt.insert(pkt.end(), nonceInitiator, nonceInitiator + 16);
	pkt.push_back((uint8_t)pubI.size());
	pkt.insert(pkt.end(), pubI.begin(), pubI.end());
	
	lora->sendPacket(pkt);
	Serial.print("[BIND] REQ -> "); Serial.println(targetId, HEX);
	return true;
}

void PairingManager::sendBindResponse(uint32_t initiatorId, const std::vector<uint8_t>& pubI) {
	security->generateRandomBytes(nonceResponder, 16);
	
	std::vector<uint8_t> pubR;
	if (!security->generateKeypair(pubR)) {
		Serial.println("[BIND] Echec gen clé");
		return;
	}
	
	currentPubR = pubR;
	
	// Calculer clé temporaire pour MAC
	std::vector<uint8_t> shared;
	if (!security->computeSharedSecret(pubI.data(), pubI.size(), shared)) {
		Serial.println("[BIND] Echec ECDH");
		return;
	}
	
	uint8_t tempKey[16];
	security->deriveSessionKeyFromShared(shared.data(), shared.size(), 
	                                    pendingNonceI, nonceResponder, tempKey);
	
	// MAC = HMAC16(tempKey, "RESP"||nonceI||nonceR||pubI||pubR)
	std::vector<uint8_t> toMac;
	const char* tag = "RESP";
	toMac.insert(toMac.end(), (const uint8_t*)tag, (const uint8_t*)tag + 4);
	toMac.insert(toMac.end(), pendingNonceI, pendingNonceI + 16);
	toMac.insert(toMac.end(), nonceResponder, nonceResponder + 16);
	toMac.insert(toMac.end(), pubI.begin(), pubI.end());
	toMac.insert(toMac.end(), pubR.begin(), pubR.end());
	
	uint8_t mac16[16];
	security->hmacSha256Trunc16(tempKey, 16, toMac.data(), toMac.size(), mac16);
	
	std::vector<uint8_t> pkt;
	pkt.reserve(1 + 4 + 4 + 16 + 1 + pubR.size() + 16);
	pkt.push_back((uint8_t)PKT_BIND_RESP);
	pkt.push_back((initiatorId >> 24) & 0xFF);
	pkt.push_back((initiatorId >> 16) & 0xFF);
	pkt.push_back((initiatorId >> 8) & 0xFF);
	pkt.push_back(initiatorId & 0xFF);
	pkt.push_back((deviceId >> 24) & 0xFF);
	pkt.push_back((deviceId >> 16) & 0xFF);
	pkt.push_back((deviceId >> 8) & 0xFF);
	pkt.push_back(deviceId & 0xFF);
	pkt.insert(pkt.end(), nonceResponder, nonceResponder + 16);
	pkt.push_back((uint8_t)pubR.size());
	pkt.insert(pkt.end(), pubR.begin(), pubR.end());
	pkt.insert(pkt.end(), mac16, mac16 + 16);
	
	lora->sendPacket(pkt);
	Serial.print("[BIND] RESP -> "); Serial.println(initiatorId, HEX);
}

void PairingManager::sendBindConfirm(const std::vector<uint8_t>& pubI, const std::vector<uint8_t>& pubR) {
	std::vector<uint8_t> shared;
	if (!security->computeSharedSecret(pubR.data(), pubR.size(), shared)) {
		Serial.println("[BIND] Echec ECDH confirm");
		return;
	}
	
	uint8_t tempKey[16];
	security->deriveSessionKeyFromShared(shared.data(), shared.size(), 
	                                    nonceInitiator, nonceResponder, tempKey);
	
	// MAC = HMAC16(tempKey, "CONF"||nonceI||nonceR||pubI||pubR)
	std::vector<uint8_t> toMac;
	const char* tag = "CONF";
	toMac.insert(toMac.end(), (const uint8_t*)tag, (const uint8_t*)tag + 4);
	toMac.insert(toMac.end(), nonceInitiator, nonceInitiator + 16);
	toMac.insert(toMac.end(), nonceResponder, nonceResponder + 16);
	toMac.insert(toMac.end(), pubI.begin(), pubI.end());
	toMac.insert(toMac.end(), pubR.begin(), pubR.end());
	
	uint8_t mac16[16];
	security->hmacSha256Trunc16(tempKey, 16, toMac.data(), toMac.size(), mac16);
	
	std::vector<uint8_t> pkt;
	pkt.reserve(1 + 16);
	pkt.push_back((uint8_t)PKT_BIND_CONFIRM);
	pkt.insert(pkt.end(), mac16, mac16 + 16);
	
	lora->sendPacket(pkt);
	Serial.println("[BIND] CONF sent");
}

bool PairingManager::acceptPendingBind() {
	if (!pendingBind) {
		Serial.println("[BIND] Rien à accepter.");
		return false;
	}
	
	memcpy(nonceInitiator, pendingNonceI, 16);
	sendBindResponse(pendingInitiatorId, pendingPubI);
	pendingBind = false;
	return true;
}

void PairingManager::cancelPendingBind() {
	pendingBind = false;
	Serial.println("[BIND] Annulé.");
}

bool PairingManager::handleBindRequest(const std::vector<uint8_t>& packet) {
	// type | targetId(4) | initiatorId(4) | nonceI(16) | pubLen(1) | pub
	if (packet.size() < 1 + 4 + 4 + 16 + 1) return false;
	
	uint32_t target = ((uint32_t)packet[1] << 24) | ((uint32_t)packet[2] << 16) | 
	                  ((uint32_t)packet[3] << 8) | packet[4];
	uint32_t initId = ((uint32_t)packet[5] << 24) | ((uint32_t)packet[6] << 16) | 
	                  ((uint32_t)packet[7] << 8) | packet[8];
	
	if (target != deviceId) return false; // pas pour nous
	
	uint8_t pubLen = packet[1 + 4 + 4 + 16];
	if (packet.size() < 1 + 4 + 4 + 16 + 1 + pubLen) return false;
	
	memcpy(pendingNonceI, &packet[1 + 4 + 4], 16);
	pendingInitiatorId = initId;
	pendingPubI.assign(packet.begin() + (1 + 4 + 4 + 16 + 1), 
	                  packet.begin() + (1 + 4 + 4 + 16 + 1 + pubLen));
	pendingBind = true;
	
	Serial.print("[BIND] REQ de "); Serial.print(initId, HEX);
	Serial.println(". Tapez 'A' pour accepter.");
	return true;
}

bool PairingManager::handleBindResponse(const std::vector<uint8_t>& packet) {
	// type | initiatorId(4) | responderId(4) | nonceR(16) | pubLen(1) | pubR | mac16
	if (packet.size() < 1 + 4 + 4 + 16 + 1 + 16) return false;
	
	uint32_t initId = ((uint32_t)packet[1] << 24) | ((uint32_t)packet[2] << 16) | 
	                  ((uint32_t)packet[3] << 8) | packet[4];
	uint32_t respId = ((uint32_t)packet[5] << 24) | ((uint32_t)packet[6] << 16) | 
	                  ((uint32_t)packet[7] << 8) | packet[8];
	
	if (initId != deviceId) return false; // pas pour nous
	
	uint8_t pubLen = packet[1 + 4 + 4 + 16];
	if (packet.size() < 1 + 4 + 4 + 16 + 1 + pubLen + 16) return false;
	
	memcpy(nonceResponder, &packet[1 + 4 + 4], 16);
	std::vector<uint8_t> pubR(packet.begin() + (1 + 4 + 4 + 16 + 1), 
	                          packet.begin() + (1 + 4 + 4 + 16 + 1 + pubLen));
	
	uint8_t macRx[16];
	memcpy(macRx, &packet[packet.size() - 16], 16);
	
	// Recalculer tempKey
	std::vector<uint8_t> shared;
	if (!security->computeSharedSecret(pubR.data(), pubR.size(), shared)) {
		Serial.println("[BIND] ECDH resp failed");
		return false;
	}
	
	uint8_t tempKey[16];
	security->deriveSessionKeyFromShared(shared.data(), shared.size(), 
	                                    nonceInitiator, nonceResponder, tempKey);
	
	// Vérifier MAC
	std::vector<uint8_t> toMac;
	const char* tag = "RESP";
	toMac.insert(toMac.end(), (const uint8_t*)tag, (const uint8_t*)tag + 4);
	toMac.insert(toMac.end(), nonceInitiator, nonceInitiator + 16);
	toMac.insert(toMac.end(), nonceResponder, nonceResponder + 16);
	
	std::vector<uint8_t> pubI;
	if (!security->exportPublicKey(pubI)) {
		Serial.println("[BIND] Echec export pubI");
		return false;
	}
	
	toMac.insert(toMac.end(), pubI.begin(), pubI.end());
	toMac.insert(toMac.end(), pubR.begin(), pubR.end());
	
	uint8_t macCalc[16];
	security->hmacSha256Trunc16(tempKey, 16, toMac.data(), toMac.size(), macCalc);
	
	if (memcmp(macRx, macCalc, 16) != 0) {
		Serial.println("[BIND] MAC RESP invalide");
		return false;
	}
	
	// OK -> on envoie CONFIRM et fixe sessionKey
	memcpy(sessionKey, tempKey, 16);
	paired = true;
	pairedDeviceId = respId;
	savePairingState();
	
	Serial.print("[BIND] Etabli avec "); Serial.println(respId, HEX);
	sendBindConfirm(pubI, pubR);
	return true;
}

bool PairingManager::handleBindConfirm(const std::vector<uint8_t>& packet) {
	// type | mac16
	if (packet.size() < 1 + 16) return false;
	
	uint8_t macRx[16];
	memcpy(macRx, &packet[1], 16);
	
	// Recalc avec tempKey côté répondeur
	std::vector<uint8_t> shared;
	if (!security->computeSharedSecret(pendingPubI.data(), pendingPubI.size(), shared)) {
		Serial.println("[BIND] ECDH confirm failed");
		return false;
	}
	
	uint8_t tempKey[16];
	security->deriveSessionKeyFromShared(shared.data(), shared.size(), 
	                                    pendingNonceI, nonceResponder, tempKey);
	
	// Vérifier MAC
	std::vector<uint8_t> toMac;
	const char* tag = "CONF";
	toMac.insert(toMac.end(), (const uint8_t*)tag, (const uint8_t*)tag + 4);
	toMac.insert(toMac.end(), pendingNonceI, pendingNonceI + 16);
	toMac.insert(toMac.end(), nonceResponder, nonceResponder + 16);
	
	std::vector<uint8_t> pubR;
	if (!security->exportPublicKey(pubR)) {
		Serial.println("[BIND] Echec export pubR");
		return false;
	}
	
	toMac.insert(toMac.end(), pendingPubI.begin(), pendingPubI.end());
	toMac.insert(toMac.end(), pubR.begin(), pubR.end());
	
	uint8_t macCalc[16];
	security->hmacSha256Trunc16(tempKey, 16, toMac.data(), toMac.size(), macCalc);
	
	if (memcmp(macRx, macCalc, 16) != 0) {
		Serial.println("[BIND] MAC CONF invalide");
		return false;
	}
	
	memcpy(sessionKey, tempKey, 16);
	paired = true;
	pairedDeviceId = pendingInitiatorId;
	savePairingState();
	pendingBind = false;
	
	Serial.print("[BIND] Appairage terminé (répondeur) avec 0x");
	Serial.println(pendingInitiatorId, HEX);
	return true;
}

