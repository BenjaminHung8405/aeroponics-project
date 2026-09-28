#!/usr/bin/env python3
"""
Aeroponics Tuya PH-W218 to MQTT Lean Bridge.
Communicates directly with Tuya PH-W218 over LAN using tinytuya (Tuya Protocol 3.3/3.4).
Decodes water quality parameters (pH, EC, TDS, Temp, ORP, Salinity, SG, CF, Turbidity, Battery)
and publishes telemetry events to Mosquitto MQTT.

Architectural highlights:
- Listens to MQTT trigger commands (aeroponics/sensors/{TUYA_SENSOR_ID}/command/trigger).
- Connects on-demand to PH-W218, reads DPs, and disconnects immediately (persist=False).
- Zero persistent socket lock: Prevents TCP port 6668 exhaustion and connection refusal.
- Honors Probe Protection Mode: Does not poll when POLL_INTERVAL_SEC is 0.
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
# Default to 0 for strict On-Demand event-driven operation (Probe Protection Mode)
POLL_INTERVAL_SEC = int(os.getenv("POLL_INTERVAL_SEC", "0"))

MQTT_HOST = os.getenv("MQTT_HOST", "mushroom_mqtt")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.getenv("MQTT_USERNAME", "aero_backend")
MQTT_PASS = os.getenv("MQTT_PASSWORD", "123456")

TOPIC_TELEMETRY = f"aeroponics/sensors/{TUYA_SENSOR_ID}/state"
TOPIC_STATUS = f"aeroponics/sensors/{TUYA_SENSOR_ID}/status"
TOPIC_TRIGGER = f"aeroponics/sensors/{TUYA_SENSOR_ID}/command/trigger"
TOPIC_TRIGGER_WILDCARD = "aeroponics/sensors/+/command/trigger"

running = True
mqtt_client = None


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
    DP 107: Turbidity (NTU)
    DP 108/109/15: Battery (%)
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

    # ORP: DP 131
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

    # Turbidity: DP 107
    raw_turbidity = dps.get("107") or dps.get(107)
    turbidity = None
    if raw_turbidity is not None:
        try:
            val = float(raw_turbidity)
            turbidity = round(val / 10.0, 2) if val > 100 else round(val, 2)
        except (ValueError, TypeError):
            turbidity = None

    # Battery: DP 108, 109, 15
    raw_battery = dps.get("108") or dps.get("109") or dps.get("15") or dps.get(108) or dps.get(109) or dps.get(15)
    battery = None
    if raw_battery is not None:
        try:
            battery = int(float(raw_battery))
        except (ValueError, TypeError):
            battery = None

    return {
        "sensor_id": TUYA_SENSOR_ID,
        "ph": ph,
        "ec": ec,
        "tds": tds,
        "temperature_c": temp,
        "orp": orp,
        "salinity": salinity,
        "turbidity": turbidity,
        "battery": battery,
        "specific_gravity": sg,
        "conductivity_factor": cf,
        "humidity": humidity,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }


def query_tuya_device_on_demand() -> dict:
    """
    Creates a one-shot connection to Tuya PH-W218 over LAN (192.168.0.x).
    Crucial: socketPersistent is FALSE so socket closes immediately after response.
    """
    if not TUYA_DEVICE_ID or not TUYA_LOCAL_KEY:
        logger.error("TUYA_DEVICE_ID or TUYA_LOCAL_KEY missing!")
        return None

    try:
        # Non-persistent device client: connect -> query -> disconnect
        device = tinytuya.OutletDevice(
            dev_id=TUYA_DEVICE_ID,
            address=TUYA_DEVICE_IP,
            local_key=TUYA_LOCAL_KEY,
            version=TUYA_VERSION,
        )
        device.set_socketPersistent(False)
        device.set_socketTimeout(5)

        logger.info(f"Querying PH-W218 at {TUYA_DEVICE_IP}:6668 (Protocol v{TUYA_VERSION})...")
        data = device.status()

        if data and "dps" in data:
            return data["dps"]
        elif data and "Error" in data:
            logger.warning(f"Tuya device error response: {data['Error']}")
            return None
        return None
    except Exception as e:
        logger.error(f"Failed to query Tuya device: {e}")
        return None


def execute_measurement_session(session_id: str = None, trigger_type: str = "ON_DEMAND") -> bool:
    """
    Executes a single measurement session, parses DPs, and publishes telemetry.
    """
    global mqtt_client
    raw_dps = query_tuya_device_on_demand()
    if not raw_dps:
        logger.warning(f"Measurement session {session_id} yielded no data from sensor.")
        if mqtt_client:
            err_payload = json.dumps({
                "sensor_id": TUYA_SENSOR_ID,
                "session_id": session_id,
                "status": "error",
                "error": "No response from Tuya PH-W218 on local network",
                "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            })
            mqtt_client.publish(TOPIC_STATUS, err_payload, qos=1)
        return False

    decoded = decode_dps(raw_dps)
    if not decoded:
        logger.warning(f"Failed to decode DPS payload: {raw_dps}")
        return False

    if session_id:
        decoded["session_id"] = session_id
    if trigger_type:
        decoded["trigger_type"] = trigger_type

    payload_str = json.dumps(decoded)
    logger.info(
        f"[Measurement Success] Session={session_id} | pH={decoded.get('ph')} | "
        f"EC={decoded.get('ec')} uS/cm | TDS={decoded.get('tds')} ppm | "
        f"Temp={decoded.get('temperature_c')} C | ORP={decoded.get('orp')} mV"
    )

    if mqtt_client:
        mqtt_client.publish(TOPIC_TELEMETRY, payload_str, qos=1, retain=False)
    return True


def on_mqtt_connect(client, userdata, flags, rc):
    if rc == 0:
        logger.info("Connected to MQTT Broker successfully.")
        # Subscribe to on-demand trigger commands
        client.subscribe(TOPIC_TRIGGER, qos=1)
        client.subscribe(TOPIC_TRIGGER_WILDCARD, qos=1)
        logger.info(f"Subscribed to trigger topic: {TOPIC_TRIGGER}")

        online_payload = json.dumps({
            "status": "online",
            "sensor_id": TUYA_SENSOR_ID,
            "mode": "on_demand",
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        })
        client.publish(TOPIC_STATUS, online_payload, qos=1, retain=True)
    else:
        logger.error(f"MQTT connection failed with return code {rc}")


def on_mqtt_message(client, userdata, msg):
    logger.info(f"Received MQTT message on topic: {msg.topic}")
    try:
        payload_data = json.loads(msg.payload.decode("utf-8")) if msg.payload else {}
    except Exception:
        payload_data = {}

    session_id = payload_data.get("session_id")
    trigger_type = payload_data.get("trigger_type", "ON_DEMAND")

    logger.info(f"Triggering on-demand measurement: session_id={session_id}, type={trigger_type}")
    execute_measurement_session(session_id=session_id, trigger_type=trigger_type)


def main():
    global mqtt_client, running
    logger.info("Initializing Tuya PH-W218 MQTT Lean Edge Bridge...")
    logger.info(f"Target Sensor: {TUYA_SENSOR_ID} at {TUYA_DEVICE_IP} (Protocol v{TUYA_VERSION})")
    logger.info(f"Connecting to MQTT Broker at {MQTT_HOST}:{MQTT_PORT}...")

    mqtt_client = mqtt.Client(client_id=f"tuya_bridge_{TUYA_SENSOR_ID}")
    if MQTT_USER and MQTT_PASS:
        mqtt_client.username_pw_set(MQTT_USER, MQTT_PASS)

    mqtt_client.on_connect = on_mqtt_connect
    mqtt_client.on_message = on_mqtt_message

    # Set Last Will and Testament
    lwt_payload = json.dumps({
        "status": "offline",
        "sensor_id": TUYA_SENSOR_ID,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    })
    mqtt_client.will_set(TOPIC_STATUS, lwt_payload, qos=1, retain=True)

    try:
        mqtt_client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
        mqtt_client.loop_start()
    except Exception as e:
        logger.error(f"Could not connect to MQTT broker initially: {e}")

    logger.info(
        f"Tuya Edge Bridge running in Event-Driven Mode (POLL_INTERVAL_SEC={POLL_INTERVAL_SEC}). "
        f"Awaiting trigger commands on '{TOPIC_TRIGGER}'..."
    )

    last_poll_time = time.time()

    while running:
        # If periodic polling is explicitly enabled (> 0)
        if POLL_INTERVAL_SEC > 0:
            now = time.time()
            if now - last_poll_time >= POLL_INTERVAL_SEC:
                logger.info("Executing scheduled periodic poll...")
                execute_measurement_session(session_id=None, trigger_type="SCHEDULED")
                last_poll_time = now

        time.sleep(0.5)

    # Teardown
    logger.info("Tuya Bridge shutting down...")
    if mqtt_client:
        offline_payload = json.dumps({
            "status": "offline",
            "sensor_id": TUYA_SENSOR_ID,
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        })
        mqtt_client.publish(TOPIC_STATUS, offline_payload, qos=1, retain=True)
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
    logger.info("Bridge stopped cleanly.")


if __name__ == "__main__":
    main()
