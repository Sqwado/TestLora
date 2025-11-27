#ifndef MESSAGE_PROTOCOL_H
#define MESSAGE_PROTOCOL_H

#include <Arduino.h>

// ============================================
// PROTOCOLE DE MESSAGE PERSONNALISÉ
// ============================================
// Format: [MAGIC_NUM][TYPE][ID_SOURCE][TAILLE][DATA...]
// 
// MAGIC_NUM (1 byte): Indicateur d'encryption
//   0x01 = Message chiffré AES128 (tout après le magic number)
//   0x02 = Message en clair
// TYPE (1 byte)    : Type de commande/données
// ID_SOURCE (1 byte): ID unique de la carte émettrice
// TAILLE (1 byte)  : Nombre d'octets de données
// DATA (N bytes)   : Données (max 250 bytes)

// ============================================
// TYPES DE COMMANDES (1 byte)
// ============================================
#define MSG_TYPE_TEMP_DATA      0x01  // Données de température
#define MSG_TYPE_HUMAN_DETECT   0x02  // Détection humaine (binaire: oui/non)
#define MSG_TYPE_HUMAN_COUNT    0x03  // Comptage humain (nombre d'humains détectés)
#define MSG_TYPE_SENSOR_DATA    0x04  // Données complètes capteur (multi-cibles)
#define MSG_TYPE_TEXT           0x10  // Message texte
#define MSG_TYPE_STATUS         0x11  // Statut général
#define MSG_TYPE_PING           0x20  // Ping
#define MSG_TYPE_PONG           0x21  // Réponse Ping
#define MSG_TYPE_ACK            0xF0  // Acquittement
#define MSG_TYPE_ERROR          0xFF  // Erreur

// Réserver 0x05-0x0F pour futures sondes
#define MSG_TYPE_HUMIDITY       0x05  // Humidité (futur)
#define MSG_TYPE_PRESSURE       0x06  // Pression (futur)
#define MSG_TYPE_LIGHT          0x07  // Luminosité (futur)
#define MSG_TYPE_MOTION         0x08  // Mouvement (futur)

// ============================================
// MAGIC NUMBERS
// ============================================
#define MAGIC_NUM_ENCRYPTED     0x01  // Message chiffré AES128
#define MAGIC_NUM_CLEAR         0x02  // Message en clair

// ============================================
// CONSTANTES
// ============================================
#define PROTOCOL_HEADER_SIZE    3     // TYPE + ID_SOURCE + TAILLE (sans MAGIC_NUM)
#define PROTOCOL_MAX_DATA_SIZE  249   // Taille max des données (253-4)
#define PROTOCOL_MAX_MSG_SIZE   253   // MAGIC_NUM + HEADER + DATA

// ============================================
// STRUCTURE DE MESSAGE
// ============================================
struct ProtocolMessage {
    uint8_t type;           // Type de message
    uint8_t sourceId;       // ID de la source
    uint8_t dataSize;       // Taille des données
    uint8_t data[PROTOCOL_MAX_DATA_SIZE]; // Données
    bool valid;             // Message valide après parsing
};

// ============================================
// CLASSE PROTOCOLE
// ============================================
class MessageProtocol {
public:
    /**
     * Encode un message selon le protocole (sans MAGIC_NUM)
     * Format: [TYPE][ID_SOURCE][TAILLE][DATA...]
     * Le MAGIC_NUM doit être ajouté par l'appelant
     * @param type Type de message (MSG_TYPE_*)
     * @param sourceId ID de la source (0-255)
     * @param data Données à envoyer
     * @param dataSize Taille des données
     * @param output Buffer de sortie (doit être assez grand)
     * @return Taille totale du message encodé (3 + dataSize)
     */
    static uint16_t encodeMessage(uint8_t type, uint8_t sourceId, 
                                   const uint8_t* data, uint8_t dataSize,
                                   uint8_t* output) {
        if (dataSize > PROTOCOL_MAX_DATA_SIZE) {
            dataSize = PROTOCOL_MAX_DATA_SIZE;
        }
        
        output[0] = type;
        output[1] = sourceId;
        output[2] = dataSize;
        
        if (dataSize > 0 && data != nullptr) {
            memcpy(&output[3], data, dataSize);
        }
        
        return PROTOCOL_HEADER_SIZE + dataSize;  // 3 + dataSize
    }
    
