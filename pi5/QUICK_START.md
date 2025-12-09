# Guide de démarrage rapide - Gateway LoRa Pi 5

## 🚀 Installation rapide

### 1. Dépendances système

```bash
sudo apt update
sudo apt upgrade -y
sudo apt install -y git python3-pip python3-venv
```

### 2. Configuration système (première fois)

```bash
# Activation UART
sudo raspi-config
# Interface Options → Serial Port → Yes

# Permissions utilisateur
sudo usermod -aG dialout $USER
# Déconnecter/reconnecter pour que les changements prennent effet
```

### 3. Installation du projet

```bash
cd ~/Documents/TestLora/pi5
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

### 4. Test de base

```bash
python3 lora_gateway.py
```

---

## 📡 Avec MQTT

### Installation

```bash
./install_mqtt.sh
```

### Test

**Terminal 1** :
```bash
mosquitto_sub -h localhost -t "lora/data" -v
```

**Terminal 2** :
```bash
python3 lora_gateway.py --mqtt
```

---

## 📊 Avec Node-RED

### Installation

```bash
./install_nodered.sh
```

### Configuration

1. Ouvrir http://localhost:1880
2. Menu (☰) → **Import** → Sélectionner `node-red-flow.json`
3. Cliquer sur **Deploy**
4. Accéder au dashboard : http://localhost:1880/ui

---

## 🔒 Sécurisation

```bash
./secure_gateway.sh
```

---

## 🐛 Dépannage rapide

### Port série inaccessible

```bash
ls -l /dev/ttyAMA0
groups $USER  # doit contenir "dialout"
```

### Messages non reçus

1. Vérifier le canal (23 par défaut)
2. Vérifier l'antenne et les connexions
3. Vérifier les permissions

### MQTT ne fonctionne pas

```bash
sudo systemctl status mosquitto
mosquitto_sub -h localhost -t "lora/data" -v
```

---

**Besoin d'aide ?** Consultez **`README.md`** pour la documentation complète.
