#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "../lora/LoRaConfig_XL1278.h"

// Variables globales
String receivedMessage = "";
bool messageReceived = false;

// Callback pour réception de messages
void onReceive(int packetSize) {
    if (packetSize == 0) return;
    
    receivedMessage = "";
    while (LoRa.available()) {
        receivedMessage += (char)LoRa.read();
    }
    messageReceived = true;
}

void configureModule() {
    Serial.println("[LoRa] Configuration du module XL1278-SMT...");
    
    // Configuration des paramètres LoRa
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setTxPower(LORA_TX_POWER);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    
    Serial.println("[LoRa] Paramètres configurés:");
    Serial.print("  - Fréquence: ");
    Serial.print(LORA_FREQUENCY / 1E6);
    Serial.println(" MHz");
    Serial.print("  - Bande passante: ");
    Serial.print(LORA_BANDWIDTH / 1E3);
    Serial.println(" kHz");
    Serial.print("  - Spreading Factor: SF");
    Serial.println(LORA_SPREADING_FACTOR);
    Serial.print("  - Coding Rate: 4/");
    Serial.println(LORA_CODING_RATE);
    Serial.print("  - Puissance TX: ");
    Serial.print(LORA_TX_POWER);
    Serial.println(" dBm");
    Serial.println("[LoRa] Module configuré avec succès!");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {}
    
    Serial.println();
    Serial.println("========================================");
    Serial.println("  MODE XL1278-SMT - 433 MHz");
    Serial.println("  Lecture et envoi de broadcasts LoRa");
    Serial.println("========================================");
    
    // Initialiser SPI
    SPI.begin(PIN_LORA_SCLK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_SS);
    
    // Initialiser LoRa
    LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);
    
    Serial.println("[LoRa] Initialisation...");
    Serial.print("[LoRa] Pins - SS:");
    Serial.print(PIN_LORA_SS);
    Serial.print(" RST:");
    Serial.print(PIN_LORA_RST);
    Serial.print(" DIO0:");
    Serial.println(PIN_LORA_DIO0);
    Serial.print("[LoRa] SPI - MOSI:");
    Serial.print(PIN_LORA_MOSI);
    Serial.print(" MISO:");
    Serial.print(PIN_LORA_MISO);
    Serial.print(" SCLK:");
    Serial.println(PIN_LORA_SCLK);
    
    if (!LoRa.begin(LORA_FREQUENCY)) {
        Serial.println("[LoRa] ERREUR: Échec initialisation!");
        Serial.println("Vérifiez:");
        Serial.println("  - Connexions SPI (MOSI, MISO, SCLK, NSS)");
        Serial.println("  - Connexion DIO0");
        Serial.println("  - Alimentation 3.3V");
        Serial.println("  - Antenne connectée");
        Serial.println();
        Serial.println("Pinout ESP32-38PIN → XL1278-SMT:");
        Serial.println("  GPIO23 (MOSI)  → MOSI");
        Serial.println("  GPIO19 (MISO)  → MISO");
        Serial.println("  GPIO18 (SCLK)  → SCLK");
        Serial.println("  GPIO5  (CS)    → NSS");
        Serial.println("  GPIO26         → DIO0");
        Serial.println("  GPIO14         → RST (optionnel)");
        Serial.println("  3.3V           → VCC");
        Serial.println("  GND            → GND");
        while (true) {
            delay(1000);
        }
    }
    
    Serial.println("[LoRa] Module initialisé");
    delay(300);
    
    // Configurer le module
    configureModule();
    
    // Activer le callback de réception
    LoRa.onReceive(onReceive);
    LoRa.receive(); // Passer en mode réception
    
    Serial.println();
    Serial.print("[LoRa] Fréquence configurée: ");
    Serial.print(LORA_FREQUENCY / 1E6);
    Serial.println(" MHz");
    Serial.println("Mode: Réception et envoi de broadcasts");
    Serial.println("Commandes:");
    Serial.println("  - Tapez un message et appuyez sur Entrée pour l'envoyer");
    Serial.println("  - Les messages reçus s'affichent automatiquement");
    Serial.println("========================================");
    Serial.println();
}

void loop() {
    // Vérifier si un message a été reçu
    if (messageReceived) {
        messageReceived = false;
        
        // Obtenir le RSSI du dernier paquet
        int rssi = LoRa.packetRssi();
        float snr = LoRa.packetSnr();
        
        Serial.print("[RX] Broadcast reçu: ");
        Serial.print(receivedMessage);
        Serial.print(" (");
        Serial.print(receivedMessage.length());
        Serial.print(" caractères, RSSI: ");
        Serial.print(rssi);
        Serial.print(" dBm, SNR: ");
        Serial.print(snr);
        Serial.println(" dB)");
        
        // Retourner en mode réception
        LoRa.receive();
    }
    
    // Gérer les commandes série pour l'envoi
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        
        if (line.length() > 0) {
            // Ignorer le préfixe de module si présent (900/433/ALL)
            // Cela permet la compatibilité avec le GUI qui envoie toujours un préfixe
            String lineUpper = line;
            lineUpper.toUpperCase();
            if (lineUpper.startsWith("900 ") || lineUpper.startsWith("433 ") || lineUpper.startsWith("ALL ")) {
                int spaceIndex = line.indexOf(' ');
                if (spaceIndex != -1) {
                    String prefix = line.substring(0, spaceIndex);
                    line = line.substring(spaceIndex + 1);
                    line.trim();
                    Serial.print("[INFO] Préfixe ");
                    Serial.print(prefix);
                    Serial.println(" ignoré en mode XL1278 simple");
                }
            }
            
            // Envoyer le message saisi
            Serial.print("[TX] Envoi broadcast: ");
            Serial.println(line);
            
            // Passer en mode émission
            LoRa.beginPacket();
            LoRa.print(line);
            bool success = LoRa.endPacket();
            
            if (success) {
                Serial.println("[TX] Message envoyé avec succès");
            } else {
                Serial.println("[TX] Erreur lors de l'envoi");
            }
            
            // Retourner en mode réception
            LoRa.receive();
        }
    }
    
    delay(10); // Petit délai pour éviter la surcharge CPU
}

