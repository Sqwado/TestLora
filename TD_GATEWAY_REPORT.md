# Compte rendu – TD Gateway Raspberry Pi & LoRa

## 1. Objectif

Mettre en œuvre une passerelle locale Raspberry Pi capable de recevoir simultanément les messages LoRa des modules 433 MHz (SX1278) et 868/915 MHz (E220-900), de stocker les données horodatées (SQLite) et de fournir une interface terminal temps réel. Ce travail s’appuie sur le support de TD fourni (`TD RPi LoRa Gateway.pdf`) et le projet de démonstration CircuitDigest (Raspberry Pi + SX1278 ↔ Arduino) [source](file://TD%20RPi%20LoRa%20Gateway.pdf), [CircuitDigest](https://circuitdigest.com/microcontroller-projects/raspberry-pi-with-lora-peer-to-peer-communication-with-arduino).

## 2. Préparation Raspberry Pi

1. Flash Raspbian Lite 64 bits (Balena Etcher).  
2. `sudo raspi-config` → activer SPI + Serial (console désactivée).  
3. `sudo apt update && sudo apt upgrade -y`.  
4. Paquets complémentaires : `sudo apt install git python3-venv python3-dev libffi-dev sqlite3`.  
5. Clonage du dépôt `TestLora`, positionnement dans `raspberry/`.

## 3. Architecture logicielle

```
raspberry/
├── run_gateway.py            # CLI unifiée
├── requirements.txt          # pyserial, pySX127x, rich…
└── gateway/
    ├── config.py             # Dataclasses + lecture ENV/CLI
    ├── protocol.py           # Décodage protocole ESP32
    ├── storage.py            # Persistance SQLite (WAL)
    ├── terminal_ui.py        # TUI Rich (table en direct)
    └── drivers/
        ├── e220.py           # UART 900 MHz
        └── sx127x.py         # SPI 433 MHz
```

Caractéristiques principales :

- Multi-bande simultané (threads dédiés).  
- Décodage complet du protocole `MAGIC + TYPE + ID + LEN + DATA`.  
- Insertion auto dans SQLite (`measurements`).  
- Interface terminal actualisée 5 Hz + mode headless (systemd).  
- Paramétrage par CLI ou variables d’environnement (`LORA433_FREQ`, `LORA_GATEWAY_DB`, etc.).

## 4. Adaptations ESP32/ESP8266

- Nouveau type de message `MSG_TYPE_ENVIRONMENT (0x09)` combinant température, pression et humidité sur 5 octets (compression simple par quantification).
- Commande série `ENV <temp> <pression> [humidité]` disponible en mode simple et dual (`main_simple.cpp`, `main_dual.cpp`).
- GUI Python (`gui_app.py`) mise à jour pour décoder et afficher ce paquet structuré.
- Documentation enrichie (`README.md`, `QUICKSTART.md`) pour refléter ces commandes.

## 5. Procédure de lancement

```bash
cd raspberry
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python run_gateway.py --serial-device /dev/ttyAMA0 --lora433-frequency 433.175
```

Options utiles :

| Option | Rôle |
|--------|------|
| `--disable-900` / `--disable-433` | Tests unitaires par bande |
| `--headless` | Exécution sans TUI (service) |
| `--db /chemin/db.sqlite` | Stockage sur un volume externe |
| `--init-db` | Création du schéma SQLite puis sortie |

## 6. Tests et performances

1. **Validation protocolaire** : envoyer `PING`, `TEMP`, `ENV`, `HUMAN_COUNT` depuis l’ESP32 → vérifier affichage temps réel + insertions SQLite (`sqlite3 gateway.db "SELECT * FROM measurements ORDER BY id DESC LIMIT 5"`).  
2. **Couverture** : reproduire les étapes 4/5 du TD (tests de distance) en loguant RSSI/SNR fournis par le driver SX127x (`metadata` conservée).  
3. **Latence P2P** : mesurer RTT `PING/PONG` via la console ESP32 et noter la valeur dans le rapport.  
4. **Robustesse stockage** : exécuter le script >1h et vérifier l’absence de pertes (compteur de messages vs nombre de lignes).  
5. **Sécurité** : vérifier l’encapsulation AES/HMAC (messages `MAGIC=0x01`) – la gateway enregistre tout de même la trame brute, flagguée « Payload chiffré ».

## 7. To-do / To be continued

- Intégration MQTT (`mosquitto`, Node-RED) comme suggéré dans le TD pour diffusion LAN.  
- Ajout d’un export CSV/InfluxDB depuis la base SQLite.  
- Dashboard web (FastAPI + React) réutilisant `storage.py`.  
- Tests de terrain supplémentaires (distance, obstacles, multi-capteurs).

---

*Dernière mise à jour : 27/11/2025 – équipe TD LoRa M1.*


