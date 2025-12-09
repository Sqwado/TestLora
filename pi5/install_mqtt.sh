#!/bin/bash
# Script d'installation de Mosquitto MQTT pour Raspberry Pi
# Conforme au TD RPi LoRa Gateway - Étape 5

set -e  # Arrêter en cas d'erreur

echo "=========================================="
echo "Installation de Mosquitto MQTT"
echo "=========================================="

# Mise à jour des paquets
echo "📦 Mise à jour des paquets..."
sudo apt update
sudo apt upgrade -y

# Installation de Mosquitto
echo "📦 Installation de Mosquitto..."
sudo apt install -y mosquitto mosquitto-clients

# Démarrer et activer le service
echo "🚀 Démarrage du service Mosquitto..."
sudo systemctl enable mosquitto
sudo systemctl start mosquitto

# Attendre que le service soit prêt
sleep 2

# Vérifier le statut
echo "✅ Vérification du statut..."
if sudo systemctl is-active --quiet mosquitto; then
    echo "✅ Mosquitto est actif et fonctionne"
    sudo systemctl status mosquitto --no-pager | head -n 5
else
    echo "❌ Erreur: Mosquitto n'est pas actif"
    exit 1
fi

echo ""
echo "=========================================="
echo "✅ Installation terminée!"
echo "=========================================="
echo ""
echo "Pour tester MQTT:"
echo "  Terminal 1 (subscribe):"
echo "    mosquitto_sub -h localhost -t 'lora/data' -v"
echo ""
echo "  Terminal 2 (publish):"
echo "    mosquitto_pub -h localhost -t 'lora/data' -m 'test'"
echo ""
echo "Pour configurer l'authentification:"
echo "  sudo mosquitto_passwd -c /etc/mosquitto/passwd username"
echo "  Puis éditer /etc/mosquitto/mosquitto.conf"
echo ""

