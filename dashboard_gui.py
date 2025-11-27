#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Dashboard pour visualisation des données capteurs LoRa
"""

import tkinter as tk
from tkinter import ttk
import time
from collections import deque
import math

class SensorCard(ttk.Frame):
    """Widget carte pour afficher les données d'un capteur"""
    
    def __init__(self, parent, sensor_id, sensor_type="UNKNOWN"):
        super().__init__(parent, relief=tk.RAISED, borderwidth=2)
        self.sensor_id = sensor_id
        self.sensor_type = sensor_type
        self.last_update = None
        
        # Header
        header = ttk.Frame(self)
        header.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Label(header, text=f"📡 Capteur #{sensor_id}", 
                 font=("Arial", 12, "bold")).pack(side=tk.LEFT)
        
        self.status_label = ttk.Label(header, text="●", foreground="gray", 
                                     font=("Arial", 16))
        self.status_label.pack(side=tk.RIGHT)
        
        # Type de capteur
        ttk.Label(self, text=sensor_type, foreground="blue",
                 font=("Arial", 10)).pack(pady=2)
        
        # Séparateur
        ttk.Separator(self, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=5)
        
        # Zone de données
        self.data_frame = ttk.Frame(self)
        self.data_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Dernière mise à jour
        self.update_label = ttk.Label(self, text="Aucune donnée", 
                                     foreground="gray", font=("Arial", 8))
        self.update_label.pack(pady=2)
    
    def update_status(self, is_online):
        """Met à jour le statut du capteur"""
        if is_online:
            self.status_label.config(foreground="green")
            self.last_update = time.time()
        else:
            self.status_label.config(foreground="red")
    
    def update_data(self, data):
        """Met à jour les données affichées"""
        # Effacer les anciennes données
        for widget in self.data_frame.winfo_children():
            widget.destroy()
        
        # Afficher selon le type de donnée
        if 'temperature' in data:
            self._display_temperature(data['temperature'])
        
        elif 'targets' in data:
            self._display_targets(data['targets'])
        
        elif 'count' in data:
            self._display_count(data['count'])
        
        # Mettre à jour le timestamp
        self.last_update = time.time()
        self.update_status(True)
        timestamp = time.strftime("%H:%M:%S")
        self.update_label.config(text=f"Mis à jour: {timestamp}")
    
    def _display_temperature(self, temp):
        """Affiche une température"""
        frame = ttk.Frame(self.data_frame)
        frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(frame, text="🌡️ Température:", 
                 font=("Arial", 10, "bold")).pack(side=tk.LEFT)
        ttk.Label(frame, text=f"{temp}°C", 
                 font=("Arial", 14, "bold"), foreground="blue").pack(side=tk.RIGHT)
    
    def _display_count(self, count):
        """Affiche un comptage"""
        frame = ttk.Frame(self.data_frame)
        frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(frame, text="👥 Humains:", 
                 font=("Arial", 10, "bold")).pack(side=tk.LEFT)
        ttk.Label(frame, text=str(count), 
                 font=("Arial", 14, "bold"), foreground="green").pack(side=tk.RIGHT)
    
    def _display_targets(self, targets):
        """Affiche les cibles détectées"""
        ttk.Label(self.data_frame, text=f"👥 {len(targets)} cible(s) valide(s)", 
                 font=("Arial", 10, "bold"), foreground="green").pack(pady=5)
        
        # Info sur le filtrage
        if len(targets) > 0:
            ttk.Label(self.data_frame, text="(Filtrées: Y>0 devant capteur, distance<6m)", 
                     font=("Arial", 7), foreground="gray").pack()
        
        for i, target in enumerate(targets):
            self._display_target(i+1, target)
    
    def _display_target(self, index, target):
        """Affiche une cible individuelle (format compact)"""
        frame = ttk.LabelFrame(self.data_frame, text=f"Cible {index}", padding=3)
        frame.pack(fill=tk.X, pady=2)
        
        # Distance (principal)
        dist_label = ttk.Label(frame, text=f"📏 {target['distance']:.1f} cm", 
                              font=("Arial", 11, "bold"), foreground="blue")
        dist_label.pack(anchor=tk.W)
        
        # Position (compact)
        pos_text = f"📍 X:{target['x']/10:.0f}cm Y:{target['y']/10:.0f}cm"
        ttk.Label(frame, text=pos_text, font=("Arial", 8), 
                 foreground="purple").pack(anchor=tk.W)
        
        # Vitesse (si non-nulle)
        if target['speed'] != 0:
            speed_text = f"⚡ {abs(target['speed'])} cm/s"
            ttk.Label(frame, text=speed_text, font=("Arial", 8),
                     foreground="orange").pack(anchor=tk.W)


