#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================
// CONFIGURATION GÉNÉRALE DU SYSTÈME
// ============================================

// ============================================
// SÉLECTION DU MODULE (décommentez UNE ligne)
// ============================================
#define MODULE_E220_900     // E220-900T22D (UART, 850-930 MHz) uniquement
// #define MODULE_XL1278_433   // XL1278-SMT (SPI, 433 MHz) uniquement
// #define MODULE_DUAL            // Les DEUX modules (DUAL-BAND)

// ============================================
// SÉLECTION DU MODE
// ============================================
#define MODE_SIMPLE            // Broadcast | Commentez pour mode COMPLET (appairage+crypto)

// ============================================
// PROTOCOLE ET SÉCURITÉ
// ============================================

#define USE_CUSTOM_PROTOCOL              // Protocole binaire structuré (recommandé)
#define USE_ENCRYPTION                   // AES-128-CTR + HMAC (nécessite USE_CUSTOM_PROTOCOL)
                                         // ⚠️ Même clé sur tous les modules (security/Encryption.h)
#define DEVICE_ID  2                     // ID unique (0-255) - CHANGER POUR CHAQUE MODULE !

// ============================================
// CAPTEUR HUMAIN 24GHz
// ============================================
#define USE_HUMAN_SENSOR_24GHZ        // Capteur HLK-LD2450
#define HUMAN_SENSOR_AUTO_SEND_INTERVAL 2500  // Intervalle auto-envoi (ms)

// ============================================
// PINS
// ============================================
// E220-900T22D (UART)
#define PIN_E220_RX        16    // GPIO16 (RX2)
#define PIN_E220_TX        17    // GPIO17 (TX2)
#define PIN_E220_AUX       4
#define PIN_E220_M0        2
#define PIN_E220_M1        15

// XL1278-SMT (SPI)
#define PIN_XL1278_SCK     18    // VSPI
#define PIN_XL1278_MISO    19
#define PIN_XL1278_MOSI    23
#define PIN_XL1278_SS      5
#define PIN_XL1278_RST     14
#define PIN_XL1278_DIO0    26

// Capteur 24GHz HLK-LD2450 (UART)
// Specs: 5V/120mA, 256000 bauds, 3 cibles max, 6m portée, ±60° azimut
#define PIN_SENSOR_RX      25    // GPIO25 (RX1) ← TX capteur
#define PIN_SENSOR_TX      26    // GPIO26 (TX1) → RX capteur
#define SENSOR_BAUD_RATE   256000  // Baudrate par défaut (manuel HLK-LD2450)

// ============================================
// CONFIGURATION LORA E220-900T22D
// ============================================
#define CONFIG_ADDH              0xFF  // Adresse broadcast
#define CONFIG_ADDL              0xFF
#define CONFIG_CHAN_E220         23    // 873.125 MHz (formule: 850.125 + CHAN)
#define AIR_DATA_RATE            AIR_DATA_RATE_010_24  // 2.4 kbps
#define TX_POWER                 POWER_22              // 22 dBm
#define UART_BAUD                UART_BPS_9600
#define UART_PARITY              MODE_00_8N1

// ============================================
// CONFIGURATION LORA XL1278-SMT
// ============================================
#define LORA_FREQUENCY_433       433E6   // 433 MHz
#define LORA_FREQUENCY_868       868E6   // 868 MHz (Europe)
#define LORA_FREQUENCY_915       915E6   // 915 MHz (USA)
#define LORA_SPREADING_FACTOR    7       // SF7 (vitesse élevée)
#define LORA_BANDWIDTH           125E3   // 125 kHz
#define LORA_CODING_RATE         5       // 4/5
#define LORA_SYNC_WORD           0x12    // Privé (0x12) ou public (0x34)
#define LORA_TX_POWER_XL         20      // 20 dBm

// ============================================
// INTERVALLES TEMPORELS (ms)
// ============================================
#define BEACON_INTERVAL_MS       3000   // Beacons
#define DISCOVERY_DISPLAY_MS     5000   // Affichage liste
#define DISCOVERY_TTL_MS         15000  // TTL entrées
#define PAIRING_TIMEOUT_MS       30000  // Timeout appairage
#define HEARTBEAT_INTERVAL_MS    10000  // Heartbeat
#define HEARTBEAT_TIMEOUT_MS     30000  // Timeout heartbeat

// ============================================
// CONSTANTES PROTOCOLE
// ============================================
#define MAGIC_ENCRYPTED          0x01   // Message chiffré
#define MAGIC_CLEAR              0x02   // Message clair
#define MAX_MESSAGE_SIZE         200    // Max message texte
#define MAX_PACKET_SIZE          255    // Max paquet LoRa
#define MAX_PAYLOAD_SIZE         220    // Max payload après headers

// Tailles crypto
#define AES_KEY_SIZE             16     // AES-128
#define AES_IV_SIZE              16     // IV
#define MAC_SIZE                 16     // MAC tronqué
#define NONCE_SIZE               16     // Nonce ECDH
#define ECDH_PUBKEY_SIZE         65     // Clé publique ECDH

// ============================================
// DEBUG & SERIAL
// ============================================
// #define DEBUG_VERBOSE                // Messages debug détaillés
// #define DEBUG_RAW_PACKETS            // Bytes bruts paquets
#define SERIAL_BAUD_RATE         115200

// ============================================
// HELPERS - Conversion fréquence E220
// ============================================

/**
 * Calcule la fréquence en MHz à partir du canal E220
 * @param chan Canal (0-80)
 * @return Fréquence en MHz
 */
inline float calculateFrequency900MHz(uint8_t chan) {
    return 850.125f + (float)chan;
}

/**
 * Calcule le canal E220 à partir de la fréquence en MHz
 * @param freqMHz Fréquence en MHz (850.125-930.125)
 * @return Canal (0-80)
 */
inline uint8_t calculateChannel900MHz(float freqMHz) {
    if (freqMHz < 850.125f) freqMHz = 850.125f;
    if (freqMHz > 930.125f) freqMHz = 930.125f;
    return (uint8_t)(freqMHz - 850.125f);
}

#endif // CONFIG_H

