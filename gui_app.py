#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Application Python GUI pour ESP32 LoRa
Gestion de l'appairage et envoi de messages via interface graphique
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading
import queue
import time
import re
import signal
import sys
import struct
from dashboard_gui import DashboardFrame

def get_timestamp():
    """Retourne un timestamp avec millisecondes (fonction globale)"""
    return time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d}"

# ============================================
# DÉCODAGE DU PROTOCOLE PERSONNALISÉ
# ============================================
MSG_TYPE_TEMP_DATA = 0x01
MSG_TYPE_HUMAN_DETECT = 0x02
MSG_TYPE_HUMAN_COUNT = 0x03
MSG_TYPE_SENSOR_DATA = 0x04
MSG_TYPE_TEXT = 0x10
MSG_TYPE_STATUS = 0x11
MSG_TYPE_PING = 0x20
MSG_TYPE_PONG = 0x21
MSG_TYPE_ACK = 0xF0
MSG_TYPE_ERROR = 0xFF

def get_type_name(msg_type):
    """Retourne le nom du type de message"""
    type_names = {
        MSG_TYPE_TEMP_DATA: "TEMP",
        MSG_TYPE_HUMAN_DETECT: "HUMAN",
        MSG_TYPE_HUMAN_COUNT: "HUMAN_COUNT",
        MSG_TYPE_SENSOR_DATA: "SENSOR_DATA",
        MSG_TYPE_TEXT: "TEXT",
        MSG_TYPE_STATUS: "STATUS",
        MSG_TYPE_PING: "PING",
        MSG_TYPE_PONG: "PONG",
        MSG_TYPE_ACK: "ACK",
        MSG_TYPE_ERROR: "ERROR",
        0x05: "HUMID",
        0x06: "PRESS",
        0x07: "LIGHT",
        0x08: "MOTION"
    }
    return type_names.get(msg_type, f"0x{msg_type:02X}")

def decode_protocol_message(data_bytes):
    """Décode un message selon le protocole personnalisé
    
    Args:
        data_bytes: bytes contenant le message
        
    Returns:
        dict avec les champs décodés ou None si invalide
    """
    if len(data_bytes) < 3:
        return None
    
    try:
        msg_type = data_bytes[0]
        source_id = data_bytes[1]
        data_size = data_bytes[2]
        
        if len(data_bytes) < (3 + data_size):
            return None
        
        payload = data_bytes[3:3+data_size]
        
        result = {
            'type': msg_type,
            'type_name': get_type_name(msg_type),
            'source_id': source_id,
            'data_size': data_size,
            'payload': payload,
            'decoded_value': None
        }
        
        # Décoder selon le type
        if msg_type == MSG_TYPE_TEMP_DATA and data_size >= 2:
            temp_x100 = struct.unpack('<h', payload[:2])[0]
            result['decoded_value'] = f"{temp_x100/100.0:.1f} °C"
            
        elif msg_type == MSG_TYPE_HUMAN_DETECT and data_size >= 1:
            detected = payload[0] != 0
            result['decoded_value'] = "OUI" if detected else "NON"
            
        elif msg_type == MSG_TYPE_HUMAN_COUNT and data_size >= 1:
            count = payload[0]
            result['decoded_value'] = f"{count} {'personne' if count <= 1 else 'personnes'}"
            
        elif msg_type == MSG_TYPE_SENSOR_DATA and data_size >= 1:
            count = payload[0]
            targets = []
            idx = 1
            for i in range(min(count, 3)):
                if idx + 7 < data_size:
                    x = struct.unpack('<h', payload[idx:idx+2])[0]
                    y = struct.unpack('<h', payload[idx+2:idx+4])[0]
                    speed = struct.unpack('<h', payload[idx+4:idx+6])[0]
                    resolution = struct.unpack('<H', payload[idx+6:idx+8])[0]
                    idx += 8
                    
                    dist_cm = ((x**2 + y**2) ** 0.5) / 10.0
                    targets.append({
                        'x': x, 'y': y, 'speed': speed,
                        'resolution': resolution, 'distance': dist_cm
                    })
            
            result['decoded_value'] = f"{count} {'cible' if count <= 1 else 'cibles'}"
            result['targets'] = targets
            
        elif msg_type == MSG_TYPE_TEXT:
            try:
                result['decoded_value'] = payload.decode('utf-8', errors='ignore')
            except:
                result['decoded_value'] = payload.hex()
                
        elif msg_type == MSG_TYPE_PING and data_size >= 4:
            timestamp = struct.unpack('<I', payload[:4])[0]
            result['decoded_value'] = f"timestamp={timestamp}"
            
        else:
            result['decoded_value'] = payload.hex()
        
        return result
        
    except Exception as e:
        print(f"Erreur décodage protocole: {e}")
        return None

class LoRaGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32 LoRa - Contrôle & Dashboard")
        self.root.geometry("900x850")  # Augmenté pour le dashboard
        
        self.serial_conn = None
        self.serial_queue = queue.Queue()
        self.running = False
        self.connected = False
        self._auto_connecting = False  # Flag pour indiquer qu'on est en mode auto-connexion
        self.is_paired = True  # État d'appairage de l'appareil (par défaut appairé jusqu'à vérification)
        self.message_send_time = None  # Timestamp de l'envoi du dernier message
        self.ack_count = 0  # Compteur d'ACKs reçus pour le message en cours
        self.last_heartbeat_time = None  # Timestamp du dernier heartbeat reçu
        self.port_check_interval = 2000  # Vérifier les ports toutes les 2 secondes (ms)
        self.last_port_check_time = 0  # Timestamp de la dernière vérification des ports
        
        # Dashboard
        self.dashboard = None
        
        self.setup_ui()
        self.auto_connect()
        # Démarrer la vérification périodique des ports COM
        self.root.after(self.port_check_interval, self.check_and_update_ports)
        # Nettoyer les capteurs hors ligne toutes les 10 secondes
        self.root.after(10000, self.cleanup_offline_sensors)
    
    def _get_timestamp(self):
        """Retourne un timestamp avec millisecondes"""
        return get_timestamp()
        
    def setup_ui(self):
        # Créer un Notebook (onglets)
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Onglet 1: Contrôle
        control_tab = ttk.Frame(self.notebook)
        self.notebook.add(control_tab, text="⚙️ Contrôle")
        
        # Onglet 2: Dashboard
        dashboard_tab = ttk.Frame(self.notebook)
        self.notebook.add(dashboard_tab, text="📊 Dashboard")
        
        # Créer le dashboard
        self.dashboard = DashboardFrame(dashboard_tab)
        self.dashboard.pack(fill=tk.BOTH, expand=True)
        
        # === Configuration de l'onglet Contrôle ===
        # Frame de connexion
        conn_frame = ttk.LabelFrame(control_tab, text="Connexion", padding=10)
        conn_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Label(conn_frame, text="Port:").grid(row=0, column=0, padx=5)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(conn_frame, textvariable=self.port_var, width=20, state="readonly")
        self.port_combo.grid(row=0, column=1, padx=5)
        
        self.refresh_ports()
        
        self.connect_btn = ttk.Button(conn_frame, text="Connecter", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=2, padx=5)
        
        ttk.Button(conn_frame, text="Actualiser", command=self.refresh_ports).grid(row=0, column=3, padx=5)
        
        self.status_label = ttk.Label(conn_frame, text="Déconnecté", foreground="red")
        self.status_label.grid(row=0, column=4, padx=10)
        
        # Frame Device ID
        id_frame = ttk.LabelFrame(control_tab, text="Informations", padding=10)
        id_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Label(id_frame, text="Device ID:").grid(row=0, column=0, padx=5)
        self.device_id_label = ttk.Label(id_frame, text="N/A", font=("Courier", 10))
        self.device_id_label.grid(row=0, column=1, padx=5)
        
        ttk.Label(id_frame, text="État:").grid(row=0, column=2, padx=5)
        self.pair_status_label = ttk.Label(id_frame, text="Appairé", foreground="green")  # Par défaut appairé jusqu'à vérification
        self.pair_status_label.grid(row=0, column=3, padx=5)
        
        ttk.Label(id_frame, text="En ligne:").grid(row=0, column=4, padx=5)
        self.online_status_label = ttk.Label(id_frame, text="?", foreground="gray")
        self.online_status_label.grid(row=0, column=5, padx=5)
        
        ttk.Button(id_frame, text="Actualiser ID", command=self.get_device_id).grid(row=0, column=6, padx=5)
        ttk.Button(id_frame, text="Statut", command=self.get_status).grid(row=0, column=7, padx=5)
        
        # Frame Appairage
        pair_frame = ttk.LabelFrame(control_tab, text="Appairage", padding=10)
        pair_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        # Mode pairing
        mode_frame = ttk.Frame(pair_frame)
        mode_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(mode_frame, text="Mode Pairing:").pack(side=tk.LEFT, padx=5)
        self.pairing_mode_var = tk.BooleanVar(value=False)  # Désactivé par défaut
        ttk.Checkbutton(mode_frame, text="ON", variable=self.pairing_mode_var, 
                       command=self.toggle_pairing_mode).pack(side=tk.LEFT, padx=5)
        
        ttk.Button(mode_frame, text="Actualiser Liste", command=self.refresh_device_list).pack(side=tk.LEFT, padx=5)
        
        # Liste des devices
        ttk.Label(pair_frame, text="Devices disponibles:").pack(anchor=tk.W, pady=(10, 5))
        
        list_frame = ttk.Frame(pair_frame)
        list_frame.pack(fill=tk.BOTH, expand=True, pady=5)
        
        self.device_listbox = tk.Listbox(list_frame, height=6)
        self.device_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        
        scrollbar = ttk.Scrollbar(list_frame, orient=tk.VERTICAL, command=self.device_listbox.yview)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.device_listbox.config(yscrollcommand=scrollbar.set)
        
        # Boutons d'action sur la sélection
        actions_frame = ttk.Frame(pair_frame)
        actions_frame.pack(fill=tk.X, pady=5)
        
        ttk.Button(actions_frame, text="S'appairer", command=self.pair_selected_device).pack(side=tk.LEFT, padx=5)
        ttk.Button(actions_frame, text="Accepter (A)", command=self.accept_bind).pack(side=tk.LEFT, padx=5)
        ttk.Button(actions_frame, text="Déappairer", command=self.unpair).pack(side=tk.LEFT, padx=5)

        # Bindings
        self.device_listbox.bind('<Double-Button-1>', self.on_device_double_click)
        
        # Appairage manuel (optionnel)
        bind_frame = ttk.LabelFrame(pair_frame, text="Appairage manuel (optionnel)", padding=5)
        bind_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(bind_frame, text="Device ID (hex):").pack(side=tk.LEFT, padx=5)
        self.bind_id_entry = ttk.Entry(bind_frame, width=15)
        self.bind_id_entry.pack(side=tk.LEFT, padx=5)
        
        ttk.Button(bind_frame, text="S'appairer (ID manuel)", command=self.send_bind_request).pack(side=tk.LEFT, padx=5)
        
        # Frame Messages
        msg_frame = ttk.LabelFrame(control_tab, text="Messages", padding=10)
        msg_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        # Zone de réception
        ttk.Label(msg_frame, text="Messages reçus:").pack(anchor=tk.W)
        self.received_text = scrolledtext.ScrolledText(msg_frame, height=10, state=tk.DISABLED)
        self.received_text.pack(fill=tk.BOTH, expand=True, pady=5)
        
        # Zone d'envoi
        send_frame = ttk.Frame(msg_frame)
        send_frame.pack(fill=tk.X, pady=5)
        
        # Sélecteur de module LoRa
        ttk.Label(send_frame, text="Module:").pack(side=tk.LEFT, padx=5)
        self.module_var = tk.StringVar(value="ALL")
        module_combo = ttk.Combobox(send_frame, textvariable=self.module_var, 
                                     values=["900", "433", "ALL"], width=8, state="readonly")
        module_combo.pack(side=tk.LEFT, padx=5)
        
        # Sélecteur de type de message
        ttk.Label(send_frame, text="Type:").pack(side=tk.LEFT, padx=5)
        self.msg_type_var = tk.StringVar(value="TEXT")
        type_combo = ttk.Combobox(send_frame, textvariable=self.msg_type_var,
                                   values=["TEXT", "TEMP", "HUMAN", "HUMAN_COUNT", "PING"], width=12, state="readonly")
        type_combo.pack(side=tk.LEFT, padx=5)
        
        ttk.Label(send_frame, text="Données:").pack(side=tk.LEFT, padx=5)
        self.message_entry = ttk.Entry(send_frame)
        self.message_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        self.message_entry.bind('<Return>', lambda e: self.send_message())
        
        ttk.Button(send_frame, text="Envoyer", command=self.send_message).pack(side=tk.LEFT, padx=5)
        
        # Thread pour lire les données série
        self.serial_thread = None
        
    def refresh_ports(self):
        """Actualise la liste des ports série disponibles"""
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_combo['values'] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])
        return ports
    
    def check_and_update_ports(self):
        """Vérifie périodiquement les ports COM disponibles et se reconnecte si nécessaire"""
        if not self.running:
            return
        
        try:
            # Obtenir la liste actuelle des ports
            current_ports = [port.device for port in serial.tools.list_ports.comports()]
            previous_ports = list(self.port_combo['values'])
            
            # Mettre à jour la liste des ports dans le combo box
            self.port_combo['values'] = current_ports
            
            # Si un nouveau port apparaît et qu'aucun port n'est sélectionné, sélectionner le premier
            if current_ports and not self.port_var.get():
                self.port_var.set(current_ports[0])
            
            # Si on est connecté, vérifier que le port est toujours disponible
            if self.connected:
                current_port = self.port_var.get()
                if current_port not in current_ports:
                    # Le port actuel n'existe plus, se déconnecter
                    print(f"[{self._get_timestamp()}] [GUI] Port {current_port} n'existe plus, déconnexion...")
                    self.disconnect_serial()
                    # Tenter de se reconnecter automatiquement
                    if current_ports:
                        self._auto_connecting = True
                        self.root.after(500, self.auto_connect)
                elif self.serial_conn and not self.serial_conn.is_open:
                    # La connexion est fermée mais on pense être connecté
                    print(f"[{self._get_timestamp()}] [GUI] Connexion fermée, tentative de reconnexion...")
                    self.connected = False
                    self.status_label.config(text="Déconnecté", foreground="red")
                    if current_ports:
                        self._auto_connecting = True
                        self.root.after(500, self.auto_connect)
            else:
                # Si on n'est pas connecté et qu'il y a des ports disponibles, tenter de se connecter
                if current_ports and not self._auto_connecting and not self.connected:
                    # Vérifier si un nouveau port est apparu ou si c'est la première fois qu'on voit des ports
                    new_ports = [p for p in current_ports if p not in previous_ports]
                    ports_changed = len(new_ports) > 0 or (not previous_ports and current_ports)
                    
                    if ports_changed:
                        if new_ports:
                            print(f"[{self._get_timestamp()}] [GUI] Nouveau(x) port(s) détecté(s): {new_ports}")
                        else:
                            print(f"[{self._get_timestamp()}] [GUI] Port(s) disponible(s), tentative de connexion...")
                        
                        # Sélectionner le premier port disponible si aucun n'est sélectionné
                        if not self.port_var.get() and current_ports:
                            self.port_var.set(current_ports[0])
                        
                        # Tenter de se connecter automatiquement
                        self._auto_connecting = True
                        self.root.after(500, self.auto_connect)
        except Exception as e:
            print(f"[{self._get_timestamp()}] [GUI] Erreur lors de la vérification des ports: {e}")
        
        # Programmer la prochaine vérification
        if self.running:
            self.root.after(self.port_check_interval, self.check_and_update_ports)
    
    def auto_connect(self):
        """Tente de se connecter automatiquement en essayant tous les ports disponibles"""
        self.refresh_ports()
        ports = self.port_combo['values']
        if ports:
            print(f"[{self._get_timestamp()}] [GUI] Auto-connexion: {len(ports)} port(s) disponible(s)")
            # Essayer chaque port jusqu'à trouver un qui fonctionne
            self._try_connect_ports(ports, 0)
        else:
            print(f"[{self._get_timestamp()}] [GUI] Aucun port série disponible")
    
    def _try_connect_ports(self, ports, index):
        """Essaie de se connecter à chaque port disponible"""
        if index >= len(ports):
            print(f"[{self._get_timestamp()}] [GUI] Aucun port disponible n'a fonctionné")
            self._auto_connecting = False
            return
        
        # Si déjà connecté, arrêter l'auto-connexion
        if self.connected:
            print(f"[{self._get_timestamp()}] [GUI] Déjà connecté, arrêt de l'auto-connexion")
            self._auto_connecting = False
            return
        
        port = ports[index]
        print(f"[{self._get_timestamp()}] [GUI] Tentative de connexion au port {port} ({index+1}/{len(ports)})...")
        self.port_var.set(port)
        self._auto_connecting = True
        
        try:
            # Tester la connexion
            test_conn = serial.Serial(port, 115200, timeout=1)
            test_conn.close()
            # Si on arrive ici, le port est disponible, on peut se connecter
            print(f"[{self._get_timestamp()}] [GUI] Port {port} disponible, connexion...")
            # Utiliser connect_serial directement
            # Vérifier après un court délai si la connexion a réussi
            self.root.after(100, lambda: self._check_connection_result(ports, index))
        except serial.SerialException as e:
            print(f"[{self._get_timestamp()}] [GUI] Port {port} indisponible: {e}")
            # Essayer le port suivant après un court délai
            self.root.after(200, lambda: self._try_connect_ports(ports, index + 1))
    
    def _check_connection_result(self, ports, index):
        """Vérifie si la connexion a réussi, sinon essaie le port suivant"""
        # Appeler connect_serial d'abord
        success = self.connect_serial()
        
        if success and self.connected:
            print(f"[{self._get_timestamp()}] [GUI] Connexion réussie sur {self.port_var.get()}")
            self._auto_connecting = False
        else:
            # La connexion a échoué, essayer le port suivant
            print(f"[{self._get_timestamp()}] [GUI] Échec de connexion, essai du port suivant...")
            self.root.after(200, lambda: self._try_connect_ports(ports, index + 1))
    
    def toggle_connection(self):
        """Connecte ou déconnecte le port série"""
        if not self.connected:
            self.connect_serial()
        else:
            self.disconnect_serial()
    
    def connect_serial(self):
        """Établit la connexion série"""
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Erreur", "Veuillez sélectionner un port")
            return False
        
        try:
            print(f"[{self._get_timestamp()}] [GUI] Connexion au port {port}...")
            self.serial_conn = serial.Serial(port, 115200, timeout=1)
            self.connected = True
            self.running = True
            
            self.status_label.config(text="Connecté", foreground="green")
            self.connect_btn.config(text="Déconnecter")
            self.port_combo.config(state="disabled")
            
            # Démarrer le thread de lecture
            self.serial_thread = threading.Thread(target=self.read_serial, daemon=True)
            self.serial_thread.start()
            
            # Traiter les messages reçus
            self.root.after(100, self.process_serial_queue)
            
            # Réinitialiser les états lors d'une nouvelle connexion
            self.last_heartbeat_time = None
            self.online_status_label.config(text="?", foreground="gray")
            
            # Récupérer les infos initiales (avec délais pour laisser le temps à l'ESP32 de répondre)
            self.root.after(500, self.get_device_id)
            self.root.after(600, self.get_status)
            # Relancer la récupération des statuts après un délai supplémentaire pour s'assurer qu'on a tout
            self.root.after(1500, self.get_status)
            
            print(f"[{self._get_timestamp()}] [GUI] Connexion établie sur {port}")
            self.log_message("Connexion établie sur " + port)
            self._auto_connecting = False  # Réinitialiser le flag après connexion réussie
            return True
            
        except serial.SerialException as e:
            print(f"[{self._get_timestamp()}] [GUI] ERREUR: Impossible de se connecter à {port}: {e}")
            # Ne pas afficher de messagebox lors de l'auto-connexion
            # car on va essayer les autres ports
            if not self._auto_connecting:
                messagebox.showerror("Erreur", f"Impossible de se connecter:\n{e}")
            self.connected = False
            # Si en mode auto-connexion et échec, _try_connect_ports continuera avec le port suivant
            if self._auto_connecting:
                # La fonction _try_connect_ports gérera l'essai du port suivant
                pass
            return False
    
    def disconnect_serial(self):
        """Ferme la connexion série"""
        print(f"[{self._get_timestamp()}] [GUI] Déconnexion...")
        self.running = False
        self.connected = False
        
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
        
        self.status_label.config(text="Déconnecté", foreground="red")
        self.connect_btn.config(text="Connecter")
        self.port_combo.config(state="readonly")
        
        # Réinitialiser le timestamp du heartbeat
        self.last_heartbeat_time = None
        self.online_status_label.config(text="?", foreground="gray")
        
        print(f"[{self._get_timestamp()}] [GUI] Déconnecté")
        self.log_message("Déconnecté")
    
    def read_serial(self):
        """Lit les données série dans un thread séparé"""
        buffer = ""
        while self.running and self.connected:
            try:
                if self.serial_conn and self.serial_conn.in_waiting:
                    data = self.serial_conn.read(self.serial_conn.in_waiting).decode('utf-8', errors='ignore')
                    buffer += data
                    
                    # Traiter les lignes complètes
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        # Ne pas utiliser strip() pour préserver les espaces au début
                        # qui sont importants pour la détection des devices
                        line = line.rstrip()  # Supprimer seulement les espaces à la fin
                        if line:
                            # Print pour débogage (seulement pour certains types de messages)
                            if any(keyword in line for keyword in ["[ACK]", "[FRAG]", "[RETRY]", "[LoRa]", "[SEC]", "[BIND]"]):
                                print(f"[{self._get_timestamp()}] [SERIAL RX] {line}")
                            self.serial_queue.put(line)
                
                time.sleep(0.01)
            except Exception as e:
                if self.running:
                    print(f"[{self._get_timestamp()}] [GUI] ERREUR lecture série: {e}")
                    self.serial_queue.put(f"[ERREUR] {e}")
                break
    
    def process_serial_queue(self):
        """Traite les messages de la queue série"""
        try:
            while True:
                line = self.serial_queue.get_nowait()
                self.handle_serial_message(line)
        except queue.Empty:
            pass
        
        if self.running:
            self.root.after(100, self.process_serial_queue)
    
    def handle_serial_message(self, line):
        """Traite un message série reçu"""
        # Essayer de décoder si c'est un message protocole pour le dashboard
        self._try_decode_for_dashboard(line)
        
        # Détecter les messages LoRa reçus AVANT de logger
        # pour les afficher avec formatage spécial
        
        # Détecter [RX-900MHz] ou [RX-433MHz]
        if "[RX-900MHz]" in line or "[RX-433MHz]" in line:
            # Extraire la provenance et le message
            if "[RX-900MHz]" in line:
                source = "900MHz"
                msg_part = line.split("[RX-900MHz]", 1)[1].strip()
            else:
                source = "433MHz"
                msg_part = line.split("[RX-433MHz]", 1)[1].strip()
            
            # Logger avec formatage spécial
            self.log_lora_message(msg_part, source)
        # Détecter les messages du protocole décodés par l'ESP32
        elif "Type     : 0x" in line and "Source   :" in line:
            # Message déjà décodé par l'ESP32, l'afficher joliment
            self.log_message(line)
        else:
            # Message normal, logger comme d'habitude
            self.log_message(line)
        
        # Détecter les devices dans la liste
        # Plusieurs formats possibles selon les versions du code
        if ("[PAIR] Devices en mode pairing détectés:" in line or 
            ("[PAIR] Debug:" in line and ("device(s) trouvé(s)" in line or "Liste découverte vide" in line))):
            print(f"[{self._get_timestamp()}] [GUI] Liste devices mise à jour: {line}")
            # Effacer la liste avant d'afficher les nouveaux devices
            self.device_listbox.delete(0, tk.END)
        elif re.match(r'^\s*0x[0-9A-Fa-f]+\s+\|.*RSSI', line):
            # Format: "0x6EDF5CB8 | RSSI=-100 dBm | SNR=0.0 | Vu il y a 2s"
            # ou "  0x6EDF5CB8 | RSSI=-100 dBm | SNR=0.0 | Vu il y a 2s"
            # Accepte avec ou sans espaces au début
            match = re.search(r'0x([0-9A-Fa-f]+)', line)
            if match:
                device_id = match.group(1).upper()
                # Extraire RSSI et SNR pour l'affichage
                rssi_match = re.search(r'RSSI=(-?\d+)', line)
                snr_match = re.search(r'SNR=([\d.]+)', line)
                rssi = rssi_match.group(1) if rssi_match else "N/A"
                snr = snr_match.group(1) if snr_match else "N/A"
                display_text = f"0x{device_id} | RSSI={rssi} dBm | SNR={snr}"
                # Vérifier si le device n'est pas déjà dans la liste
                existing_items = self.device_listbox.get(0, tk.END)
                device_exists = any(device_id in item for item in existing_items)
                if not device_exists:
                    print(f"[{self._get_timestamp()}] [GUI] Nouveau device ajouté: {display_text}")
                    self.device_listbox.insert(tk.END, display_text)
                else:
                    # Mettre à jour l'entrée existante
                    print(f"[{self._get_timestamp()}] [GUI] Device mis à jour: {display_text}")
                    for i, item in enumerate(existing_items):
                        if device_id in item:
                            self.device_listbox.delete(i)
                            self.device_listbox.insert(i, display_text)
                            break
        
        # Détecter Device ID (plusieurs formats possibles)
        if "Device ID: 0x" in line or "DeviceId: 0x" in line:
            match = re.search(r'0x([0-9A-Fa-f]+)', line)
            if match:
                device_id = match.group(1).upper()
                print(f"[{self._get_timestamp()}] [GUI] Device ID détecté: 0x{device_id}")
                self.device_id_label.config(text=f"0x{device_id}")
        
        # Détecter état d'appairage (ancien format: Paired=yes/no, nouveau format: [STATUS] État d'appairage: ...)
        if "Paired=yes" in line or "[STATUS] État d'appairage: Appairé" in line:
            print(f"[{self._get_timestamp()}] [GUI] État: Appairé")
            self.is_paired = True
            self.pair_status_label.config(text="Appairé", foreground="green")
        elif "Paired=no" in line or "[STATUS] État d'appairage: Non appairé" in line:
            print(f"[{self._get_timestamp()}] [GUI] État: Non appairé")
            self.is_paired = False
            self.pair_status_label.config(text="Non appairé", foreground="orange")
            self.online_status_label.config(text="N/A", foreground="gray")
            # Réinitialiser le timestamp du heartbeat
            self.last_heartbeat_time = None
        
        # Détecter mode pairing (mise à jour automatique de la checkbox)
        if "[STATUS] Mode pairing: ON" in line or "[PAIR] Mode pairing: ON" in line:
            if not self.pairing_mode_var.get():
                print(f"[{self._get_timestamp()}] [GUI] Mode pairing détecté: ON")
                self.pairing_mode_var.set(True)
        elif "[STATUS] Mode pairing: OFF" in line or "[PAIR] Mode pairing: OFF" in line:
            if self.pairing_mode_var.get():
                print(f"[{self._get_timestamp()}] [GUI] Mode pairing détecté: OFF")
                self.pairing_mode_var.set(False)
        
        # Détecter état de présence du device appairé
        if "[STATUS] Device appairé en ligne: OUI" in line or "Device appairé en ligne: OUI" in line:
            print(f"[{self._get_timestamp()}] [GUI] Device appairé: En ligne (via STATUS)")
            self.online_status_label.config(text="OUI", foreground="green")
            # Mettre à jour le timestamp du heartbeat pour éviter de passer offline immédiatement
            if self.last_heartbeat_time is None:
                self.last_heartbeat_time = time.time()
        elif "[STATUS] Device appairé en ligne: NON" in line or "Device appairé en ligne: NON" in line:
            print(f"[{self._get_timestamp()}] [GUI] Device appairé: Hors ligne (via STATUS)")
            self.online_status_label.config(text="NON", foreground="red")
            # Réinitialiser le timestamp pour forcer l'état offline
            self.last_heartbeat_time = None
        
        # Détecter ID du device appairé
        if "[STATUS] Device appairé ID: 0x" in line:
            match = re.search(r'0x([0-9A-Fa-f]+)', line)
            if match:
                paired_id = match.group(1).upper()
                print(f"[{self._get_timestamp()}] [GUI] Device appairé ID: 0x{paired_id}")
        
        # Détecter les heartbeats reçus pour mettre à jour l'état en temps réel
        if "[HEARTBEAT]" in line and ("Device appairé détecté:" in line or "Heartbeat valide reçu" in line):
            # Mettre à jour le timestamp du dernier heartbeat
            self.last_heartbeat_time = time.time()
            if "Device appairé détecté:" in line:
                match = re.search(r'0x([0-9A-Fa-f]+)', line)
                if match:
                    paired_id = match.group(1).upper()
                    print(f"[{self._get_timestamp()}] [GUI] Heartbeat reçu de 0x{paired_id}")
            else:
                print(f"[{self._get_timestamp()}] [GUI] Heartbeat valide reçu")
            # Mettre à jour l'état en ligne immédiatement
            if self.is_paired:
                self.online_status_label.config(text="OUI", foreground="green")
        
        # Vérifier l'état en ligne à chaque message reçu (si appairé)
        if self.is_paired and self.connected and self.last_heartbeat_time is not None:
            current_time = time.time()
            time_since_last_heartbeat = current_time - self.last_heartbeat_time
            heartbeat_timeout = 7.5  # 7.5 secondes sans heartbeat = offline
            if time_since_last_heartbeat > heartbeat_timeout:
                # Plus de heartbeat depuis plus de 3 secondes = offline
                if self.online_status_label.cget("text") != "NON":
                    print(f"[{self._get_timestamp()}] [GUI] Pas de heartbeat depuis {time_since_last_heartbeat:.1f}s, device considéré hors ligne")
                    self.online_status_label.config(text="NON", foreground="red")
        
        # Détecter messages reçus
        if "[SEC] Reçu:" in line or "[SEC] Reçu (fragmenté):" in line:
            match = re.search(r'Reçu[^:]*:\s*(.+)', line)
            if match:
                msg = match.group(1).split('|')[0].strip()
                print(f"[{self._get_timestamp()}] [GUI] Message reçu: {msg}")
                self.log_received_message(msg)
        
        # Détecter les ACKs et calculer le temps de réponse
        if "[ACK]" in line:
            print(f"[{self._get_timestamp()}] [GUI] ACK détecté: {line}")
            # Détecter les ACKs valides (confirmation finale)
            if "ACK valide pour" in line or "Reçu pour seq=" in line:
                if self.message_send_time is not None:
                    elapsed_time = time.time() - self.message_send_time
                    elapsed_ms = elapsed_time * 1000
                    self.ack_count += 1
                    
                    # Extraire les informations du fragment si disponibles
                    seq_match = re.search(r'seq=(\d+)', line)
                    frag_match = re.search(r'frag=(\d+)', line)
                    seq_info = f"seq={seq_match.group(1)}" if seq_match else ""
                    frag_info = f"frag={frag_match.group(1)}" if frag_match else ""
                    info_str = f" {seq_info} {frag_info}".strip() if seq_info or frag_info else ""
                    
                    print(f"[{self._get_timestamp()}] [GUI] Temps de réponse: {elapsed_ms:.2f} ms{info_str}")
                    if self.ack_count == 1:
                        # Premier ACK (ou seul ACK pour message non fragmenté)
                        self.log_message(f"[ACK] Confirmation reçue - Temps: {elapsed_ms:.2f} ms{info_str}")
                    else:
                        # ACK supplémentaire pour message fragmenté
                        self.log_message(f"[ACK] Fragment acquitté ({self.ack_count}) - Temps: {elapsed_ms:.2f} ms{info_str}")
                    
                    # Note: On garde le timestamp pour afficher le temps total si nécessaire
                    # Il sera réinitialisé lors du prochain envoi de message
        
        # Détecter les fragments
        if "[FRAG]" in line:
            print(f"[{self._get_timestamp()}] [GUI] Fragment: {line}")
        
        # Détecter les retries
        if "[RETRY]" in line:
            print(f"[{self._get_timestamp()}] [GUI] Retry: {line}")
    
    def send_command(self, command, silent=False):
        """Envoie une commande série
        
        Args:
            command: La commande à envoyer
            silent: Si True, ne pas logger la commande (pour les mises à jour automatiques)
        """
        if self.connected and self.serial_conn and self.serial_conn.is_open:
            try:
                if not silent:
                    print(f"[{self._get_timestamp()}] [GUI] Envoi commande: {command}")
                self.serial_conn.write((command + '\n').encode('utf-8'))
                self.serial_conn.flush()
                return True
            except Exception as e:
                print(f"[{self._get_timestamp()}] [GUI] ERREUR envoi commande '{command}': {e}")
                self.log_message(f"[ERREUR] Échec envoi: {e}")
                return False
        if not silent:
            print(f"[{self._get_timestamp()}] [GUI] ERREUR: Non connecté, impossible d'envoyer: {command}")
        return False
    
    def get_device_id(self):
        """Récupère le Device ID"""
        self.send_command("ID")
    
    def get_status(self):
        """Récupère le statut d'appairage"""
        self.send_command("STATUS")
    
    def toggle_pairing_mode(self):
        """Active/désactive le mode pairing"""
        if self.pairing_mode_var.get():
            # Vérifier si l'appareil est déjà appairé
            if self.is_paired:
                print(f"[{self._get_timestamp()}] [GUI] Appareil déjà appairé, impossible d'activer le mode pairing")
                messagebox.showwarning("Attention", "L'appareil est déjà appairé. Veuillez d'abord déappairer pour activer le mode pairing.")
                # Remettre la checkbox à OFF
                self.pairing_mode_var.set(False)
                return
            self.send_command("PAIR ON")
        else:
            self.send_command("PAIR OFF")
    
    def refresh_device_list(self):
        """Actualise la liste des devices"""
        self.device_listbox.delete(0, tk.END)
        self.send_command("LIST")
    
    def on_device_double_click(self, event):
        """Double-clic sur un device pour s'appairer"""
        self.pair_selected_device()

    def pair_selected_device(self):
        """S'appairer avec l'élément sélectionné de la liste sans saisir l'ID"""
        selection = self.device_listbox.curselection()
        if not selection:
            print(f"[{self._get_timestamp()}] [GUI] Aucun device sélectionné")
            messagebox.showinfo("Info", "Sélectionnez un device dans la liste")
            return
        item = self.device_listbox.get(selection[0])
        match = re.search(r'0x([0-9A-Fa-f]+)', item)
        if not match:
            print(f"[{self._get_timestamp()}] [GUI] ERREUR: Impossible d'extraire l'ID de: {item}")
            messagebox.showerror("Erreur", "Impossible d'extraire l'ID du device")
            return
        device_id = match.group(1).upper()
        print(f"[{self._get_timestamp()}] [GUI] Demande d'appairage vers 0x{device_id}")
        # Utilise directement la commande B <id> sans passer par le champ
        self.send_command(f"B {device_id}")
        self.log_message(f"[APP] Demande d'appairage envoyée vers 0x{device_id}")
    
    def send_bind_request(self):
        """Envoie une demande d'appairage"""
        device_id = self.bind_id_entry.get().strip().upper()
        if not device_id:
            messagebox.showwarning("Attention", "Veuillez entrer un Device ID")
            return
        
        # Nettoyer l'ID (enlever 0x si présent)
        device_id = device_id.replace('0x', '').replace('0X', '')
        
        if len(device_id) != 8 or not all(c in '0123456789ABCDEF' for c in device_id):
            messagebox.showerror("Erreur", "Device ID invalide (format hex: A1B2C3D4)")
            return
        
        self.send_command(f"B {device_id}")
        self.log_message(f"[APP] Demande d'appairage envoyée vers 0x{device_id}")
    
    def accept_bind(self):
        """Accepte une demande d'appairage"""
        self.send_command("A")
        self.log_message("[APP] Demande d'appairage acceptée")
    
    def unpair(self):
        """Déappaire l'appareil"""
        if messagebox.askyesno("Confirmation", "Voulez-vous vraiment déappairer cet appareil ?"):
            self.send_command("UNPAIR")
            self.log_message("[APP] Commande déappairage envoyée")
    
    def send_message(self):
        """Envoie un message sur le module LoRa sélectionné selon le protocole"""
        data = self.message_entry.get().strip()
        msg_type = self.msg_type_var.get()
        
        # PING et HUMAN_COUNT n'ont pas nécessairement besoin de données
        if msg_type == "PING":
            data = ""
        elif not data and msg_type not in ["PING", "HUMAN_COUNT"]:
            return
        
        if not self.connected:
            print(f"[{self._get_timestamp()}] [GUI] ERREUR: Tentative d'envoi sans connexion")
            messagebox.showwarning("Attention", "Non connecté à l'ESP32")
            return
        
        # Récupérer le module sélectionné
        module = self.module_var.get()
        
        # Construire la commande selon le type de message
        if msg_type == "TEMP":
            # Valider que c'est un nombre
            try:
                temp_val = float(data)
                command = f"{module} TEMP {temp_val}"
                display_msg = f"Température: {temp_val}°C"
            except ValueError:
                messagebox.showerror("Erreur", "TEMP nécessite une valeur numérique (ex: 25.3)")
                return
        elif msg_type == "HUMAN":
            # Valider que c'est 0 ou 1
            if data not in ["0", "1"]:
                messagebox.showerror("Erreur", "HUMAN nécessite 0 (non détecté) ou 1 (détecté)")
                return
            command = f"{module} HUMAN {data}"
            display_msg = f"Détection humaine: {'OUI' if data == '1' else 'NON'}"
        elif msg_type == "HUMAN_COUNT":
            # Si aucune valeur fournie, envoyer simplement la commande
            if not data:
                command = f"{module} HUMAN_COUNT"
                display_msg = "Demande comptage capteur"
            else:
                # Sinon, valider que c'est un nombre entre 0 et 255
                try:
                    count_val = int(data)
                    if count_val < 0 or count_val > 255:
                        messagebox.showerror("Erreur", "HUMAN_COUNT nécessite un nombre entre 0 et 255")
                        return
                    command = f"{module} HUMAN_COUNT {count_val}"
                    display_msg = f"Comptage humain: {count_val} {'personne' if count_val <= 1 else 'personnes'}"
                except ValueError:
                    messagebox.showerror("Erreur", "HUMAN_COUNT nécessite un nombre (ex: 3) ou laissez vide pour demander le comptage")
                    return
        elif msg_type == "PING":
            command = f"{module} PING"
            display_msg = "PING"
        else:  # TEXT (par défaut)
            command = f"{module} TEXT {data}"
            display_msg = f"Texte: {data}"
        
        # Enregistrer le timestamp de l'envoi et réinitialiser le compteur d'ACKs
        self.message_send_time = time.time()
        self.ack_count = 0
        
        print(f"[{self._get_timestamp()}] [GUI] Envoi {msg_type} sur {module}: {command}")
        
        # Envoyer la commande
        self.send_command(command)
        self.message_entry.delete(0, tk.END)
        
        # Logger avec indication du module et du type
        if module == "ALL":
            self.log_message(f"[APP] {msg_type} envoyé sur 900MHz + 433MHz: {display_msg}")
        else:
            self.log_message(f"[APP] {msg_type} envoyé sur {module}MHz: {display_msg}")
    
    def log_message(self, message):
        """Ajoute un message dans la zone de réception"""
        self.received_text.config(state=tk.NORMAL)
        timestamp = self._get_timestamp()
        self.received_text.insert(tk.END, f"[{timestamp}] {message}\n")
        self.received_text.see(tk.END)
        self.received_text.config(state=tk.DISABLED)
    
    def _try_decode_for_dashboard(self, line):
        """Essaie de décoder un message pour le dashboard"""
        try:
            # Rechercher les patterns de messages protoc oles décodés
            # Format ESP32: [RX]   Source   : 1
            # Suivi de Type et données
            
            if "Source   :" in line:
                match = re.search(r'Source\s*:\s*(\d+)', line)
                if match:
                    sensor_id = int(match.group(1))
                    # Stocker temporairement pour associer avec les données suivantes
                    self._temp_sensor_id = sensor_id
            
            elif "Type     : 0x" in line and hasattr(self, '_temp_sensor_id'):
                match = re.search(r'Type\s*:\s*0x([0-9A-Fa-f]+)', line)
                if match:
                    msg_type = int(match.group(1), 16)
                    self._temp_msg_type = msg_type
            
            elif "Capteur  :" in line and hasattr(self, '_temp_sensor_id') and hasattr(self, '_temp_msg_type'):
                # Message SENSOR_DATA avec cibles
                # Les lignes suivantes contiennent les cibles
                pass  # Les cibles seront traitées dans les lignes suivantes
            
            elif "Cible " in line and "X=" in line and "Y=" in line:
                # Parse les données de cible
                match = re.search(r'X=(-?\d+)mm Y=(-?\d+)mm \(([0-9.]+)cm\) v=(-?\d+)cm/s', line)
                if match and hasattr(self, '_temp_sensor_id'):
                    x = int(match.group(1))
                    y = int(match.group(2))
                    dist = float(match.group(3))
                    speed = int(match.group(4))
                    
                    if not hasattr(self, '_temp_targets'):
                        self._temp_targets = []
                    
                    self._temp_targets.append({
                        'x': x, 'y': y, 'distance': dist, 'speed': speed, 'resolution': 0
                    })
            
            elif ("─────────────────" in line or "════════" in line) and hasattr(self, '_temp_targets'):
                # Fin du message, envoyer au dashboard
                if hasattr(self, '_temp_sensor_id') and self.dashboard:
                    data = {'targets': self._temp_targets}
                    self.dashboard.update_sensor_data(
                        self._temp_sensor_id,
                        getattr(self, '_temp_msg_type', 0x04),
                        data
                    )
                
                # Nettoyer
                if hasattr(self, '_temp_targets'):
                    delattr(self, '_temp_targets')
                if hasattr(self, '_temp_sensor_id'):
                    delattr(self, '_temp_sensor_id')
                if hasattr(self, '_temp_msg_type'):
                    delattr(self, '_temp_msg_type')
            
            # Détecter aussi les messages simples (TEMP, HUMAN_COUNT, etc.)
            elif "Temp     :" in line and hasattr(self, '_temp_sensor_id'):
                match = re.search(r'Temp\s*:\s*([0-9.]+)\s*°C', line)
                if match and self.dashboard:
                    temp = float(match.group(1))
                    self.dashboard.update_sensor_data(
                        self._temp_sensor_id, 0x01, {'temperature': temp}
                    )
            
            elif "Humains  :" in line and hasattr(self, '_temp_sensor_id'):
                match = re.search(r'Humains\s*:\s*(\d+)', line)
                if match and self.dashboard:
                    count = int(match.group(1))
                    self.dashboard.update_sensor_data(
                        self._temp_sensor_id, 0x03, {'count': count}
                    )
        
        except Exception as e:
            # Ignorer les erreurs de parsing silencieusement
            pass
    
    def cleanup_offline_sensors(self):
        """Nettoie les capteurs hors ligne dans le dashboard"""
        if self.dashboard:
            self.dashboard.cleanup_offline_sensors(timeout=30)
        
        # Reprogrammer la vérification
        if self.running:
            self.root.after(10000, self.cleanup_offline_sensors)
    
    def cleanup(self):
        """Nettoie les ressources avant la fermeture"""
        print(f"[{self._get_timestamp()}] [GUI] Nettoyage des ressources...")
        self.running = False
        self.connected = False
        
        if self.serial_conn and self.serial_conn.is_open:
            try:
                self.serial_conn.close()
                print(f"[{self._get_timestamp()}] [GUI] Port série fermé")
            except Exception as e:
                print(f"[{self._get_timestamp()}] [GUI] Erreur lors de la fermeture du port: {e}")
        
        print(f"[{self._get_timestamp()}] [GUI] Nettoyage terminé")
    
    def log_received_message(self, message):
        """Ajoute un message reçu dans la zone de réception"""
        self.received_text.config(state=tk.NORMAL)
        timestamp = self._get_timestamp()
        self.received_text.insert(tk.END, f"[{timestamp}] 📨 {message}\n", "received")
        self.received_text.tag_config("received", foreground="blue", font=("Arial", 9, "bold"))
        self.received_text.see(tk.END)
        self.received_text.config(state=tk.DISABLED)
    
    def log_lora_message(self, message, source):
        """Ajoute un message LoRa reçu avec indication de provenance
        
        Args:
            message: Le message reçu
            source: "900MHz" ou "433MHz"
        """
        self.received_text.config(state=tk.NORMAL)
        timestamp = self._get_timestamp()
        
        # Créer un tag unique pour cette source
        tag_name = f"lora_{source}"
        
        # Afficher avec emoji et couleur selon la source
        emoji = "📡" if source == "900MHz" else "📻"
        
        # Vérifier si c'est un message du protocole déjà décodé
        if "Message protocole" in message:
            # Message déjà formaté par l'ESP32, l'afficher tel quel
            self.received_text.insert(tk.END, f"[{timestamp}] {emoji} [{source}] ", tag_name)
            self.received_text.insert(tk.END, f"{message}\n", f"{tag_name}_msg")
        else:
            # Message normal
            self.received_text.insert(tk.END, f"[{timestamp}] {emoji} [{source}] ", tag_name)
            self.received_text.insert(tk.END, f"{message}\n", f"{tag_name}_msg")
        
        # Configuration des tags avec couleurs différentes
        if source == "900MHz":
            self.received_text.tag_config(tag_name, foreground="darkred", font=("Arial", 9, "bold"))
            self.received_text.tag_config(f"{tag_name}_msg", foreground="red", font=("Arial", 9))
        else:  # 433MHz
            self.received_text.tag_config(tag_name, foreground="darkblue", font=("Arial", 9, "bold"))
            self.received_text.tag_config(f"{tag_name}_msg", foreground="blue", font=("Arial", 9))
        
        self.received_text.see(tk.END)
        self.received_text.config(state=tk.DISABLED)

def main():
    app_instance = None
    
    def signal_handler(sig, frame):
        """Gestionnaire pour Ctrl+C"""
        print(f"\n[{get_timestamp()}] [GUI] Interruption détectée (Ctrl+C), fermeture propre...")
        if app_instance:
            app_instance.cleanup()
        sys.exit(0)
    
    # Enregistrer le gestionnaire de signal
    signal.signal(signal.SIGINT, signal_handler)
    
    root = tk.Tk()
    app_instance = LoRaGUI(root)
    
    # Gérer la fermeture de la fenêtre
    def on_closing():
        print(f"[{get_timestamp()}] [GUI] Fermeture de la fenêtre...")
        app_instance.cleanup()
        root.destroy()
    
    root.protocol("WM_DELETE_WINDOW", on_closing)
    root.mainloop()

if __name__ == "__main__":
    main()

