#include <Arduino.h>
#include "../lora/LoRaConfig.h"
#include "../storage/NVSManager.h"
#include "../security/SecurityManager.h"
#include "../lora/LoRaModule.h"
#include "../security/PairingManager.h"
#include "../protocol/FragmentManager.h"
#include "../utils/HeartbeatManager.h"
#include "../security/DiscoveryManager.h"
#include "../lora/PacketHandler.h"

// Variables globales pour les managers
static NVSManager* nvsManager = nullptr;
static SecurityManager* securityManager = nullptr;
static LoRaModule* loraModule = nullptr;
static PairingManager* pairingManager = nullptr;
static FragmentManager* fragmentManager = nullptr;
static HeartbeatManager* heartbeatManager = nullptr;
static DiscoveryManager* discoveryManager = nullptr;
static PacketHandler* packetHandler = nullptr;

// État global
static uint32_t deviceId = 0xA1B2C3D4;
static uint32_t seqNumber = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Serial.println();
  Serial.println("==== Demo LoRa ESP32 (E220-900T22D LLCC68) ====");
  #if E220_PIN_MODE == MODE_MINIMAL
  Serial.println("Mode pins: MINIMAL (RX+TX seulement)");
  #elif E220_PIN_MODE == MODE_RECOMMANDE
  Serial.println("Mode pins: RECOMMANDE (RX+TX+AUX)");
  #elif E220_PIN_MODE == MODE_COMPLET
  Serial.println("Mode pins: COMPLET (RX+TX+AUX+M0+M1)");
  #endif
  
	// Initialiser les managers
	nvsManager = new NVSManager();
	securityManager = new SecurityManager();
	loraModule = new LoRaModule();
	
	// Initialiser le SecurityManager
	if (!securityManager->init()) {
		Serial.println("[SEC] ERREUR: Echec init SecurityManager");
		while (true) delay(1000);
	}
	
	// Charger ou générer le Device ID
	if (!nvsManager->loadDeviceId(deviceId)) {
		Serial.println("[NVS] Erreur lors du chargement du Device ID");
	}
	
	Serial.print("Device ID: 0x");
	Serial.println(deviceId, HEX);
	
	// Initialiser le module LoRa
	if (!loraModule->begin()) {
		Serial.println("[LoRa] ERREUR: Echec init LoRa");
		while (true) delay(1000);
	}
	
	// Initialiser les autres managers
	pairingManager = new PairingManager(securityManager, loraModule, nvsManager);
	pairingManager->setDeviceId(deviceId);
	
	fragmentManager = new FragmentManager(securityManager, loraModule);
	heartbeatManager = new HeartbeatManager(securityManager, loraModule);
	discoveryManager = new DiscoveryManager(loraModule);
	packetHandler = new PacketHandler(pairingManager, fragmentManager, 
	                                  heartbeatManager, discoveryManager);
	
	// Charger l'état d'appairage
	pairingManager->loadPairingState();
	
	Serial.print("[NVS] État d'appairage au démarrage: ");
	Serial.println(pairingManager->isPaired() ? "Appairé" : "Non appairé");
	
	Serial.println("Mode: BIDIRECTIONNEL (RX/TX)");
}

