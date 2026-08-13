#!/usr/bin/env python3
"""Sprint 2 integration gate using the production MqttClient/NvsStorage path.

Paho is deliberately limited to the backend observer/publisher role. The
device process is the native-integration PlatformIO target, which executes
MqttClient::connect(), publishHeartbeat(), _onMessage(), ScheduleManager, and
NvsStorage against the real Mosquitto broker.
"""

import json
import os
import signal
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

import paho.mqtt.client as mqtt


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "aeroponics-firmware"
RUNNER = FIRMWARE / ".pio/build/native-integration/program"
HOST = os.environ.get("MQTT_HOST", "127.0.0.1")
PORT = int(os.environ.get("MQTT_PORT", "1883"))
DEVICE_ID = os.environ.get("MQTT_DEVICE_ID", os.environ.get("MQTT_DEVICE_USER", "qa-production-device"))
STATUS_TOPIC = f"aeroponics/device/{DEVICE_ID}/status"
COMMAND_TOPIC = f"aeroponics/device/{DEVICE_ID}/command/config/assignment"
ACL_DENIAL_TOPIC = COMMAND_TOPIC



def client(client_id, user, password):
    instance = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id,
                           protocol=mqtt.MQTTv311)
    instance.username_pw_set(user, password)
    return instance


def connected(instance):
    event = threading.Event()

    def on_connect(_client, _userdata, _flags, reason_code, _properties):
        if reason_code == 0:
            event.set()

    instance.on_connect = on_connect
    instance.connect(HOST, PORT, keepalive=10)
    instance.loop_start()
    if not event.wait(5):
        raise RuntimeError("MQTT observer/backend connection timed out")
    return instance


def wait_for_ready(process):
    deadline = time.monotonic() + 8
    output = []
    while time.monotonic() < deadline:
        line = process.stdout.readline()
        if line:
            output.append(line.rstrip())
            if line.strip() == "PRODUCTION_READY":
                return output
        if process.poll() is not None:
            raise RuntimeError(f"production runner exited early ({process.returncode}): {output}")
    raise RuntimeError(f"production runner did not become ready: {output}")


def runner(mode, nvs_path):
    environment = os.environ.copy()
    environment["MQTT_NVS_PATH"] = str(nvs_path)
    return subprocess.Popen([str(RUNNER), mode], cwd=FIRMWARE, env=environment,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            bufsize=1)


def stop(process):
    if process.poll() is None:
        process.send_signal(signal.SIGKILL)
    process.wait(timeout=5)


def capture_message(observer, topic, timeout=15):
    event, received = threading.Event(), {}

    def on_message(_client, _userdata, message):
        if message.topic == topic:
            received["payload"] = message.payload.decode("utf-8")
            received["retain"] = message.retain
            event.set()

    observer.on_message = on_message
    result, _ = observer.subscribe(topic, qos=1)
    if result != mqtt.MQTT_ERR_SUCCESS or not event.wait(timeout):
        raise RuntimeError(f"no production message on {topic}")
    return received


def test_lwt(nvs_path):
    observer = connected(client("qa-production-lwt-observer", os.environ["MQTT_BACKEND_USER"],
                                os.environ["MQTT_BACKEND_PASS"]))
    try:
        event, received = threading.Event(), {}

        def on_message(_client, _userdata, message):
            if message.topic == STATUS_TOPIC:
                received["payload"] = message.payload.decode("utf-8")
                event.set()

        observer.on_message = on_message
        observer.subscribe(STATUS_TOPIC, qos=1)
        process = runner("lwt", nvs_path)
        wait_for_ready(process)
        stop(process)  # SIGKILL intentionally simulates firmware/power loss.
        if not event.wait(6):
            raise RuntimeError("production MqttClient LWT was not observed")
        payload = json.loads(received["payload"])
        assert payload == {"status": "offline", "device_id": DEVICE_ID, "timestamp_utc": None}

        retained = connected(client("qa-production-lwt-retained", os.environ["MQTT_BACKEND_USER"],
                                    os.environ["MQTT_BACKEND_PASS"]))
        try:
            message = capture_message(retained, STATUS_TOPIC, timeout=5)
            assert message["retain"] is True and json.loads(message["payload"]) == payload
        finally:
            retained.disconnect(); retained.loop_stop()
        print("PASS production MqttClient LWT retained offline:", received["payload"])
    finally:
        observer.disconnect(); observer.loop_stop()


def test_heartbeat(nvs_path):
    observer = connected(client("qa-production-heartbeat-observer", os.environ["MQTT_BACKEND_USER"],
                                os.environ["MQTT_BACKEND_PASS"]))
    try:
        event, received = threading.Event(), {}

        def on_message(_client, _userdata, message):
            if message.topic == STATUS_TOPIC:
                payload = json.loads(message.payload.decode("utf-8"))
                if payload.get("status") == "online" and payload.get("uptime_s", 0) >= 10:
                    received.update(payload)
                    event.set()

        observer.on_message = on_message
        observer.subscribe(STATUS_TOPIC, qos=0)
        process = runner("heartbeat", nvs_path)
        wait_for_ready(process)
        if not event.wait(15):
            stop(process)
            raise RuntimeError("production heartbeat was not observed")
        output = process.communicate(timeout=8)[0]
        required = {"status", "device_id", "uptime_s", "rssi_dbm", "free_heap_b", "ntp_synced", "rtc_valid", "timestamp_utc"}
        assert set(received) == required and received["device_id"] == DEVICE_ID
        # Native runner has no NTP source; null is the valid fail-closed schema path.
        assert received["timestamp_utc"] is None and "PRODUCTION_HEARTBEAT_PUBLISHED" in output
        print("PASS production MqttClient heartbeat after 10s schema:", json.dumps(received))
    finally:
        observer.disconnect(); observer.loop_stop()


