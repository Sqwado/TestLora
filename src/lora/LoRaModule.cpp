#include "../lora/LoRaModule.h"

LoRaModule::LoRaModule() {
	serial = new HardwareSerial(2);
	
	#if E220_PIN_MODE == MODE_MINIMAL
	e220ttl = new LoRa_E220(serial);
	#elif E220_PIN_MODE == MODE_RECOMMANDE
	e220ttl = new LoRa_E220(serial, PIN_LORA_AUX);
	#elif E220_PIN_MODE == MODE_COMPLET
	e220ttl = new LoRa_E220(serial, PIN_LORA_AUX, PIN_LORA_M0, PIN_LORA_M1);
	#endif
}

LoRaModule::~LoRaModule() {
	delete e220ttl;
	delete serial;
}

bool LoRaModule::begin() {
	serial->begin(9600, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
	delay(500);
	
	Serial.println("[LoRa] Initialisation du module E220...");
	if (!e220ttl->begin()) {
		Serial.println("[LoRa] ERREUR: Echec init E220. Vérifiez le câblage.");
		return false;
	}
	
	Serial.println("[LoRa] Module E220 initialisé avec succès");
	delay(300);
	
	#if E220_PIN_MODE == MODE_COMPLET
	e220ttl->setMode(MODE_3_CONFIGURATION);
	delay(300);
	
	bool configRead = false;
	Configuration currentConfig;
	for (int i = 0; i < 3 && !configRead; i++) {
		if (readConfiguration(currentConfig)) {
			printConfiguration();
			configRead = true;
		} else if (i < 2) {
			delay(200);
		}
	}
	
	if (configRead) {
		Serial.println("[LoRa] Vérification de la configuration...");
		if (configureForTransparentMode(false)) {
			Serial.println("[LoRa] Configuration vérifiée et prête");
		} else {
			Serial.println("[LoRa] ATTENTION: Problème lors de la configuration");
		}
	} else {
		Serial.println("[LoRa] ATTENTION: Configuration non lue");
	}
	
	e220ttl->setMode(MODE_0_NORMAL);
	delay(200);
	#else
	Serial.println("[LoRa] E220-900T22D initialisé");
	Serial.println("[LoRa] Note: Configuration non accessible (nécessite pins M0/M1)");
	#endif
	
	Serial.print("[LoRa] Mode pins: ");
	#if E220_PIN_MODE == MODE_MINIMAL
	Serial.println("MINIMAL (RX+TX seulement)");
	#elif E220_PIN_MODE == MODE_RECOMMANDE
	Serial.println("RECOMMANDE (RX+TX+AUX)");
	#elif E220_PIN_MODE == MODE_COMPLET
	Serial.println("COMPLET (tous les pins)");
	#endif
	
	return true;
}

bool LoRaModule::readConfiguration(Configuration& config) {
	ResponseStructContainer c = e220ttl->getConfiguration();
	if (c.status.getResponseDescription() == "Success") {
		config = *(Configuration*)c.data;
		c.close();
		return true;
	}
	c.close();
	return false;
}

bool LoRaModule::writeConfiguration(const Configuration& config) {
	ResponseStatus rs = e220ttl->setConfiguration(config, WRITE_CFG_PWR_DWN_SAVE);
	return (rs.getResponseDescription() == "Success");
}

void LoRaModule::printConfiguration() {
	Configuration config;
	if (!readConfiguration(config)) return;
	
	float actualFreq = calculateFrequency900MHz(config.CHAN);
	Serial.print("[LoRa] E220-900T22D @ ");
	Serial.print(actualFreq, 3);
	Serial.println("MHz");
	
	Serial.println("[LoRa] Configuration actuelle:");
	Serial.print("  ADDH: 0x"); Serial.println(config.ADDH, HEX);
	Serial.print("  ADDL: 0x"); Serial.println(config.ADDL, HEX);
	Serial.print("  CHAN: "); Serial.print(config.CHAN, DEC);
	Serial.print(" -> "); Serial.print(calculateFrequency900MHz(config.CHAN)); Serial.println(" MHz");
	Serial.print("  Air Data Rate: "); Serial.println(config.SPED.getAirDataRateDescription());
	Serial.print("  UART Baud: "); Serial.println(config.SPED.getUARTBaudRateDescription());
	Serial.print("  UART Parity: "); Serial.println(config.SPED.getUARTParityDescription());
	Serial.print("  Transmission Power: ");
	switch(config.OPTION.transmissionPower) {
		case 0: Serial.print("22dBm (POWER_22)"); break;
		case 1: Serial.print("17dBm (POWER_17)"); break;
		case 2: Serial.print("13dBm (POWER_13)"); break;
		case 3: Serial.print("10dBm (POWER_10)"); break;
		default: Serial.print(config.OPTION.transmissionPower); Serial.print(" (code inconnu)");
	}
	Serial.println();
	Serial.print("  Transmission Mode: ");
	Serial.println(config.TRANSMISSION_MODE.fixedTransmission == FT_TRANSPARENT_TRANSMISSION ? "Transparent" : "Fixed");
}

bool LoRaModule::configureForTransparentMode(bool forceConfig) {
	#if E220_PIN_MODE == MODE_COMPLET
	Serial.println("[LoRa] Configuration du module pour mode transparent...");
	
	e220ttl->setMode(MODE_3_CONFIGURATION);
	delay(300);
	
	Configuration configuration;
	bool configRead = false;
	for (int retry = 0; retry < 3 && !configRead; retry++) {
		if (readConfiguration(configuration)) {
			configRead = true;
		} else if (retry < 2) {
			delay(200);
		}
	}
	
	if (!configRead) {
		Serial.println("[LoRa] Erreur: Impossible de lire la configuration.");
		e220ttl->setMode(MODE_0_NORMAL);
		delay(100);
		return false;
	}
	
	bool needsUpdate = forceConfig;
	
	if (configuration.ADDH != CONFIG_ADDH || 
	    configuration.ADDL != CONFIG_ADDL || 
	    configuration.CHAN != CONFIG_CHAN_E220 ||
	    configuration.SPED.airDataRate != AIR_DATA_RATE_101_192 ||
	    configuration.SPED.uartBaudRate != UART_BPS_9600 ||
	    configuration.SPED.uartParity != MODE_00_8N1 ||
	    configuration.OPTION.transmissionPower != POWER_22 ||
	    configuration.TRANSMISSION_MODE.fixedTransmission != FT_TRANSPARENT_TRANSMISSION) {
		needsUpdate = true;
	}
	
	if (needsUpdate) {
		Serial.println("[LoRa] Mise à jour de la configuration...");
		
		configuration.ADDH = CONFIG_ADDH;
		configuration.ADDL = CONFIG_ADDL;
		configuration.CHAN = CONFIG_CHAN_E220;
		configuration.SPED.airDataRate = AIR_DATA_RATE_101_192;
		configuration.SPED.uartBaudRate = UART_BPS_9600;
		configuration.SPED.uartParity = MODE_00_8N1;
		configuration.OPTION.transmissionPower = POWER_22;
		configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_DISABLED;
		configuration.TRANSMISSION_MODE.fixedTransmission = FT_TRANSPARENT_TRANSMISSION;
		configuration.TRANSMISSION_MODE.enableRSSI = RSSI_DISABLED;
		configuration.TRANSMISSION_MODE.enableLBT = LBT_DISABLED;
		configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;
		
		bool configSaved = false;
		for (int retry = 0; retry < 3 && !configSaved; retry++) {
			if (writeConfiguration(configuration)) {
				Serial.println("[LoRa] Configuration sauvegardée avec succès!");
				configSaved = true;
			} else if (retry < 2) {
				Serial.print("[LoRa] Tentative "); Serial.print(retry + 1); Serial.println(" échouée, nouvelle tentative...");
				delay(200);
			}
		}
		
		if (!configSaved) {
			e220ttl->setMode(MODE_0_NORMAL);
			delay(100);
			return false;
		}
	} else {
		Serial.println("[LoRa] Configuration déjà correcte, pas de modification nécessaire.");
	}
	
	e220ttl->setMode(MODE_0_NORMAL);
	delay(200);
	
	return true;
	#else
	Serial.println("[LoRa] Configuration impossible: nécessite pins M0/M1 (mode COMPLET)");
	return false;
	#endif
}

bool LoRaModule::sendPacket(const std::vector<uint8_t>& data) {
	if (data.empty()) {
		Serial.println("[LoRa] Erreur: Tentative d'envoi d'un paquet vide");
		return false;
	}
	
	const size_t MAX_SEND_SIZE = 200;
	if (data.size() > MAX_SEND_SIZE) {
		Serial.print("[LoRa] ERREUR: Paquet trop grand (");
		Serial.print(data.size());
		Serial.print(" octets, max ");
		Serial.print(MAX_SEND_SIZE);
		Serial.println(" octets)");
		return false;
	}
	
	ResponseStatus rs = e220ttl->sendMessage(data.data(), (uint8_t)data.size());
	if (rs.getResponseDescription() != "Success") {
		Serial.print("[LoRa] Erreur envoi: ");
		Serial.println(rs.getResponseDescription());
		return false;
	}
	
	return true;
}

bool LoRaModule::available() {
	return e220ttl->available() > 0;
}

bool LoRaModule::receiveMessage(std::vector<uint8_t>& buffer) {
	if (!available()) return false;
	
	ResponseContainer rc = e220ttl->receiveMessageComplete(false);
	if (rc.status.getResponseDescription() != "Success") {
		return false;
	}
	
	buffer.clear();
	if (rc.data.length() > 0) {
		buffer.reserve(rc.data.length());
		for (size_t i = 0; i < rc.data.length(); ++i) {
			buffer.push_back((uint8_t)rc.data[i]);
		}
	}
	
	return !buffer.empty();
}

void LoRaModule::setMode(MODE_TYPE mode) {
	e220ttl->setMode(static_cast<MODE_TYPE>(mode));
}

MODE_TYPE LoRaModule::getMode() {
	// La bibliothèque ne fournit pas de getter direct, on assume le mode
	return MODE_0_NORMAL;
}

