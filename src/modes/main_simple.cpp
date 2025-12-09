#include <Arduino.h>
#include <HardwareSerial.h>
#include <LoRa_E220.h>
#include "../lora/LoRaConfig.h"
#include "../protocol/MessageProtocol.h"

#ifdef USE_ENCRYPTION
#include "../security/Encryption.h"
#endif

#ifdef USE_HUMAN_SENSOR_24GHZ
#include "../sensors/HumanSensor24GHz.h"
#endif

// HardwareSerial pour E220 (UART2 sur ESP32)
HardwareSerial SerialE220(2);

// Objet LoRa_E220 avec tous les pins
// Les pins sont définis dans LoRaConfig.h
LoRa_E220 e220ttl(&SerialE220, PIN_LORA_AUX, PIN_LORA_M0, PIN_LORA_M1);

// Les constantes de configuration sont définies dans LoRaConfig.h
// CONFIG_ADDH, CONFIG_ADDL, CONFIG_CHAN sont déjà définis

// Variables pour gestion PING/PONG
uint32_t lastPingTimestamp = 0;
bool waitingForPong = false;

#ifdef USE_HUMAN_SENSOR_24GHZ
// HardwareSerial pour le capteur humain (UART1 sur ESP32)
HardwareSerial SerialSensor(1);

// Objet capteur humain 24GHz
HumanSensor24GHz humanSensor(&SerialSensor, 1000);

// Variables pour l'envoi automatique
uint32_t lastSensorSendTime = 0;
uint8_t lastSentHumanCount = 255; // Valeur invalide pour forcer le premier envoi

// Flag pour activer/désactiver l'envoi automatique
bool autoSendEnabled = (HUMAN_SENSOR_AUTO_SEND_INTERVAL > 0);
#endif

void configureModule() {
	Serial.println("[LoRa] Configuration du module...");
	
	// Mettre en mode configuration
	e220ttl.setMode(MODE_3_CONFIGURATION);
	delay(300);
	
	// Lire la configuration actuelle
	ResponseStructContainer c = e220ttl.getConfiguration();
	if (c.status.getResponseDescription() == "Success") {
		Configuration configuration = *(Configuration*)c.data;
		
		// Afficher la configuration actuelle
		float currentFreq = calculateFrequency900MHz(configuration.CHAN);
		Serial.print("[LoRa] Configuration actuelle: CHAN=");
		Serial.print(configuration.CHAN);
		Serial.print(" -> ");
		Serial.print(currentFreq, 3);
		Serial.println(" MHz");
		
	// Configurer pour mode transparent
	configuration.ADDH = CONFIG_ADDH;
	configuration.ADDL = CONFIG_ADDL;
	configuration.CHAN = CONFIG_CHAN_E220; // Canal configuré
		configuration.SPED.airDataRate = AIR_DATA_RATE_010_24; // 2.4kbps
		configuration.SPED.uartBaudRate = UART_BPS_9600;   // 9600 bauds
		configuration.SPED.uartParity = MODE_00_8N1;           // 8N1
		configuration.OPTION.transmissionPower = POWER_22;      // 22dBm (max)
		configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_DISABLED;
		configuration.TRANSMISSION_MODE.fixedTransmission = FT_TRANSPARENT_TRANSMISSION;
		configuration.TRANSMISSION_MODE.enableRSSI = RSSI_DISABLED;
		configuration.TRANSMISSION_MODE.enableLBT = LBT_DISABLED;
		configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;
		
	// Afficher la nouvelle fréquence configurée
	float newFreq = calculateFrequency900MHz(CONFIG_CHAN_E220);
	Serial.print("[LoRa] Configuration: CHAN=");
	Serial.print(CONFIG_CHAN_E220);
		Serial.print(" -> ");
		Serial.print(newFreq, 3);
		Serial.println(" MHz");
		
		// Sauvegarder la configuration
		ResponseStatus rs = e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
		if (rs.getResponseDescription() == "Success") {
			Serial.println("[LoRa] Configuration sauvegardée avec succès!");
		} else {
			Serial.print("[LoRa] Erreur sauvegarde: ");
			Serial.println(rs.getResponseDescription());
		}
		
		c.close();
	} else {
		Serial.println("[LoRa] Erreur lecture configuration");
	}
	
	// Revenir en mode normal
	e220ttl.setMode(MODE_0_NORMAL);
	delay(200);
	Serial.println("[LoRa] Module en mode normal (prêt à envoyer/recevoir)");
}

