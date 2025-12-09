#!/usr/bin/env python3
"""
Gateway LoRa pour Raspberry Pi 5
Module: E220-900T22D
UART: /dev/ttyAMA0
Protocole: Compatible ESP32 avec chiffrement AES-128-CBC
Base de données: SQLite pour stockage des messages
"""

import serial
import time
import sys
import signal
import sqlite3
import threading
import json
from datetime import datetime
from typing import Optional, Dict, Any, Tuple, List
from pathlib import Path

# Import MQTT (optionnel)
MQTT_AVAILABLE = False
try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    MQTT_AVAILABLE = False
    print("ℹ️  paho-mqtt non disponible. Installez avec: pip install paho-mqtt")

# Compatibilité GPIO pour Raspberry Pi
GPIO_AVAILABLE = False
try:
    import RPi.GPIO as GPIO
    try:
        GPIO.setmode(GPIO.BCM)
        GPIO.setwarnings(False)
        GPIO_AVAILABLE = True
    except (RuntimeError, OSError) as e:
        print(f"⚠️  RPi.GPIO ne peut pas s'initialiser: {e}")
        GPIO_AVAILABLE = False
except ImportError:
    print("⚠️  RPi.GPIO non disponible. Installez avec: pip install RPi.GPIO")

# Stubs pour compatibilité avec la bibliothèque ebyte-lora-e220
class MachinePin:
    IN = 0
    OUT = 1
    
    def __init__(self, pin, mode):
        self.pin = pin
        self.mode = mode
        self._created_time = time.time()
        self._aux_ready_delay = 0.1
        self._aux_ready = False
        if GPIO_AVAILABLE:
            try:
                if mode == self.OUT:
                    GPIO.setup(pin, GPIO.OUT)
                else:
                    GPIO.setup(pin, GPIO.IN)
            except (RuntimeError, OSError):
                pass
    
    def on(self):
        if self.mode == self.OUT and GPIO_AVAILABLE:
            try:
                GPIO.output(self.pin, GPIO.HIGH)
            except (RuntimeError, OSError):
                pass
    
    def off(self):
        if self.mode == self.OUT and GPIO_AVAILABLE:
            try:
                GPIO.output(self.pin, GPIO.LOW)
            except (RuntimeError, OSError):
                pass
    
    def value(self, val=None):
        if val is not None:
            if GPIO_AVAILABLE:
                try:
                    GPIO.output(self.pin, val)
                except (RuntimeError, OSError):
                    pass
        else:
            if GPIO_AVAILABLE:
                try:
                    return GPIO.input(self.pin)
                except (RuntimeError, OSError):
                    pass
            
            # Mode simulé pour AUX
            if self.mode == self.IN:
                elapsed = time.time() - self._created_time
                if elapsed >= self._aux_ready_delay:
                    if not self._aux_ready:
                        self._aux_ready = True
                    return 1
                return 0
            return 0

class Machine:
    Pin = MachinePin

# Injecter dans sys.modules
import types
machine = types.ModuleType('machine')
machine.Pin = MachinePin
sys.modules['machine'] = machine

# Stubs pour utime, ure, ujson
class UTime:
    @staticmethod
    def ticks_ms():
        return int(time.time() * 1000)
    
    @staticmethod
    def ticks_add(ticks, delta):
        return ticks + delta
    
    @staticmethod
    def ticks_diff(end, start):
        return end - start

utime = types.ModuleType('utime')
utime.ticks_ms = UTime.ticks_ms
utime.ticks_add = UTime.ticks_add
utime.ticks_diff = UTime.ticks_diff
sys.modules['utime'] = utime

import re
ure = types.ModuleType('ure')
ure.compile = re.compile
sys.modules['ure'] = ure

import json
ujson = types.ModuleType('ujson')
ujson.loads = json.loads
ujson.dumps = json.dumps
sys.modules['ujson'] = ujson

# Importer la bibliothèque ebyte-lora-e220
try:
    from lora_e220 import LoRaE220
    from lora_e220_operation_constant import ResponseStatusCode
except ImportError as exc:
    raise SystemExit(
        "La bibliothèque 'ebyte-lora-e220' est requise.\n"
        "Installez-la avec: pip install ebyte-lora-e220\n"
        f"Erreur: {exc}"
    ) from exc

# Import pour le déchiffrement AES
try:
    from Crypto.Cipher import AES
    AES_AVAILABLE = True
except ImportError:
    AES_AVAILABLE = False
    print("⚠️  PyCryptodome non disponible. Les messages chiffrés ne pourront pas être déchiffrés.")
    print("   Installez avec: pip install pycryptodome")

# Import du module de base de données
from database import LoRaDatabase


