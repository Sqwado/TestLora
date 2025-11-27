#include <Arduino.h>
#include "Config.h"

// ============================================
// CONFIGURATION PRINCIPALE
// ============================================
// NOTE: Toutes les configurations sont centralisées dans Config.h
// Ce fichier sert uniquement de point d'entrée pour inclure le bon mode

// ============================================
// INCLUSION DU CODE SELON LE MODULE ET LE MODE
// ============================================

#ifdef MODULE_DUAL
	// Mode DUAL : Les deux modules en même temps
	#ifdef MODE_SIMPLE
		// Mode DUAL simple : Les deux modules en mode broadcast
		#include "modes/main_dual.cpp"
	#else
		// Mode DUAL complet : E220 avec appairage/sécurité + XL1278 simple
		#include "modes/main_dual_complet.cpp"
	#endif

#elif defined(MODULE_E220_900)
	// Module E220-900T22D (UART, 900 MHz) uniquement
	#ifdef MODE_SIMPLE
		// Mode simple : lecture broadcast uniquement
		#include "modes/main_simple.cpp"
	#else
		// Mode complet : avec appairage et sécurité
		#include "modes/main_complet.cpp"
	#endif

#elif defined(MODULE_XL1278_433)
	// Module XL1278-SMT (SPI, 433 MHz) uniquement
	#include "modes/main_xl1278.cpp"

#else
	#error "Aucun module sélectionné! Décommentez MODULE_E220_900, MODULE_XL1278_433 ou MODULE_DUAL dans main.cpp"
#endif
