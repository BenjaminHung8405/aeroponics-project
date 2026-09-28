#!/usr/bin/env python3
"""
Aeroponics Tuya PH-W218 to MQTT Lean Bridge.
Communicates directly with Tuya PH-W218 over LAN using tinytuya (Tuya Protocol 3.3).
Decodes water quality parameters (pH, EC, TDS, Temp, ORP, Salinity, SG, CF)
and publishes telemetry events to Mosquitto MQTT (shared with mushroom-cp).
Zero Home Assistant overhead.
"""

import os
import sys
import time
import json
import logging
import signal
import tinytuya
import paho.mqtt.client as mqtt

# Configure Logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [TuyaBridge] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger("TuyaBridge")

# Configuration from Environment Variables
TUYA_DEVICE_ID = os.getenv("TUYA_DEVICE_ID", "a3ad08cba7cf0f7f873wty")
TUYA_DEVICE_IP = os.getenv("TUYA_DEVICE_IP", "192.168.0.102")
TUYA_LOCAL_KEY = os.getenv("TUYA_LOCAL_KEY", "")
TUYA_SENSOR_ID = os.getenv("TUYA_SENSOR_ID", "ph-w218-01")
TUYA_VERSION = float(os.getenv("TUYA_VERSION", "3.3"))
POLL_INTERVAL_SEC = int(os.getenv("POLL_INTERVAL_SEC", "15"))

MQTT_HOST = os.getenv("MQTT_HOST", "mushroom_mqtt")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.getenv("MQTT_USERNAME", "aero_backend")
MQTT_PASS = os.getenv("MQTT_PASSWORD", "123456")

TOPIC_TELEMETRY = f"aeroponics/sensors/{TUYA_SENSOR_ID}/state"
TOPIC_STATUS = f"aeroponics/sensors/{TUYA_SENSOR_ID}/status"

running = True


def handle_sigterm(signum, frame):
    global running
    logger.info("Received termination signal, stopping bridge gracefully...")
    running = False


signal.signal(signal.SIGINT, handle_sigterm)
signal.signal(signal.SIGTERM, handle_sigterm)


def decode_dps(dps: dict) -> dict:
    """
    Decodes raw Tuya DPs based on production hardware specs:
    DP 8: Water Temperature (°C * 10)
    DP 106: pH (* 100, 1500 = probe missing/unconnected)
    DP 111: TDS (ppm)
    DP 116: EC (μS/cm)
    DP 121: Salinity (ppm)
    DP 126: Specific Gravity (* 1000)
    DP 131: ORP (mV)
    DP 136: Conductivity Factor (* 100)
    DP 141: Ambient Humidity (%)
    """
    if not dps or not isinstance(dps, dict):
        return None

    # pH: DP 106 (fallback 101/1)
    raw_ph = dps.get("106") or dps.get(106) or dps.get("101") or dps.get(101)
    ph = None
    if raw_ph is not None and str(raw_ph) != "1500":
        try:
            val = float(raw_ph)
            if val > 140:
                ph = round(val / 100.0, 2)
            elif val > 14:
                ph = round(val / 10.0, 2)
            else:
                ph = round(val, 2)
        except (ValueError, TypeError):
            ph = None

    # Water Temp: DP 8 (fallback 104)
    raw_temp = dps.get("8") or dps.get(8) or dps.get("104") or dps.get(104)
    temp = None
    if raw_temp is not None:
        try:
            val = float(raw_temp)
            if abs(val) >= 100:
                temp = round(val / 10.0, 1)
            else:
                temp = round(val, 1)
        except (ValueError, TypeError):
            temp = None

    # EC: DP 116 (fallback 102)
    raw_ec = dps.get("116") or dps.get(116) or dps.get("102") or dps.get(102)
    ec = None
    if raw_ec is not None:
        try:
            ec = int(float(raw_ec))
        except (ValueError, TypeError):
            ec = None

    # TDS: DP 111 (fallback 103)
    raw_tds = dps.get("111") or dps.get(111) or dps.get("103") or dps.get(103)
    tds = None
    if raw_tds is not None:
        try:
            tds = int(float(raw_tds))
        except (ValueError, TypeError):
            tds = None

    # ORP: DP 131 (fallback 106)
    # Note: if DP 106 was already consumed as pH and is <= 1400, it's not ORP
    raw_orp = dps.get("131") or dps.get(131)
    orp = None
    if raw_orp is not None:
        try:
            orp = int(float(raw_orp))
        except (ValueError, TypeError):
            orp = None

    # Salinity: DP 121 (fallback 105)
    raw_salinity = dps.get("121") or dps.get(121) or dps.get("105") or dps.get(105)
    salinity = None
    if raw_salinity is not None:
        try:
            salinity = int(float(raw_salinity))
        except (ValueError, TypeError):
            salinity = None

    # Specific Gravity (SG): DP 126
    raw_sg = dps.get("126") or dps.get(126)
    sg = None
    if raw_sg is not None:
        try:
            sg = round(float(raw_sg) / 1000.0, 3)
        except (ValueError, TypeError):
            sg = None

    # Conductivity Factor (CF): DP 136
    raw_cf = dps.get("136") or dps.get(136)
    cf = None
    if raw_cf is not None:
        try:
            cf = round(float(raw_cf) / 100.0, 2)
        except (ValueError, TypeError):
            cf = None

    # Ambient Humidity: DP 141
    raw_humidity = dps.get("141") or dps.get(141)
    humidity = None
    if raw_humidity is not None:
        try:
            humidity = int(float(raw_humidity))
        except (ValueError, TypeError):
            humidity = None

    return {
        "sensor_id": TUYA_SENSOR_ID,
        "ph": ph,
        "ec": ec,
        "tds": tds,
        "temperature_c": temp,
        "orp": orp,
        "salinity": salinity,
        "specific_gravity": sg,
        "conductivity_factor": cf,
        "humidity": humidity,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }


