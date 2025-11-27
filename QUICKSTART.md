# 🚀 Quick Start - Système LoRa ESP32

Guide rapide pour démarrer en 5 minutes.

---

## ⚡ Installation rapide

### 1. Matériel requis

**Minimum** :
- 1× ESP32 DevKit
- 1× Module E220-900T22D (900 MHz) **OU** XL1278-SMT (433 MHz)
- Câbles Dupont + Antenne

**Optionnel** :
- Capteur HLK-LD2450 (24GHz)
- Second module LoRa (mode dual)

---

## 🔌 Branchements express

### Module E220-900 (900 MHz)

```
E220      →  ESP32
─────────────────────
VCC       →  3.3V
GND       →  GND
TXD       →  GPIO 16 (RX)
RXD       →  GPIO 17 (TX)
M0        →  GPIO 2
M1        →  GPIO 15
```

⚠️ **Antenne SMA-K obligatoire !**

### Module XL1278 (433 MHz)

```
XL1278    →  ESP32
─────────────────────
VCC       →  3.3V (STRICT)
GND       →  GND
MOSI      →  GPIO 23
MISO      →  GPIO 19
SCK       →  GPIO 18
NSS       →  GPIO 5
DIO0      →  GPIO 26
RST       →  GPIO 14
```

### Capteur 24GHz (optionnel)

```
LD2450    →  ESP32
─────────────────────
5V        →  VIN (5V)
GND       →  GND
TX        →  GPIO 25 (RX)
RX        →  GPIO 26 (TX)
```

⚠️ **5V strict, 256000 bauds**

---

## ⚙️ Configuration - 3 étapes

### Étape 1 : Choisir le module

Éditer `src/Config.h` lignes 13-15 :

```cpp
// Décommentez UNE ligne :
#define MODULE_E220_900     // E220 900MHz
// #define MODULE_XL1278_433   // XL1278 433MHz
// #define MODULE_DUAL         // Les deux
```

### Étape 2 : Choisir le mode

```cpp
// MODE SIMPLE = Broadcast (tests)
#define MODE_SIMPLE

// MODE COMPLET = Appairage + Crypto (commentez MODE_SIMPLE)
// #define MODE_SIMPLE
```

### Étape 3 : ID unique

```cpp
#define DEVICE_ID  1  // CHANGER pour chaque module !
```

---

## 🔨 Compilation & Flash

```bash
# Compiler
pio run

# Flasher
pio run -t upload

# Moniteur série
pio device monitor -b 115200

# Tout en un
pio run -t upload && pio device monitor -b 115200
```

---

## 🎮 Test rapide

### Test 1 : Module seul

```
> TEXT Hello
[TX] OK (8 bytes)
```

✅ **Fonctionne** : Module configuré !

### Test 2 : Capteur 24GHz

Activer dans `Config.h` :
```cpp
#define USE_HUMAN_SENSOR_24GHZ
```

Commande :
```
> SENSOR_TEST
```

✅ **Fonctionne** : Données capteur affichées !

### Test 3 : Appairage (2 modules)

**Module A** (ID=1) :
```
> PAIR ON
> LIST
> B 02         # Bind vers module 2
```

**Module B** (ID=2) :
```
> PAIR ON
> A            # Accept
```

✅ **Fonctionne** : `[BIND] ✅ Appairage réussi !`

---

## 🖥️ Interface GUI

### Dashboard avec radar

```bash
pip install pyserial tkinter
python dashboard_gui.py
```

**Affichage** :
- 🎯 Radar visuel (moitié haute, ±60°)
- 📊 Cartes capteurs multiples
- 📡 Données temps réel

### GUI classique

```bash
python gui_app.py
```

**Fonctions** :
- Appairage simplifié
- Envoi messages
- Liste modules

---

## 🔧 Configurations typiques

### Config 1 : Test simple (1 module)

```cpp
#define MODULE_E220_900
#define MODE_SIMPLE
#define DEVICE_ID  1
// #define USE_ENCRYPTION
```

**Utilisation** : Tests, broadcast

### Config 2 : Production sécurisée

```cpp
#define MODULE_E220_900
// #define MODE_SIMPLE      // Commenté = mode complet
#define USE_ENCRYPTION
#define DEVICE_ID  1        // Différent sur chaque module
```

**Utilisation** : Déploiement, sécurité

### Config 3 : Capteur + Dual

```cpp
#define MODULE_DUAL
#define MODE_SIMPLE
#define USE_HUMAN_SENSOR_24GHZ
#define DEVICE_ID  2
```

**Utilisation** : Multi-bandes + détection

---

## 🐛 Problèmes courants

### Compilation échoue

```bash
# Nettoyer et recompiler
pio run -t clean
pio run
```

### Module ne répond pas

1. Vérifier **antenne connectée**
2. Vérifier pins M0/M1 (E220)
3. Vérifier alimentation 3.3V/5V

### Capteur 24GHz : données aberrantes

1. Vérifier **5V** (pas 3.3V)
2. Vérifier baudrate **256000**
3. Position : **1.5-2m hauteur, face zone**

### Appairage impossible

1. Vérifier **IDs différents** sur les 2 modules
2. `UNPAIR` pour reset
3. Vérifier `USE_ENCRYPTION` sur les 2

---

## 📚 Documentation complète

➡️ Voir **README.md** pour :
- Architecture détaillée
- Protocole cryptographique
- Commandes complètes
- Troubleshooting avancé

---

## 🎯 Checklist démarrage

- [ ] Matériel câblé selon schéma
- [ ] Antenne(s) connectée(s)
- [ ] `Config.h` édité (module + mode + ID)
- [ ] Code compilé sans erreur
- [ ] Flashé sur ESP32
- [ ] Moniteur série 115200 bauds
- [ ] Message `[LoRa] Module initialisé` affiché
- [ ] Test `TEXT Hello` fonctionne

✅ **Prêt à l'emploi !**
