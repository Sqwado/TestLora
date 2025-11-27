#include <Arduino.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <LoRa.h>
#include <LoRa_E220.h>
#include "../lora/LoRaConfig.h"
#include "../lora/LoRaConfig_XL1278.h"
#include "../protocol/MessageProtocol.h"
#include "../security/Encryption.h"

// ============================================
// MODE DUAL COMPLET
// ============================================
// E220-900T22D (900 MHz) : Mode COMPLET (avec appairage et sécurité)
// XL1278-SMT (433 MHz) : Mode SIMPLE (broadcast uniquement)
//
// Note : Le mode complet avec tous les managers (Discovery, Pairing, Security, etc.)
// nécessite une intégration plus poussée. Cette version utilise le E220 en mode
// transparent avec gestion basique.
//
// Pour un mode complet total, il faudrait intégrer :
// - DiscoveryManager, PairingManager, SecurityManager, etc.
// - Commandes : PAIR ON/OFF, LIST, B <id>, A, S <msg>, etc.
//
// TODO: Intégrer les managers du mode complet pour une sécurité complète

// HardwareSerial pour E220 (UART2 sur ESP32)
HardwareSerial SerialE220(2);

// Objet LoRa_E220
LoRa_E220 e220ttl(&SerialE220, PIN_LORA_AUX, PIN_LORA_M0, PIN_LORA_M1);

// Variables globales pour XL1278
uint8_t receivedBuffer433[PROTOCOL_MAX_MSG_SIZE];
int receivedBytes433 = 0;
bool messageReceived433 = false;

// Mode d'opération E220 (pour extensions futures)
enum E220Mode {
    E220_MODE_BROADCAST,    // Mode simple broadcast
    E220_MODE_PAIRED        // Mode appairé (TODO: à implémenter)
};

E220Mode e220Mode = E220_MODE_BROADCAST;

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
    Serial.println("[900MHz] Module en mode normal (mode complet)");
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
    Serial.println("  MODE DUAL COMPLET");
    Serial.println("  E220 (900 MHz) Mode COMPLET");
    Serial.println("  XL1278 (433 MHz) Mode SIMPLE");
    Serial.println("========================================");
    Serial.println();
    Serial.println("ATTENTION : Cette version utilise le E220 en mode");
    Serial.println("transparent étendu. Pour un mode complet avec");
    Serial.println("appairage/sécurité, l'intégration des managers");
    Serial.println("est nécessaire (TODO: future version).");
    Serial.println();
    
    // ====================================
    // Initialiser le module E220 (900 MHz)
    // ====================================
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
    Serial.println("Système LoRa Dual-Band (Mode Complet) initialisé!");
    Serial.println();
    Serial.println("Commandes:");
    Serial.println("  900 <message>  - Envoyer sur 900 MHz");
    Serial.println("  433 <message>  - Envoyer sur 433 MHz");
    Serial.println("  ALL <message>  - Envoyer sur les deux");
    Serial.println();
    Serial.println("Commandes futures (TODO) :");
    Serial.println("  PAIR ON/OFF    - Mode appairage (900 MHz)");
    Serial.println("  LIST           - Liste devices (900 MHz)");
    Serial.println("  B <id>         - Appairage (900 MHz)");
    Serial.println("  S <msg>        - Message sécurisé (900 MHz)");
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
        // Lire directement les octets bruts
        uint8_t buffer[PROTOCOL_MAX_MSG_SIZE];
        int bytesRead = 0;
        
        delay(50);
        
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
            uint8_t* messageData = buffer + 1;
            uint16_t messageLen = bytesRead - 1;
            
            uint8_t* dataToProcess900 = messageData;
            uint16_t dataLen900 = messageLen;
            
            if (magicNum == MAGIC_NUM_ENCRYPTED) {
                Serial.println("[900MHz] Message CHIFFRÉ détecté");
#ifdef USE_ENCRYPTION
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
                } else {
                    Serial.println("[900MHz ENCRYPTION] ERREUR: Échec déchiffrement!");
                    return;
                }
