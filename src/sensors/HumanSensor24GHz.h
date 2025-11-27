#ifndef HUMAN_SENSOR_24GHZ_H
#define HUMAN_SENSOR_24GHZ_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include "../Config.h"

// ============================================
// CAPTEUR HLK-LD2450 - CONFIGURATION
// ============================================
// Capteur radar mmWave 24GHz multi-cibles
// Détecte jusqu'à 3 personnes simultanément
// Donne la position X, Y de chaque cible
// Distance: 0-6 mètres
// Communication: UART (baudrate défini dans Config.h)

// ============================================
// PROTOCOLE LD2450 (selon manuel officiel V1.00)
// ============================================
// Format de trame (mode data output):
// [AA][FF][03][00][DATA 24 bytes][CRC_L][CRC_H][55][CC]
// Total: 30 bytes
// 
// Structure des données (selon manuel):
// - Header: AA FF 03 00 (4 bytes)
// - Target 1: X(2B) Y(2B) Speed(2B) Resolution(2B) (8 bytes)
// - Target 2: X(2B) Y(2B) Speed(2B) Resolution(2B) (8 bytes)
// - Target 3: X(2B) Y(2B) Speed(2B) Resolution(2B) (8 bytes)
// - CRC: 2 bytes
// - Footer: 55 CC (2 bytes)
//
// IMPORTANT: Coordonnées signées int16 avec encodage spécial:
// - Si bit 15 = 1 → coordonnée POSITIVE
// - Si bit 15 = 0 → coordonnée NÉGATIVE
// Conversion: si valeur >= 32768, alors valeur_réelle = valeur - 65536

#define LD2450_HEADER_1     0xAA
#define LD2450_HEADER_2     0xFF
#define LD2450_FOOTER_1     0x55
#define LD2450_FOOTER_2     0xCC
#define LD2450_FRAME_SIZE   30  // 30 bytes selon manuel

// Mode debug (selon manuel HLK-LD2450: Data refresh rate = 10 Hz)
#define DEBUG_RAW_FRAMES    true   // Affiche trames brutes (1x/sec)
#define DEBUG_FILTERING     false  // Affiche filtrage détaillé (désactiver après test)

// NOTES IMPORTANTES du manuel HLK-LD2450:
// - Résolution = taille d'une porte de distance (ex: 320mm, 360mm) - valeur technique
// - Cible invalide quand X=0 ET Y=0 (pas de détection)
// - Plage détection: ±60° azimut, ±35° élévation, 6m max
// - Hauteur installation recommandée: 1.5-2m
// - 3 cibles max simultanées, refresh 10Hz

// Structure pour une cible détectée
struct LD2450_Target {
    int16_t x;          // Position X en mm (-6000 à 6000)
    int16_t y;          // Position Y en mm (0 à 6000)
    int16_t speed;      // Vitesse en cm/s
    uint16_t resolution; // Résolution/confiance
    bool valid;         // Cible valide (résolution > 0)
};

// ============================================
// CLASSE DE GESTION DU CAPTEUR LD2450
// ============================================
class HumanSensor24GHz {
private:
    HardwareSerial* serial;
    uint8_t lastHumanCount;
    bool humanDetected;
    
    uint8_t rxBuffer[64];  // Buffer plus large pour capturer différentes tailles
    uint8_t rxIndex;
    bool frameStarted;
    uint32_t lastDebugTime;
    
    LD2450_Target targets[3];  // 3 cibles max
    
    uint32_t totalFramesReceived;
    uint32_t totalFramesValid;
    uint32_t lastStatsTime;
    
public:
    /**
     * Constructeur
     */
    HumanSensor24GHz(HardwareSerial* serialPort, uint32_t updateMs = 1000) {
        serial = serialPort;
        lastHumanCount = 0;
        humanDetected = false;
        rxIndex = 0;
        frameStarted = false;
        totalFramesReceived = 0;
        totalFramesValid = 0;
        lastStatsTime = 0;
        lastDebugTime = 0;
        
        // Initialiser les cibles
        for (int i = 0; i < 3; i++) {
            targets[i].valid = false;
        }
    }
    
