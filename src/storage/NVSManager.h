#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <Preferences.h>
#include <cstring>

class NVSManager {
public:
	static const char* NVS_NAMESPACE;
	
	NVSManager();
	~NVSManager();
	
	// Gestion de l'appairage
	bool savePairingState(const uint8_t* sessionKey, size_t keyLen, bool isPaired);
	bool loadPairingState(uint8_t* sessionKey, size_t keyLen, bool& isPaired);
	bool clearPairingState();
	
	// Gestion du Device ID
	bool loadDeviceId(uint32_t& deviceId);
	bool saveDeviceId(uint32_t deviceId);
	
private:
	Preferences nvs;
	bool begin();
	void end();
};

#endif // NVS_MANAGER_H

