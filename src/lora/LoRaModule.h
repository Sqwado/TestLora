#ifndef LORA_MODULE_H
#define LORA_MODULE_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <LoRa_E220.h>
#include <vector>
#include "../lora/LoRaConfig.h"

class LoRaModule {
public:
	LoRaModule();
	~LoRaModule();
	
	bool begin();
	bool configureForTransparentMode(bool forceConfig = false);
	bool sendPacket(const std::vector<uint8_t>& data);
	bool available();
	bool receiveMessage(std::vector<uint8_t>& buffer);
	
	void setMode(MODE_TYPE mode);
	MODE_TYPE getMode();
	
	void printConfiguration();
	
private:
	HardwareSerial* serial;
	LoRa_E220* e220ttl;
	
	bool readConfiguration(Configuration& config);
	bool writeConfiguration(const Configuration& config);
};

#endif // LORA_MODULE_H

