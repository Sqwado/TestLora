#include <Arduino.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <LoRa.h>
#include <LoRa_E220.h>
#include "../lora/LoRaConfig.h"
#include "../lora/LoRaConfig_XL1278.h"

#ifdef USE_CUSTOM_PROTOCOL
#include "../protocol/MessageProtocol.h"
#endif

#ifdef USE_ENCRYPTION
#include "../security/Encryption.h"
#endif

// ============================================
// MODE DUAL - Utilisation simultanée des deux modules
// ============================================
// E220-900T22D (900 MHz) sur UART
// XL1278-SMT (433 MHz) sur SPI

// HardwareSerial pour E220 (UART2 sur ESP32)
HardwareSerial SerialE220(2);

// Objet LoRa_E220 avec tous les pins
LoRa_E220 e220ttl(&SerialE220, PIN_LORA_AUX, PIN_LORA_M0, PIN_LORA_M1);

// Variables globales pour XL1278
uint8_t receivedBuffer433[PROTOCOL_MAX_MSG_SIZE];
int receivedBytes433 = 0;
bool messageReceived433 = false;

// Variables pour gestion PING/PONG
uint32_t lastPingTimestamp900 = 0;
bool waitingForPong900 = false;
uint32_t lastPingTimestamp433 = 0;
bool waitingForPong433 = false;

// Callback pour réception de messages sur XL1278 (433 MHz)
void onReceive433(int packetSize) {
    if (packetSize == 0) return;
    
    // Lire les octets bruts dans le buffer
    receivedBytes433 = 0;
    while (LoRa.available() && receivedBytes433 < PROTOCOL_MAX_MSG_SIZE) {
        receivedBuffer433[receivedBytes433++] = (uint8_t)LoRa.read();
    }
    messageReceived433 = true;
}

void configureE220() {
    Serial.println("[900MHz] Configuration du module E220...");
    
    // Mettre en mode configuration
    e220ttl.setMode(MODE_3_CONFIGURATION);
    delay(300);
    
    // Lire la configuration actuelle
    ResponseStructContainer c = e220ttl.getConfiguration();
    if (c.status.getResponseDescription() == "Success") {
        Configuration configuration = *(Configuration*)c.data;
        
        // Afficher la configuration actuelle
        float currentFreq = calculateFrequency900MHz(configuration.CHAN);
        Serial.print("[900MHz] Configuration actuelle: CHAN=");
        Serial.print(configuration.CHAN);
        Serial.print(" -> ");
        Serial.print(currentFreq, 3);
        Serial.println(" MHz");
        
        // Configurer pour mode transparent
        configuration.ADDH = CONFIG_ADDH;
        configuration.ADDL = CONFIG_ADDL;
        configuration.CHAN = CONFIG_CHAN;
        configuration.SPED.airDataRate = AIR_DATA_RATE_010_24;
        configuration.SPED.uartBaudRate = UART_BPS_9600;
        configuration.SPED.uartParity = MODE_00_8N1;
        configuration.OPTION.transmissionPower = POWER_22;
        configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_DISABLED;
        configuration.TRANSMISSION_MODE.fixedTransmission = FT_TRANSPARENT_TRANSMISSION;
        configuration.TRANSMISSION_MODE.enableRSSI = RSSI_DISABLED;
        configuration.TRANSMISSION_MODE.enableLBT = LBT_DISABLED;
        configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;
        
        // Afficher la nouvelle fréquence configurée
        float newFreq = calculateFrequency900MHz(CONFIG_CHAN);
        Serial.print("[900MHz] Nouvelle config: CHAN=");
        Serial.print(CONFIG_CHAN);
        Serial.print(" -> ");
        Serial.print(newFreq, 3);
        Serial.println(" MHz");
        
        // Sauvegarder la configuration
        ResponseStatus rs = e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
        if (rs.getResponseDescription() == "Success") {
            Serial.println("[900MHz] Configuration sauvegardée avec succès!");
        } else {
            Serial.print("[900MHz] Erreur sauvegarde: ");
            Serial.println(rs.getResponseDescription());
        }
        
        c.close();
    } else {
        Serial.println("[900MHz] Erreur lecture configuration");
    }
    
    // Revenir en mode normal
    e220ttl.setMode(MODE_0_NORMAL);
    delay(200);
    Serial.println("[900MHz] Module en mode normal");
}

