#!/bin/bash
# Script de sécurisation de la gateway LoRa
# Conforme au TD RPi LoRa Gateway - Étape 7

set -e  # Arrêter en cas d'erreur

echo "=========================================="
echo "Sécurisation de la Gateway LoRa"
echo "=========================================="

# 1. Changer le mot de passe par défaut
echo "🔐 1. Changement du mot de passe..."
echo "   Exécutez manuellement: passwd"
echo "   (Appuyez sur Entrée pour continuer...)"
read -r

# 2. Installer et configurer le pare-feu
echo ""
echo "🔥 2. Configuration du pare-feu (ufw)..."

# Vérifier si ufw est déjà installé
if ! command -v ufw &> /dev/null; then
    echo "📦 Installation de ufw..."
    sudo apt update
    sudo apt install -y ufw
fi

# Autoriser SSH (essentiel pour ne pas perdre l'accès)
echo "   Autorisation de SSH..."
sudo ufw allow ssh
sudo ufw allow 22/tcp
echo "   ✅ Port 22 (SSH) autorisé"

# Autoriser MQTT si utilisé
read -p "   Autoriser MQTT (port 1883)? (o/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Oo]$ ]]; then
    sudo ufw allow 1883/tcp
    echo "   ✅ Port 1883 (MQTT) autorisé"
fi

# Autoriser Node-RED si utilisé
read -p "   Autoriser Node-RED (port 1880)? (o/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Oo]$ ]]; then
    sudo ufw allow 1880/tcp
    echo "   ✅ Port 1880 (Node-RED) autorisé"
fi

# Activer le pare-feu
echo ""
echo "   Activation du pare-feu..."
sudo ufw --force enable

# Afficher le statut
echo ""
echo "📊 Statut du pare-feu:"
sudo ufw status verbose

echo ""
echo "=========================================="
echo "✅ Sécurisation terminée!"
echo "=========================================="
echo ""
echo "Recommandations supplémentaires:"
echo "  - Désactiver les services inutiles"
echo "  - Configurer l'authentification MQTT"
echo "  - Utiliser HTTPS pour Node-RED (si exposé)"
echo "  - Configurer des sauvegardes régulières"
echo ""