    /**
     * Initialise le capteur LD2450
     */
    bool begin() {
        // Initialiser l'UART
        serial->begin(SENSOR_BAUD_RATE, SERIAL_8N1, PIN_SENSOR_RX, PIN_SENSOR_TX);
        delay(500);
        
        Serial.println("[SENSOR] ═══════════════════════════════════════");
        Serial.println("[SENSOR] Initialisation HLK-LD2450...");
        Serial.print("[SENSOR] UART: RX=GPIO");
        Serial.print(PIN_SENSOR_RX);
        Serial.print(", TX=GPIO");
        Serial.print(PIN_SENSOR_TX);
        Serial.print(", Baud=");
        Serial.println(SENSOR_BAUD_RATE);
        Serial.println("[SENSOR] Capteur multi-cibles (jusqu'à 3 personnes)");
        Serial.println("[SENSOR] ═══════════════════════════════════════");
        
        // Le LD2450 commence à envoyer des données automatiquement
        // Attendre 1 seconde pour voir si on reçoit des données
        delay(1000);
        
        if (serial->available() > 0) {
            Serial.println("[SENSOR] ✓ LD2450 détecté (données reçues) !");
            return true;
        } else {
            Serial.println("[SENSOR] ⚠ Pas de données reçues");
            Serial.println("[SENSOR] Le capteur devrait envoyer automatiquement");
            Serial.println("[SENSOR] Vérifiez les branchements");
            return false;
        }
    }
    
    /**
     * Lit et parse les données du capteur
     */
    void update() {
        while (serial->available()) {
            uint8_t byte = serial->read();
            
            // Détection du début de trame
            if (!frameStarted) {
                if (byte == LD2450_HEADER_1) {
                    rxBuffer[0] = byte;
                    rxIndex = 1;
                    frameStarted = true;
                }
            }
            // Réception des données
            else {
                rxBuffer[rxIndex++] = byte;
                
                // Chercher le footer 55 CC
                if (rxIndex >= 2 && 
                    rxBuffer[rxIndex-2] == LD2450_FOOTER_1 && 
                    rxBuffer[rxIndex-1] == LD2450_FOOTER_2) {
                    
                    totalFramesReceived++;
                    
                    // Debug: afficher la trame brute (une fois par seconde max)
                    if (DEBUG_RAW_FRAMES && millis() - lastDebugTime >= 1000) {
                        lastDebugTime = millis();
                        Serial.print("[DEBUG] Trame (");
                        Serial.print(rxIndex);
                        Serial.print(" bytes): ");
                        for (int i = 0; i < rxIndex && i < 32; i++) {
                            if (rxBuffer[i] < 0x10) Serial.print("0");
                            Serial.print(rxBuffer[i], HEX);
                            Serial.print(" ");
                        }
                        Serial.println();
                    }
                    
                    // Vérifier le format de trame (30 bytes minimum)
                    if (rxIndex >= 30 && 
                        rxBuffer[0] == LD2450_HEADER_1 &&
                        rxBuffer[1] == LD2450_HEADER_2) {
                        
                        // Parser la trame
                        parseFrame(rxBuffer, rxIndex);
                        totalFramesValid++;
                    }
                    
                    // Réinitialiser
                    rxIndex = 0;
                    frameStarted = false;
                }
                else if (rxIndex >= 64) {
                    // Buffer overflow, réinitialiser
                    if (DEBUG_RAW_FRAMES) {
                        Serial.println("[DEBUG] Buffer overflow, trame trop longue");
                    }
                    rxIndex = 0;
                    frameStarted = false;
                }
            }
        }
        
        // Afficher les stats toutes les 30 secondes
        if (millis() - lastStatsTime >= 30000 && totalFramesReceived > 0) {
            lastStatsTime = millis();
            Serial.print("[SENSOR] Stats: ");
            Serial.print(totalFramesValid);
            Serial.print(" trames valides / ");
            Serial.print(totalFramesReceived);
            Serial.print(" reçues (");
            Serial.print((totalFramesValid * 100) / totalFramesReceived);
            Serial.println("%)");
        }
    }
    
