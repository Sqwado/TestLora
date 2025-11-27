#include "DiscoveryManager.h"

DiscoveryManager::DiscoveryManager(LoRaModule* lora)
	: lora(lora), pairingMode(false), lastBeaconMs(0), lastDiscoveryPrintMs(0) {
}

void DiscoveryManager::upsertDiscovered(uint32_t id, int rssi, float snr) {
	bool found = false;
	for (auto &d : discovered) {
		if (d.id == id) {
			d.rssi = rssi;
			d.snr = snr;
			d.lastSeenMs = millis();
			found = true;
			break;
		}
	}
	if (!found) {
		DiscoveredDevice d{ id, rssi, snr, millis() };
		discovered.push_back(d);
	}
}

void DiscoveryManager::purgeDiscovered() {
	const unsigned long now = millis();
	std::vector<DiscoveredDevice> keep;
	keep.reserve(discovered.size());
	for (auto &d : discovered) {
		if (now - d.lastSeenMs <= DISCOVERY_TTL_MS) {
			keep.push_back(d);
		}
	}
	discovered.swap(keep);
}

void DiscoveryManager::sendBeaconIfDue(uint32_t deviceId) {
	if (!pairingMode) return;
	
	const unsigned long now = millis();
	if (now - lastBeaconMs < BEACON_INTERVAL_MS) return;
	
	lastBeaconMs = now;
	
	std::vector<uint8_t> pkt;
	pkt.reserve(1 + 4);
	pkt.push_back((uint8_t)PKT_BEACON);
	pkt.push_back((deviceId >> 24) & 0xFF);
	pkt.push_back((deviceId >> 16) & 0xFF);
	pkt.push_back((deviceId >> 8) & 0xFF);
	pkt.push_back(deviceId & 0xFF);
	
	lora->sendPacket(pkt);
}

bool DiscoveryManager::handleBeacon(const std::vector<uint8_t>& packet, uint32_t deviceId) {
	if (packet.size() < 1 + 4) {
		return false;
	}
	
	uint32_t id = ((uint32_t)packet[1] << 24) | ((uint32_t)packet[2] << 16) | 
	              ((uint32_t)packet[3] << 8) | packet[4];
	
	if (id == deviceId) {
		return false; // Beacon de nous-même, ignorer
	}
	
	// RSSI désactivé temporairement
	upsertDiscovered(id, -100, 0.0);
	
	Serial.print("[BEACON] Device ajouté/mis à jour: 0x");
	Serial.println(id, HEX);
	
	return true;
}

void DiscoveryManager::printDiscoveredIfDue() {
	if (!pairingMode) return;
	
	const unsigned long now = millis();
	if (now - lastDiscoveryPrintMs < DISCOVERY_PRINT_INTERVAL_MS) return;
	
	lastDiscoveryPrintMs = now;
	purgeDiscovered();
	
	Serial.println("[PAIR] Devices en mode pairing détectés:");
	if (discovered.empty()) {
		Serial.println("  (aucun)");
		return;
	}
	
	Serial.print("[PAIR] Debug: ");
	Serial.print(discovered.size());
	Serial.println(" device(s) trouvé(s)");
	
	for (auto &d : discovered) {
		Serial.print("  0x");
		Serial.print(d.id, HEX);
		Serial.print(" | RSSI/SNR: N/A");
		Serial.print(" | Vu il y a ");
		Serial.print((now - d.lastSeenMs) / 1000);
		Serial.println("s");
	}
}