class LoRaProtocolDecoder:
    """Décodeur du protocole ESP32 avec encryption AES-128-CBC."""
    
    MAGIC_ENCRYPTED = 0x01
    MAGIC_CLEAR = 0x02
    
    # Clé AES identique à celle des ESP32 (src/security/Encryption.h)
    AES_KEY = bytes([
        0x2B, 0x7E, 0x15, 0x16,
        0x28, 0xAE, 0xD2, 0xA6,
        0xAB, 0xF7, 0x15, 0x88,
        0x09, 0xCF, 0x4F, 0x3C,
    ])
    AES_IV = bytes([0x00] * 16)
    
    MSG_TYPE_NAMES = {
        0x01: "TEMP",
        0x02: "HUMAN",
        0x03: "HUMAN_COUNT",
        0x04: "SENSOR_DATA",
        0x05: "HUMIDITY",
        0x06: "PRESSURE",
        0x07: "LIGHT",
        0x08: "MOTION",
        0x09: "ENVIRONMENT",
        0x10: "TEXT",
        0x11: "STATUS",
        0x20: "PING",
        0x21: "PONG",
        0xF0: "ACK",
        0xFF: "ERROR",
    }
    
    @staticmethod
    def _remove_padding(data: bytes) -> bytes:
        """Retire le padding PKCS7."""
        if not data or len(data) == 0:
            raise ValueError("Données vides")
        pad_len = data[-1]
        if pad_len == 0 or pad_len > 16 or pad_len > len(data):
            raise ValueError(f"Padding invalide: {pad_len}")
        padding_bytes = data[-pad_len:]
        if padding_bytes != bytes([pad_len]) * pad_len:
            raise ValueError(f"Padding non uniforme")
        return data[:-pad_len]
    
    @classmethod
    def decode(cls, frame: bytes) -> Optional[Dict[str, Any]]:
        """Décode un frame LoRa selon le protocole ESP32."""
        if not frame or len(frame) < 1:
            return None
        
        magic = frame[0]
        payload = frame[1:]
        encrypted = (magic == cls.MAGIC_ENCRYPTED)
        
        if magic not in (cls.MAGIC_ENCRYPTED, cls.MAGIC_CLEAR):
            return None
        
        # Déchiffrer si nécessaire
        if encrypted:
            if not AES_AVAILABLE or AES is None:
                return {"error": "Message chiffré mais PyCryptodome non disponible"}
            if len(payload) == 0 or len(payload) % 16 != 0:
                return {"error": f"Taille invalide pour AES: {len(payload)}"}
            try:
                iv = bytes([0x00] * 16)
                cipher = AES.new(cls.AES_KEY, AES.MODE_CBC, iv)
                decrypted = cipher.decrypt(payload)
                decrypted = cls._remove_padding(decrypted)
            except Exception as e:
                return {"error": f"Erreur déchiffrement: {e}"}
        else:
            decrypted = payload
        
        # Décoder le protocole
        if len(decrypted) < 3:
            return None
        
        msg_type = decrypted[0]
        source_id = decrypted[1]
        data_size = decrypted[2]
        
        if len(decrypted) < 3 + data_size:
            return None
        
        data = decrypted[3:3 + data_size]
        type_name = cls.MSG_TYPE_NAMES.get(msg_type, f"0x{msg_type:02X}")
        
        # Décoder selon le type
        details = cls._decode_by_type(msg_type, data)
        
        return {
            "encrypted": encrypted,
            "magic": magic,
            "type": msg_type,
            "type_name": type_name,
            "source_id": source_id,
            "data_size": data_size,
            "data": data,
            "details": details,
        }
    
    @staticmethod
    def _decode_by_type(msg_type: int, data: bytes) -> Optional[Dict[str, Any]]:
        """Décode les données selon le type de message."""
        if msg_type == 0x10:  # TEXT
            try:
                text = data.decode("utf-8", errors="replace")
                return {"text": text}
            except Exception:
                return {"text": data.hex()}
        
        if msg_type == 0x01:  # TEMP
            if len(data) >= 2:
                temp_x100 = int.from_bytes(data[:2], "little", signed=True)
                return {"temperature_c": temp_x100 / 100.0}
        
        if msg_type == 0x02:  # HUMAN_DETECT
            if len(data) >= 1:
                return {"detected": data[0] != 0}
        
        if msg_type == 0x03:  # HUMAN_COUNT
            if len(data) >= 1:
                return {"count": data[0]}
        
        if msg_type == 0x04:  # SENSOR_DATA (capteur humain avec cibles)
            if len(data) >= 1:
                count = data[0]
                targets = []
                idx = 1
                # Décoder jusqu'à 3 cibles (comme dans gui_app.py)
                for _ in range(min(count, 3)):
                    if idx + 7 < len(data):
                        # X (little endian, signed)
                        x = int.from_bytes(data[idx:idx+2], "little", signed=True)
                        # Y (little endian, signed)
                        y = int.from_bytes(data[idx+2:idx+4], "little", signed=True)
                        # Speed (little endian, signed)
                        speed = int.from_bytes(data[idx+4:idx+6], "little", signed=True)
                        # Resolution (little endian, unsigned)
                        resolution = int.from_bytes(data[idx+6:idx+8], "little", signed=False)
                        idx += 8
                        
                        # Calculer la distance en cm
                        dist_cm = ((x**2 + y**2) ** 0.5) / 10.0
                        
                        targets.append({
                            'x': x,
                            'y': y,
                            'speed': speed,
                            'resolution': resolution,
                            'distance_cm': dist_cm
                        })
                
                return {
                    "count": count,
                    "targets": targets
                }
        
        if msg_type == 0x09:  # ENVIRONMENT
            if len(data) >= 4:
                temp_x100 = int.from_bytes(data[0:2], "little", signed=True)
                pressure_x10 = int.from_bytes(data[2:4], "little", signed=False)
                result = {
                    "temperature_c": temp_x100 / 100.0,
                    "pressure_hpa": pressure_x10 / 10.0,
                }
                if len(data) >= 5 and data[4] <= 100:
                    result["humidity_pct"] = data[4]
                return result
        
        if msg_type in (0x20, 0x21):  # PING/PONG
            if len(data) >= 4:
                ts = int.from_bytes(data[:4], "little", signed=False)
                return {"timestamp_ms": ts}
        
        return None