def main():
    logger.info("Initializing Tuya PH-W218 MQTT Lean Bridge...")
    logger.info(f"Target Sensor: {TUYA_SENSOR_ID} at {TUYA_DEVICE_IP} (Protocol v{TUYA_VERSION})")
    logger.info(f"Connecting to MQTT Broker at {MQTT_HOST}:{MQTT_PORT}...")

    # MQTT Client setup
    mqtt_client = mqtt.Client(client_id=f"tuya_bridge_{TUYA_SENSOR_ID}")
    if MQTT_USER and MQTT_PASS:
        mqtt_client.username_pw_set(MQTT_USER, MQTT_PASS)

    # Set Last Will and Testament
    lwt_payload = json.dumps({"status": "offline", "sensor_id": TUYA_SENSOR_ID, "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})
    mqtt_client.will_set(TOPIC_STATUS, lwt_payload, qos=1, retain=True)

    mqtt_connected = False
    try:
        mqtt_client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
        mqtt_client.loop_start()
        mqtt_connected = True
        logger.info("MQTT Broker connected successfully!")
        online_payload = json.dumps({"status": "online", "sensor_id": TUYA_SENSOR_ID, "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})
        mqtt_client.publish(TOPIC_STATUS, online_payload, qos=1, retain=True)
    except Exception as e:
        logger.warning(f"Could not connect to MQTT broker ({e}). Will retry in loop.")

    # Tuya Device Client setup
    device = None
    if TUYA_DEVICE_ID and TUYA_LOCAL_KEY:
        device = tinytuya.OutletDevice(
            dev_id=TUYA_DEVICE_ID,
            address=TUYA_DEVICE_IP,
            local_key=TUYA_LOCAL_KEY,
            version=TUYA_VERSION,
        )
        device.set_socketPersistent(True)
        device.set_socketTimeout(5)
    else:
        logger.error("TUYA_DEVICE_ID or TUYA_LOCAL_KEY missing! Check environment variables.")

    last_good_payload = None

    while running:
        # Reconnect MQTT if disconnected
        if not mqtt_connected:
            try:
                mqtt_client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
                mqtt_client.loop_start()
                mqtt_connected = True
                logger.info("Reconnected to MQTT Broker!")
                online_payload = json.dumps({"status": "online", "sensor_id": TUYA_SENSOR_ID, "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})
                mqtt_client.publish(TOPIC_STATUS, online_payload, qos=1, retain=True)
            except Exception as e:
                logger.debug(f"MQTT reconnection failed: {e}")

        # Fetch Tuya DPS
        if device:
            try:
                data = device.status()
                if data and "dps" in data:
                    raw_dps = data["dps"]
                    decoded = decode_dps(raw_dps)
                    if decoded and (decoded.get("ph") is not None or decoded.get("ec") is not None):
                        payload_str = json.dumps(decoded)
                        logger.info(
                            f"[Telemetry] pH={decoded.get('ph')} | EC={decoded.get('ec')} uS/cm | "
                            f"TDS={decoded.get('tds')} ppm | Temp={decoded.get('temperature_c')} C | ORP={decoded.get('orp')} mV"
                        )
                        if mqtt_connected:
                            mqtt_client.publish(TOPIC_TELEMETRY, payload_str, qos=0, retain=False)
                        last_good_payload = decoded
                    else:
                        logger.debug(f"Received DPS with no valid water metrics: {raw_dps}")
                elif data and "Error" in data:
                    logger.warning(f"Tuya read error: {data['Error']}")
            except Exception as e:
                logger.warning(f"Error communicating with Tuya sensor: {e}")

        # Sleep interval
        for _ in range(POLL_INTERVAL_SEC):
            if not running:
                break
            time.sleep(1)

    # Teardown
    logger.info("Tuya Bridge shutting down...")
    if mqtt_connected:
        offline_payload = json.dumps({"status": "offline", "sensor_id": TUYA_SENSOR_ID, "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})
        mqtt_client.publish(TOPIC_STATUS, offline_payload, qos=1, retain=True)
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
    logger.info("Bridge stopped cleanly.")


if __name__ == "__main__":
    main()