    /**
     * Encode un message texte
     */
    static uint16_t encodeTextMessage(uint8_t sourceId, const char* text, uint8_t* output) {
        uint8_t len = strlen(text);
        if (len > PROTOCOL_MAX_DATA_SIZE) {
            len = PROTOCOL_MAX_DATA_SIZE;
        }
        return encodeMessage(MSG_TYPE_TEXT, sourceId, (const uint8_t*)text, len, output);
    }
    
    /**
     * Encode un message de température
     * Format data: [température_x100 (2 bytes, little endian)]
     * Exemple: 25.3°C -> 2530 -> 0xE2 0x09
     */
    static uint16_t encodeTempMessage(uint8_t sourceId, float temperature, uint8_t* output) {
        int16_t temp_x100 = (int16_t)(temperature * 100.0f);
        uint8_t data[2];
        data[0] = temp_x100 & 0xFF;         // Low byte
        data[1] = (temp_x100 >> 8) & 0xFF;  // High byte
        return encodeMessage(MSG_TYPE_TEMP_DATA, sourceId, data, 2, output);
    }
    
    /**
     * Encode un message de détection humaine (binaire)
     * Format data: [detected (1 byte): 0=non, 1=oui]
     */
    static uint16_t encodeHumanDetectMessage(uint8_t sourceId, bool detected, uint8_t* output) {
        uint8_t data[1];
        data[0] = detected ? 0x01 : 0x00;
        return encodeMessage(MSG_TYPE_HUMAN_DETECT, sourceId, data, 1, output);
    }
    
    /**
     * Encode un message de comptage humain (nombre d'humains détectés)
     * Format data: [count (1 byte): nombre d'humains (0-255)]
     */
    static uint16_t encodeHumanCountMessage(uint8_t sourceId, uint8_t humanCount, uint8_t* output) {
        uint8_t data[1];
        data[0] = humanCount;
        return encodeMessage(MSG_TYPE_HUMAN_COUNT, sourceId, data, 1, output);
    }
    
    /**
     * Encode un message de données capteur complètes (multi-cibles)
     * Format data: [count][target1_data][target2_data][target3_data]
     * Chaque target: X(2B) Y(2B) Speed(2B) Resolution(2B) = 8 bytes
     * Total max: 1 + (3 * 8) = 25 bytes
     */
    static uint16_t encodeSensorDataMessage(uint8_t sourceId, uint8_t count,
                                             int16_t* x, int16_t* y, 
                                             int16_t* speed, uint16_t* resolution,
                                             uint8_t* output) {
        uint8_t data[25];
        uint8_t idx = 0;
        
        // Nombre de cibles
        data[idx++] = count;
        
        // Données de chaque cible (jusqu'à 3)
        for (uint8_t i = 0; i < 3 && i < count; i++) {
            // X (little endian)
            data[idx++] = x[i] & 0xFF;
            data[idx++] = (x[i] >> 8) & 0xFF;
            // Y (little endian)
            data[idx++] = y[i] & 0xFF;
            data[idx++] = (y[i] >> 8) & 0xFF;
            // Speed (little endian)
            data[idx++] = speed[i] & 0xFF;
            data[idx++] = (speed[i] >> 8) & 0xFF;
            // Resolution (little endian)
            data[idx++] = resolution[i] & 0xFF;
            data[idx++] = (resolution[i] >> 8) & 0xFF;
        }
        
        return encodeMessage(MSG_TYPE_SENSOR_DATA, sourceId, data, idx, output);
    }
    
