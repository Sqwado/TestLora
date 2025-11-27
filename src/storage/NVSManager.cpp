#include "NVSManager.h"

const char* NVSManager::NVS_NAMESPACE = "lora_pair";

NVSManager::NVSManager() {
}

NVSManager::~NVSManager() {
	if (nvs.isKey("")) { // Vérifier si ouvert
		nvs.end();
	}
}

bool NVSManager::begin() {
	return nvs.begin(NVS_NAMESPACE, false);
}

void NVSManager::end() {
	nvs.end();
}

bool NVSManager::savePairingState(const uint8_t* sessionKey, size_t keyLen, bool isPaired) {
	if (!begin()) {
		Serial.println("[NVS] Erreur ouverture NVS");
		return false;
	}
	
	if (keyLen == 16) {
		nvs.putBytes("sessionKey", sessionKey, 16);
	}
	nvs.putBool("isPaired", isPaired);
	end();
	
	Serial.println("[NVS] État appairage sauvegardé");
	return true;
}

bool NVSManager::loadPairingState(uint8_t* sessionKey, size_t keyLen, bool& isPaired) {
	if (!begin()) {
		Serial.println("[NVS] Impossible d'ouvrir NVS, état d'appairage réinitialisé");
		isPaired = false;
		if (keyLen == 16) {
			memset(sessionKey, 0, 16);
		}
		return false;
	}
	
	// Par défaut, on considère qu'on est appairé
	isPaired = true;
	
	if (keyLen == 16) {
		if (nvs.isKey("sessionKey")) {
			size_t len = nvs.getBytesLength("sessionKey");
			if (len == 16) {
				nvs.getBytes("sessionKey", sessionKey, 16);
				
				// Vérifier que la clé n'est pas toute nulle
				bool keyValid = false;
				for (int i = 0; i < 16; i++) {
					if (sessionKey[i] != 0) {
						keyValid = true;
						break;
					}
				}
				
				if (keyValid) {
					bool savedState = nvs.getBool("isPaired", true);
					if (!savedState) {
						nvs.putBool("isPaired", true);
						Serial.println("[NVS] État d'appairage corrigé (clé valide trouvée)");
					} else {
						Serial.println("[NVS] Appairage confirmé (clé de session valide)");
					}
				} else {
					Serial.println("[NVS] Clé de session invalide (toute nulle), état d'appairage: Non appairé");
					isPaired = false;
					memset(sessionKey, 0, 16);
					nvs.putBool("isPaired", false);
				}
			} else {
				Serial.println("[NVS] Taille de clé incorrecte, état d'appairage: Non appairé");
				isPaired = false;
				memset(sessionKey, 0, 16);
				nvs.putBool("isPaired", false);
			}
		} else {
			Serial.println("[NVS] Clé de session manquante, état d'appairage: Non appairé");
			isPaired = false;
			memset(sessionKey, 0, 16);
			nvs.putBool("isPaired", false);
		}
	}
	
	end();
	return isPaired;
}

bool NVSManager::clearPairingState() {
	if (!begin()) {
		Serial.println("[NVS] Erreur ouverture NVS");
		return false;
	}
	
	nvs.remove("sessionKey");
	nvs.remove("isPaired");
	end();
	
	Serial.println("[NVS] Appairage effacé");
	return true;
}

bool NVSManager::loadDeviceId(uint32_t& deviceId) {
	const uint32_t DEFAULT_DEVICE_ID = 0xA1B2C3D4;
	
	if (!begin()) {
		Serial.println("[NVS] Erreur ouverture NVS pour Device ID");
		return false;
	}
	
	if (nvs.isKey("deviceId")) {
		uint32_t savedId = nvs.getUInt("deviceId", DEFAULT_DEVICE_ID);
		if (savedId != DEFAULT_DEVICE_ID) {
			deviceId = savedId;
			Serial.print("[NVS] Device ID restauré depuis NVS: 0x");
			Serial.println(deviceId, HEX);
			end();
			return true;
		}
	}
	
	end();
	
	// Générer un nouveau Device ID unique basé sur l'ESP32 MAC address
	uint8_t mac[6];
	esp_read_mac(mac, ESP_MAC_WIFI_STA);
	
	uint32_t newId = 0;
	newId |= ((uint32_t)mac[2] << 24);
	newId |= ((uint32_t)mac[3] << 16);
	newId |= ((uint32_t)mac[4] << 8);
	newId |= (uint32_t)mac[5];
	
	uint32_t randomPart = esp_random();
	newId ^= randomPart;
	
	if (newId == DEFAULT_DEVICE_ID) {
		newId ^= 0x12345678;
	}
	
	deviceId = newId;
	
	if (begin()) {
		nvs.putUInt("deviceId", deviceId);
		end();
	}
	
	Serial.print("[NVS] Nouveau Device ID généré et sauvegardé: 0x");
	Serial.println(deviceId, HEX);
	Serial.println("[NVS] Ce Device ID sera conservé entre les redémarrages");
	
	return true;
}

bool NVSManager::saveDeviceId(uint32_t deviceId) {
	if (!begin()) {
		return false;
	}
	
	nvs.putUInt("deviceId", deviceId);
	end();
	return true;
}

