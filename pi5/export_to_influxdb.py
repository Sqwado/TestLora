#!/usr/bin/env python3
"""
Script d'export des données SQLite vers InfluxDB
Pour utiliser avec Grafana
"""

import sqlite3
import sys
from datetime import datetime
from typing import Optional

try:
    import requests
except ImportError:
    requests = None

try:
    from influxdb import InfluxDBClient
    INFLUXDB_AVAILABLE = True
except ImportError:
    INFLUXDB_AVAILABLE = False
    print("⚠️  influxdb non disponible. Installez avec: pip install influxdb")
    sys.exit(1)


def export_messages(
    db_path: str = "lora_messages.db",
    influxdb_host: str = "localhost",
    influxdb_port: int = 8086,
    influxdb_database: str = "lora",
    influxdb_username: Optional[str] = None,
    influxdb_password: Optional[str] = None,
):
    """Exporte les messages depuis SQLite vers InfluxDB."""
    
    # Connexion SQLite
    print(f"📂 Lecture de {db_path}...")
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    
    # Connexion InfluxDB
    print(f"🔌 Connexion à InfluxDB ({influxdb_host}:{influxdb_port})...")
    try:
        client = InfluxDBClient(
            host=influxdb_host,
            port=influxdb_port,
            username=influxdb_username,
            password=influxdb_password,
            database=influxdb_database,
        )
        
        # Tester la connexion en listant les bases de données
        databases = client.get_list_database()
    except Exception as e:
        # Vérifier si c'est une erreur de connexion
        error_type = type(e).__name__
        error_str = str(e)
        is_connection_error = (
            'Connection' in error_type or 
            'ConnectionRefused' in error_type or
            'Connection refused' in error_str or
            (requests and isinstance(e, getattr(requests.exceptions, 'ConnectionError', type(None))))
        )
        
        if is_connection_error:
            print(f"\n❌ Erreur de connexion à InfluxDB!")
            print(f"   Impossible de se connecter à {influxdb_host}:{influxdb_port}")
            print(f"\n💡 Solutions possibles:")
            print(f"   1. Vérifiez qu'InfluxDB est installé et en cours d'exécution")
            print(f"   2. Pour démarrer InfluxDB avec Docker:")
            print(f"      docker run -d -p 8086:8086 -v influxdb-storage:/var/lib/influxdb2 influxdb:latest")
            print(f"   3. Pour installer InfluxDB sur Raspberry Pi:")
            print(f"      wget https://dl.influxdata.com/influxdb/releases/influxdb2-2.7.4-linux-arm64.tar.gz")
            print(f"      tar xvzf influxdb2-2.7.4-linux-arm64.tar.gz")
            print(f"      sudo cp influxdb2-2.7.4-linux-arm64/influxd /usr/local/bin/")
            print(f"      influxd")
            print(f"   4. Vérifiez que le port {influxdb_port} n'est pas bloqué par un pare-feu")
            print(f"\n   Erreur détaillée: {e}")
        else:
            print(f"\n❌ Erreur lors de la connexion à InfluxDB: {e}")
        conn.close()
        sys.exit(1)
    if not any(db['name'] == influxdb_database for db in databases):
        print(f"📦 Création de la base de données {influxdb_database}...")
        client.create_database(influxdb_database)
    
    # Lire tous les messages
    cursor.execute("SELECT * FROM messages ORDER BY timestamp ASC")
    rows = cursor.fetchall()
    
    print(f"📊 Export de {len(rows)} messages...")
    
    points = []
    for row in rows:
        timestamp = datetime.fromisoformat(row['timestamp'])
        
        # Point de base
        point = {
            "measurement": "lora_messages",
            "time": timestamp.isoformat(),
            "tags": {
                "source_id": row['source_id'],
                "msg_type": row['msg_type_name'],
                "encrypted": "true" if row['encrypted'] else "false",
            },
            "fields": {
                "msg_type_code": row['msg_type'],
                "data_size": row['data_size'],
            }
        }
        
        # Ajouter les détails selon le type
        if row['details_json']:
            import json
            try:
                details = json.loads(row['details_json'])
                if details:
                    # Température
                    if 'temperature_c' in details:
                        point["fields"]["temperature_c"] = details['temperature_c']
                    
                    # Pression
                    if 'pressure_hpa' in details:
                        point["fields"]["pressure_hpa"] = details['pressure_hpa']
                    
                    # Humidité
                    if 'humidity_pct' in details:
                        point["fields"]["humidity_pct"] = details['humidity_pct']
                    
                    # Détection humaine
                    if 'detected' in details:
                        point["fields"]["human_detected"] = 1 if details['detected'] else 0
                    
                    # Comptage humain
                    if 'count' in details:
                        point["fields"]["human_count"] = details['count']
            except json.JSONDecodeError:
                pass
        
        points.append(point)
        
        # Écrire par batch de 1000
        if len(points) >= 1000:
            client.write_points(points)
            print(f"  ✅ {len(points)} points écrits...")
            points = []
    
    # Écrire les points restants
    if points:
        client.write_points(points)
        print(f"  ✅ {len(points)} points écrits...")
    
    conn.close()
    print(f"\n✅ Export terminé: {len(rows)} messages exportés vers InfluxDB")


if __name__ == '__main__':
    import argparse
    
    parser = argparse.ArgumentParser(description="Export SQLite vers InfluxDB")
    parser.add_argument('--db', default='lora_messages.db', help='Fichier SQLite')
    parser.add_argument('--host', default='localhost', help='Host InfluxDB')
    parser.add_argument('--port', type=int, default=8086, help='Port InfluxDB')
    parser.add_argument('--database', default='lora', help='Base de données InfluxDB')
    parser.add_argument('--username', default=None, help='Username InfluxDB')
    parser.add_argument('--password', default=None, help='Password InfluxDB')
    
    args = parser.parse_args()
    
    export_messages(
        db_path=args.db,
        influxdb_host=args.host,
        influxdb_port=args.port,
        influxdb_database=args.database,
        influxdb_username=args.username,
        influxdb_password=args.password,
    )