    /**
     * Encode un ping
     * Format data: [timestamp (4 bytes, little endian)]
     */
    static uint16_t encodePingMessage(uint8_t sourceId, uint8_t* output) {
        uint32_t timestamp = millis();
        uint8_t data[4];
        data[0] = timestamp & 0xFF;
        data[1] = (timestamp >> 8) & 0xFF;
        data[2] = (timestamp >> 16) & 0xFF;
        data[3] = (timestamp >> 24) & 0xFF;
        return encodeMessage(MSG_TYPE_PING, sourceId, data, 4, output);
    }
    
    /**
     * Encode un pong (réponse à un ping)
     * Format data: [timestamp original (4 bytes)] - renvoie le timestamp reçu
     */
    static uint16_t encodePongMessage(uint8_t sourceId, const uint8_t* pingData, uint8_t* output) {
        // Renvoyer le timestamp du ping reçu
        return encodeMessage(MSG_TYPE_PONG, sourceId, pingData, 4, output);
    }
    
    /**
     * Décode un message reçu (sans MAGIC_NUM)
     * Format attendu: [TYPE][ID_SOURCE][TAILLE][DATA...]
     * Le MAGIC_NUM doit être traité par l'appelant avant
     * @param buffer Buffer contenant le message (sans magic number)
     * @param bufferSize Taille du buffer
     * @param msg Structure à remplir
     * @return true si le décodage a réussi
     */
    static bool decodeMessage(const uint8_t* buffer, uint16_t bufferSize, ProtocolMessage* msg) {
        msg->valid = false;
        
        // Vérifier taille minimale: TYPE + ID + SIZE = 3 bytes
        if (bufferSize < PROTOCOL_HEADER_SIZE) {
            return false;
        }
        
        msg->type = buffer[0];
        msg->sourceId = buffer[1];
        msg->dataSize = buffer[2];
        
        // Vérifier la cohérence
        if (msg->dataSize > PROTOCOL_MAX_DATA_SIZE) {
            return false;
        }
        
        if (bufferSize < (PROTOCOL_HEADER_SIZE + msg->dataSize)) {
            return false;
        }
        
        // Copier les données
        if (msg->dataSize > 0) {
            memcpy(msg->data, &buffer[3], msg->dataSize);
        }
        
        msg->valid = true;
        return true;
    }
    
    /**
     * Obtient le nom du type de message
     */
    static String getTypeName(uint8_t type) {
        switch (type) {
            case MSG_TYPE_TEMP_DATA:    return "TEMP";
            case MSG_TYPE_HUMAN_DETECT: return "HUMAN";
            case MSG_TYPE_HUMAN_COUNT:  return "HUMAN_COUNT";
            case MSG_TYPE_SENSOR_DATA:  return "SENSOR_DATA";
            case MSG_TYPE_TEXT:         return "TEXT";
            case MSG_TYPE_STATUS:       return "STATUS";
            case MSG_TYPE_PING:         return "PING";
            case MSG_TYPE_PONG:         return "PONG";
            case MSG_TYPE_ACK:          return "ACK";
            case MSG_TYPE_ERROR:        return "ERROR";
            case MSG_TYPE_HUMIDITY:     return "HUMID";
            case MSG_TYPE_PRESSURE:     return "PRESS";
            case MSG_TYPE_LIGHT:        return "LIGHT";
            case MSG_TYPE_MOTION:       return "MOTION";
            default:                    return "UNKNOWN";
        }
    }
    
    /**
     * Décode et affiche une température
     */
    static float decodeTempData(const ProtocolMessage* msg) {
        if (msg->dataSize < 2) return 0.0f;
        int16_t temp_x100 = msg->data[0] | (msg->data[1] << 8);
        return temp_x100 / 100.0f;
    }
    
    /**
     * Décode une détection humaine (binaire)
     */
    static bool decodeHumanDetect(const ProtocolMessage* msg) {
        if (msg->dataSize < 1) return false;
        return msg->data[0] != 0x00;
    }
    
    /**
     * Décode un comptage humain (nombre)
     */
    static uint8_t decodeHumanCount(const ProtocolMessage* msg) {
        if (msg->dataSize < 1) return 0;
        return msg->data[0];
    }
    