void setup() {
	Serial.begin(115200);
	while (!Serial) {}
	
	Serial.println();
	Serial.println("========================================");
	Serial.println("  MODE SIMPLE - Lecture Broadcast LoRa");
#ifdef USE_HUMAN_SENSOR_24GHZ
	Serial.println("  + Capteur Humain 24GHz");
#endif
	Serial.println("========================================");
	
	// Initialiser le HardwareSerial pour E220
	SerialE220.begin(9600, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
	// Étend le délai d'attente inter-caractères pour éviter la troncature des messages reçus
	SerialE220.setTimeout(600);
	delay(500);
	
	// Initialiser le module E220
	Serial.println("[LoRa] Initialisation...");
	if (!e220ttl.begin()) {
		Serial.println("[LoRa] ERREUR: Echec initialisation!");
		Serial.println("Vérifiez:");
		Serial.println("  - Connexions RX/TX (croisées)");
		Serial.println("  - Alimentation 3.3V");
		Serial.println("  - Antenne connectée");
		while (true) {
			delay(1000);
		}
	}
	
	Serial.println("[LoRa] Module initialisé");
	delay(300);
	
	// Configurer le module
	configureModule();
	
	// Afficher la fréquence finale configurée
	float finalFreq = calculateFrequency900MHz(CONFIG_CHAN_E220);
	Serial.println();
	Serial.print("[LoRa] Fréquence configurée: ");
	Serial.print(finalFreq, 3);
	Serial.print(" MHz (CHAN=");
	Serial.print(CONFIG_CHAN_E220);
	Serial.println(")");
	Serial.println("Mode: Réception et envoi de broadcasts");
	
#ifdef USE_HUMAN_SENSOR_24GHZ
	// Initialiser le capteur humain 24GHz
	Serial.println();
	Serial.println("[INIT] Initialisation capteur humain 24GHz...");
	if (humanSensor.begin()) {
		Serial.println("[INIT] ✓ Capteur humain prêt");
		if (autoSendEnabled) {
			Serial.print("[INIT] Envoi automatique activé (toutes les ");
			Serial.print(HUMAN_SENSOR_AUTO_SEND_INTERVAL / 1000);
			Serial.println(" secondes)");
		} else {
			Serial.println("[INIT] Envoi automatique désactivé (envoi manuel uniquement)");
		}
	} else {
		Serial.println("[INIT] ⚠ Erreur initialisation capteur");
	}
#endif
	
	Serial.println();
	Serial.println("Commandes:");
	Serial.println("  - Tapez un message et appuyez sur Entrée pour l'envoyer");
	Serial.println("  - Les messages reçus s'affichent automatiquement");
#ifdef USE_HUMAN_SENSOR_24GHZ
	Serial.println("  - HUMAN_COUNT : Envoyer le comptage actuel du capteur");
	Serial.println("  - SENSOR_TEST : Tester le capteur (lecture brute)");
	Serial.println("  - AUTO_ON/OFF : Activer/désactiver l'envoi automatique");
#endif
	Serial.println("========================================");
	Serial.println();
}

void loop() {
#ifdef USE_HUMAN_SENSOR_24GHZ
	// Mettre à jour le capteur humain
	humanSensor.update();
	
	// Envoi automatique des données du capteur (si activé)
	if (autoSendEnabled && HUMAN_SENSOR_AUTO_SEND_INTERVAL > 0) {
		if (millis() - lastSensorSendTime >= HUMAN_SENSOR_AUTO_SEND_INTERVAL) {
			lastSensorSendTime = millis();
			
			uint8_t currentCount = humanSensor.getHumanCount();
			lastSentHumanCount = currentCount;
			
			// Récupérer toutes les données des cibles
			int16_t x[3], y[3], speed[3];
			uint16_t resolution[3];
			humanSensor.getAllTargetsData(x, y, speed, resolution);
			
			// Encoder le message avec toutes les données
			uint8_t buffer[PROTOCOL_MAX_MSG_SIZE];
			uint16_t msgSize = MessageProtocol::encodeSensorDataMessage(
				DEVICE_ID, currentCount, x, y, speed, resolution, buffer
			);
			
			// Créer le buffer final avec magic number
			uint8_t finalBuffer[PROTOCOL_MAX_MSG_SIZE];
			uint16_t finalLen = 0;
			
#ifdef USE_ENCRYPTION
			uint8_t encryptedBuffer[PROTOCOL_MAX_MSG_SIZE];
			uint16_t encryptedLen;
			if (Encryption::encrypt(buffer, msgSize, encryptedBuffer, &encryptedLen)) {
				finalBuffer[0] = MAGIC_NUM_ENCRYPTED;
				memcpy(finalBuffer + 1, encryptedBuffer, encryptedLen);
				finalLen = 1 + encryptedLen;
				
				ResponseStatus rs = e220ttl.sendMessage(finalBuffer, finalLen);
				if (rs.getResponseDescription() == "Success") {
					Serial.print("[AUTO] 📡 Capteur: ");
					Serial.print(currentCount);
					Serial.print(currentCount > 1 ? " cibles" : (currentCount == 1 ? " cible" : " cible"));
					Serial.print(" détaillées | ");
					Serial.print(finalLen);
					Serial.println(" bytes");
				}
			}
#else
			finalBuffer[0] = MAGIC_NUM_CLEAR;
			memcpy(finalBuffer + 1, buffer, msgSize);
			finalLen = 1 + msgSize;
			
			ResponseStatus rs = e220ttl.sendMessage(finalBuffer, finalLen);
			if (rs.getResponseDescription() == "Success") {
				Serial.print("[AUTO] 📡 Capteur: ");
				Serial.print(currentCount);
				Serial.print(currentCount > 1 ? " cibles" : (currentCount == 1 ? " cible" : " cible"));
				Serial.print(" détaillées | ");
				Serial.print(finalLen);
				Serial.println(" bytes");
			}
#endif
		}
	}
#endif

	// Vérifier si on reçoit un message
	if (e220ttl.available() > 0) {
		// Lire directement les octets bruts pour éviter la troncature sur les octets nuls
		uint8_t buffer[PROTOCOL_MAX_MSG_SIZE];
		int bytesRead = 0;
		
		// Attendre un peu pour que tous les octets arrivent
		delay(50);
		
		// Lire tous les octets disponibles
		while (SerialE220.available() && bytesRead < PROTOCOL_MAX_MSG_SIZE) {
			buffer[bytesRead++] = SerialE220.read();
		}
		
		if (bytesRead > 0) {
#ifdef USE_CUSTOM_PROTOCOL
			// Vérifier le magic number
			if (bytesRead < 4) {
				Serial.println("[RX] Message trop court, ignoré");
				return;
			}
			
			uint8_t magicNum = buffer[0];
			uint8_t* messageData = buffer + 1; // Données après le magic number
			uint16_t messageLen = bytesRead - 1;
			
			uint8_t* dataToProcess = messageData;
			uint16_t dataLen = messageLen;
			
			// DEBUG: Afficher les données brutes reçues
			Serial.print("[DEBUG] Magic: 0x");
			if (magicNum < 0x10) Serial.print("0");
			Serial.print(magicNum, HEX);
			Serial.print(" | Données (HEX): ");
			for (int i = 0; i < messageLen && i < 16; i++) {
				if (messageData[i] < 0x10) Serial.print("0");
				Serial.print(messageData[i], HEX);
				Serial.print(" ");
			}
			Serial.println();
			
			if (magicNum == MAGIC_NUM_ENCRYPTED) {
				// Message chiffré - déchiffrer
				Serial.println("[RX] Message CHIFFRÉ détecté");
				uint8_t decryptedBuffer[PROTOCOL_MAX_MSG_SIZE];
				uint16_t decryptedLen;
				
				if (Encryption::decrypt(messageData, messageLen, decryptedBuffer, &decryptedLen)) {
					dataToProcess = decryptedBuffer;
					dataLen = decryptedLen;
					Serial.print("[ENCRYPTION] Déchiffré (");
					Serial.print(messageLen);
					Serial.print(" → ");
					Serial.print(decryptedLen);
					Serial.println(" bytes)");
					
					// DEBUG: Afficher les données déchiffrées
					Serial.print("[DEBUG] Déchiffré (HEX): ");
					for (int i = 0; i < decryptedLen && i < 16; i++) {
						if (decryptedBuffer[i] < 0x10) Serial.print("0");
						Serial.print(decryptedBuffer[i], HEX);
						Serial.print(" ");
					}
					Serial.println();
				} else {
					Serial.println("[ENCRYPTION] ERREUR: Échec du déchiffrement!");
					Serial.println("[INFO] Message ignoré (clé ou mode incompatible)");
					return;
				}
			} else if (magicNum == MAGIC_NUM_CLEAR) {
				// Message en clair
				Serial.println("[RX] Message EN CLAIR détecté");
			} else {
				// Magic number inconnu - peut-être ancien format sans magic number
				Serial.print("[RX] Magic number inconnu (0x");
				if (magicNum < 0x10) Serial.print("0");
				Serial.print(magicNum, HEX);
				Serial.println(") - tentative de décodage direct");
				// Essayer de décoder le message complet (avec le magic number)
				dataToProcess = buffer;
				dataLen = bytesRead;
			}
			
			// Décoder le message avec le protocole
			ProtocolMessage msg;
			if (MessageProtocol::decodeMessage(dataToProcess, dataLen, &msg)) {
				Serial.print("[RX] Message protocole reçu (");
				Serial.print(bytesRead);
				Serial.println(" bytes):");
				MessageProtocol::printMessage(&msg, "[RX]   ");
				
			// Gestion automatique PING/PONG
			if (msg.type == MSG_TYPE_PING && msg.dataSize >= 4) {
				// Répondre automatiquement avec un PONG
				uint8_t pongBuffer[PROTOCOL_MAX_MSG_SIZE];
				uint16_t pongSize = MessageProtocol::encodePongMessage(DEVICE_ID, msg.data, pongBuffer);
				
				// Créer le buffer final avec magic number
				uint8_t finalPongBuffer[PROTOCOL_MAX_MSG_SIZE];
				uint16_t finalPongLen = 0;
				
#ifdef USE_ENCRYPTION
				// Chiffrer le PONG avant envoi
				uint8_t encryptedPong[PROTOCOL_MAX_MSG_SIZE];
				uint16_t encryptedPongLen;
				if (Encryption::encrypt(pongBuffer, pongSize, encryptedPong, &encryptedPongLen)) {
					// Ajouter magic number + données chiffrées
					finalPongBuffer[0] = MAGIC_NUM_ENCRYPTED;
					memcpy(finalPongBuffer + 1, encryptedPong, encryptedPongLen);
					finalPongLen = 1 + encryptedPongLen;
					
					ResponseStatus rs = e220ttl.sendMessage(finalPongBuffer, finalPongLen);
					if (rs.getResponseDescription() == "Success") {
						Serial.println("[PING/PONG] Réponse PONG chiffrée envoyée");
					}
				}
#else
				// PONG en clair
				finalPongBuffer[0] = MAGIC_NUM_CLEAR;
				memcpy(finalPongBuffer + 1, pongBuffer, pongSize);
				finalPongLen = 1 + pongSize;
				
				ResponseStatus rs = e220ttl.sendMessage(finalPongBuffer, finalPongLen);
				if (rs.getResponseDescription() == "Success") {
					Serial.println("[PING/PONG] Réponse PONG envoyée");
				}
#endif
				} else if (msg.type == MSG_TYPE_PONG && msg.dataSize >= 4 && waitingForPong) {
					// Calculer le RTT
					uint32_t originalTimestamp = msg.data[0] | (msg.data[1] << 8) | (msg.data[2] << 16) | (msg.data[3] << 24);
					uint32_t rtt = millis() - originalTimestamp;
					Serial.print("[PING/PONG] RTT: ");
					Serial.print(rtt);
					Serial.println(" ms");
					waitingForPong = false;
				}
			} else {
				// Message non protocole ou invalide
				Serial.print("[RX] Message brut: ");
				for (int i = 0; i < bytesRead; i++) {
					if (buffer[i] >= 32 && buffer[i] < 127) {
						Serial.print((char)buffer[i]);
					} else {
						Serial.print(".");
					}
				}
				Serial.print(" (");
				Serial.print(bytesRead);
				Serial.println(" bytes)");
			}
#else
			// Mode texte brut
			Serial.print("[RX] Broadcast reçu: ");
			for (int i = 0; i < bytesRead; i++) {
				Serial.print((char)buffer[i]);
			}
			Serial.print(" (");
			Serial.print(bytesRead);
			Serial.println(" caractères)");
#endif
		}
	}
	
	// Gérer les commandes série pour l'envoi
	if (Serial.available()) {
		String line = Serial.readStringUntil('\n');
		line.trim();
		
		if (line.length() > 0) {
#ifdef USE_CUSTOM_PROTOCOL
			// Parser la commande pour le protocole
			// Format: [900/433/ALL] TYPE param1 param2...
			// Exemples:
			//   TEMP 25.3
			//   900 TEMP 25.3  (le préfixe 900/433/ALL est ignoré en mode simple)
			//   HUMAN 1
			//   TEXT Bonjour
			//   PING
			
			uint8_t buffer[PROTOCOL_MAX_MSG_SIZE];
			uint16_t msgSize = 0;
			
			line.toUpperCase();
			
			// Ignorer le préfixe de module si présent (900/433/ALL)
			// Cela permet la compatibilité avec le GUI qui envoie toujours un préfixe
			if (line.startsWith("900 ") || line.startsWith("433 ") || line.startsWith("ALL ")) {
				int spaceIndex = line.indexOf(' ');
				if (spaceIndex != -1) {
					line = line.substring(spaceIndex + 1);
					line.trim();
				}
			}
			
			if (line.startsWith("TEMP ")) {
				// Température: TEMP 25.3
				float temp = line.substring(5).toFloat();
				msgSize = MessageProtocol::encodeTempMessage(DEVICE_ID, temp, buffer);
				Serial.print("[TX] Envoi température: ");
				Serial.print(temp, 1);
				Serial.println(" °C");
			}
			else if (line.startsWith("ENV")) {
				// ENV <tempC> <pression_hPa> [humidité]
				String args = line.substring(3);
				args.trim();
				if (args.length() == 0) {
					Serial.println("[TX] ENV requiert au moins température et pression");
				} else {
					int firstSpace = args.indexOf(' ');
					if (firstSpace == -1) {
						Serial.println("[TX] Format: ENV <tempC> <pression_hPa> [humidité]");
					} else {
						float temp = args.substring(0, firstSpace).toFloat();
						String remaining = args.substring(firstSpace + 1);
						remaining.trim();
						int secondSpace = remaining.indexOf(' ');
						float pressure = 0.0f;
						float humidity = -1.0f;
						if (secondSpace == -1) {
							pressure = remaining.toFloat();
						} else {
							pressure = remaining.substring(0, secondSpace).toFloat();
							humidity = remaining.substring(secondSpace + 1).toFloat();
						}
						
						msgSize = MessageProtocol::encodeEnvironmentMessage(DEVICE_ID, temp, pressure, humidity, buffer);
						Serial.print("[TX] Envoi ENV: ");
						Serial.print(temp, 1);
						Serial.print(" °C | ");
						Serial.print(pressure, 1);
						Serial.print(" hPa");
						if (humidity >= 0.0f) {
							Serial.print(" | ");
							Serial.print(humidity, 0);
							Serial.print(" %RH");
						}
						Serial.println();
					}
				}
			}
			else if (line.startsWith("HUMAN ")) {
				// Détection humaine: HUMAN 1 ou HUMAN 0
				bool detected = line.substring(6).toInt() != 0;
				msgSize = MessageProtocol::encodeHumanDetectMessage(DEVICE_ID, detected, buffer);
				Serial.print("[TX] Envoi détection humaine: ");
				Serial.println(detected ? "OUI" : "NON");
			}
			else if (line.startsWith("HUMAN_COUNT")) {
				// Comptage humain: HUMAN_COUNT [nombre]
				uint8_t count = 0;
#ifdef USE_HUMAN_SENSOR_24GHZ
				if (line == "HUMAN_COUNT") {
					// Pas de paramètre: lire depuis le capteur
					count = humanSensor.getHumanCount();
					Serial.print("[TX] Envoi comptage capteur: ");
				} else if (line.startsWith("HUMAN_COUNT ")) {
					// Avec paramètre: utiliser la valeur fournie
					count = line.substring(12).toInt();
					Serial.print("[TX] Envoi comptage manuel: ");
				}
#else
				// Sans capteur: utiliser la valeur fournie ou 0
				if (line.startsWith("HUMAN_COUNT ")) {
					count = line.substring(12).toInt();
					Serial.print("[TX] Envoi comptage: ");
				} else {
					Serial.println("[TX] HUMAN_COUNT nécessite une valeur (ex: HUMAN_COUNT 3)");
					msgSize = 0; // Pas d'envoi
				}
#endif
				if (msgSize == 0 && count >= 0) { // Si pas d'erreur
					msgSize = MessageProtocol::encodeHumanCountMessage(DEVICE_ID, count, buffer);
					Serial.print(count);
					Serial.println(count > 1 ? " humains" : " humain");
				}
			}
#ifdef USE_HUMAN_SENSOR_24GHZ
			else if (line == "SENSOR_TEST") {
				// Tester le capteur
				humanSensor.test();
				msgSize = 0; // Pas d'envoi LoRa
			}
			else if (line == "AUTO_ON") {
				// Activer l'envoi automatique
				autoSendEnabled = true;
				Serial.println("[SENSOR] Envoi automatique ACTIVÉ");
				msgSize = 0; // Pas d'envoi LoRa
			}
			else if (line == "AUTO_OFF") {
				// Désactiver l'envoi automatique
				autoSendEnabled = false;
				Serial.println("[SENSOR] Envoi automatique DÉSACTIVÉ");
				msgSize = 0; // Pas d'envoi LoRa
			}
#endif
			else if (line.startsWith("TEXT ")) {
				// Message texte: TEXT Bonjour
				String text = line.substring(5);
				msgSize = MessageProtocol::encodeTextMessage(DEVICE_ID, text.c_str(), buffer);
				Serial.print("[TX] Envoi texte: ");
				Serial.println(text);
			}
			else if (line == "PING") {
				// Ping
				msgSize = MessageProtocol::encodePingMessage(DEVICE_ID, buffer);
				lastPingTimestamp = millis();
				waitingForPong = true;
				Serial.println("[TX] Envoi PING (attente PONG...)");
			}
			else {
				// Par défaut, envoyer comme texte
				msgSize = MessageProtocol::encodeTextMessage(DEVICE_ID, line.c_str(), buffer);
				Serial.print("[TX] Envoi texte (auto): ");
				Serial.println(line);
			}
			
		if (msgSize > 0) {
			// Créer le buffer final avec magic number
			uint8_t finalBuffer[PROTOCOL_MAX_MSG_SIZE];
			uint16_t finalLen = 0;
			
#ifdef USE_ENCRYPTION
			// Message chiffré
			uint8_t encryptedBuffer[PROTOCOL_MAX_MSG_SIZE];
			uint16_t encryptedLen;
			
			if (Encryption::encrypt(buffer, msgSize, encryptedBuffer, &encryptedLen)) {
				// Ajouter magic number + données chiffrées
				finalBuffer[0] = MAGIC_NUM_ENCRYPTED;
				memcpy(finalBuffer + 1, encryptedBuffer, encryptedLen);
				finalLen = 1 + encryptedLen;
				
				Serial.print("[ENCRYPTION] ");
				Serial.print(msgSize);
				Serial.print(" → ");
				Serial.print(encryptedLen);
				Serial.print(" bytes | ");
				
				// Envoyer le message chiffré
				ResponseStatus rs = e220ttl.sendMessage(finalBuffer, finalLen);
				if (rs.getResponseDescription() == "Success") {
					Serial.print("[TX] OK (");
					Serial.print(finalLen);
					Serial.println(" bytes totaux)");
				} else {
					Serial.print("[TX] ERREUR: ");
					Serial.println(rs.getResponseDescription());
				}
			} else {
				Serial.println("[ENCRYPTION] ERREUR: Échec du chiffrement!");
			}
#else
			// Message en clair
			finalBuffer[0] = MAGIC_NUM_CLEAR;
			memcpy(finalBuffer + 1, buffer, msgSize);
			finalLen = 1 + msgSize;
			
			// Envoyer le message en clair
			ResponseStatus rs = e220ttl.sendMessage(finalBuffer, finalLen);
			if (rs.getResponseDescription() == "Success") {
				Serial.print("[TX] OK (");
				Serial.print(finalLen);
				Serial.println(" bytes)");
			} else {
				Serial.print("[TX] ERREUR: ");
				Serial.println(rs.getResponseDescription());
			}
#endif
		}
#else
			// Mode texte brut
			// Ignorer le préfixe de module si présent (900/433/ALL)
			line.toUpperCase();
			if (line.startsWith("900 ") || line.startsWith("433 ") || line.startsWith("ALL ")) {
				int spaceIndex = line.indexOf(' ');
				if (spaceIndex != -1) {
					String prefix = line.substring(0, spaceIndex);
					line = line.substring(spaceIndex + 1);
					line.trim();
					Serial.print("[INFO] Préfixe ");
					Serial.print(prefix);
					Serial.println(" ignoré en mode simple");
				}
			}
			
			Serial.print("[TX] Envoi broadcast: ");
			Serial.println(line);
			
			ResponseStatus rs = e220ttl.sendMessage(line);
			if (rs.getResponseDescription() == "Success") {
				Serial.println("[TX] Message envoyé avec succès");
			} else {
				Serial.print("[TX] Erreur: ");
				Serial.println(rs.getResponseDescription());
			}
#endif
		}
	}
	
	delay(10); // Petit délai pour éviter la surcharge CPU
}
