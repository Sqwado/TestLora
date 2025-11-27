#ifndef COMMON_H
#define COMMON_H

#include <Arduino.h>
#include "../Config.h"

/**
 * Fonctions utilitaires communes à tous les modes
 * Évite la duplication de code entre modes
 */

namespace Common {

/**
 * Affiche un header de démarrage formaté
 */
inline void printHeader(const char* modeName, bool hasSensor = false) {
    Serial.println();
    Serial.println("========================================");
    Serial.print("  ");
    Serial.println(modeName);
    if (hasSensor) {
        Serial.println("  + Capteur Humain 24GHz");
    }
    Serial.println("========================================");
}

/**
 * Affiche une erreur fatale et bloque
 */
inline void fatalError(const char* message) {
    Serial.println();
    Serial.print("[ERREUR FATALE] ");
    Serial.println(message);
    Serial.println("Système arrêté.");
    while (true) {
        delay(1000);
    }
}

/**
 * Parse un ID hexadécimal depuis une string
 */
inline uint32_t parseHexId(const String& hexStr) {
    String hex = hexStr;
    hex.trim();
    hex.toUpperCase();
    
    // Supprimer le préfixe 0x si présent
    if (hex.startsWith("0X")) {
        hex = hex.substring(2);
    }
    
    uint32_t id = 0;
    for (size_t i = 0; i < hex.length() && i < 8; i++) {
        char c = hex[i];
        uint8_t v = 0;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
        else continue;
        id = (id << 4) | v;
    }
    return id;
}

/**
 * Affiche la configuration système
 */
inline void printSystemInfo() {
    Serial.println();
    Serial.println("Configuration:");
    Serial.print("  Device ID    : 0x");
    Serial.println(DEVICE_ID, HEX);
    
    #ifdef MODULE_E220_900
    Serial.println("  Module       : E220-900 (900 MHz)");
    #elif defined(MODULE_XL1278_433)
    Serial.println("  Module       : XL1278 (433 MHz)");
    #elif defined(MODULE_DUAL)
    Serial.println("  Module       : DUAL (433+900 MHz)");
    #endif
    
    #ifdef MODE_SIMPLE
    Serial.println("  Mode         : Simple (Broadcast)");
    #else
    Serial.println("  Mode         : Complet (Appairage)");
    #endif
    
    #ifdef USE_ENCRYPTION
    Serial.println("  Chiffrement  : AES-128-CTR");
    #else
    Serial.println("  Chiffrement  : Désactivé");
    #endif
    
    #ifdef USE_HUMAN_SENSOR_24GHZ
    Serial.print("  Capteur 24GHz: Activé (auto: ");
    Serial.print(HUMAN_SENSOR_AUTO_SEND_INTERVAL);
    Serial.println("ms)");
    #else
    Serial.println("  Capteur 24GHz: Désactivé");
    #endif
    
    Serial.println("========================================");
}

/**
 * Affiche un message de debug formaté
 */
inline void debug(const char* category, const String& message) {
    #ifdef DEBUG_VERBOSE
    Serial.print("[");
    Serial.print(category);
    Serial.print("] ");
    Serial.println(message);
    #endif
}

/**
 * Affiche des bytes en hexadécimal (pour debug)
 */
inline void printHex(const uint8_t* data, size_t length, const char* label = nullptr) {
    #ifdef DEBUG_RAW_PACKETS
    if (label) {
        Serial.print(label);
        Serial.print(": ");
    }
    for (size_t i = 0; i < length; i++) {
        if (data[i] < 0x10) Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    #endif
}

} // namespace Common

#endif // COMMON_H