    /**
     * Décode un timestamp (ping)
     */
    static uint32_t decodeTimestamp(const ProtocolMessage* msg) {
        if (msg->dataSize < 4) return 0;
        return msg->data[0] | 
               (msg->data[1] << 8) | 
               (msg->data[2] << 16) | 
               (msg->data[3] << 24);
    }
    
    /**
     * Affiche un message décodé (debug)
     */
    static void printMessage(const ProtocolMessage* msg, const char* prefix = "") {
        if (!msg->valid) {
            Serial.println(String(prefix) + "Message invalide");
            return;
        }
        
        Serial.println(String(prefix) + "─────────────────");
        Serial.println(String(prefix) + "Type     : 0x" + String(msg->type, HEX) + " (" + getTypeName(msg->type) + ")");
        Serial.println(String(prefix) + "Source   : " + String(msg->sourceId));
        Serial.println(String(prefix) + "Taille   : " + String(msg->dataSize) + " bytes");
        
        // Afficher les données selon le type
        if (msg->type == MSG_TYPE_TEMP_DATA && msg->dataSize >= 2) {
            float temp = decodeTempData(msg);
            Serial.println(String(prefix) + "Temp     : " + String(temp, 1) + " °C");
        }
        else if (msg->type == MSG_TYPE_HUMAN_DETECT && msg->dataSize >= 1) {
            bool detected = decodeHumanDetect(msg);
            Serial.println(String(prefix) + "Détecté  : " + String(detected ? "OUI" : "NON"));
        }
        else if (msg->type == MSG_TYPE_HUMAN_COUNT && msg->dataSize >= 1) {
            uint8_t count = decodeHumanCount(msg);
            Serial.println(String(prefix) + "Humains  : " + String(count) + (count > 1 ? " personnes" : " personne"));
        }
        else if (msg->type == MSG_TYPE_SENSOR_DATA && msg->dataSize >= 1) {
            uint8_t count = msg->data[0];
            Serial.println(String(prefix) + "Capteur  : " + String(count) + (count > 1 ? " cibles" : (count == 1 ? " cible" : " cible (aucune détection)")));
            
            if (count == 0) {
                Serial.println(String(prefix) + "  → Zone libre (pas de présence détectée)");
            }
            
            // Décoder chaque cible
            uint8_t idx = 1;
            for (uint8_t i = 0; i < count && i < 3 && (idx + 7) < msg->dataSize; i++) {
                int16_t x = msg->data[idx] | (msg->data[idx+1] << 8);
                int16_t y = msg->data[idx+2] | (msg->data[idx+3] << 8);
                int16_t speed = msg->data[idx+4] | (msg->data[idx+5] << 8);
                uint16_t res = msg->data[idx+6] | (msg->data[idx+7] << 8);
                idx += 8;
                
                float dist = sqrt(x * x + y * y) / 10.0;
                Serial.print(String(prefix) + "  Cible " + String(i+1) + ": ");
                Serial.print("X=" + String(x) + "mm Y=" + String(y) + "mm ");
                Serial.print("(" + String(dist, 1) + "cm) ");
                Serial.print("v=" + String(speed) + "cm/s ");
                Serial.println("res=" + String(res));
            }
        }
        else if (msg->type == MSG_TYPE_TEXT) {
            String text = String((char*)msg->data, msg->dataSize);
            Serial.println(String(prefix) + "Texte    : " + text);
        }
        else if (msg->type == MSG_TYPE_PING && msg->dataSize >= 4) {
            uint32_t timestamp = decodeTimestamp(msg);
            Serial.println(String(prefix) + "Timestamp: " + String(timestamp));
        }
        else {
            // Afficher en hexa
            Serial.print(String(prefix) + "Data (hex): ");
            for (uint8_t i = 0; i < msg->dataSize; i++) {
                if (msg->data[i] < 0x10) Serial.print("0");
                Serial.print(msg->data[i], HEX);
                Serial.print(" ");
            }
            Serial.println();
        }
        
        Serial.println(String(prefix) + "─────────────────");
    }
};

#endif // MESSAGE_PROTOCOL_H