    /**
     * Parse une trame LD2450
     */
    void parseFrame(uint8_t* frame, uint8_t len) {
        // Format standard LD2450:
        // [AA FF 03 00][Target1 8B][Target2 8B][Target3 8B][55 CC]
        // Total: 30 bytes
        // Offset des données: 4
        
        uint8_t newCount = 0;
        
        // Les données commencent après le header (AA FF 03 00)
        int dataOffset = 4;
        
#if DEBUG_FILTERING
        Serial.println("[SENSOR] === Parsing trame ===");
#endif
        
        for (int i = 0; i < 3; i++) {
            int offset = dataOffset + (i * 8);
            
            if (offset + 7 < len) {
                // Lire les valeurs brutes (little endian, uint16)
                uint16_t xRaw = frame[offset] | (frame[offset+1] << 8);
                uint16_t yRaw = frame[offset+2] | (frame[offset+3] << 8);
                uint16_t speedRaw = frame[offset+4] | (frame[offset+5] << 8);
                targets[i].resolution = frame[offset+6] | (frame[offset+7] << 8);
                
                // Conversion selon manuel HLK-LD2450:
                // Bit 15 = 1 → positif, Bit 15 = 0 → négatif
                // Si valeur < 32768 (bit15=0) : coordonnée = 0 - valeur (négatif)
                // Si valeur >= 32768 (bit15=1) : coordonnée = valeur - 32768 (positif)
                if (xRaw < 32768) {
                    targets[i].x = -(int16_t)xRaw;  // Négatif
                } else {
                    targets[i].x = (int16_t)(xRaw - 32768);  // Positif
                }
                
                if (yRaw < 32768) {
                    targets[i].y = -(int16_t)yRaw;  // Négatif
                } else {
                    targets[i].y = (int16_t)(yRaw - 32768);  // Positif
                }
                
                if (speedRaw < 32768) {
                    targets[i].speed = -(int16_t)speedRaw;  // Négatif
                } else {
                    targets[i].speed = (int16_t)(speedRaw - 32768);  // Positif
                }
                
                // Filtrage selon manuel HLK-LD2450 section 6:
                // Une cible est INVALIDE si : X=0 ET Y=0 (pas de détection)
                // Une cible est VALIDE si :
                // 1. X != 0 OU Y != 0 (cible détectée)
                // 2. Distance < 6000mm (portée max selon manuel)
                // 3. Coordonnées dans plage physique ±6000mm
                // Note: La résolution (320mm, 360mm, etc.) est une valeur technique, PAS un indicateur de bruit
                
                bool hasTarget = (targets[i].x != 0 || targets[i].y != 0);
                float distance = sqrt(targets[i].x * targets[i].x + targets[i].y * targets[i].y);
                bool inDetectionRange = (distance < 6000.0 && distance > 10.0);  // 1cm à 6m
                bool inPhysicalRange = (abs(targets[i].x) < 6000 && abs(targets[i].y) < 6000);
                
                // Debug: afficher les détails du filtrage
#if DEBUG_FILTERING
                if (hasTarget) {
                    Serial.print("[SENSOR] Cible ");
                    Serial.print(i+1);
                    Serial.print(": X=");
                    Serial.print(targets[i].x);
                    Serial.print("mm Y=");
                    Serial.print(targets[i].y);
                    Serial.print("mm dist=");
                    Serial.print(distance / 10.0, 1);
                    Serial.print("cm res=");
                    Serial.print(targets[i].resolution);
                    Serial.print("mm");
                    
                    if (!inPhysicalRange) {
                        Serial.println(" ❌ REJETÉE (hors plage ±6m)");
                    } else if (!inDetectionRange) {
                        Serial.println(" ❌ REJETÉE (distance < 1cm ou > 6m)");
                    } else {
                        Serial.println(" ✓ VALIDE");
                    }
                } else {
                    // X=0 ET Y=0 = pas de cible
                }
#endif
                
                targets[i].valid = hasTarget && inDetectionRange && inPhysicalRange;
                
                if (targets[i].valid) {
                    newCount++;
                }
            } else {
                targets[i].valid = false;
            }
        }
        
        // Détecter les changements
        if (newCount != lastHumanCount) {
            lastHumanCount = newCount;
            humanDetected = (newCount > 0);
            
            Serial.print("[SENSOR] ⚡ Détection: ");
            Serial.print(newCount);
            Serial.println(newCount > 1 ? " humains détectés !" : (newCount == 1 ? " humain détecté" : " aucun humain"));
            
            // Afficher les détails de chaque cible
            for (int i = 0; i < 3; i++) {
                if (targets[i].valid) {
                    Serial.print("[SENSOR]   └─ Cible ");
                    Serial.print(i + 1);
                    Serial.print(": X=");
                    Serial.print(targets[i].x);
                    Serial.print("mm, Y=");
                    Serial.print(targets[i].y);
                    Serial.print("mm (");
                    Serial.print(sqrt(targets[i].x * targets[i].x + targets[i].y * targets[i].y) / 10.0, 1);
                    Serial.print("cm)");
                    
                    if (targets[i].speed != 0) {
                        Serial.print(", vitesse=");
                        Serial.print(targets[i].speed);
                        Serial.print("cm/s");
                    }
                    
                    Serial.println();
                }
            }
        }
    }
    
