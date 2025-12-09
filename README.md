# 🌐 Système IoT Multi-Module LoRa ESP32

> 🚀 **Démarrage rapide** : Voir [QUICKSTART.md](QUICKSTART.md) pour une installation en 5 minutes  
> 📝 **Compte rendu TD** : Voir [TD_GATEWAY_REPORT.md](TD_GATEWAY_REPORT.md) pour le suivi pédagogique

## 📋 Vue d'ensemble

Système de communication IoT sécurisé basé sur ESP32 avec support multi-bandes (433 MHz / 900 MHz) et capteur de détection humaine 24GHz. Architecture modulaire permettant différentes configurations selon les besoins.

### Fonctionnalités principales

- ✅ **Multi-bandes** : Support des modules 433 MHz (XL1278-SMT) et 900 MHz (E220-900T22D)
- ✅ **Capteur 24GHz** : Détection et comptage automatique d'humains (HLK-LD2450)
- ✅ **Sécurité avancée** : Appairage ECDH (secp256r1) + chiffrement AES-128-CTR + HMAC-SHA256
- ✅ **Protocole personnalisé** : Messages binaires structurés avec typage
- ✅ **Interface GUI Python** : Gestion simplifiée de l'appairage et communication
- ✅ **Mode dual** : Utilisation simultanée de deux modules LoRa
- ✅ **Gateway Raspberry Pi** : Réception 433/900 MHz + stockage SQLite + TUI (`raspberry/`)

---

## 🏗️ Architecture du système

### Schéma général

```
┌─────────────────────────────────────────────────────────────┐
│                      ESP32 (DevKit)                         │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │   Module     │  │   Module     │  │   Capteur    │       │
│  │  E220-900    │  │  XL1278-433  │  │   24GHz      │       │
│  │  (900 MHz)   │  │  (433 MHz)   │  │  (HLK-LD2450)│       │
│  │              │  │              │  │              │       │
│  │  UART (RX/TX)│  │  SPI (MOSI/  │  │  UART (RX/TX)│       │
│  │  GPIO 16/17  │  │  MISO/SCK)   │  │  GPIO 25/26  │       │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘       │
│         │                 │                 │               │
│         └──────────┬──────┴─────────────────┘               │
│                    │                                        │
│         ┌──────────▼──────────┐                             │
│         │  Logique principale │                             │
│         │  - Protocole        │                             │
│         │  - Chiffrement      │                             │
│         │  - Appairage        │                             │
│         └─────────────────────┘                             │
└─────────────────────────────────────────────────────────────┘
                         │
                         │ LoRa 433 MHz / 900 MHz
                         ▼
            ┌────────────────────────┐
            │  Autres modules ESP32  │
            │  du réseau             │
            └────────────────────────┘
```

### Configurations possibles

| Configuration | Module(s) utilisé(s) | Capteur 24GHz | Mode |
|---------------|----------------------|---------------|------|
| **Config 1** | E220-900 (900 MHz) | ❌ | Simple ou Complet |
| **Config 2** | XL1278 (433 MHz) | ❌ | Simple uniquement |
| **Config 3** | E220-900 + XL1278 (Dual) | ❌ | Simple ou Complet |
| **Config 4** | E220-900 (900 MHz) | ✅ | Simple ou Complet |
| **Config 5** | E220-900 + XL1278 (Dual) | ✅ | Simple ou Complet |

