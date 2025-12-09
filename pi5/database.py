#!/usr/bin/env python3
"""
Module de base de données SQLite pour stocker les messages LoRa
"""

import sqlite3
import json
from datetime import datetime
from typing import Optional, Dict, Any, List
from pathlib import Path


class LoRaDatabase:
    """Gestionnaire de base de données SQLite pour les messages LoRa."""
    
    def __init__(self, db_path: str = "lora_messages.db"):
        """
        Args:
            db_path: Chemin vers le fichier de base de données SQLite
        """
        self.db_path = db_path
        self.conn: Optional[sqlite3.Connection] = None
    
    def initialize(self) -> bool:
        """Initialise la base de données et crée les tables si nécessaire."""
        try:
            self.conn = sqlite3.connect(self.db_path, check_same_thread=False)
            self.conn.row_factory = sqlite3.Row
            
            cursor = self.conn.cursor()
            
            # Table principale des messages
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS messages (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
                    source_id INTEGER NOT NULL,
                    msg_type INTEGER NOT NULL,
                    msg_type_name TEXT NOT NULL,
                    encrypted INTEGER NOT NULL DEFAULT 0,
                    data_size INTEGER NOT NULL,
                    data_hex TEXT NOT NULL,
                    details_json TEXT,
                    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
                )
            """)
            
            # Index pour améliorer les performances de requête
            cursor.execute("""
                CREATE INDEX IF NOT EXISTS idx_timestamp ON messages(timestamp)
            """)
            cursor.execute("""
                CREATE INDEX IF NOT EXISTS idx_source_id ON messages(source_id)
            """)
            cursor.execute("""
                CREATE INDEX IF NOT EXISTS idx_msg_type ON messages(msg_type)
            """)
            
            # Table des statistiques par source
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS source_stats (
                    source_id INTEGER PRIMARY KEY,
                    first_seen DATETIME,
                    last_seen DATETIME,
                    message_count INTEGER DEFAULT 0,
                    last_message_type INTEGER,
                    last_message_type_name TEXT
                )
            """)
            
            self.conn.commit()
            print(f"✅ Base de données initialisée: {self.db_path}")
            return True
            
        except sqlite3.Error as e:
            print(f"❌ Erreur initialisation base de données: {e}")
            return False
    
    def insert_message(
        self,
        source_id: int,
        msg_type: int,
        msg_type_name: str,
        encrypted: bool,
        data_size: int,
        data_hex: str,
        details: Optional[Dict[str, Any]] = None,
    ) -> bool:
        """
        Insère un message dans la base de données.
        
        Args:
            source_id: ID de la source (0-255)
            msg_type: Type de message (0x01, 0x02, etc.)
            msg_type_name: Nom du type de message
            encrypted: True si le message était chiffré
            data_size: Taille des données
            data_hex: Données en hexadécimal
            details: Détails décodés du message (dict)
        
        Returns:
            True si l'insertion a réussi
        """
        if not self.conn:
            return False
        
        try:
            cursor = self.conn.cursor()
            timestamp = datetime.now().isoformat()
            details_json = json.dumps(details) if details else None
            
            cursor.execute("""
                INSERT INTO messages (
                    timestamp, source_id, msg_type, msg_type_name,
                    encrypted, data_size, data_hex, details_json
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                timestamp,
                source_id,
                msg_type,
                msg_type_name,
                1 if encrypted else 0,
                data_size,
                data_hex,
                details_json,
            ))
            
            # Mettre à jour les statistiques de la source
            cursor.execute("""
                INSERT OR REPLACE INTO source_stats (
                    source_id, first_seen, last_seen, message_count,
                    last_message_type, last_message_type_name
                ) VALUES (
                    ?,
                    COALESCE((SELECT first_seen FROM source_stats WHERE source_id = ?), ?),
                    ?,
                    COALESCE((SELECT message_count FROM source_stats WHERE source_id = ?), 0) + 1,
                    ?,
                    ?
                )
            """, (
                source_id, source_id, timestamp, timestamp, source_id,
                msg_type, msg_type_name
            ))
            
            self.conn.commit()
            return True
            
        except sqlite3.Error as e:
            print(f"⚠️  Erreur insertion message: {e}")
            return False
    
    def get_recent_messages(self, limit: int = 50) -> List[Dict[str, Any]]:
        """
        Récupère les messages les plus récents.
        
        Args:
            limit: Nombre maximum de messages à récupérer
        
        Returns:
            Liste de dictionnaires contenant les messages
        """
        if not self.conn:
            return []
        
        try:
            cursor = self.conn.cursor()
            cursor.execute("""
                SELECT * FROM messages
                ORDER BY timestamp DESC
                LIMIT ?
            """, (limit,))
            
            rows = cursor.fetchall()
            messages = []
            for row in rows:
                msg = dict(row)
                if msg['details_json']:
                    try:
                        msg['details'] = json.loads(msg['details_json'])
                    except json.JSONDecodeError:
                        msg['details'] = None
                else:
                    msg['details'] = None
                del msg['details_json']
                messages.append(msg)
            
            return messages
            
        except sqlite3.Error as e:
            print(f"⚠️  Erreur récupération messages: {e}")
            return []
    
    def get_messages_by_source(self, source_id: int, limit: int = 50) -> List[Dict[str, Any]]:
        """
        Récupère les messages d'une source spécifique.
        
        Args:
            source_id: ID de la source
            limit: Nombre maximum de messages à récupérer
        
        Returns:
            Liste de dictionnaires contenant les messages
        """
        if not self.conn:
            return []
        
        try:
            cursor = self.conn.cursor()
            cursor.execute("""
                SELECT * FROM messages
                WHERE source_id = ?
                ORDER BY timestamp DESC
                LIMIT ?
            """, (source_id, limit))
            
            rows = cursor.fetchall()
            messages = []
            for row in rows:
                msg = dict(row)
                if msg['details_json']:
                    try:
                        msg['details'] = json.loads(msg['details_json'])
                    except json.JSONDecodeError:
                        msg['details'] = None
                else:
                    msg['details'] = None
                del msg['details_json']
                messages.append(msg)
            
            return messages
            
        except sqlite3.Error as e:
            print(f"⚠️  Erreur récupération messages: {e}")
            return []
    
    def get_source_stats(self) -> List[Dict[str, Any]]:
        """
        Récupère les statistiques de toutes les sources.
        
        Returns:
            Liste de dictionnaires contenant les statistiques
        """
        if not self.conn:
            return []
        
        try:
            cursor = self.conn.cursor()
            cursor.execute("""
                SELECT * FROM source_stats
                ORDER BY last_seen DESC
            """)
            
            rows = cursor.fetchall()
            return [dict(row) for row in rows]
            
        except sqlite3.Error as e:
            print(f"⚠️  Erreur récupération statistiques: {e}")
            return []
    
    def get_message_count(self) -> int:
        """Retourne le nombre total de messages dans la base de données."""
        if not self.conn:
            return 0
        
        try:
            cursor = self.conn.cursor()
            cursor.execute("SELECT COUNT(*) FROM messages")
            result = cursor.fetchone()
            return result[0] if result else 0
        except sqlite3.Error:
            return 0
    
    def close(self):
        """Ferme la connexion à la base de données."""
        if self.conn:
            self.conn.close()
            self.conn = None

