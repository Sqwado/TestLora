#ifndef FRAGMENT_MANAGER_H
#define FRAGMENT_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <cstdint>
#include "PacketTypes.h"
#include "SecurityManager.h"
#include "LoRaModule.h"

struct PendingPacket {
	uint32_t seq;
	uint16_t fragId;
	std::vector<uint8_t> packetData;
	unsigned long lastSentMs;
	uint8_t retryCount;
	bool acked;
};

struct PendingMessage {
	uint32_t seq;
	uint16_t totalFrags;
	std::vector<PendingPacket> packets;
	unsigned long firstSentMs;
};

struct FragmentBuffer {
	uint32_t seq;
	uint16_t totalFrags;
	uint8_t iv[16];
	std::vector<std::vector<uint8_t>> fragments;
	unsigned long firstSeenMs;
	bool complete;
	bool hasIv;
};

class FragmentManager {
public:
	static const size_t MAX_FRAGMENT_PAYLOAD = 156;
	static const unsigned long FRAGMENT_TIMEOUT_MS = 15000;
	static const unsigned long ACK_TIMEOUT_MS = 2000;
	static const unsigned long ACK_FAST_WINDOW_MS = 350;
	static const unsigned long INTER_FRAGMENT_GAP_MS = 40;
	static const unsigned long ACK_POLL_DELAY_MS = 5;
	static const uint8_t MAX_RETRIES = 3;
	
	FragmentManager(SecurityManager* security, LoRaModule* lora);
	
	// Envoi de messages fragmentés
	bool sendSecureMessage(const String& text, const uint8_t* sessionKey, uint32_t& seqNumber);
	
	// Réception de fragments
	bool handleDataPacket(const std::vector<uint8_t>& packet, const uint8_t* sessionKey);
	
	// Gestion des ACKs
	bool handleAck(const std::vector<uint8_t>& packet, const uint8_t* sessionKey);
	bool waitForAck(uint32_t seq, uint16_t fragId, unsigned long timeoutMs = ACK_TIMEOUT_MS);
	
	// Maintenance
	void processPendingRetries();
	void purgeOldFragments();
	
	// Vérifier si une transmission est en cours
	bool hasPendingMessages() const { return !pendingMessages.empty(); }
	
	// Vérifier si une transmission est réellement en cours (pas juste en attente d'ACK)
	bool isTransmitting() const;
	
private:
	SecurityManager* security;
	LoRaModule* lora;
	
	std::vector<PendingMessage> pendingMessages;
	std::vector<FragmentBuffer> fragmentBuffers;
	
	void sendSecureMessageFragment(const uint8_t* cipherData, size_t cipherLen, 
	                               uint32_t seq, uint16_t fragId, uint16_t totalFrags,
	                               const uint8_t iv[16], const uint8_t* sessionKey);
	void sendAck(uint32_t seq, uint16_t fragId, const uint8_t* sessionKey);
};

#endif // FRAGMENT_MANAGER_H

