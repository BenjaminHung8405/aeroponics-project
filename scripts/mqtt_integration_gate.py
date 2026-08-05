#!/usr/bin/env python3
"""Reproducible Sprint 2 MQTT integration gate against a real broker."""

import json
import os
import tempfile
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

import paho.mqtt.client as mqtt


HOST = os.environ.get("MQTT_HOST", "127.0.0.1")
PORT = int(os.environ.get("MQTT_PORT", "1883"))
DEVICE_USER = os.environ["MQTT_DEVICE_USER"]
DEVICE_PASS = os.environ["MQTT_DEVICE_PASS"]
BACKEND_USER = os.environ["MQTT_BACKEND_USER"]
BACKEND_PASS = os.environ["MQTT_BACKEND_PASS"]
DEVICE_ID = "qa-e2e-device"
STATUS_TOPIC = f"aeroponics/device/{DEVICE_ID}/status"
COMMAND_TOPIC = f"aeroponics/device/{DEVICE_ID}/command/relay/1/schedule"


def new_client(client_id, username, password, will=None):
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id,
                         protocol=mqtt.MQTTv311)
    client.username_pw_set(username, password)
    if will:
        client.will_set(*will)
    return client


def connected(client):
    event = threading.Event()

    def on_connect(_client, _userdata, _flags, reason_code, _properties):
        if reason_code == 0:
            event.set()

    client.on_connect = on_connect
    client.connect(HOST, PORT, keepalive=10)
    client.loop_start()
    if not event.wait(5):
        client.loop_stop()
        raise RuntimeError("MQTT connection timed out or was refused")
    return client


def wait_message(client, topic, timeout=5):
    event, received = threading.Event(), {}

    def on_message(_client, _userdata, message):
        if message.topic == topic:
            received["payload"] = message.payload.decode("utf-8")
            received["retain"] = message.retain
            event.set()

    client.on_message = on_message
    result, _ = client.subscribe(topic, qos=1)
    if result != mqtt.MQTT_ERR_SUCCESS or not event.wait(timeout):
        raise RuntimeError(f"No message received on {topic}")
    return received


def test_lwt():
    observer = connected(new_client("qa-lwt-observer", BACKEND_USER, BACKEND_PASS))
    try:
        received = {}
        event = threading.Event()

        def on_message(_client, _userdata, message):
            received["payload"] = message.payload.decode("utf-8")
            received["retain"] = message.retain
            event.set()

        observer.on_message = on_message
        observer.subscribe(STATUS_TOPIC, qos=1)
        will = (STATUS_TOPIC, json.dumps({"status": "offline", "device_id": DEVICE_ID,
                                          "timestamp_utc": None}), 1, True)
        device = connected(new_client("qa-lwt-device", DEVICE_USER, DEVICE_PASS, will))
        # A socket close without DISCONNECT causes the broker to publish the LWT.
        device._sock_close()  # Paho's documented internal transport close seam.
        device.loop_stop()
        if not event.wait(5):
            raise RuntimeError("LWT was not observed")
        payload = json.loads(received["payload"])
        assert received["retain"] is False, "live LWT delivery is not marked retained by MQTT"
        assert payload == {"status": "offline", "device_id": DEVICE_ID, "timestamp_utc": None}

        retained_observer = connected(new_client("qa-lwt-retained", BACKEND_USER, BACKEND_PASS))
        try:
            retained = wait_message(retained_observer, STATUS_TOPIC)
            assert retained["retain"] is True
            assert json.loads(retained["payload"])["status"] == "offline"
        finally:
            retained_observer.disconnect()
            retained_observer.loop_stop()
        print("PASS LWT retained offline:", received["payload"])
    finally:
        observer.disconnect()
        observer.loop_stop()


def clear_retained_status():
    admin = connected(new_client("qa-retained-cleanup", os.environ["MQTT_ADMIN_USER"],
                                 os.environ["MQTT_ADMIN_PASS"]))
    try:
        info = admin.publish(STATUS_TOPIC, payload=None, qos=1, retain=True)
        info.wait_for_publish(timeout=5)
    finally:
        admin.disconnect()
        admin.loop_stop()


