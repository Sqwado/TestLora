#ifndef LORA_CONFIG_H
#define LORA_CONFIG_H

#include <Arduino.h>
#include "../Config.h"

/**
 * Configuration LoRa E220 - Utilise Config.h pour éviter les redondances
 * Ce fichier ne contient que les constantes spécifiques au module E220
 */

// Mode de configuration des pins E220 (COMPLET uniquement - pins M0/M1 requis)
#define MODE_COMPLET    3
#define E220_PIN_MODE   MODE_COMPLET

// Les pins sont maintenant dans Config.h (PIN_E220_RX, PIN_E220_TX, etc.)
// Aliases pour compatibilité avec le code existant
static const int PIN_LORA_RX = PIN_E220_RX;
static const int PIN_LORA_TX = PIN_E220_TX;
static const int PIN_LORA_AUX = PIN_E220_AUX;
static const int PIN_LORA_M0 = PIN_E220_M0;
static const int PIN_LORA_M1 = PIN_E220_M1;

// Configuration standard pour mode transparent (utilise Config.h)
static const byte CONFIG_CHAN = CONFIG_CHAN_E220;

#endif // LORA_CONFIG_H