#else
                Serial.println("[900MHz] ERREUR: Message chiffré reçu mais encryption non activée!");
                return;
#endif
            } else if (magicNum == MAGIC_NUM_CLEAR) {
                Serial.println("[900MHz] Message EN CLAIR détecté");
            } else {
                Serial.print("[900MHz] Magic number inconnu (0x");
                if (magicNum < 0x10) Serial.print("0");
                Serial.print(magicNum, HEX);
                Serial.println(") - tentative de décodage direct");
                dataToProcess900 = buffer;
                dataLen900 = bytesRead;
            }
            
            ProtocolMessage msg;
            if (MessageProtocol::decodeMessage(dataToProcess900, dataLen900, &msg)) {
                Serial.print("[RX-900MHz] Message protocole (");
                Serial.print(bytesRead);
                Serial.println(" bytes):");
                MessageProtocol::printMessage(&msg, "[RX-900MHz]   ");
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
        uint8_t* messageData = receivedBuffer433 + 1;
        uint16_t messageLen = receivedBytes433 - 1;
        
        uint8_t* dataToProcess433 = messageData;
        uint16_t dataLen433 = messageLen;
        
        if (magicNum == MAGIC_NUM_ENCRYPTED) {
            Serial.println("[433MHz] Message CHIFFRÉ détecté");
#ifdef USE_ENCRYPTION
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
            } else {
                Serial.println("[433MHz ENCRYPTION] ERREUR: Échec déchiffrement!");
                LoRa.receive();
                return;
            }
#else
            Serial.println("[433MHz] ERREUR: Message chiffré reçu mais encryption non activée!");
            LoRa.receive();
            return;
#endif
        } else if (magicNum == MAGIC_NUM_CLEAR) {
            Serial.println("[433MHz] Message EN CLAIR détecté");
        } else {
            Serial.print("[433MHz] Magic number inconnu (0x");
            if (magicNum < 0x10) Serial.print("0");
            Serial.print(magicNum, HEX);
            Serial.println(") - tentative de décodage direct");
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
                Serial.println("[ERREUR] Format: 900/433/ALL <message>");
                Serial.println("        ou commandes: PAIR/LIST/B/A/S (TODO)");
                return;
            }
            
            String cmd = line.substring(0, spaceIndex);
            String message = line.substring(spaceIndex + 1);
            cmd.toUpperCase();
            
            if (cmd == "900") {
                // Envoyer sur 900 MHz
                Serial.print("[TX-900MHz] ");
#ifdef USE_CUSTOM_PROTOCOL
                // Parser et encoder selon le protocole
                uint8_t buffer[PROTOCOL_MAX_MSG_SIZE];
                uint16_t msgSize = MessageProtocol::encodeTextMessage(DEVICE_ID, message.c_str(), buffer);
                
                if (msgSize > 0) {
                    uint8_t finalBuffer[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t finalLen = 0;
                    
#ifdef USE_ENCRYPTION
                    uint8_t encryptedBuffer[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t encryptedLen;
                    
                    if (Encryption::encrypt(buffer, msgSize, encryptedBuffer, &encryptedLen)) {
                        finalBuffer[0] = MAGIC_NUM_ENCRYPTED;
                        memcpy(finalBuffer + 1, encryptedBuffer, encryptedLen);
                        finalLen = 1 + encryptedLen;
                        Serial.print("[CHIFFRÉ] ");
                    } else {
                        Serial.println("[ENCRYPTION] ERREUR!");
                        return;
                    }
#else
                    finalBuffer[0] = MAGIC_NUM_CLEAR;
                    memcpy(finalBuffer + 1, buffer, msgSize);
                    finalLen = 1 + msgSize;
                    Serial.print("[CLAIR] ");
#endif
                    ResponseStatus rs = e220ttl.sendMessage(finalBuffer, finalLen);
                    if (rs.getResponseDescription() == "Success") {
                        Serial.println("OK");
                    } else {
                        Serial.print("ERREUR: ");
                        Serial.println(rs.getResponseDescription());
                    }
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
                uint8_t buffer[PROTOCOL_MAX_MSG_SIZE];
                uint16_t msgSize = MessageProtocol::encodeTextMessage(DEVICE_ID, message.c_str(), buffer);
                
                if (msgSize > 0) {
                    uint8_t finalBuffer[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t finalLen = 0;
                    
#ifdef USE_ENCRYPTION
                    uint8_t encryptedBuffer[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t encryptedLen;
                    
                    if (Encryption::encrypt(buffer, msgSize, encryptedBuffer, &encryptedLen)) {
                        finalBuffer[0] = MAGIC_NUM_ENCRYPTED;
                        memcpy(finalBuffer + 1, encryptedBuffer, encryptedLen);
                        finalLen = 1 + encryptedLen;
                        Serial.print("[CHIFFRÉ] ");
                    } else {
                        Serial.println("[ENCRYPTION] ERREUR!");
                        LoRa.receive();
                        return;
                    }
#else
                    finalBuffer[0] = MAGIC_NUM_CLEAR;
                    memcpy(finalBuffer + 1, buffer, msgSize);
                    finalLen = 1 + msgSize;
                    Serial.print("[CLAIR] ");
#endif
                    LoRa.beginPacket();
                    LoRa.write(finalBuffer, finalLen);
                    bool success = LoRa.endPacket();
                    if (success) {
                        Serial.println("OK");
                    } else {
                        Serial.println("ERREUR");
                    }
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
                uint8_t buffer[PROTOCOL_MAX_MSG_SIZE];
                uint16_t msgSize = MessageProtocol::encodeTextMessage(DEVICE_ID, message.c_str(), buffer);
                
                if (msgSize > 0) {
                    uint8_t finalBuffer[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t finalLen = 0;
                    
#ifdef USE_ENCRYPTION
                    uint8_t encryptedBuffer[PROTOCOL_MAX_MSG_SIZE];
                    uint16_t encryptedLen;
                    
                    if (Encryption::encrypt(buffer, msgSize, encryptedBuffer, &encryptedLen)) {
                        finalBuffer[0] = MAGIC_NUM_ENCRYPTED;
                        memcpy(finalBuffer + 1, encryptedBuffer, encryptedLen);
                        finalLen = 1 + encryptedLen;
                        Serial.print("[CHIFFRÉ] ");
                    } else {
                        Serial.println("[ENCRYPTION] ERREUR!");
                        return;
                    }
#else
                    finalBuffer[0] = MAGIC_NUM_CLEAR;
                    memcpy(finalBuffer + 1, buffer, msgSize);
                    finalLen = 1 + msgSize;
                    Serial.print("[CLAIR] ");
#endif
                    // 900 MHz
                    ResponseStatus rs900 = e220ttl.sendMessage(finalBuffer, finalLen);
                    if (rs900.getResponseDescription() == "Success") {
                        Serial.print("900MHz OK | ");
                    } else {
                        Serial.print("900MHz ERREUR | ");
                    }
                    
                    // 433 MHz
                    LoRa.beginPacket();
                    LoRa.write(finalBuffer, finalLen);
                    bool success433 = LoRa.endPacket();
                    if (success433) {
                        Serial.println("433MHz OK");
                    } else {
                        Serial.println("433MHz ERREUR");
                    }
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
                Serial.println("[INFO] Commande non reconnue: " + cmd);
                Serial.println("[INFO] Commandes disponibles: 900, 433, ALL");
                Serial.println("[TODO] Commandes futures: PAIR, LIST, B, A, S");
            }
        }
    }
    
    delay(10);
}