class RadarVisualization(tk.Canvas):
    """Widget pour visualiser les cibles sur un radar 2D (moitié haute seulement)"""
    
    def __init__(self, parent, width=400, height=300):
        super().__init__(parent, width=width, height=height, bg="black")
        self.width = width
        self.height = height
        self.center_x = width // 2
        self.center_y = height - 20  # Capteur en bas, zone de détection vers le haut
        self.max_distance = 6000  # Distance max en mm (6 mètres)
        self.scale = (height - 40) / self.max_distance  # Échelle basée sur hauteur
        
        self.draw_radar_grid()
    
    def draw_radar_grid(self):
        """Dessine la grille du radar (moitié supérieure seulement)"""
        self.delete("all")
        
        # Demi-cercles de distance (tous les mètres) - partie supérieure seulement
        for i in range(1, 7):
            radius = i * 1000 * self.scale  # i mètres en pixels
            # Dessiner arc au lieu de cercle complet (de -90° à +90°, soit 180° vers le haut)
            self.create_arc(
                self.center_x - radius, self.center_y - radius,
                self.center_x + radius, self.center_y + radius,
                start=0, extent=180,  # 180° = demi-cercle supérieur
                outline="green", width=1, style=tk.ARC, tags="grid"
            )
            # Étiquette de distance (en haut au centre)
            self.create_text(
                self.center_x, self.center_y - radius + 10,
                text=f"{i}m", fill="green", font=("Arial", 8), tags="grid"
            )
        
        # Axes
        # Axe vertical (centre vers le haut)
        self.create_line(self.center_x, 0, self.center_x, self.center_y,
                        fill="green", width=2, tags="grid")
        # Axe horizontal (ligne de base)
        self.create_line(0, self.center_y, self.width, self.center_y,
                        fill="green", width=2, tags="grid")
        
        # Lignes angulaires ±60° (zone de détection selon manuel HLK-LD2450)
        angle_60 = math.radians(60)
        max_radius = 6 * 1000 * self.scale
        # Ligne gauche (-60°)
        x_left = self.center_x - max_radius * math.sin(angle_60)
        y_left = self.center_y - max_radius * math.cos(angle_60)
        self.create_line(self.center_x, self.center_y, x_left, y_left,
                        fill="yellow", width=1, dash=(5, 3), tags="grid")
        # Ligne droite (+60°)
        x_right = self.center_x + max_radius * math.sin(angle_60)
        y_right = self.center_y - max_radius * math.cos(angle_60)
        self.create_line(self.center_x, self.center_y, x_right, y_right,
                        fill="yellow", width=1, dash=(5, 3), tags="grid")
        
        # Texte "±60°"
        self.create_text(10, 10, text="±60°", fill="yellow", 
                        font=("Arial", 9), tags="grid", anchor="nw")
        
        # Position du capteur (en bas au centre)
        self.create_oval(
            self.center_x - 6, self.center_y - 6,
            self.center_x + 6, self.center_y + 6,
            fill="red", outline="white", width=2, tags="sensor"
        )
        self.create_text(
            self.center_x, self.center_y - 15,
            text="📡 CAPTEUR", fill="red", font=("Arial", 9, "bold"), tags="sensor"
        )
    
    def update_targets(self, targets):
        """Met à jour l'affichage des cibles (moitié supérieure seulement)"""
        # Effacer les anciennes cibles
        self.delete("target")
        
        # Filtrer les cibles avec Y positif (devant le capteur) et distance < 6m
        valid_targets = [t for t in targets 
                        if t['y'] > 0 and t['distance'] < 600]  # Y>0 = devant, <600cm = 6m
        
        # Afficher chaque cible
        for i, target in enumerate(valid_targets):
            x_mm = target['x']
            y_mm = target['y']  # Y positif = devant le capteur
            
            # Vérifier que la cible est dans les limites
            distance = (x_mm**2 + y_mm**2) ** 0.5
            if distance > self.max_distance:
                continue  # Ignorer les cibles hors limites
            
            # Convertir en pixels (Y vers le haut = négatif en coordonnées écran)
            x_px = self.center_x + (x_mm * self.scale)
            y_px = self.center_y - (y_mm * self.scale)  # - car Y écran inversé
            
            # Ne dessiner que si dans la zone visible
            if y_px < 0 or y_px > self.center_y:
                continue
            
            # Dessiner la cible
            radius = 12
            color = ["yellow", "cyan", "magenta"][i % 3]
            
            self.create_oval(
                x_px - radius, y_px - radius,
                x_px + radius, y_px + radius,
                fill=color, outline="white", width=2, tags="target"
            )
            
            # Étiquette avec info
            dist_text = f"#{i+1} {target['distance']:.1f}cm"
            if target.get('speed', 0) != 0:
                dist_text += f"\n⚡{target['speed']}cm/s"
            
            self.create_text(
                x_px, y_px + radius + 12,
                text=dist_text,
                fill=color, font=("Arial", 9, "bold"), tags="target"
            )
            
            # Ligne vers le centre (capteur)
            self.create_line(
                self.center_x, self.center_y, x_px, y_px,
                fill=color, width=2, dash=(4, 2), tags="target"
            )