void loop() {
	// Réception de paquets
	if (loraModule->available()) {
		std::vector<uint8_t> buffer;
		if (loraModule->receiveMessage(buffer)) {
			packetHandler->handlePacket(buffer, deviceId, 
			                           pairingManager->isPaired(),
			                           pairingManager->getSessionKey(),
			                           pairingManager);
		}
	}
	
	// Envoi de beacons (si mode pairing activé)
	discoveryManager->sendBeaconIfDue(deviceId);
	
	// Envoi de heartbeat (si appairé)
	heartbeatManager->sendHeartbeatIfDue(deviceId, 
	                                    pairingManager->getSessionKey(),
	                                    pairingManager->isPaired(),
	                                    fragmentManager->isTransmitting());
	
	// Affichage des devices découverts
	discoveryManager->printDiscoveredIfDue();
	
	// Maintenance des fragments
	fragmentManager->purgeOldFragments();
	fragmentManager->processPendingRetries();
	
	// Mise à jour de l'état en ligne
	heartbeatManager->updateAndSendOnlineStatus(pairingManager->isPaired(),
	                                           pairingManager->getPairedDeviceId());
	
	// Commandes série
	if (Serial.available()) {
		String line = Serial.readStringUntil('\n');
		line.trim();
		
		if (line.equalsIgnoreCase("ID")) {
			Serial.print("DeviceId: 0x");
			Serial.println(deviceId, HEX);
		} 
		else if (line.length() > 2 && (line[0] == 'B' || line[0] == 'b') && line[1] == ' ') {
			// B <hexId> - Initier un appairage
			String hex = line.substring(2);
			hex.trim();
			hex.toUpperCase();
			uint32_t tgt = 0;
			for (size_t i = 0; i < hex.length(); ++i) {
				char c = hex[i];
				uint8_t v = 0;
				if (c >= '0' && c <= '9') v = c - '0';
				else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
				else continue;
				tgt = (tgt << 4) | v;
			}
			Serial.print("[BIND] Init vers 0x");
			Serial.println(tgt, HEX);
			pairingManager->sendBindRequest(tgt);
		} 
		else if (line.equalsIgnoreCase("A")) {
			// A - Accepter une demande d'appairage
			if (!pairingManager->acceptPendingBind()) {
				Serial.println("[BIND] Rien à accepter.");
			}
		} 
		else if (line.equalsIgnoreCase("C")) {
			// C - Annuler une demande d'appairage
			pairingManager->cancelPendingBind();
		} 
		else if (line.equalsIgnoreCase("PAIR ON")) {
			discoveryManager->setPairingMode(true);
			Serial.println("[PAIR] Mode pairing: ON");
		} 
		else if (line.equalsIgnoreCase("PAIR OFF")) {
			discoveryManager->setPairingMode(false);
			Serial.println("[PAIR] Mode pairing: OFF");
		} 
		else if (line.equalsIgnoreCase("LIST")) {
			discoveryManager->printDiscoveredIfDue();
		} 
		else if (line.length() > 2 && (line[0] == 'S' || line[0] == 's') && line[1] == ' ') {
			// S <message> - Envoyer un message sécurisé
			String msg = line.substring(2);
			if (pairingManager->isPaired()) {
				fragmentManager->sendSecureMessage(msg, pairingManager->getSessionKey(), seqNumber);
			} else {
				Serial.println("[SEC] Non appairé.");
			}
		} 
		else if (line.equalsIgnoreCase("UNPAIR")) {
			pairingManager->clearPairingState();
		} 
		else if (line.equalsIgnoreCase("STATUS")) {
			Serial.print("[STATUS] État d'appairage: ");
			Serial.println(pairingManager->isPaired() ? "Appairé" : "Non appairé");
			Serial.print("[STATUS] Device ID: 0x");
			Serial.println(deviceId, HEX);
			Serial.print("[STATUS] Mode pairing: ");
			Serial.println(discoveryManager->isPairingMode() ? "ON" : "OFF");
			if (pairingManager->isPaired()) {
				bool online = heartbeatManager->isPairedDeviceOnline();
				Serial.print("[STATUS] Device appairé en ligne: ");
				Serial.println(online ? "OUI" : "NON");
				if (pairingManager->getPairedDeviceId() != 0) {
					Serial.print("[STATUS] Device appairé ID: 0x");
					Serial.println(pairingManager->getPairedDeviceId(), HEX);
				}
			}
		} 
		else if (line.equalsIgnoreCase("CONFIG")) {
			// CONFIG - Forcer la configuration du module
			loraModule->configureForTransparentMode(true);
		} 
		else if (line.equalsIgnoreCase("RESET")) {
			// RESET - Remettre le module en mode normal
			#if E220_PIN_MODE == MODE_COMPLET
			Serial.println("[LoRa] Remise en mode normal...");
			loraModule->setMode(MODE_0_NORMAL);
			delay(200);
			Serial.println("[LoRa] Mode normal activé");
			#else
			Serial.println("[LoRa] Commande RESET disponible uniquement en mode COMPLET");
			#endif
		}
	}
}

