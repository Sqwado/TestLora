#include "../protocol/FragmentManager.h"
#include <cstring>

FragmentManager::FragmentManager(SecurityManager* security, LoRaModule* lora)
	: security(security), lora(lora) {
}

void FragmentManager::sendAck(uint32_t seq, uint16_t fragId, const uint8_t* sessionKey) {
	std::vector<uint8_t> pkt;
	pkt.reserve(1 + 4 + 2 + 16);
	pkt.push_back((uint8_t)PKT_ACK);
	pkt.push_back((seq >> 24) & 0xFF);
	pkt.push_back((seq >> 16) & 0xFF);
	pkt.push_back((seq >> 8) & 0xFF);
	pkt.push_back(seq & 0xFF);
	pkt.push_back((fragId >> 8) & 0xFF);
	pkt.push_back(fragId & 0xFF);
	
	uint8_t mac16[16];
	security->hmacSha256Trunc16(sessionKey, 16, pkt.data(), pkt.size(), mac16);
	pkt.insert(pkt.end(), mac16, mac16 + 16);
	
	Serial.print("[ACK] Envoi ACK pour seq=");
	Serial.print(seq);
	Serial.print(" frag=");
	Serial.println(fragId);
	
	lora->sendPacket(pkt);
}

void FragmentManager::sendSecureMessageFragment(const uint8_t* cipherData, size_t cipherLen,
                                                uint32_t seq, uint16_t fragId, uint16_t totalFrags,
                                                const uint8_t iv[16], const uint8_t* sessionKey) {
	const bool includeIv = (fragId == 0);
	std::vector<uint8_t> pkt;
	pkt.reserve(1 + 4 + 2 + 2 + (includeIv ? 16 : 0) + cipherLen + 16);
	pkt.push_back((uint8_t)PKT_DATA);
	pkt.push_back((seq >> 24) & 0xFF);
	pkt.push_back((seq >> 16) & 0xFF);
	pkt.push_back((seq >> 8) & 0xFF);
	pkt.push_back(seq & 0xFF);
	pkt.push_back((fragId >> 8) & 0xFF);
	pkt.push_back(fragId & 0xFF);
	pkt.push_back((totalFrags >> 8) & 0xFF);
	pkt.push_back(totalFrags & 0xFF);
	
	if (includeIv) {
		pkt.insert(pkt.end(), iv, iv + 16);
	}
	pkt.insert(pkt.end(), cipherData, cipherData + cipherLen);
	
	uint8_t mac16[16];
	security->hmacSha256Trunc16(sessionKey, 16, pkt.data(), pkt.size(), mac16);
	pkt.insert(pkt.end(), mac16, mac16 + 16);
	
	lora->sendPacket(pkt);
	
	PendingPacket pp;
	pp.seq = seq;
	pp.fragId = fragId;
	pp.packetData = pkt;
	pp.lastSentMs = millis();
	pp.retryCount = 0;
	pp.acked = false;
	
	PendingMessage* pm = nullptr;
	for (auto &p : pendingMessages) {
		if (p.seq == seq) {
			pm = &p;
			break;
		}
	}
	
	if (!pm) {
		PendingMessage newPm;
		newPm.seq = seq;
		newPm.totalFrags = totalFrags;
		newPm.firstSentMs = millis();
		pendingMessages.push_back(newPm);
		pm = &pendingMessages.back();
	}
	pm->packets.push_back(pp);
}