class LoRaGateway:
    """Gateway LoRa pour Raspberry Pi 5 avec stockage SQLite."""
    
    def __init__(
        self,
        device: str = "/dev/ttyAMA0",
        baudrate: int = 9600,
        channel: int = 23,
        frequency_mhz: float = 873.125,
        aux_pin: int = 17,
        m0_pin: int = 22,
        m1_pin: int = 27,
        db_path: str = "lora_messages.db",
        mqtt_enabled: bool = False,
        mqtt_host: str = "localhost",
        mqtt_port: int = 1883,
        mqtt_topic: str = "lora/data",
        mqtt_username: Optional[str] = None,
        mqtt_password: Optional[str] = None,
    ):
        """
        Args:
            device: Port série (défaut: /dev/ttyAMA0)
            baudrate: Vitesse UART (9600 par défaut)
            channel: Canal LoRa (23 = 873.125 MHz)
            frequency_mhz: Fréquence calculée
            aux_pin: GPIO relié à AUX
            m0_pin: GPIO relié à M0
            m1_pin: GPIO relié à M1
            db_path: Chemin vers la base de données SQLite
            mqtt_enabled: Activer la publication MQTT
            mqtt_host: Adresse du broker MQTT
            mqtt_port: Port du broker MQTT
            mqtt_topic: Topic MQTT pour publier les messages
            mqtt_username: Nom d'utilisateur MQTT (optionnel)
            mqtt_password: Mot de passe MQTT (optionnel)
        """
        self.device = device
        self.baudrate = baudrate
        self.channel = channel
        self.frequency_mhz = frequency_mhz
        self.aux_pin = aux_pin
        self.m0_pin = m0_pin
        self.m1_pin = m1_pin
        self.serial: Optional[serial.Serial] = None
        self.lora: Optional[LoRaE220] = None
        self._serial_lock = threading.Lock()
        self._partial_buffer = bytearray()
        self._partial_buffer_timeout = 0
        self.running = False
        
        # Base de données
        self.db = LoRaDatabase(db_path)
        self.db.initialize()
        
        # MQTT
        self.mqtt_enabled = mqtt_enabled and MQTT_AVAILABLE
        self.mqtt_host = mqtt_host
        self.mqtt_port = mqtt_port
        self.mqtt_topic = mqtt_topic
        self.mqtt_username = mqtt_username
        self.mqtt_password = mqtt_password
        self.mqtt_client: Optional[Any] = None
        
        if self.mqtt_enabled:
            self._init_mqtt()
        
        # Statistiques
        self.stats = {
            "total_received": 0,
            "total_decoded": 0,
            "total_encrypted": 0,
            "total_errors": 0,
            "total_mqtt_published": 0,
            "start_time": time.time(),
        }
    
    def connect(self) -> bool:
        """Initialise la connexion au module LoRa."""
        print("🔌 Connexion au module LoRa E220-900T22D...")
        print(f"   Port: {self.device}")
        print(f"   Baudrate: {self.baudrate}")
        print(f"   Canal: {self.channel} ({self.frequency_mhz} MHz)")
        print(f"   GPIO: AUX={self.aux_pin}, M0={self.m0_pin}, M1={self.m1_pin}")
        
        try:
            # Wrapper pour compatibilité MicroPython -> pyserial
            class SerialWrapper:
                def __init__(self, serial_obj):
                    self._serial = serial_obj
                
                def init(self, baudrate=None, bits=8, parity=None, stop=1, 
                        timeout=None, timeout_char=None):
                    if baudrate is not None:
                        self._serial.baudrate = baudrate
                    if timeout is not None:
                        self._serial.timeout = timeout / 1000.0 if timeout > 0 else None
                    if timeout_char is not None:
                        self._serial.inter_byte_timeout = timeout_char / 1000.0 if timeout_char > 0 else None
                    if parity is not None:
                        parity_map = {0: serial.PARITY_NONE, 1: serial.PARITY_ODD, 2: serial.PARITY_EVEN}
                        self._serial.parity = parity_map.get(parity, serial.PARITY_NONE)
                    if stop == 2:
                        self._serial.stopbits = serial.STOPBITS_TWO
                    else:
                        self._serial.stopbits = serial.STOPBITS_ONE
                    if not self._serial.is_open:
                        self._serial.open()
                
                def any(self):
                    return self._serial.in_waiting
                
                def read(self, size=None):
                    if size is None:
                        available = self._serial.in_waiting
                        if available == 0:
                            return b''
                        return self._serial.read(available)
                    else:
                        return self._serial.read(size)
                
                def deinit(self):
                    if self._serial.is_open:
                        self._serial.close()
                
                def __getattr__(self, name):
                    return getattr(self._serial, name)
            
            serial_obj = serial.Serial(
                port=self.device,
                baudrate=self.baudrate,
                timeout=1.0,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
            )
            time.sleep(0.3)
            
            self.serial = SerialWrapper(serial_obj)
            
            aux_pin = self.aux_pin if GPIO_AVAILABLE else None
            m0_pin = self.m0_pin if GPIO_AVAILABLE else None
            m1_pin = self.m1_pin if GPIO_AVAILABLE else None
            
            if GPIO_AVAILABLE:
                print(f"✅ GPIO disponibles")
            else:
                print("ℹ️  Mode sans GPIO: utilisation du mode 'wait_no_aux'")
            
            self.lora = LoRaE220(
                "900T22D",
                self.serial,
                aux_pin=aux_pin,
                m0_pin=m0_pin,
                m1_pin=m1_pin,
            )
            
            status = self._safe_call(self.lora, "begin")
            if not self._is_success(status):
                print(f"❌ Initialisation LoRa échouée")
                return False
            
            self._configure_module()
            print("✅ Module prêt!\n")
            return True
        
        except serial.SerialException as exc:
            print(f"❌ Erreur série: {exc}")
            print("\n💡 Vérifications:")
            print("   - Le port série existe-t-il? (ls -l /dev/tty*)")
            print("   - Avez-vous les permissions? (sudo usermod -aG dialout $USER)")
            print("   - L'UART est-il activé? (sudo raspi-config)")
            return False
        except Exception as exc:
            print(f"❌ Erreur inattendue: {exc}")
            import traceback
            traceback.print_exc()
            return False
    
    def _configure_module(self) -> None:
        """Configure le module LoRa."""
        if not self.lora:
            return
        
        # Essayer plusieurs méthodes pour entrer en mode configuration
        config_mode_methods = [
            "enter_configuration_mode",
            "setMode",
            "set_mode",
        ]
        
        config_entered = False
        for method in config_mode_methods:
            if hasattr(self.lora, method):
                try:
                    if method == "setMode":
                        # Pour setMode, il faut peut-être passer un paramètre
                        result = self._safe_call(self.lora, method, 3)  # MODE_3_CONFIGURATION
                    else:
                        result = self._safe_call(self.lora, method)
                    if result is not None:
                        config_entered = True
                        break
                except Exception:
                    continue
        
        if not config_entered:
            # Essayer directement avec les pins si disponibles
            if GPIO_AVAILABLE and self.m0_pin and self.m1_pin:
                try:
                    GPIO.output(self.m0_pin, GPIO.HIGH)
                    GPIO.output(self.m1_pin, GPIO.HIGH)
                    time.sleep(0.3)
                    config_entered = True
                except Exception:
                    pass
        
        if not config_entered:
            print("⚠️ Impossible d'entrer en mode configuration")
            return
        
        time.sleep(0.3)  # Délai important pour laisser le module se stabiliser
        
        # Essayer plusieurs méthodes pour lire la configuration
        config = None
        read_methods = [
            "read_configuration",
            "getConfiguration",
            "get_configuration",
        ]
        
        for method in read_methods:
            if hasattr(self.lora, method):
                try:
                    result = self._safe_call(self.lora, method)
                    if result:
                        # Vérifier si c'est un ResponseStructContainer ou directement une config
                        if hasattr(result, "data"):
                            # C'est un ResponseStructContainer
                            if hasattr(result, "status"):
                                status = getattr(result, "status")
                                if hasattr(status, "getResponseDescription"):
                                    desc = status.getResponseDescription()
                                    if desc == "Success" or "success" in str(desc).lower():
                                        config = result.data
                                        break
                        elif hasattr(result, "CHAN") or hasattr(result, "chan"):
                            # C'est directement une Configuration
                            config = result
                            break
                except Exception:
                    continue
        
        # Si on n'a pas réussi, essayer de lire directement depuis le port série
        if not config:
            print("⚠️ Impossible de lire la configuration via la bibliothèque")
            print("   Le module utilisera sa configuration par défaut")
            # Remettre en mode normal
            if GPIO_AVAILABLE and self.m0_pin and self.m1_pin:
                try:
                    GPIO.output(self.m0_pin, GPIO.LOW)
                    GPIO.output(self.m1_pin, GPIO.LOW)
                    time.sleep(0.2)
                except Exception:
                    pass
            else:
                self._safe_call(self.lora, "enter_normal_mode")
            return
        
        # Afficher la configuration lue
        try:
            if hasattr(config, "CHAN"):
                chan = config.CHAN
                print(f"ℹ️ Configuration lue: Canal {chan} ({850.125 + chan:.3f} MHz)")
            elif hasattr(config, "chan"):
                chan = config.chan
                print(f"ℹ️ Configuration lue: Canal {chan} ({850.125 + chan:.3f} MHz)")
        except Exception:
            pass
        
        # Vérifier et modifier si nécessaire
        changed = False
        try:
            if hasattr(config, "CHAN") and config.CHAN != self.channel:
                config.CHAN = self.channel
                changed = True
            elif hasattr(config, "chan") and config.chan != self.channel:
                config.chan = self.channel
                changed = True
        except Exception:
            pass
        
        if changed:
            # Essayer d'écrire la configuration
            write_methods = [
                "write_configuration",
                "setConfiguration",
                "set_configuration",
            ]
            
            config_written = False
            for method in write_methods:
                if hasattr(self.lora, method):
                    try:
                        # Certaines méthodes nécessitent un paramètre supplémentaire
                        if method == "setConfiguration":
                            result = self._safe_call(self.lora, method, config, "WRITE_CFG_PWR_DWN_SAVE")
                        else:
                            result = self._safe_call(self.lora, method, config)
                        if result:
                            config_written = True
                            break
                    except Exception:
                        continue
            
            if config_written:
                print(f"⚙️ Canal radio configuré sur {self.channel} ({self.frequency_mhz} MHz)")
            else:
                print(f"⚠️ Impossible d'écrire la configuration, canal peut être incorrect")
        else:
            print("ℹ️ Canal déjà conforme")
        
        # Remettre en mode normal
        if GPIO_AVAILABLE and self.m0_pin and self.m1_pin:
            try:
                GPIO.output(self.m0_pin, GPIO.LOW)
                GPIO.output(self.m1_pin, GPIO.LOW)
                time.sleep(0.2)
            except Exception:
                self._safe_call(self.lora, "enter_normal_mode")
        else:
            self._safe_call(self.lora, "enter_normal_mode")
    
    def _init_mqtt(self):
        """Initialise la connexion MQTT."""
        if not MQTT_AVAILABLE:
            return
        
        try:
            self.mqtt_client = mqtt.Client(client_id=f"lora_gateway_{int(time.time())}")
            
            if self.mqtt_username and self.mqtt_password:
                self.mqtt_client.username_pw_set(self.mqtt_username, self.mqtt_password)
            
            try:
                self.mqtt_client.connect(self.mqtt_host, self.mqtt_port, 60)
                self.mqtt_client.loop_start()
                print(f"✅ MQTT connecté: {self.mqtt_host}:{self.mqtt_port}, topic: {self.mqtt_topic}")
            except Exception as e:
                print(f"⚠️  Erreur connexion MQTT: {e}")
                self.mqtt_enabled = False
                self.mqtt_client = None
        except Exception as e:
            print(f"⚠️  Erreur initialisation MQTT: {e}")
            self.mqtt_enabled = False
            self.mqtt_client = None
    
    def _publish_mqtt(self, message_data: Dict[str, Any]):
        """Publie un message sur MQTT."""
        if not self.mqtt_enabled or not self.mqtt_client:
            return
        
        try:
            payload = json.dumps(message_data, default=str)
            result = self.mqtt_client.publish(self.mqtt_topic, payload, qos=1)
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                self.stats["total_mqtt_published"] += 1
            else:
                print(f"⚠️  Erreur publication MQTT: {result.rc}")
        except Exception as e:
            print(f"⚠️  Erreur publication MQTT: {e}")
    
    def disconnect(self):
        """Ferme la connexion proprement."""
        self.running = False
        
        # Fermer MQTT
        if self.mqtt_client:
            try:
                self.mqtt_client.loop_stop()
                self.mqtt_client.disconnect()
            except Exception:
                pass
            self.mqtt_client = None
        
        if self.lora:
            self._safe_call(self.lora, "enter_sleep_mode")
        if self.serial and self.serial.is_open:
            self.serial.close()
        self.serial = None
        self.lora = None
        self.db.close()
        print("\n🔌 Connexion fermée")
    
    def _read_serial_bytes(self, serial_obj) -> Optional[bytearray]:
        """Lit tous les octets disponibles depuis le port série."""
        if not hasattr(serial_obj, 'in_waiting'):
            return None
        
        waiting = serial_obj.in_waiting
        if waiting == 0:
            return None
        
        buffer = bytearray()
        max_size = 2048  # Augmenté pour permettre plusieurs messages
        
        # Lire immédiatement ce qui est disponible
        initial_chunk = serial_obj.read(min(waiting, max_size))
        if initial_chunk:
            buffer.extend(initial_chunk)
        
        # Attendre un peu pour que d'autres octets arrivent (messages multiples)
        time.sleep(0.1)  # 100ms pour laisser le temps aux messages suivants
        
        # Lire tous les bytes supplémentaires disponibles en plusieurs passes
        max_iterations = 30  # Augmenté pour lire plus de données
        iteration = 0
        no_data_count = 0  # Compteur pour les itérations sans nouvelles données
        
        while iteration < max_iterations:
            current_waiting = serial_obj.in_waiting
            if current_waiting == 0:
                no_data_count += 1
                # Si on n'a pas de nouvelles données pendant 3 itérations, arrêter
                if no_data_count >= 3:
                    break
                time.sleep(0.01)
                iteration += 1
                continue
            
            no_data_count = 0  # Réinitialiser le compteur
            
            # Lire tous les bytes disponibles
            chunk = serial_obj.read(min(current_waiting, max_size - len(buffer)))
            if chunk:
                buffer.extend(chunk)
            else:
                break
            
            # Si on a reçu des données, attendre un peu pour voir si d'autres arrivent
            if serial_obj.in_waiting > 0:
                time.sleep(0.02)  # 20ms entre les lectures
            
            iteration += 1
        
        return buffer if len(buffer) > 0 else None
    
    def _is_complete_encrypted_message(self, buffer: bytearray) -> bool:
        """Vérifie si le buffer contient un message chiffré complet."""
        if len(buffer) < 1:
            return False
        magic = buffer[0]
        if magic != LoRaProtocolDecoder.MAGIC_ENCRYPTED:
            return False
        payload_len = len(buffer) - 1
        return payload_len > 0 and payload_len % 16 == 0
    
    def _is_complete_clear_message(self, buffer: bytearray) -> bool:
        """Vérifie si le buffer contient un message en clair complet."""
        if len(buffer) < 4:
            return False
        magic = buffer[0]
        if magic != LoRaProtocolDecoder.MAGIC_CLEAR:
            return False
        payload_len = len(buffer) - 1
        if payload_len < 3:
            return False
        data_size = buffer[3]
        expected_len = 1 + 3 + data_size
        return len(buffer) >= expected_len
    
    def _try_decrypt_to_get_length(self, encrypted_data: bytes) -> Optional[int]:
        """Essaie de déchiffrer pour déterminer la taille réelle du message."""
        if not AES_AVAILABLE or len(encrypted_data) < 16:
            return None
        
        # Essayer différentes tailles (16, 32, 48, 64 bytes)
        for size in [16, 32, 48, 64, 80, 96, 112, 128]:
            if len(encrypted_data) < size:
                break
            
            try:
                iv = bytes([0x00] * 16)
                cipher = AES.new(LoRaProtocolDecoder.AES_KEY, AES.MODE_CBC, iv)
                decrypted = cipher.decrypt(encrypted_data[:size])
                
                # Vérifier le padding PKCS7
                if len(decrypted) > 0:
                    pad_len = decrypted[-1]
                    if 1 <= pad_len <= 16 and pad_len <= len(decrypted):
                        # Vérifier que tous les bytes de padding sont identiques
                        padding_bytes = decrypted[-pad_len:]
                        if padding_bytes == bytes([pad_len]) * pad_len:
                            # Padding valide, c'est probablement la bonne taille
                            # Vérifier aussi qu'on a au moins le header (3 bytes)
                            unpadded_len = len(decrypted) - pad_len
                            if unpadded_len >= 3:
                                return size
            except Exception:
                continue
        
        return None
    
    def _get_message_length(self, buffer: bytearray, start_pos: int = 0) -> Optional[int]:
        """Retourne la longueur d'un message complet à partir de start_pos, ou None si incomplet."""
        if start_pos >= len(buffer):
            return None
        
        magic = buffer[start_pos]
        
        if magic == LoRaProtocolDecoder.MAGIC_ENCRYPTED:
            # Message chiffré: doit être un multiple de 16 (après le magic byte)
            payload_available = len(buffer) - start_pos - 1
            if payload_available < 16:
                return None  # Pas assez de données pour un bloc complet
            
            # Essayer de déterminer la taille réelle en déchiffrant
            encrypted_payload = bytes(buffer[start_pos + 1:])
            real_length = self._try_decrypt_to_get_length(encrypted_payload)
            
            if real_length:
                return 1 + real_length  # magic + payload déchiffré
            
            # Si on ne peut pas déterminer, prendre le premier bloc de 16 bytes
            # (c'est mieux que rien, mais peut causer des problèmes avec les messages multiples)
            return 1 + 16  # magic + premier bloc de 16 bytes
        
        if magic == LoRaProtocolDecoder.MAGIC_CLEAR:
            # Message en clair: [MAGIC][TYPE][SOURCE_ID][DATA_SIZE][DATA...]
            if start_pos + 4 > len(buffer):
                return None  # Pas assez pour lire la taille
            data_size = buffer[start_pos + 3]
            expected_len = 1 + 3 + data_size  # magic + header + data
            if start_pos + expected_len <= len(buffer):
                return expected_len
            return None  # Message incomplet
        
        return None  # Magic number inconnu
    
    def _extract_all_messages(self, buffer: bytearray) -> Tuple[List[bytes], bytearray]:
        """
        Extrait tous les messages complets du buffer.
        Retourne (liste_des_messages, buffer_reste_partiel).
        """
        messages = []
        remaining = bytearray()
        
        # Combiner avec le buffer partiel s'il existe
        current_time = time.time()
        if len(self._partial_buffer) > 0:
            if current_time - self._partial_buffer_timeout > 1.0:
                self._partial_buffer.clear()
            else:
                buffer = self._partial_buffer + buffer
                self._partial_buffer.clear()
        
        pos = 0
        max_iterations = 100  # Limite de sécurité
        iteration = 0
        
        while pos < len(buffer) and iteration < max_iterations:
            iteration += 1
            
            if pos >= len(buffer):
                break
            
            magic = buffer[pos]
            
            # Vérifier si c'est un magic number valide
            if magic not in (LoRaProtocolDecoder.MAGIC_ENCRYPTED, LoRaProtocolDecoder.MAGIC_CLEAR):
                # Magic number invalide, chercher le prochain
                pos += 1
                continue
            
            # Obtenir la longueur du message
            msg_len = self._get_message_length(buffer, pos)
            
            if msg_len is None:
                # Message incomplet, garder le reste
                remaining = buffer[pos:]
                break
            
            # Vérifier qu'on a assez de données
            if pos + msg_len > len(buffer):
                # Message incomplet
                remaining = buffer[pos:]
                break
            
            # Extraire le message complet
            message = bytes(buffer[pos:pos + msg_len])
            messages.append(message)
            pos += msg_len
        
        # Mettre à jour le buffer partiel
        if len(remaining) > 0:
            self._partial_buffer = remaining
            self._partial_buffer_timeout = current_time
        else:
            self._partial_buffer.clear()
        
        return messages, remaining
    
    def _process_buffer(self, buffer: bytearray) -> Tuple[Optional[bytes], bool]:
        """Traite le buffer et retourne (message_complet, doit_garder_partiel)."""
        # Cette fonction est conservée pour compatibilité mais utilise maintenant _extract_all_messages
        messages, remaining = self._extract_all_messages(buffer)
        if messages:
            # Retourner le premier message (les autres seront traités dans read_all_data)
            return messages[0], len(remaining) > 0
        return None, len(remaining) > 0
    
    def read_all_data(self) -> List[bytes]:
        """Lit tous les messages disponibles depuis le module."""
        messages = []
        if not self.lora:
            return messages
        
        if not self._serial_lock.acquire(blocking=False):
            return messages
        
        try:
            if not self.serial:
                return messages
            
            try:
                serial_obj = self.serial
                if hasattr(self.serial, '_serial'):
                    serial_obj = self.serial._serial
                
                buffer = self._read_serial_bytes(serial_obj)
                if buffer:
                    extracted_messages, _ = self._extract_all_messages(buffer)
                    messages.extend(extracted_messages)
                    
            except (serial.SerialException, OSError):
                try:
                    if hasattr(self.serial, '_serial'):
                        serial_obj = self.serial._serial
                        if hasattr(serial_obj, 'reset_input_buffer'):
                            serial_obj.reset_input_buffer()
                except Exception:
                    pass
            except Exception:
                pass
        finally:
            try:
                self._serial_lock.release()
            except Exception:
                pass
        
        return messages
    
    def read_data(self) -> Optional[bytes]:
        """Lit un message disponible depuis le module (compatibilité)."""
        messages = self.read_all_data()
        return messages[0] if messages else None
    
    def process_message(self, data: bytes) -> None:
        """Traite un message reçu et le stocke dans la base de données."""
        self.stats["total_received"] += 1
        
        try:
            decoded = LoRaProtocolDecoder.decode(data)
            if decoded and "error" not in decoded:
                self.stats["total_decoded"] += 1
                if decoded["encrypted"]:
                    self.stats["total_encrypted"] += 1
                
                # Afficher le message avec formatage amélioré
                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                print(f"[{timestamp}] 📥 Message reçu:")
                print(f"   Type: {decoded['type_name']} (0x{decoded['type']:02X})")
                print(f"   Source ID: {decoded['source_id']}")
                print(f"   Chiffré: {'Oui' if decoded['encrypted'] else 'Non'}")
                
                # Formatage spécial selon le type de message
                if decoded['details']:
                    details = decoded['details']
                    
                    # Formatage pour SENSOR_DATA (capteur humain)
                    if decoded['type'] == 0x04 and 'targets' in details:
                        count = details.get('count', 0)
                        targets = details.get('targets', [])
                        print(f"   Capteur: {count} {'cible' if count <= 1 else 'cibles'}")
                        
                        if count == 0:
                            print(f"   → Zone libre (pas de présence détectée)")
                        else:
                            for i, target in enumerate(targets):
                                x = target.get('x', 0)
                                y = target.get('y', 0)
                                speed = target.get('speed', 0)
                                resolution = target.get('resolution', 0)
                                dist_cm = target.get('distance_cm', 0.0)
                                print(f"   Cible {i+1}: X={x}mm Y={y}mm ({dist_cm:.1f}cm) v={speed}cm/s res={resolution}")
                    
                    # Formatage pour TEMP
                    elif decoded['type'] == 0x01 and 'temperature_c' in details:
                        temp = details['temperature_c']
                        print(f"   Température: {temp:.1f} °C")
                    
                    # Formatage pour HUMAN_DETECT
                    elif decoded['type'] == 0x02 and 'detected' in details:
                        detected = details['detected']
                        print(f"   Détecté: {'OUI' if detected else 'NON'}")
                    
                    # Formatage pour HUMAN_COUNT
                    elif decoded['type'] == 0x03 and 'count' in details:
                        count = details['count']
                        print(f"   Humains: {count} {'personne' if count <= 1 else 'personnes'}")
                    
                    # Formatage pour ENVIRONMENT
                    elif decoded['type'] == 0x09:
                        temp = details.get('temperature_c', 'N/A')
                        pressure = details.get('pressure_hpa', 'N/A')
                        humidity = details.get('humidity_pct')
                        env_str = f"   Température: {temp:.1f} °C | Pression: {pressure:.1f} hPa"
                        if humidity is not None:
                            env_str += f" | Humidité: {humidity}% RH"
                        print(env_str)
                    
                    # Formatage pour TEXT
                    elif decoded['type'] == 0x10 and 'text' in details:
                        print(f"   Texte: {details['text']}")
                    
                    # Formatage pour PING/PONG
                    elif decoded['type'] in (0x20, 0x21) and 'timestamp_ms' in details:
                        ts = details['timestamp_ms']
                        print(f"   Timestamp: {ts}")
                    
                    # Formatage générique pour les autres types
                    else:
                        for key, value in details.items():
                            if key != 'targets':  # On a déjà affiché les targets
                                print(f"   {key}: {value}")
                
                # Stocker dans la base de données
                self.db.insert_message(
                    source_id=decoded['source_id'],
                    msg_type=decoded['type'],
                    msg_type_name=decoded['type_name'],
                    encrypted=decoded['encrypted'],
                    data_size=decoded['data_size'],
                    data_hex=data.hex(),
                    details=decoded['details'],
                )
                
                # Publier sur MQTT si activé
                if self.mqtt_enabled:
                    mqtt_payload = {
                        "timestamp": timestamp,
                        "source_id": decoded['source_id'],
                        "type": decoded['type_name'],
                        "type_code": decoded['type'],
                        "encrypted": decoded['encrypted'],
                        "data_size": decoded['data_size'],
                        "data_hex": data.hex(),
                        "details": decoded['details'],
                    }
                    self._publish_mqtt(mqtt_payload)
                
                print()
            elif decoded and "error" in decoded:
                self.stats["total_errors"] += 1
                print("⚠️  Erreur décodage: {}".format(decoded['error']))
        except Exception as e:
            self.stats["total_errors"] += 1
            print("⚠️  Erreur traitement message: {}".format(e))
    
    def run(self):
        """Boucle principale de la gateway."""
        self.running = True
        print("=" * 60)
        print("📡 GATEWAY LORA - MODE RÉCEPTION")
        print("=" * 60)
        print("En attente de messages... (Ctrl+C pour arrêter)\n")
        
        try:
            while self.running:
                # Lire TOUS les messages disponibles en une seule fois
                messages = self.read_all_data()
                if messages:
                    # Traiter tous les messages reçus
                    for data in messages:
                        self.process_message(data)
                else:
                    time.sleep(0.01)
        except KeyboardInterrupt:
            print("\n\n⏹️  Arrêt demandé par l'utilisateur")
        finally:
            self._print_stats()
    
    def _print_stats(self):
        """Affiche les statistiques."""
        elapsed = time.time() - self.stats["start_time"]
        print("\n" + "=" * 60)
        print("📊 STATISTIQUES")
        print("=" * 60)
        print(f"Temps d'exécution: {elapsed:.1f} secondes")
        print(f"Messages reçus: {self.stats['total_received']}")
        print(f"Messages décodés: {self.stats['total_decoded']}")
        print(f"Messages chiffrés: {self.stats['total_encrypted']}")
        print(f"Erreurs: {self.stats['total_errors']}")
        if self.mqtt_enabled:
            print(f"Messages MQTT publiés: {self.stats['total_mqtt_published']}")
        print(f"Base de données: {self.db.db_path}")
        print("=" * 60)
    
    @staticmethod
    def _safe_call(target: Any, method_name: str, *args, **kwargs) -> Any:
        if not target:
            return None
        method = getattr(target, method_name, None)
        if not callable(method):
            return None
        return method(*args, **kwargs)
    
    @staticmethod
    def _is_success(status: Any) -> bool:
        if status is None:
            return True
        if isinstance(status, bool):
            return status
        if isinstance(status, int):
            return status == 1
        if hasattr(status, "name"):
            return getattr(status, "name", "").upper() in {"SUCCESS", "OK"}
        return str(status).lower() in {"ok", "success"}