void configureXL1278() {
    Serial.println("[433MHz] Configuration du module XL1278...");
    
    // Configuration des paramètres LoRa
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setTxPower(LORA_TX_POWER_XL);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    
    Serial.println("[433MHz] Paramètres configurés:");
    Serial.print("  - Fréquence: ");
    Serial.print(LORA_FREQUENCY / 1E6);
    Serial.println(" MHz");
    Serial.print("  - Bande passante: ");
    Serial.print(LORA_BANDWIDTH / 1E3);
    Serial.println(" kHz");
    Serial.print("  - Spreading Factor: SF");
    Serial.println(LORA_SPREADING_FACTOR);
    Serial.print("  - Puissance TX: ");
    Serial.print(LORA_TX_POWER_XL);
    Serial.println(" dBm");
    Serial.println("[433MHz] Module configuré avec succès!");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {}
    
    Serial.println();
    Serial.println("========================================");
    Serial.println("  MODE DUAL - Deux modules LoRa");
    Serial.println("  E220 (900 MHz) + XL1278 (433 MHz)");
    Serial.println("========================================");
    
    // ====================================
    // Initialiser le module E220 (900 MHz)
    // ====================================
    Serial.println();
    Serial.println("[900MHz] === Initialisation E220-900T22D ===");
    SerialE220.begin(9600, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
    SerialE220.setTimeout(600);
    delay(500);
    
    Serial.println("[900MHz] Initialisation...");
    if (!e220ttl.begin()) {
        Serial.println("[900MHz] ERREUR: Echec initialisation!");
        Serial.println("[900MHz] Le système continuera avec le module 433 MHz uniquement");
    } else {
        Serial.println("[900MHz] Module initialisé");
        delay(300);
        configureE220();
        
        float finalFreq = calculateFrequency900MHz(CONFIG_CHAN);
        Serial.print("[900MHz] Fréquence finale: ");
        Serial.print(finalFreq, 3);
        Serial.println(" MHz");
    }
    
    // ====================================
    // Initialiser le module XL1278 (433 MHz)
    // ====================================
    Serial.println();
    Serial.println("[433MHz] === Initialisation XL1278-SMT ===");
    SPI.begin(PIN_LORA_SCLK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_SS);
    LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);
    
    Serial.println("[433MHz] Initialisation...");
    if (!LoRa.begin(LORA_FREQUENCY)) {
        Serial.println("[433MHz] ERREUR: Échec initialisation!");
        Serial.println("[433MHz] Le système continuera avec le module 900 MHz uniquement");
    } else {
        Serial.println("[433MHz] Module initialisé");
        delay(300);
        configureXL1278();
        
        // Activer le callback de réception
        LoRa.onReceive(onReceive433);
        LoRa.receive();
        
        Serial.print("[433MHz] Fréquence finale: ");
        Serial.print(LORA_FREQUENCY / 1E6);
        Serial.println(" MHz");
    }
    
    // ====================================
    // Résumé et commandes
    // ====================================
    Serial.println();
    Serial.println("========================================");
    Serial.println("Système LoRa Dual-Band initialisé!");
    Serial.println();
#ifdef USE_CUSTOM_PROTOCOL
    Serial.println("PROTOCOLE PERSONNALISÉ ACTIF");
    Serial.println("Device ID: " + String(DEVICE_ID));
    Serial.println();
    Serial.println("Commandes:");
    Serial.println("  900 TEMP 25.3        - Température sur 900 MHz");
    Serial.println("  433 HUMAN 1          - Détection humaine sur 433 MHz");
    Serial.println("  ALL TEXT Bonjour     - Texte sur les deux");
    Serial.println("  900 PING             - Ping sur 900 MHz");
    Serial.println("  433 message          - Auto-texte sur 433 MHz");
#else
    Serial.println("Commandes:");
    Serial.println("  900 <message>  - Envoyer sur 900 MHz");
    Serial.println("  433 <message>  - Envoyer sur 433 MHz");
    Serial.println("  ALL <message>  - Envoyer sur les deux");
#endif
    Serial.println();
    Serial.println("Les messages reçus indiquent leur provenance:");
    Serial.println("  [RX-900MHz] ou [RX-433MHz]");
    Serial.println("========================================");
    Serial.println();
}

