#ifndef LORA_CONFIG_XL1278_H
#define LORA_CONFIG_XL1278_H

#include <Arduino.h>
#include "../Config.h"

/**
 * Configuration LoRa XL1278 - Utilise Config.h pour éviter les redondances
 * Ce fichier ne contient que les aliases pour compatibilité avec le code existant
 */

// Aliases des pins (définies dans Config.h)
static const int PIN_LORA_SS = PIN_XL1278_SS;
static const int PIN_LORA_RST = PIN_XL1278_RST;
static const int PIN_LORA_DIO0 = PIN_XL1278_DIO0;
static const int PIN_LORA_MISO = PIN_XL1278_MISO;
static const int PIN_LORA_MOSI = PIN_XL1278_MOSI;
static const int PIN_LORA_SCLK = PIN_XL1278_SCK;

// Aliases des paramètres (définis dans Config.h)
static const long LORA_FREQUENCY = LORA_FREQUENCY_433;

#endif // LORA_CONFIG_XL1278_H