def main():
    """Point d'entrée principal."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Gateway LoRa pour Raspberry Pi 5",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    
    parser.add_argument(
        '--device',
        type=str,
        default='/dev/ttyAMA0',
        help='Port série (défaut: /dev/ttyAMA0)'
    )
    
    parser.add_argument(
        '--baudrate',
        type=int,
        default=9600,
        help='Vitesse de transmission (défaut: 9600)'
    )
    
    parser.add_argument(
        '--channel',
        type=int,
        default=23,
        help='Canal LoRa (défaut: 23 = 873.125 MHz)'
    )
    
    parser.add_argument(
        '--aux-pin',
        type=int,
        default=17,
        help='GPIO relié à AUX (défaut: 17)'
    )
    
    parser.add_argument(
        '--m0-pin',
        type=int,
        default=22,
        help='GPIO relié à M0 (défaut: 22)'
    )
    
    parser.add_argument(
        '--m1-pin',
        type=int,
        default=27,
        help='GPIO relié à M1 (défaut: 27)'
    )
    
    parser.add_argument(
        '--db',
        type=str,
        default='lora_messages.db',
        help='Chemin vers la base de données SQLite (défaut: lora_messages.db)'
    )
    
    parser.add_argument(
        '--mqtt',
        action='store_true',
        help='Activer la publication MQTT'
    )
    
    parser.add_argument(
        '--mqtt-host',
        type=str,
        default='localhost',
        help='Adresse du broker MQTT (défaut: localhost)'
    )
    
    parser.add_argument(
        '--mqtt-port',
        type=int,
        default=1883,
        help='Port du broker MQTT (défaut: 1883)'
    )
    
    parser.add_argument(
        '--mqtt-topic',
        type=str,
        default='lora/data',
        help='Topic MQTT pour publier les messages (défaut: lora/data)'
    )
    
    parser.add_argument(
        '--mqtt-username',
        type=str,
        default=None,
        help='Nom d\'utilisateur MQTT (optionnel)'
    )
    
    parser.add_argument(
        '--mqtt-password',
        type=str,
        default=None,
        help='Mot de passe MQTT (optionnel)'
    )
    
    args = parser.parse_args()
    
    frequency_mhz = 850.125 + args.channel
    
    gateway = LoRaGateway(
        device=args.device,
        baudrate=args.baudrate,
        channel=args.channel,
        frequency_mhz=frequency_mhz,
        aux_pin=args.aux_pin,
        m0_pin=args.m0_pin,
        m1_pin=args.m1_pin,
        db_path=args.db,
        mqtt_enabled=args.mqtt,
        mqtt_host=args.mqtt_host,
        mqtt_port=args.mqtt_port,
        mqtt_topic=args.mqtt_topic,
        mqtt_username=args.mqtt_username,
        mqtt_password=args.mqtt_password,
    )
    
    # Gestionnaire de signal pour arrêt propre
    def signal_handler(sig, frame):
        print("\n\n⏹️  Signal d'arrêt reçu...")
        gateway.disconnect()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    if not gateway.connect():
        sys.exit(1)
    
    try:
        gateway.run()
    finally:
        gateway.disconnect()


if __name__ == '__main__':
    main()