bool FragmentManager::sendSecureMessage(const String& text, const uint8_t* sessionKey, uint32_t& seqNumber) {
	std::vector<uint8_t> plain;
	plain.reserve(2 + text.length());
	uint16_t tlen = (uint16_t)text.length();
	plain.push_back((tlen >> 8) & 0xFF);
	plain.push_back(tlen & 0xFF);
	for (size_t i = 0; i < text.length(); ++i) {
		plain.push_back((uint8_t)text[i]);
	}
	
	uint8_t iv[16];
	security->generateRandomBytes(iv, 16);
	
	std::vector<uint8_t> cipher(plain.size());
	security->aesCtrCrypt(sessionKey, iv, plain.data(), cipher.data(), cipher.size());
	
	uint32_t s = seqNumber++;
	
	size_t cipherLen = cipher.size();
	uint16_t totalFrags = (cipherLen + MAX_FRAGMENT_PAYLOAD - 1) / MAX_FRAGMENT_PAYLOAD;
	
	if (totalFrags == 1) {
		sendSecureMessageFragment(cipher.data(), cipherLen, s, 0, 1, iv, sessionKey);
		Serial.print("[SEC] Envoi chiffré: ");
		Serial.println(text);
		if (waitForAck(s, 0, ACK_FAST_WINDOW_MS)) {
			Serial.println("[SEC] ACK rapide reçu");
		} else {
			Serial.println("[SEC] ACK différé (gestion asynchrone)");
		}
	} else {
		Serial.print("[SEC] Fragmentation: ");
		Serial.print(totalFrags);
		Serial.print(" fragments pour ");
		Serial.print(text.length());
		Serial.println(" caractères");
		
		for (uint16_t fragId = 0; fragId < totalFrags; ++fragId) {
			size_t offset = fragId * MAX_FRAGMENT_PAYLOAD;
			size_t fragLen = (offset + MAX_FRAGMENT_PAYLOAD <= cipherLen) ? 
			                MAX_FRAGMENT_PAYLOAD : (cipherLen - offset);
			sendSecureMessageFragment(cipher.data() + offset, fragLen, s, fragId, totalFrags, iv, sessionKey);
			Serial.print("[SEC] Fragment ");
			Serial.print(fragId + 1);
			Serial.print("/");
			Serial.print(totalFrags);
			Serial.print(" envoyé");
			if (waitForAck(s, fragId, ACK_FAST_WINDOW_MS)) {
				Serial.println(" (ACK)");
			} else {
				Serial.println(" (ACK différé)");
			}
			unsigned long waitStart = millis();
			while (millis() - waitStart < INTER_FRAGMENT_GAP_MS) {
				delay(ACK_POLL_DELAY_MS);
			}
		}
		Serial.println("[SEC] Tous les fragments envoyés (ACK asynchrone)");
	}
	
	return true;
}

