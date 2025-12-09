#!/bin/bash
# Script d'installation de Node-RED pour Raspberry Pi
# Conforme au TD RPi LoRa Gateway - Étape 6

set -e  # Arrêter en cas d'erreur

echo "=========================================="
echo "Installation de Node-RED"
echo "=========================================="

# Vérifier que curl est installé
if ! command -v curl &> /dev/null; then
    echo "📦 Installation de curl..."
    sudo apt update
    sudo apt install -y curl
fi

# Installation de Node-RED
echo "📦 Installation de Node-RED..."
echo "   (Cela peut prendre plusieurs minutes...)"
bash <(curl -sL https://raw.githubusercontent.com/node-red/linux-installers/master/deb/update-nodejs-and-nodered)

# Démarrer et activer le service
echo "🚀 Démarrage du service Node-RED..."
sudo systemctl enable nodered.service
sudo systemctl start nodered.service

# Attendre que le service soit prêt
sleep 3

# Vérifier le statut
echo "✅ Vérification du statut..."
if sudo systemctl is-active --quiet nodered.service; then
    echo "✅ Node-RED est actif et fonctionne"
    sudo systemctl status nodered.service --no-pager | head -n 5
else
    echo "❌ Erreur: Node-RED n'est pas actif"
    exit 1
fi

# Attendre que Node-RED soit complètement démarré
echo ""
echo "⏳ Attente du démarrage complet de Node-RED (10 secondes)..."
sleep 10

# Installer @flowfuse/node-red-dashboard pour les nœuds UI
echo ""
echo "📦 Installation de @flowfuse/node-red-dashboard..."
echo "   (Nécessaire pour les nœuds ui_gauge, ui_text, ui_table, etc.)"
echo "   (Version recommandée, remplace node-red-dashboard déprécié)"

# Utiliser npm de Node-RED (généralement dans ~/.node-red)
NODE_RED_DIR="$HOME/.node-red"
if [ -d "$NODE_RED_DIR" ]; then
    cd "$NODE_RED_DIR"
    npm install @flowfuse/node-red-dashboard
    echo "✅ @flowfuse/node-red-dashboard installé"
else
    echo "⚠️  Répertoire Node-RED non trouvé, installation via npm global..."
    sudo npm install -g @flowfuse/node-red-dashboard
    echo "✅ @flowfuse/node-red-dashboard installé (global)"
fi

# Redémarrer Node-RED pour charger les nouveaux nœuds
echo ""
echo "🔄 Redémarrage de Node-RED pour charger les nouveaux nœuds..."
sudo systemctl restart nodered.service
sleep 5

echo ""
echo "=========================================="
echo "✅ Installation terminée!"
echo "=========================================="
echo ""
echo "Node-RED est accessible à:"
echo "  http://localhost:1880"
echo "  ou"
echo "  http://<IP_RASPBERRY_PI>:1880"
echo ""
echo "📦 Packages installés:"
echo "  - @flowfuse/node-red-dashboard (pour les nœuds UI)"
echo ""
echo "📋 Prochaines étapes:"
echo "  1. Accédez à Node-RED: http://localhost:1880"
echo "  2. Menu (☰) → Manage palette → Install"
echo "  3. Recherchez '@flowfuse/node-red-dashboard' et installez si nécessaire"
echo "  4. Importez le flux depuis: node-red-flow.json"
echo "  5. Accédez au tableau de bord: http://localhost:1880/ui"
echo ""