**Mode Simple** : Communication broadcast uniquement (pas d'appairage)
**Mode Complet** : Appairage sécurisé ECDH + chiffrement AES-128

---

## 🔌 Branchements détaillés

### 1️⃣ Module E220-900T22D (UART - 900 MHz)

**Specs** : LLCC68 | 850-930 MHz (défaut 873) | 22dBm | 5km portée | UART | 3.3-5V | **Antenne SMA-K obligatoire**

**Connexions E220 → ESP32** :

| Pin E220 | Pin ESP32 | Description |
|----------|-----------|-------------|
| VCC | 3.3V | Alimentation |
| GND | GND | Masse |
| TXD | GPIO 16 | TX module → RX ESP32 |
| RXD | GPIO 17 | RX module ← TX ESP32 |
| M0 | GPIO 2 | Mode config (LOW = normal) |
| M1 | GPIO 15 | Mode config (LOW = normal) |
| AUX | GPIO 4 | Auxiliaire (optionnel) |

⚠️ **Antenne SMA-K obligatoire** - Ne jamais transmettre sans antenne !

### 2️⃣ Module XL1278-SMT (SPI - 433 MHz)

**Specs** : SX1278 | 410-525 MHz (433 ISM) | 20dBm | 2km portée | SPI | **3.3V strict** | Antenne 433 MHz

**Connexions XL1278 → ESP32** :

| Pin XL1278 | Pin ESP32 | Description |
|------------|-----------|-------------|
| VCC | 3.3V | **Strict 3.3V uniquement !** |
| GND | GND | Masse |
| MOSI | GPIO 23 | SPI Master Out |
| MISO | GPIO 19 | SPI Master In |
| SCK | GPIO 18 | SPI Clock |
| NSS (CS) | GPIO 5 | Chip Select |
| DIO0 | GPIO 26 | Interrupt |
| RST | GPIO 14 | Reset |

⚠️ **Pas de 5V toléré sur ce module** - Antenne 433 MHz obligatoire

### 3️⃣ Capteur Humain 24GHz HLK-LD2450 (optionnel)

**Specs** : Radar FMCW 24GHz | 3 cibles max | Portée 6m | ±60° azimut, ±35° élévation | UART 256000 bauds | 5V/120mA | Refresh 10Hz | Installation murale 1.5-2m

**Connexions HLK-LD2450 → ESP32** :

| Pin LD2450 | Pin ESP32 | Description |
|------------|-----------|-------------|
| 5V | 5V (VIN) | Alimentation **5V strict** (>200mA) |
| GND | GND | Masse |
| TX | GPIO 25 | TX capteur → RX ESP32 |
| RX | GPIO 26 | RX capteur ← TX ESP32 |

⚠️ **UART 256000 bauds** - Antenne patch 24GHz intégrée - Installation murale 1.5-2m

---

## ⚙️ Configuration du système

### Sélection du module

Dans `src/Config.h`, décommentez **UNE** ligne : `MODULE_E220_900` (900MHz) | `MODULE_XL1278_433` (433MHz) | `MODULE_DUAL` (les deux)

### Sélection du mode

**MODE_SIMPLE** : Broadcast uniquement (tests/démo)  
**Mode Complet** : Commentez `MODE_SIMPLE` → Appairage ECDH + Chiffrement AES (production)

### Options avancées (`src/Config.h`)

**Protocole** : `USE_CUSTOM_PROTOCOL`, `DEVICE_ID` (0-255, unique par module)  
**Chiffrement** : `USE_ENCRYPTION` (AES-128-CTR + HMAC) - ⚠️ Même clé sur tous les modules  
**Capteur 24GHz** : `USE_HUMAN_SENSOR_24GHZ`, `AUTO_SEND_INTERVAL`  
**LoRa** : Fréquence (433/868/915 MHz), SF (7-12), BW (125/250/500 kHz), Power (2-20 dBm)

---

## 🔐 Système de cryptographie

### Architecture de sécurité

**4 couches** : Anti-rejeu (Nonces+Séquence) | Intégrité (HMAC-SHA256) | Confidentialité (AES-128-CTR) | Échange de clés (ECDH secp256r1)

### 1. Appairage dynamique ECDH (Elliptic Curve Diffie-Hellman)

**Courbe** : `secp256r1` (NIST P-256, 256 bits)

**Flux** :
1. **Initiateur** → `BIND_REQ` : pubKeyA (65B) + nonceI (16B)
2. **Répondeur** → `BIND_RESP` : pubKeyB (65B) + nonceR (16B) + MAC (16B)
3. **Initiateur** → `BIND_CONFIRM` : MAC (16B)
4. **Dérivation** : sessionKey = SHA256(shared || nonceI || nonceR)[0..15]

**Sécurité** : Pas de PIN, nonces anti-rejeu, MAC bidirectionnel, éphémère

### 2. Chiffrement des messages (AES-128-CTR)

**Structure** : `MAGIC(1) | TYPE(1) | SEQ(4) | IV(16) | CIPHERTEXT | MAC(16)`
- **AES-128-CTR** : Clé 128 bits, IV aléatoire par message
- **HMAC-SHA256** : Tronqué à 16 bytes pour intégrité
- **Avantages** : Pas de padding, rapide, parallélisable

**Flux** : Générer IV → Chiffrer → Calculer MAC → Transmettre | Vérifier MAC → Déchiffrer

### 3. Protection contre le rejeu

**Mécanismes** : Nonces (16B), Compteur de séquence (32 bits), IV aléatoire unique

Le système rejette les messages avec un numéro de séquence ≤ au dernier reçu.

### 4. Persistance NVS

**Sauvegarde auto** : Clé de session (16B), Device ID partenaire, Statut, Compteur de séquence
**Restauration** : Au démarrage ESP32 | **Effacement** : Commande `UNPAIR`

---

## 📦 Protocole de messages

### Format général

**Clairs** : `MAGIC(0x02,1) | TYPE(1) | SOURCE(1) | SIZE(1) | PAYLOAD(0-255)`  
**Chiffrés** : `MAGIC(0x01,1) | TYPE(1) | SEQ(4) | IV(16) | CIPHERTEXT | MAC(16)`

### Types de messages

| Type | Valeur | Description | Payload |
|------|--------|-------------|---------|
| `PKT_BEACON` | 0x10 | Beacon d'appairage | deviceId (4 bytes) |
| `PKT_BIND_REQ` | 0x11 | Demande d'appairage | pubKey + nonceI |
| `PKT_BIND_RESP` | 0x12 | Réponse d'appairage | pubKey + nonceR + MAC |
| `PKT_BIND_CONFIRM` | 0x13 | Confirmation | MAC |
| `PKT_DATA` | 0x20 | Données chiffrées | Message utilisateur |
| `MSG_TYPE_TEXT` | 0x00 | Message texte | String (max 200 bytes) |
| `MSG_TYPE_PING` | 0x01 | Ping | Vide |
| `MSG_TYPE_HUMAN_DETECT` | 0x02 | Détection humaine | 1 byte (0/1) |
| `MSG_TYPE_HUMAN_COUNT` | 0x03 | Comptage humain | 1 byte (0-255) |
| `MSG_TYPE_TEMP` | 0x04 | Température | 4 bytes (float) |

### Exemples

- **Beacon** : `02 10 01 04 A1 B2 C3 D4` (deviceId 0xA1B2C3D4)
- **Texte chiffré** : `01 20 00 00 00 42 [IV 16B] [Cipher] [MAC 16B]`
- **Comptage humain** : `02 03 01 01 03` (3 humains)

---

## 📂 Structure du code

### Arborescence du projet

```
TestLora/
├── platformio.ini              # Configuration PlatformIO
├── README.md                   # Documentation complète
├── QUICKSTART.md               # 🚀 Guide démarrage rapide (5 min)
├── raspberry/                  # 🥧 Passerelle Raspberry Pi + SQLite + TUI
├── gui_app.py                  # Interface Python classique
├── dashboard_gui.py            # Interface Dashboard avec radar
├── requirements.txt            # Dépendances Python
│
├── src/
│   ├── main.cpp                # Point d'entrée (sélection module/mode)
│   ├── Config.h                # ⭐ Configuration centralisée
│   │
│   ├── modes/                  # 🎮 Modes d'exécution
│   │   ├── main_simple.cpp     #    Mode simple E220 (broadcast)
│   │   ├── main_complet.cpp    #    Mode complet E220 (appairage+crypto)
│   │   ├── main_xl1278.cpp     #    Mode XL1278 (SPI, 433 MHz)
│   │   ├── main_dual.cpp       #    Mode dual simple (E220+XL1278)
│   │   └── main_dual_complet.cpp #  Mode dual complet
│   │
│   ├── lora/                   # 📡 Communication LoRa
│   │   ├── LoRaModule.cpp/.h   #    Abstraction module E220
│   │   ├── LoRaConfig.h        #    Configuration E220
│   │   ├── LoRaConfig_XL1278.h #    Configuration XL1278
│   │   └── PacketHandler.cpp/.h #   Gestion des paquets
│   │
│   ├── security/               # 🔐 Sécurité et appairage
│   │   ├── Encryption.h        #    Chiffrement AES-128 + clé
│   │   ├── SecurityManager.cpp/.h # Gestion de la sécurité
│   │   ├── PairingManager.cpp/.h  # Gestion de l'appairage ECDH
│   │   └── DiscoveryManager.cpp/.h # Découverte des modules
│   │
│   ├── protocol/               # 📦 Protocole de communication
│   │   ├── MessageProtocol.h   #    Définition du protocole binaire
│   │   ├── PacketTypes.h       #    Types de paquets
│   │   └── FragmentManager.cpp/.h # Fragmentation de messages
│   │
│   ├── sensors/                # 📡 Capteurs
│   │   └── HumanSensor24GHz.h  #    Capteur radar 24GHz
│   │
│   ├── storage/                # 💾 Persistance
│   │   └── NVSManager.cpp/.h   #    Gestion NVS (appairage)
│   │
│   └── utils/                  # 🛠️ Utilitaires
│       ├── Common.h            #    ⭐ Fonctions utilitaires communes
│       └── HeartbeatManager.cpp/.h # Heartbeat/Keep-alive
│
└── lib/
    └── [Bibliothèques PlatformIO]
```

**⭐ Code optimisé (v2.2)** :
- ✅ **Zéro redondance** : Config centralisée dans `Config.h` (source unique)
- ✅ **Fonctions communes** : `utils/Common.h` évite duplication (~250 lignes)
- ✅ **Conflits résolus** : Constantes temporelles/capteur unifiées (11 erreurs corrigées)
- ✅ **HLK-LD2450 conforme** : Parsing selon manuel officiel V1.00
- ✅ **Code DRY** : Don't Repeat Yourself appliqué partout
- ✅ **Organisation claire** : 7 dossiers thématiques, 30 fichiers, 100% utilisés
- ✅ **Maintenabilité** : Un seul endroit à modifier pour chaque configuration

### Fichiers clés

**`Config.h`** ⭐ : Config centralisée (pins, fréquences, modes, options)  
**`protocol/MessageProtocol.h`** : Types de messages + encodage/décodage  
**`security/Encryption.h`** : AES-128-CTR + HMAC-SHA256  
**`security/PairingManager`** : Appairage ECDH complet  
**`sensors/HumanSensor24GHz.h`** : Interface capteur HLK-LD2450  
**`utils/Common.h`** ⭐ : Fonctions communes (évite duplication)  
**`lora/LoRaModule`** : Abstraction E220  
**`storage/NVSManager`** : Persistance appairage  
**`modes/`** : Implémentations simple/complet/dual

---

## 🚀 Compilation et flashage

### Configuration PlatformIO

**Plateforme** : ESP32 (espressif32) | **Board** : esp32dev | **Monitor** : 115200 bauds  
**Libs** : EByte LoRa E220 (v1.0.8+), LoRa Sandeep Mistry (v0.8.0+), mbedTLS

### Commandes

```bash
pio run                                # Compiler
pio run -t upload                      # Flasher
pio device monitor -b 115200           # Moniteur série
pio run -t upload && pio device monitor -b 115200  # Tout en un
```

### Premiers tests

1. **LoRa seul** : `Config.h` → `MODULE_E220_900` + `MODE_SIMPLE` → Vérifier init
2. **Capteur 24GHz** : `Config.h` → `USE_HUMAN_SENSOR_24GHZ` → `SENSOR_TEST` (5s)
3. **Appairage** : 2 ESP32 (IDs différents) → Mode complet → `PAIR ON` → `B <ID>` / `A` → `S Hello`

---

## 🎮 Commandes série

### Commandes de base

### Mode simple (broadcast)

| Commande | Paramètre | Description | Exemple |
|----------|-----------|-------------|---------|
| `TEXT` | `<message>` | Envoyer un message texte | `TEXT Hello World` |
| `PING` | - | Envoyer un ping | `PING` |
| `TEMP` | `<valeur>` | Envoyer une température (°C) | `TEMP 23.5` |
| `ENV` | `<temp> <pression> [humidité]` | Paquet compressé température + pression (+ humidité optionnelle) | `ENV 23.8 1012.7 46` |
| `HUMAN_COUNT` | `[nombre]` | Envoyer comptage humain (capteur ou manuel) | `HUMAN_COUNT` ou `HUMAN_COUNT 3` |
| `SENSOR_TEST` | - | Test du capteur 24GHz (affichage brut 5s) | `SENSOR_TEST` |
| `AUTO_ON` | - | Activer envoi automatique capteur | `AUTO_ON` |
| `AUTO_OFF` | - | Désactiver envoi automatique capteur | `AUTO_OFF` |

### Mode complet (avec appairage)

**Découverte et appairage** :

| Commande | Paramètre | Description | Exemple |
|----------|-----------|-------------|---------|
| `PAIR ON` | - | Activer mode appairage (beacons) | `PAIR ON` |
| `PAIR OFF` | - | Désactiver mode appairage | `PAIR OFF` |
| `LIST` | - | Afficher la liste des modules détectés | `LIST` |
| `ID` | - | Afficher votre deviceId (hex) | `ID` |
| `B` | `<deviceId>` | Initier appairage vers un module | `B A1B2C3D4` |
| `A` | - | Accepter une demande d'appairage | `A` |
| `C` | - | Annuler la demande en attente | `C` |
| `UNPAIR` | - | Supprimer l'appairage (efface NVS) | `UNPAIR` |
| `STATUS` | - | Afficher l'état d'appairage actuel | `STATUS` |

**Messages sécurisés** (après appairage) :

| Commande | Paramètre | Description | Exemple |
|----------|-----------|-------------|---------|
| `S` | `<message>` | Envoyer un message chiffré | `S Secret message` |
| `HUMAN_COUNT` | `[nombre]` | Envoyer comptage humain chiffré | `HUMAN_COUNT` |

### Exemples

**Broadcast** : `TEXT Hello` → `[TX] OK (8 bytes)`

**Appairage** :
- Module A : `PAIR ON` → `LIST` → `B 02` → `[BIND] ✅ Réussi`
- Module B : `PAIR ON` → `A` (accepter) → `[BIND] ✅ Réussi`
- Module A : `S Secret` → `[TX] Message chiffré`

**Capteur** : Détection changement → Auto-envoi si intervalle écoulé

---

## 🖥️ Interface GUI Python (optionnelle)

### Installation & Lancement

```bash
pip install -r requirements.txt  # pyserial>=3.5, tkinter
python gui_app.py        # GUI appairage classique
python dashboard_gui.py  # Dashboard avec radar visuel
```

### Dashboard (`dashboard_gui.py`)

**Vue Radar optimisée** :
- ✅ **Moitié supérieure uniquement** (zone de détection réelle)
- ✅ **Angles ±60°** visualisés (selon specs HLK-LD2450)
- ✅ **Graduation 1-6m** avec demi-cercles
- ✅ **Capteur en bas** (position réaliste)
- ✅ **Filtrage automatique** : Seulement cibles Y>0 (devant capteur)
- ✅ **Cartes capteurs** : Affichage multi-capteurs avec scroll horizontal
- ✅ **Temps réel** : Refresh 10Hz selon données capteur

### GUI Classique (`gui_app.py`)

- ✅ Connexion auto port série + affichage Device ID & état
- ✅ Mode Pairing (PAIR ON/OFF)
- ✅ Liste modules détectés (RSSI/SNR)
- ✅ Double-clic pour appairage
- ✅ Messages temps réel + envoi texte
- ✅ Déappairage

---

## 🔧 Fonctionnement

### Mode Simple
`setup()` → Init Serial (115200), LoRa (868MHz), Capteur 24GHz  
`loop()` → RX LoRa, Update capteur, Commandes série (non-blocking)

### Mode Complet
**Découverte** : Beacons PKT_BEACON toutes les 3s (deviceId), Purge entrées > 15s  
**Appairage** : BIND_REQ → BIND_RESP → BIND_CONFIRM → Dérivation sessionKey → NVS  
**Messages** : Encode → IV → Chiffre AES → MAC → TX | RX → Vérif MAC → Déchiffre

### Capteur 24GHz
**Trames HLK-LD2450** : `AA FF 03 00 [24B data] [2B CRC] 55 CC` (30 bytes)  
**Format cible** : X(2B) Y(2B) Speed(2B) Resolution(2B) - 8 bytes × 3 cibles max  
**Encodage coordonnées** : Bit15=1→positif, Bit15=0→négatif (selon manuel V1.00)  
**Cible invalide** : X=0 ET Y=0 uniquement  
**Résolution** : Valeur technique (ex: 320mm, 360mm), PAS un indicateur de validité  
**Auto-envoi** : Comptage changé + intervalle → TX LoRa

### Persistance NVS
**Namespace** : `lora_pairing` | **Clés** : `paired`, `peerId`, `sessionKey` (16B), `seqCounter`

---

## 🔧 Paramètres configurables

### LoRa
**Recommandations** : Courte portée → SF7/BW250 | Longue portée → SF12/BW125 | Équilibré → SF9/BW125

### Sécurité
ECDH: secp256r1 | AES-128-CTR | HMAC-SHA256 (16B) | Nonces: 16B | IV: 16B

### Intervalles
Beacons: 3s | Discovery: 5s (display), 15s (TTL) | Capteur: 2.5s (auto-send)

---