bool FragmentManager::handleDataPacket(const std::vector<uint8_t>& packet, const uint8_t* sessionKey) {
	if (packet.size() < 1 + 4 + 2 + 2 + 16) {
		Serial.print("[SEC] Paquet trop court: ");
		Serial.println(packet.size());
		return false;
	}
	
	const size_t macOffset = packet.size() - 16;
	uint8_t macRx[16];
	memcpy(macRx, &packet[macOffset], 16);
	uint8_t macCalc[16];
	security->hmacSha256Trunc16(sessionKey, 16, packet.data(), macOffset, macCalc);
	
	if (memcmp(macRx, macCalc, 16) != 0) {
		Serial.println("[SEC] MAC invalide. Paquet rejeté.");
		return false;
	}
	
	uint32_t seq = ((uint32_t)packet[1] << 24) | ((uint32_t)packet[2] << 16) | 
	               ((uint32_t)packet[3] << 8) | packet[4];
	uint16_t fragId = ((uint16_t)packet[5] << 8) | packet[6];
	uint16_t totalFrags = ((uint16_t)packet[7] << 8) | packet[8];
	size_t offset = 1 + 4 + 2 + 2;
	
	bool packetHasIv = (fragId == 0);
	uint8_t ivFromPacket[16];
	if (packetHasIv) {
		if (macOffset < offset + 16) {
			Serial.println("[SEC] Paquet fragment 0 trop court (IV manquant)");
			return false;
		}
		memcpy(ivFromPacket, &packet[offset], 16);
		offset += 16;
	}
	
	std::vector<uint8_t> cipherFrag(packet.begin() + offset, packet.begin() + macOffset);
	
	sendAck(seq, fragId, sessionKey);
	
	if (totalFrags == 1) {
		if (!packetHasIv) {
			Serial.println("[SEC] Fragment unique sans IV, ignoré");
			return false;
		}
		std::vector<uint8_t> plain(cipherFrag.size());
		security->aesCtrCrypt(sessionKey, ivFromPacket, cipherFrag.data(), plain.data(), plain.size());
		if (plain.size() < 2) return false;
		uint16_t tlen = ((uint16_t)plain[0] << 8) | plain[1];
		if (plain.size() < 2 + tlen) return false;
		String msg;
		for (size_t i = 0; i < tlen; ++i) msg += (char)plain[2 + i];
		Serial.print("[SEC] Reçu: ");
		Serial.println(msg);
		return true;
	}
	
	purgeOldFragments();
	FragmentBuffer* fb = nullptr;
	for (auto &f : fragmentBuffers) {
		if (f.seq == seq && f.totalFrags == totalFrags) {
			fb = &f;
			break;
		}
	}
	
	if (!fb) {
		FragmentBuffer newFb;
		newFb.seq = seq;
		newFb.totalFrags = totalFrags;
		memset(newFb.iv, 0, sizeof(newFb.iv));
		newFb.fragments.resize(totalFrags);
		newFb.firstSeenMs = millis();
		newFb.complete = false;
		newFb.hasIv = false;
		fragmentBuffers.push_back(newFb);
		fb = &fragmentBuffers.back();
	}
	
	if (packetHasIv) {
		if (fb->hasIv) {
			if (memcmp(fb->iv, ivFromPacket, 16) != 0) {
				Serial.println("[FRAG] Erreur: IV différent détecté, fragment ignoré");
				return false;
			}
		} else {
			memcpy(fb->iv, ivFromPacket, 16);
			fb->hasIv = true;
		}
	}
	
	if (fragId >= fb->fragments.size()) {
		Serial.print("[FRAG] Erreur: fragId ");
		Serial.print(fragId);
		Serial.print(" >= totalFrags ");
		Serial.println(fb->fragments.size());
		return false;
	}
	
	if (!fb->fragments[fragId].empty()) {
		if (fb->fragments[fragId].size() == cipherFrag.size() &&
		    memcmp(fb->fragments[fragId].data(), cipherFrag.data(), cipherFrag.size()) == 0) {
			Serial.print("[FRAG] Fragment ");
			Serial.print(fragId + 1);
			Serial.println("/ déjà reçu, ignoré");
			return false;
		}
	}
	
	fb->fragments[fragId] = cipherFrag;
	Serial.print("[FRAG] Reçu fragment ");
	Serial.print(fragId + 1);
	Serial.print("/");
	Serial.print(totalFrags);
	Serial.print(" (seq=");
	Serial.print(seq);
	Serial.println(")");
	
	bool allReceived = true;
	for (size_t i = 0; i < fb->fragments.size(); ++i) {
		if (fb->fragments[i].empty()) {
			allReceived = false;
			break;
		}
	}
	
	if (allReceived && fb->hasIv && !fb->complete) {
		fb->complete = true;
		std::vector<uint8_t> cipherFull;
		for (const auto &frag : fb->fragments) {
			cipherFull.insert(cipherFull.end(), frag.begin(), frag.end());
		}
		std::vector<uint8_t> plain(cipherFull.size());
		security->aesCtrCrypt(sessionKey, fb->iv, cipherFull.data(), plain.data(), plain.size());
		if (plain.size() < 2) {
			Serial.println("[FRAG] Erreur déchiffrement");
			return false;
		}
		uint16_t tlen = ((uint16_t)plain[0] << 8) | plain[1];
		if (plain.size() < 2 + tlen) {
			Serial.println("[FRAG] Taille invalide");
			return false;
		}
		String msg;
		for (size_t i = 0; i < tlen; ++i) {
			msg += (char)plain[2 + i];
		}
		Serial.print("[SEC] Reçu (fragmenté): ");
		Serial.println(msg);
		return true;
	}
	
	return false;
}