void loop() {
    // ====================================
    // Vérifier réception sur E220 (900 MHz)
    // ====================================
    if (e220ttl.available() > 0) {
        // Lire directement les octets bruts pour éviter la troncature
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
                Serial.println("[900MHz] Message trop court, ignoré");
                return;
            }
            
            uint8_t magicNum = buffer[0];
            uint8_t* messageData = buffer + 1; // Données après le magic number
            uint16_t messageLen = bytesRead - 1;
            
            uint8_t* dataToProcess900 = messageData;
            uint16_t dataLen900 = messageLen;
            
            // DEBUG: Afficher les données brutes reçues
            Serial.print("[900MHz DEBUG] Magic: 0x");
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
                Serial.println("[900MHz] Message CHIFFRÉ détecté");
                uint8_t decryptedBuffer900[PROTOCOL_MAX_MSG_SIZE];
                uint16_t decryptedLen900;
                
                if (Encryption::decrypt(messageData, messageLen, decryptedBuffer900, &decryptedLen900)) {
                    dataToProcess900 = decryptedBuffer900;
                    dataLen900 = decryptedLen900;
                    Serial.print("[900MHz ENCRYPTION] Déchiffré (");
                    Serial.print(messageLen);
                    Serial.print(" → ");
                    Serial.print(decryptedLen900);
                    Serial.println(" bytes)");
                    
                    // DEBUG: Afficher les données déchiffrées
                    Serial.print("[900MHz DEBUG] Déchiffré (HEX): ");
                    for (int i = 0; i < decryptedLen900 && i < 16; i++) {
                        if (decryptedBuffer900[i] < 0x10) Serial.print("0");
                        Serial.print(decryptedBuffer900[i], HEX);
                        Serial.print(" ");
                    }
                    Serial.println();
                } else {
                    Serial.println("[900MHz ENCRYPTION] ERREUR: Échec déchiffrement!");
                    Serial.println("[900MHz] Message ignoré (clé ou mode incompatible)");
                    return;
                }
            } else if (magicNum == MAGIC_NUM_CLEAR) {
                // Message en clair
                Serial.println("[900MHz] Message EN CLAIR détecté");
            } else {
                // Magic number inconnu - peut-être ancien format sans magic number
                Serial.print("[900MHz] Magic number inconnu (0x");
                if (magicNum < 0x10) Serial.print("0");
                Serial.print(magicNum, HEX);
                Serial.println(") - tentative de décodage direct");
                // Essayer de décoder le message complet (avec le magic number)
                dataToProcess900 = buffer;
                dataLen900 = bytesRead;
            }
            
            ProtocolMessage msg;
            if (MessageProtocol::decodeMessage(dataToProcess900, dataLen900, &msg)) {
                Serial.print("[RX-900MHz] Message protocole (");
                Serial.print(bytesRead);
                Serial.println(" bytes):");
                MessageProtocol::printMessage(&msg, "[RX-900MHz]   ");
                
                // Gestion automatique PING/PONG
                if (msg.type == MSG_TYPE_PING && msg.dataSize >= 4) {
                    uint8_t pongBuffer[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t pongSize = MessageProtocol::encodePongMessage(DEVICE_ID, msg.data, pongBuffer);
                    
#ifdef USE_ENCRYPTION
                    // Chiffrer le PONG avant envoi
                    uint8_t encryptedPong900[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t encryptedPongLen900;
                    if (Encryption::encrypt(pongBuffer, pongSize, encryptedPong900, &encryptedPongLen900)) {
                        ResponseStatus rs = e220ttl.sendMessage(encryptedPong900, encryptedPongLen900);
                        if (rs.getResponseDescription() == "Success") {
                            Serial.println("[900MHz PING/PONG] Réponse PONG chiffrée envoyée");
                        }
                    }
#else
                    ResponseStatus rs = e220ttl.sendMessage(pongBuffer, pongSize);
                    if (rs.getResponseDescription() == "Success") {
                        Serial.println("[900MHz PING/PONG] Réponse PONG envoyée");
                    }
#endif
                } else if (msg.type == MSG_TYPE_PONG && msg.dataSize >= 4 && waitingForPong900) {
                    uint32_t originalTimestamp = msg.data[0] | (msg.data[1] << 8) | (msg.data[2] << 16) | (msg.data[3] << 24);
                    uint32_t rtt = millis() - originalTimestamp;
                    Serial.print("[900MHz PING/PONG] RTT: ");
                    Serial.print(rtt);
                    Serial.println(" ms");
                    waitingForPong900 = false;
                }
            } else {
                Serial.print("[RX-900MHz] Message brut: ");
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
            Serial.print("[RX-900MHz] ");
            for (int i = 0; i < bytesRead; i++) {
                Serial.print((char)buffer[i]);
            }
            Serial.print(" (");
            Serial.print(bytesRead);
            Serial.println(" chars)");
#endif
        }
    }
    
    // ====================================
    // Vérifier réception sur XL1278 (433 MHz)
    // ====================================
    if (messageReceived433) {
        messageReceived433 = false;
        
        int rssi = LoRa.packetRssi();
        float snr = LoRa.packetSnr();
        
#ifdef USE_CUSTOM_PROTOCOL
        // Vérifier le magic number
        if (receivedBytes433 < 4) {
            Serial.println("[433MHz] Message trop court, ignoré");
            LoRa.receive();
            return;
        }
        
        uint8_t magicNum = receivedBuffer433[0];
        uint8_t* messageData = receivedBuffer433 + 1; // Données après le magic number
        uint16_t messageLen = receivedBytes433 - 1;
        
        uint8_t* dataToProcess433 = messageData;
        uint16_t dataLen433 = messageLen;
        
        // DEBUG: Afficher les données brutes reçues
        Serial.print("[433MHz DEBUG] Magic: 0x");
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
            Serial.println("[433MHz] Message CHIFFRÉ détecté");
            uint8_t decryptedBuffer433[PROTOCOL_MAX_MSG_SIZE];
            uint16_t decryptedLen433;
            
            if (Encryption::decrypt(messageData, messageLen, decryptedBuffer433, &decryptedLen433)) {
                dataToProcess433 = decryptedBuffer433;
                dataLen433 = decryptedLen433;
                Serial.print("[433MHz ENCRYPTION] Déchiffré (");
                Serial.print(messageLen);
                Serial.print(" → ");
                Serial.print(decryptedLen433);
                Serial.println(" bytes)");
                
                // DEBUG: Afficher les données déchiffrées
                Serial.print("[433MHz DEBUG] Déchiffré (HEX): ");
                for (int i = 0; i < decryptedLen433 && i < 16; i++) {
                    if (decryptedBuffer433[i] < 0x10) Serial.print("0");
                    Serial.print(decryptedBuffer433[i], HEX);
                    Serial.print(" ");
                }
                Serial.println();
            } else {
                Serial.println("[433MHz ENCRYPTION] ERREUR: Échec déchiffrement!");
                Serial.println("[433MHz] Message ignoré (clé ou mode incompatible)");
                LoRa.receive();
                return;
            }
        } else if (magicNum == MAGIC_NUM_CLEAR) {
            // Message en clair
            Serial.println("[433MHz] Message EN CLAIR détecté");
        } else {
            // Magic number inconnu - peut-être ancien format sans magic number
            Serial.print("[433MHz] Magic number inconnu (0x");
            if (magicNum < 0x10) Serial.print("0");
            Serial.print(magicNum, HEX);
            Serial.println(") - tentative de décodage direct");
            // Essayer de décoder le message complet (avec le magic number)
            dataToProcess433 = receivedBuffer433;
            dataLen433 = receivedBytes433;
        }
        
        ProtocolMessage msg;
        if (MessageProtocol::decodeMessage(dataToProcess433, dataLen433, &msg)) {
            Serial.print("[RX-433MHz] Message protocole (");
            Serial.print(receivedBytes433);
            Serial.print(" bytes, RSSI: ");
            Serial.print(rssi);
            Serial.print(" dBm, SNR: ");
            Serial.print(snr);
            Serial.println(" dB):");
            MessageProtocol::printMessage(&msg, "[RX-433MHz]   ");
            
            // Gestion automatique PING/PONG
            if (msg.type == MSG_TYPE_PING && msg.dataSize >= 4) {
                uint8_t pongBuffer[PROTOCOL_MAX_MSG_SIZE];
                uint16_t pongSize = MessageProtocol::encodePongMessage(DEVICE_ID, msg.data, pongBuffer);
                
#ifdef USE_ENCRYPTION
                // Chiffrer le PONG avant envoi
                uint8_t encryptedPong433[PROTOCOL_MAX_MSG_SIZE];
                uint16_t encryptedPongLen433;
                if (Encryption::encrypt(pongBuffer, pongSize, encryptedPong433, &encryptedPongLen433)) {
                    LoRa.beginPacket();
                    LoRa.write(encryptedPong433, encryptedPongLen433);
                    bool success = LoRa.endPacket();
                    if (success) {
                        Serial.println("[433MHz PING/PONG] Réponse PONG chiffrée envoyée");
                    }
                    LoRa.receive();
                }
#else
                LoRa.beginPacket();
                LoRa.write(pongBuffer, pongSize);
                bool success = LoRa.endPacket();
                if (success) {
                    Serial.println("[433MHz PING/PONG] Réponse PONG envoyée");
                }
                LoRa.receive();
#endif
            } else if (msg.type == MSG_TYPE_PONG && msg.dataSize >= 4 && waitingForPong433) {
                uint32_t originalTimestamp = msg.data[0] | (msg.data[1] << 8) | (msg.data[2] << 16) | (msg.data[3] << 24);
                uint32_t rtt = millis() - originalTimestamp;
                Serial.print("[433MHz PING/PONG] RTT: ");
                Serial.print(rtt);
                Serial.println(" ms");
                waitingForPong433 = false;
            }
        } else {
            Serial.print("[RX-433MHz] Message brut: ");
            for (int i = 0; i < receivedBytes433; i++) {
                if (receivedBuffer433[i] >= 32 && receivedBuffer433[i] < 127) {
                    Serial.print((char)receivedBuffer433[i]);
                } else {
                    Serial.print(".");
                }
            }
            Serial.print(" (");
            Serial.print(receivedBytes433);
            Serial.print(" bytes, RSSI: ");
            Serial.print(rssi);
            Serial.print(" dBm, SNR: ");
            Serial.print(snr);
            Serial.println(" dB)");
        }
#else
        Serial.print("[RX-433MHz] ");
        for (int i = 0; i < receivedBytes433; i++) {
            Serial.print((char)receivedBuffer433[i]);
        }
        Serial.print(" (");
        Serial.print(receivedBytes433);
        Serial.print(" chars, RSSI: ");
        Serial.print(rssi);
        Serial.print(" dBm, SNR: ");
        Serial.print(snr);
        Serial.println(" dB)");
#endif
        
        // Retourner en mode réception
        LoRa.receive();
    }
    
    // ====================================
    // Gérer les commandes série pour l'envoi
    // ====================================
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        
        if (line.length() > 0) {
            // Extraire la commande et le message
            int spaceIndex = line.indexOf(' ');
            if (spaceIndex == -1) {
#ifdef USE_CUSTOM_PROTOCOL
                Serial.println("[ERREUR] Format: 900/433/ALL TEMP/HUMAN/TEXT/PING [params]");
#else
                Serial.println("[ERREUR] Format: 900/433/ALL <message>");
#endif
                return;
            }
            
            String cmd = line.substring(0, spaceIndex);
            String message = line.substring(spaceIndex + 1);
            cmd.toUpperCase();
            
#ifdef USE_CUSTOM_PROTOCOL
            // Encoder le message avec le protocole
            uint8_t buffer[PROTOCOL_MAX_MSG_SIZE];
            uint16_t msgSize = 0;
            
            message.toUpperCase();
            
            if (message.startsWith("TEMP ")) {
                float temp = message.substring(5).toFloat();
                msgSize = MessageProtocol::encodeTempMessage(DEVICE_ID, temp, buffer);
                Serial.print("Température: ");
                Serial.print(temp, 1);
                Serial.println(" °C");
            }
            else if (message.startsWith("HUMAN ")) {
                bool detected = message.substring(6).toInt() != 0;
                msgSize = MessageProtocol::encodeHumanDetectMessage(DEVICE_ID, detected, buffer);
                Serial.print("Détection humaine: ");
                Serial.println(detected ? "OUI" : "NON");
            }
            else if (message.startsWith("TEXT ")) {
                String text = message.substring(5);
                msgSize = MessageProtocol::encodeTextMessage(DEVICE_ID, text.c_str(), buffer);
                Serial.print("Texte: ");
                Serial.println(text);
            }
            else if (message == "PING") {
                msgSize = MessageProtocol::encodePingMessage(DEVICE_ID, buffer);
                Serial.println("PING (attente PONG...)");
                // Enregistrer selon le module cible
                if (cmd == "900") {
                    lastPingTimestamp900 = millis();
                    waitingForPong900 = true;
                } else if (cmd == "433") {
                    lastPingTimestamp433 = millis();
                    waitingForPong433 = true;
                } else if (cmd == "ALL") {
                    lastPingTimestamp900 = millis();
                    waitingForPong900 = true;
                    lastPingTimestamp433 = millis();
                    waitingForPong433 = true;
                }
            }
            else {
                // Par défaut, texte
                msgSize = MessageProtocol::encodeTextMessage(DEVICE_ID, message.c_str(), buffer);
                Serial.print("Texte (auto): ");
                Serial.println(message);
            }
#endif
            
            if (cmd == "900") {
                // Envoyer sur 900 MHz
                Serial.print("[TX-900MHz] ");
#ifdef USE_CUSTOM_PROTOCOL
                if (msgSize > 0) {
                    // Créer le buffer final avec magic number
                    uint8_t finalBuffer900[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t finalLen900 = 0;
                    
#ifdef USE_ENCRYPTION
                    // Message chiffré
                    uint8_t encryptedBuffer900[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t encryptedLen900;
                    
                    if (Encryption::encrypt(buffer, msgSize, encryptedBuffer900, &encryptedLen900)) {
                        // Ajouter magic number + données chiffrées
                        finalBuffer900[0] = MAGIC_NUM_ENCRYPTED;
                        memcpy(finalBuffer900 + 1, encryptedBuffer900, encryptedLen900);
                        finalLen900 = 1 + encryptedLen900;
                        
                        Serial.print("[CHIFFRÉ] ");
                        Serial.print(msgSize);
                        Serial.print(" → ");
                        Serial.print(encryptedLen900);
                        Serial.print(" bytes | ");
                        
                        ResponseStatus rs = e220ttl.sendMessage(finalBuffer900, finalLen900);
                        if (rs.getResponseDescription() == "Success") {
                            Serial.print("OK (");
                            Serial.print(finalLen900);
                            Serial.println(" bytes totaux)");
                        } else {
                            Serial.print("ERREUR: ");
                            Serial.println(rs.getResponseDescription());
                        }
                    } else {
                        Serial.println("[ENCRYPTION] ERREUR chiffrement!");
                    }
#else
                    // Message en clair
                    finalBuffer900[0] = MAGIC_NUM_CLEAR;
                    memcpy(finalBuffer900 + 1, buffer, msgSize);
                    finalLen900 = 1 + msgSize;
                    
                    Serial.print("[CLAIR] ");
                    ResponseStatus rs = e220ttl.sendMessage(finalBuffer900, finalLen900);
                    if (rs.getResponseDescription() == "Success") {
                        Serial.print("OK (");
                        Serial.print(finalLen900);
                        Serial.println(" bytes)");
                    } else {
                        Serial.print("ERREUR: ");
                        Serial.println(rs.getResponseDescription());
                    }
#endif
                }
#else
                Serial.println(message);
                ResponseStatus rs = e220ttl.sendMessage(message);
                if (rs.getResponseDescription() == "Success") {
                    Serial.println("OK");
                } else {
                    Serial.print("ERREUR: ");
                    Serial.println(rs.getResponseDescription());
                }
#endif
                
            } else if (cmd == "433") {
                // Envoyer sur 433 MHz
                Serial.print("[TX-433MHz] ");
#ifdef USE_CUSTOM_PROTOCOL
                if (msgSize > 0) {
                    // Créer le buffer final avec magic number
                    uint8_t finalBuffer433[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t finalLen433 = 0;
                    
#ifdef USE_ENCRYPTION
                    // Message chiffré
                    uint8_t encryptedBuffer433[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t encryptedLen433;
                    
                    if (Encryption::encrypt(buffer, msgSize, encryptedBuffer433, &encryptedLen433)) {
                        // Ajouter magic number + données chiffrées
                        finalBuffer433[0] = MAGIC_NUM_ENCRYPTED;
                        memcpy(finalBuffer433 + 1, encryptedBuffer433, encryptedLen433);
                        finalLen433 = 1 + encryptedLen433;
                        
                        Serial.print("[CHIFFRÉ] ");
                        Serial.print(msgSize);
                        Serial.print(" → ");
                        Serial.print(encryptedLen433);
                        Serial.print(" bytes | ");
                        
                        LoRa.beginPacket();
                        LoRa.write(finalBuffer433, finalLen433);
                        bool success = LoRa.endPacket();
                        if (success) {
                            Serial.print("OK (");
                            Serial.print(finalLen433);
                            Serial.println(" bytes totaux)");
                        } else {
                            Serial.println("ERREUR");
                        }
                    } else {
                        Serial.println("[ENCRYPTION] ERREUR chiffrement!");
                    }
#else
                    // Message en clair
                    finalBuffer433[0] = MAGIC_NUM_CLEAR;
                    memcpy(finalBuffer433 + 1, buffer, msgSize);
                    finalLen433 = 1 + msgSize;
                    
                    Serial.print("[CLAIR] ");
                    LoRa.beginPacket();
                    LoRa.write(finalBuffer433, finalLen433);
                    bool success = LoRa.endPacket();
                    if (success) {
                        Serial.print("OK (");
                        Serial.print(finalLen433);
                        Serial.println(" bytes)");
                    } else {
                        Serial.println("ERREUR");
                    }
#endif
                }
#else
                Serial.println(message);
                LoRa.beginPacket();
                LoRa.print(message);
                bool success = LoRa.endPacket();
                if (success) {
                    Serial.println("OK");
                } else {
                    Serial.println("ERREUR");
                }
#endif
                LoRa.receive();
                
            } else if (cmd == "ALL") {
                // Envoyer sur les deux bandes
                Serial.print("[TX-DUAL] ");
#ifdef USE_CUSTOM_PROTOCOL
                if (msgSize > 0) {
                    // Créer le buffer final avec magic number
                    uint8_t finalBufferAll[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t finalLenAll = 0;
                    
#ifdef USE_ENCRYPTION
                    // Message chiffré
                    uint8_t encryptedBufferAll[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t encryptedLenAll;
                    
                    if (Encryption::encrypt(buffer, msgSize, encryptedBufferAll, &encryptedLenAll)) {
                        // Ajouter magic number + données chiffrées
                        finalBufferAll[0] = MAGIC_NUM_ENCRYPTED;
                        memcpy(finalBufferAll + 1, encryptedBufferAll, encryptedLenAll);
                        finalLenAll = 1 + encryptedLenAll;
                        
                        Serial.print("[CHIFFRÉ] ");
                        Serial.print(msgSize);
                        Serial.print(" → ");
                        Serial.print(encryptedLenAll);
                        Serial.print(" bytes | ");
                        
                        // 900 MHz
                        ResponseStatus rs900 = e220ttl.sendMessage(finalBufferAll, finalLenAll);
                        if (rs900.getResponseDescription() == "Success") {
                            Serial.print("900MHz OK (");
                            Serial.print(finalLenAll);
                            Serial.print(" bytes) | ");
                        } else {
                            Serial.print("900MHz ERREUR | ");
                        }
                        
                        // 433 MHz
                        LoRa.beginPacket();
                        LoRa.write(finalBufferAll, finalLenAll);
                        bool success433 = LoRa.endPacket();
                        if (success433) {
                            Serial.print("433MHz OK (");
                            Serial.print(finalLenAll);
                            Serial.println(" bytes)");
                        } else {
                            Serial.println("433MHz ERREUR");
                        }
                    } else {
                        Serial.println("[ENCRYPTION] ERREUR chiffrement!");
                    }
#else
                    // Message en clair
                    finalBufferAll[0] = MAGIC_NUM_CLEAR;
                    memcpy(finalBufferAll + 1, buffer, msgSize);
                    finalLenAll = 1 + msgSize;
                    
                    Serial.print("[CLAIR] ");
                    
                    // 900 MHz
                    ResponseStatus rs900 = e220ttl.sendMessage(finalBufferAll, finalLenAll);
                    if (rs900.getResponseDescription() == "Success") {
                        Serial.print("900MHz OK (");
                        Serial.print(finalLenAll);
                        Serial.print(" bytes) | ");
                    } else {
                        Serial.print("900MHz ERREUR | ");
                    }
                    
                    // 433 MHz
                    LoRa.beginPacket();
                    LoRa.write(finalBufferAll, finalLenAll);
                    bool success433 = LoRa.endPacket();
                    if (success433) {
                        Serial.print("433MHz OK (");
                        Serial.print(finalLenAll);
                        Serial.println(" bytes)");
                    } else {
                        Serial.println("433MHz ERREUR");
                    }
#endif
                }
#else
                Serial.println(message);
                // 900 MHz
                ResponseStatus rs900 = e220ttl.sendMessage(message);
                if (rs900.getResponseDescription() == "Success") {
                    Serial.println("900MHz OK");
                } else {
                    Serial.print("900MHz ERREUR: ");
                    Serial.println(rs900.getResponseDescription());
                }
                // 433 MHz
                LoRa.beginPacket();
                LoRa.print(message);
                bool success433 = LoRa.endPacket();
                if (success433) {
                    Serial.println("433MHz OK");
                } else {
                    Serial.println("433MHz ERREUR");
                }
#endif
                LoRa.receive();
                
            } else {
                Serial.println("[ERREUR] Commande inconnue. Utilisez: 900, 433, ou ALL");
            }
        }
    }
    
    delay(10);
}