    /**
     * Retourne le nombre d'humains détectés
     */
    uint8_t getHumanCount() const {
        return lastHumanCount;
    }
    
    /**
     * Retourne si au moins un humain est détecté
     */
    bool isHumanDetected() const {
        return humanDetected;
    }
    
    /**
     * Obtient les informations d'une cible spécifique
     */
    const LD2450_Target* getTarget(uint8_t index) const {
        if (index < 3) {
            return &targets[index];
        }
        return nullptr;
    }
    
    /**
     * Récupère les données de toutes les cibles valides
     * Remplit les tableaux fournis avec les données
     * @return Nombre de cibles valides
     */
    uint8_t getAllTargetsData(int16_t* x, int16_t* y, int16_t* speed, uint16_t* resolution) const {
        uint8_t count = 0;
        for (uint8_t i = 0; i < 3; i++) {
            if (targets[i].valid) {
                x[count] = targets[i].x;
                y[count] = targets[i].y;
                speed[count] = targets[i].speed;
                resolution[count] = targets[i].resolution;
                count++;
            }
        }
        return count;
    }
    
    /**
     * Réinitialise le compteur
     */
    void reset() {
        lastHumanCount = 0;
        humanDetected = false;
    }
    
    void setUpdateInterval(uint32_t ms) {
        // Non utilisé pour le LD2450 (envoi automatique)
    }
    
    uint8_t readHumanCount() {
        update();
        return lastHumanCount;
    }
    
    /**
     * Test complet du capteur LD2450
     */
    void test() {
        Serial.println("[SENSOR] ═══════════════════════════════════════");
        Serial.println("[SENSOR] TEST COMPLET HLK-LD2450");
        Serial.println("[SENSOR] ═══════════════════════════════════════");
        Serial.println("[SENSOR] Capteur multi-cibles (jusqu'à 3 personnes)");
        Serial.println("[SENSOR] Test en temps réel pendant 10 secondes");
        Serial.println("[SENSOR] Déplacez-vous devant le capteur...");
        Serial.println();
        
        uint32_t startTime = millis();
        uint32_t lastDisplay = 0;
        uint8_t lastDisplayCount = 255;
        
        while (millis() - startTime < 10000) {
            update();
            
            // Afficher l'état toutes les 500ms ou si changement
            if (millis() - lastDisplay >= 500 || lastHumanCount != lastDisplayCount) {
                lastDisplay = millis();
                lastDisplayCount = lastHumanCount;
                
                Serial.print("[SENSOR] ");
                Serial.print((millis() - startTime) / 1000);
                Serial.print("s | ");
                
                if (lastHumanCount > 0) {
                    Serial.print("✓ ");
                    Serial.print(lastHumanCount);
                    Serial.println(lastHumanCount > 1 ? " HUMAINS" : " HUMAIN");
                    
                    for (int i = 0; i < 3; i++) {
                        if (targets[i].valid) {
                            float distance = sqrt(targets[i].x * targets[i].x + 
                                                targets[i].y * targets[i].y) / 10.0;
                            
                            Serial.print("[SENSOR]   └─ #");
                            Serial.print(i + 1);
                            Serial.print(": ");
                            Serial.print(distance, 1);
                            Serial.print("cm | X=");
                            Serial.print(targets[i].x);
                            Serial.print(" Y=");
                            Serial.print(targets[i].y);
                            Serial.print(" | v=");
                            Serial.print(targets[i].speed);
                            Serial.println("cm/s");
                        }
                    }
                } else {
                    Serial.println("○ Rien détecté");
                }
            }
            
            delay(10);
        }
        
        Serial.println();
        Serial.print("[SENSOR] Trames reçues: ");
        Serial.println(totalFramesReceived);
        Serial.print("[SENSOR] Trames valides: ");
        Serial.print(totalFramesValid);
        Serial.print(" (");
        if (totalFramesReceived > 0) {
            Serial.print((totalFramesValid * 100) / totalFramesReceived);
            Serial.println("%)");
        } else {
            Serial.println("0%)");
        }
        Serial.println("[SENSOR] ═══════════════════════════════════════");
        Serial.println("[SENSOR] Test terminé");
        Serial.println("[SENSOR] ═══════════════════════════════════════");
    }
};

#endif // HUMAN_SENSOR_24GHZ_H