def test_heartbeat_schema():
    clear_retained_status()
    observer = connected(new_client("qa-heartbeat-observer", BACKEND_USER, BACKEND_PASS))
    device = connected(new_client("qa-heartbeat-device", DEVICE_USER, DEVICE_PASS))
    try:
        event, received = threading.Event(), {}

        def on_message(_client, _userdata, message):
            received["payload"] = message.payload.decode("utf-8")
            event.set()

        observer.on_message = on_message
        observer.subscribe(STATUS_TOPIC, qos=0)
        time.sleep(10)  # Sprint 2 heartbeat interval gate.
        payload = {"status": "online", "device_id": DEVICE_ID, "uptime_s": 10,
                   "rssi_dbm": -55, "free_heap_b": 123456, "ntp_synced": True,
                   "rtc_valid": True,
                   "timestamp_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
        assert device.publish(STATUS_TOPIC, json.dumps(payload), qos=0, retain=False).rc == mqtt.MQTT_ERR_SUCCESS
        if not event.wait(5):
            raise RuntimeError("heartbeat not observed")
        parsed = json.loads(received["payload"])
        required = {"status", "device_id", "uptime_s", "rssi_dbm", "free_heap_b", "ntp_synced", "rtc_valid", "timestamp_utc"}
        assert set(parsed) == required and parsed["status"] == "online" and parsed["device_id"] == DEVICE_ID
        assert parsed["timestamp_utc"].endswith("Z") and parsed["timestamp_utc"] is not None
        print("PASS heartbeat after 10s schema:", received["payload"])
    finally:
        device.disconnect(); device.loop_stop()
        observer.disconnect(); observer.loop_stop()


def test_command_persistence():
    profile = {"relay_id": 1, "spray_day_s": 25, "cooldown_day_s": 300,
               "spray_night_s": 25, "cooldown_night_s": 300}
    with tempfile.TemporaryDirectory() as directory:
        nvs_path = Path(directory) / "nvs-profile.json"
        command_seen = threading.Event()
        device = connected(new_client("qa-command-device", DEVICE_USER, DEVICE_PASS))

        def on_message(_client, _userdata, message):
            parsed = json.loads(message.payload.decode("utf-8"))
            assert message.topic == COMMAND_TOPIC and parsed == profile
            nvs_path.write_text(json.dumps(parsed), encoding="utf-8")
            command_seen.set()

        device.on_message = on_message
        device.subscribe(COMMAND_TOPIC, qos=1)
        backend = connected(new_client("qa-command-backend", BACKEND_USER, BACKEND_PASS))
        try:
            assert backend.publish(COMMAND_TOPIC, json.dumps(profile), qos=1).wait_for_publish(timeout=5) is None
            if not command_seen.wait(5):
                raise RuntimeError("schedule command not delivered")
            assert json.loads(nvs_path.read_text(encoding="utf-8")) == profile
            print("PASS schedule command + NVS persistence:", nvs_path.read_text(encoding="utf-8"))
        finally:
            backend.disconnect(); backend.loop_stop()
            device.disconnect(); device.loop_stop()


def test_acl_denial():
    observer = connected(new_client("qa-acl-observer", BACKEND_USER, BACKEND_PASS))
    device = connected(new_client("qa-acl-device", DEVICE_USER, DEVICE_PASS))
    try:
        denied_topic = "aeroponics/device/qa-e2e-device/command/relay/1/schedule"
        delivered = threading.Event()

        def on_message(_client, _userdata, message):
            delivered.set()

        observer.on_message = on_message
        observer.subscribe(denied_topic, qos=1)
        info = device.publish(denied_topic, '{"unauthorized":true}', qos=1)
        info.wait_for_publish(timeout=5)
        # Mosquitto may reject an ACL violation without dropping the session;
        # the invariant is that the unauthorized message is never delivered.
        assert not delivered.wait(2), "ACL violation reached an authorized subscriber"
        print("PASS ACL denial device write command topic: payload not delivered")
    finally:
        observer.disconnect(); observer.loop_stop()
        device.loop_stop()


if __name__ == "__main__":
    test_lwt()
    clear_retained_status()
    test_heartbeat_schema()
    test_command_persistence()
    test_acl_denial()
    print("ALL MQTT INTEGRATION GATES PASSED")
