#ifndef DISCOVERY_MANAGER_H
#define DISCOVERY_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <cstdint>
#include "../Config.h"
#include "../protocol/PacketTypes.h"
#include "../lora/LoRaModule.h"

struct DiscoveredDevice {
	uint32_t id;
	int rssi;
	float snr;
	unsigned long lastSeenMs;
};

class DiscoveryManager {
public:
	// Utilise les constantes de Config.h : BEACON_INTERVAL_MS, DISCOVERY_DISPLAY_MS, DISCOVERY_TTL_MS
	static const unsigned long DISCOVERY_PRINT_INTERVAL_MS = DISCOVERY_DISPLAY_MS;
	
	DiscoveryManager(LoRaModule* lora);
	
	// Gestion du mode pairing
	void setPairingMode(bool enabled) { pairingMode = enabled; }
	bool isPairingMode() const { return pairingMode; }
	
	// Envoi de beacons
	void sendBeaconIfDue(uint32_t deviceId);
	
	// Réception de beacons
	bool handleBeacon(const std::vector<uint8_t>& packet, uint32_t deviceId);
	
	// Affichage des devices découverts
	void printDiscoveredIfDue();
	
	// Liste des devices découverts
	const std::vector<DiscoveredDevice>& getDiscoveredDevices() const { return discovered; }
	
private:
	LoRaModule* lora;
	bool pairingMode;
	unsigned long lastBeaconMs;
	unsigned long lastDiscoveryPrintMs;
	std::vector<DiscoveredDevice> discovered;
	
	void upsertDiscovered(uint32_t id, int rssi, float snr);
	void purgeDiscovered();
};

#endif // DISCOVERY_MANAGER_H