def test_command_persistence(nvs_path):
    process = runner("command", nvs_path)
    try:
        wait_for_ready(process)
        backend = connected(client("qa-production-command-backend", os.environ["MQTT_BACKEND_USER"],
                                   os.environ["MQTT_BACKEND_PASS"]))
        ack_event, acknowledgements = threading.Event(), []
        ack_topic = f"aeroponics/device/{DEVICE_ID}/ack/cmd-gate-1"
        def on_ack(_client, _userdata, message):
            acknowledgements.append(json.loads(message.payload.decode("utf-8")))
            ack_event.set()
        backend.on_message = on_ack
        backend.subscribe(ack_topic, qos=1)
        payload = {"command_id": "cmd-gate-1", "version": 1, "node_id": 1, "group_id": 2}
        try:
            info = backend.publish(COMMAND_TOPIC, json.dumps(payload), qos=1)
            info.wait_for_publish(timeout=5)
            if not ack_event.wait(5):
                raise RuntimeError("assignment ACCEPTED acknowledgement not received")
            assert acknowledgements[0] == {
                "command_id": "cmd-gate-1", "status": "ACCEPTED", "node_id": 1,
                "reason": "Safe-off queued; mapping commits after RF OFF ACK"
            }
        finally:
            backend.disconnect(); backend.loop_stop()
        output = process.communicate(timeout=18)[0]
        assert process.returncode == 0 and "PRODUCTION_ASSIGNMENT_SAFE_OFF_ACKED" in output
        print("PASS production assignment safe-OFF queue -> RF ACK -> mapping commit:", output.strip())
    finally:
        if process.poll() is None:
            stop(process)

def test_acl_denial():
    """A device credential must never be able to publish command topics."""
    username = os.environ["MQTT_DEVICE_USER"]
    password = os.environ["MQTT_DEVICE_PASS"]
    payload = '{"relay_id":1,"spray_day_s":999}'
    delivered = threading.Event()
    observer = connected(client("qa-acl-denial-observer", os.environ["MQTT_BACKEND_USER"],
                                os.environ["MQTT_BACKEND_PASS"]))
    observer.on_message = lambda *_args: delivered.set()
    observer.subscribe(ACL_DENIAL_TOPIC, qos=1)
    device = client("qa-acl-denial-device", username, password)
    disconnect_reason = {}
    device.on_disconnect = lambda _client, _userdata, _disconnect_flags, reason_code, _properties: disconnect_reason.update(reason=str(reason_code))
    try:
        device.connect(HOST, PORT, keepalive=10)
        device.loop_start()
        time.sleep(0.3)
        info = device.publish(ACL_DENIAL_TOPIC, payload, qos=1)
        info.wait_for_publish(timeout=3)
        time.sleep(1)
        result_code = info.rc
    finally:
        device.disconnect(); device.loop_stop()
        observer.disconnect(); observer.loop_stop()
    print(f"ACL denial: username={username} role=device topic={ACL_DENIAL_TOPIC} "
          f"payload={payload} client_publish_rc={result_code} "
          f"disconnect_reason={disconnect_reason.get('reason')} delivered={delivered.is_set()}")
    if result_code == mqtt.MQTT_ERR_SUCCESS and delivered.is_set():
        raise RuntimeError("device role was allowed to publish a command topic")
    print("PASS ACL denial (device publish rejected or message not delivered)")


def test_acl_gateway_isolation():
    """A gateway identity must be confined to its own MQTT namespace."""
    username = os.environ["MQTT_DEVICE_USER"]
    password = os.environ["MQTT_DEVICE_PASS"]
    foreign_id = f"{DEVICE_ID}-other"
    foreign_status = f"aeroponics/device/{foreign_id}/status"
    foreign_command = f"aeroponics/device/{foreign_id}/command/config/assignment"
    observer = connected(client("qa-acl-isolation-observer", os.environ["MQTT_BACKEND_USER"],
                                os.environ["MQTT_BACKEND_PASS"]))
    delivered = threading.Event()
    observer.on_message = lambda *_args: delivered.set()
    observer.subscribe(foreign_status, qos=1)
    device = client(DEVICE_ID, username, password)
    try:
        device.connect(HOST, PORT, keepalive=10); device.loop_start(); time.sleep(0.2)
        publish = device.publish(foreign_status, '{"status":"forged"}', qos=1)
        publish.wait_for_publish(timeout=3)
        subscribed, _ = device.subscribe(foreign_command, qos=1)
        time.sleep(1)
        if delivered.is_set():
            raise RuntimeError("device A published into device B namespace")
        if subscribed == mqtt.MQTT_ERR_SUCCESS and device.is_connected():
            # Mosquitto rejects unauthorized subscriptions asynchronously; a broker-disconnect is valid denial.
            raise RuntimeError("device A remained connected after subscribing to device B command")
    finally:
        device.disconnect(); device.loop_stop()
        observer.disconnect(); observer.loop_stop()
    print("PASS ACL gateway isolation (cross-device publish/subscribe denied)")


def main():
    subprocess.run(["pio", "run", "-e", "native-integration"], cwd=FIRMWARE, check=True)
    if not RUNNER.exists():
        raise RuntimeError("native integration runner was not built")
    with tempfile.TemporaryDirectory() as directory:
        nvs_path = Path(directory) / "production-nvs.bin"
        test_lwt(nvs_path)
        test_heartbeat(nvs_path)
        test_command_persistence(nvs_path)
        test_acl_denial()
        test_acl_gateway_isolation()
    print("ALL PRODUCTION MQTT INTEGRATION GATES PASSED")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL production MQTT integration gate: {error}", file=sys.stderr)
        sys.exit(1)