class DashboardFrame(ttk.Frame):
    """Frame principal du dashboard"""
    
    def __init__(self, parent):
        super().__init__(parent)
        self.sensors = {}  # Dict[sensor_id, SensorCard]
        self.setup_ui()
    
    def setup_ui(self):
        """Configure l'interface du dashboard"""
        # Header
        header = ttk.Frame(self)
        header.pack(fill=tk.X, padx=10, pady=10)
        
        ttk.Label(header, text="📊 Dashboard Capteurs LoRa", 
                 font=("Arial", 16, "bold")).pack(side=tk.LEFT)
        
        self.sensor_count_label = ttk.Label(header, text="0 capteur(s)", 
                                           font=("Arial", 12))
        self.sensor_count_label.pack(side=tk.RIGHT)
        
        # Frame principal avec scrollbar HORIZONTAL
        main_container = ttk.Frame(self)
        main_container.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        # Canvas avec scrollbar horizontale
        canvas = tk.Canvas(main_container, height=350)  # Hauteur fixe pour les cartes
        scrollbar = ttk.Scrollbar(main_container, orient=tk.HORIZONTAL, command=canvas.xview)
        self.scrollable_frame = ttk.Frame(canvas)
        
        self.scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        
        canvas.create_window((0, 0), window=self.scrollable_frame, anchor="nw")
        canvas.configure(xscrollcommand=scrollbar.set)
        
        scrollbar.pack(side=tk.BOTTOM, fill=tk.X)
        canvas.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        
        # Visualisation radar (en bas, zone supérieure seulement)
        self.radar_frame = ttk.LabelFrame(self, text="🎯 Vue Radar (Zone avant: 6m, ±60°)", padding=10)
        self.radar_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        # Centrer le radar
        radar_container = ttk.Frame(self.radar_frame)
        radar_container.pack(expand=True)
        
        self.radar = RadarVisualization(radar_container, width=600, height=350)
        self.radar.pack()
        
        # Message si aucun capteur
        self.no_sensor_label = ttk.Label(
            self.scrollable_frame,
            text="Aucun capteur détecté\nLes capteurs apparaîtront automatiquement lors de la réception de données",
            font=("Arial", 12),
            foreground="gray"
        )
        self.no_sensor_label.pack(pady=50)
    
    def update_sensor_data(self, sensor_id, msg_type, data):
        """Met à jour les données d'un capteur"""
        # Créer le capteur s'il n'existe pas
        if sensor_id not in self.sensors:
            self._create_sensor_card(sensor_id, msg_type)
        
        # Mettre à jour les données
        if sensor_id in self.sensors:
            self.sensors[sensor_id].update_data(data)
            
            # Mettre à jour le radar si données de cibles
            if 'targets' in data:
                self.radar.update_targets(data['targets'])
        
        # Mettre à jour le compteur
        self.sensor_count_label.config(text=f"{len(self.sensors)} capteur(s)")
    
    def _create_sensor_card(self, sensor_id, msg_type):
        """Crée une carte pour un nouveau capteur"""
        # Masquer le message "aucun capteur"
        self.no_sensor_label.pack_forget()
        
        # Type de capteur selon le message
        sensor_types = {
            0x01: "TEMPÉRATURE",
            0x02: "DÉTECTION HUMAINE",
            0x03: "COMPTAGE HUMAIN",
            0x04: "CAPTEUR MULTI-CIBLES"
        }
        sensor_type = sensor_types.get(msg_type, "CAPTEUR GÉNÉRIQUE")
        
        # Créer la carte (côte à côte horizontalement)
        card = SensorCard(self.scrollable_frame, sensor_id, sensor_type)
        card.pack(side=tk.LEFT, fill=tk.BOTH, expand=False, padx=5, pady=5)
        card.config(width=280)  # Largeur fixe pour uniformité
        
        self.sensors[sensor_id] = card
    
    def cleanup_offline_sensors(self, timeout=30):
        """Marque les capteurs hors ligne après un timeout"""
        current_time = time.time()
        for sensor in self.sensors.values():
            if sensor.last_update and (current_time - sensor.last_update) > timeout:
                sensor.update_status(False)

