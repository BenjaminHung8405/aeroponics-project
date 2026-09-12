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


def load_workspace_env():
    """Load the data-only workspace .env without shell evaluation or logging secrets."""
    allowed = {
        "DB_USER", "DB_PASS", "DB_NAME", "MQTT_PORT", "MQTT_WS_PORT",
        "MQTT_ADMIN_USER", "MQTT_ADMIN_PASS", "MQTT_DEVICE_USER", "MQTT_DEVICE_PASS",
        "MQTT_DEVICE_ID", "MQTT_BACKEND_USER", "MQTT_BACKEND_PASS", "BACKEND_PORT",
        "JWT_SECRET", "TUYA_DEVICE_IP", "TUYA_DEVICE_ID", "TUYA_LOCAL_KEY",
        "TUYA_SENSOR_ID", "TUYA_ON_DEMAND_TIMEOUT_MS", "WIFI_SSID", "WIFI_PASSWORD",
        "DEVICE_ID",
    }
    env_file = ROOT / ".env"
    if not env_file.exists():
        return
    for raw_line in env_file.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise RuntimeError("unsafe or malformed .env entry rejected")
        key, value = line.split("=", 1)
        if key not in allowed or not key.isidentifier() or not key.isupper():
            raise RuntimeError(f"unsupported .env key rejected: {key}")
        if any(token in value for token in ("$(", "`", ";", "&", "|", "\r", "\n")):
            raise RuntimeError("unsafe .env value rejected")
        os.environ.setdefault(key, value)


load_workspace_env()
HOST = os.environ.get("MQTT_HOST", "127.0.0.1")
PORT = int(os.environ.get("MQTT_PORT", "1883"))
DEVICE_ID = os.environ.get("MQTT_DEVICE_ID", os.environ.get("MQTT_DEVICE_USER", "qa-production-device"))
STATUS_TOPIC = f"aeroponics/device/{DEVICE_ID}/status"
COMMAND_TOPIC = f"aeroponics/device/{DEVICE_ID}/command/config/assignment"
FLOW_POLICY_TOPIC = f"aeroponics/device/{DEVICE_ID}/command/config/flow-policy"
NODE_OVERRIDE_TOPIC = f"aeroponics/device/{DEVICE_ID}/command/node/1/override"
ACL_DENIAL_TOPIC = COMMAND_TOPIC


def command_event_topic(command_id):
    return f"aeroponics/device/{DEVICE_ID}/telemetry/command/{command_id}/event"



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


def test_flow_policy_provisioning_after_reconnect(nvs_path, retained):
    suffix = "retained" if retained else "live"
    backend = connected(client(f"qa-flow-policy-{suffix}-backend", os.environ["MQTT_BACKEND_USER"],
                               os.environ["MQTT_BACKEND_PASS"]))
    acknowledgements, events = {}, {}
    lifecycle_events, lifecycle_event_received = [], threading.Event()
    command_ids = (f"on-before-{suffix}", f"policy-{suffix}", f"on-after-{suffix}")
    admin = connected(client(f"qa-ack-cleaner-{suffix}", os.environ["MQTT_ADMIN_USER"],
                             os.environ["MQTT_ADMIN_PASS"]))
    try:
        for cid in command_ids:
            admin.publish(f"aeroponics/device/{DEVICE_ID}/ack/{cid}", "", qos=1, retain=True).wait_for_publish(timeout=5)
    finally:
        admin.disconnect(); admin.loop_stop()
    policy_ver = 2 if retained else 1
    policy = {
        "command_id": command_ids[1], "version": policy_ver, "node_id": 1,
        "policy_version": policy_ver, "treatment_version_id": 101, "calibration_id": 1001,
        "min_flow_lpm_x100": 50, "max_off_flow_lpm_x100": 20,
        "max_flow_lpm_x100": 600, "flow_start_timeout_ms": 3000,
        "run_lease_ms": 60000, "max_on_duration_ms": 300000,
    }
    if retained:
        backend.publish(FLOW_POLICY_TOPIC, json.dumps(policy), qos=1, retain=True).wait_for_publish(timeout=5)
    process = runner("policy", nvs_path)
    try:
        for command_id in command_ids:
            event = threading.Event()
            events[command_id] = event
            def on_ack(_client, _userdata, message, expected=command_id):
                if not message.payload:
                    return
                payload = json.loads(message.payload.decode("utf-8"))
                acknowledgements.setdefault(expected, []).append(payload)
                events[expected].set()
            backend.message_callback_add(f"aeroponics/device/{DEVICE_ID}/ack/{command_id}", on_ack)
            backend.subscribe(f"aeroponics/device/{DEVICE_ID}/ack/{command_id}", qos=1)
        def on_lifecycle_event(_client, _userdata, message):
            lifecycle_events.append(json.loads(message.payload.decode("utf-8")))
            lifecycle_event_received.set()
        backend.message_callback_add(command_event_topic(command_ids[2]), on_lifecycle_event)
        backend.subscribe(command_event_topic(command_ids[2]), qos=1)
        time.sleep(0.3)  # Wait for broker SUBACK before starting the device runner.

        wait_for_ready(process)
        if retained:
            if not events[command_ids[1]].wait(5):
                raise RuntimeError("retained flow-policy ACCEPTED acknowledgement not received after reconnect")
            assert acknowledgements[command_ids[1]] == [{
                "command_id": command_ids[1], "status": "ACCEPTED", "node_id": 1,
                "reason": "Authenticated flow policy provisioned"
            }]
        else:
            rejected_on = {"command_id": command_ids[0], "version": 1, "desired_state": "ON",
                           "source": "MANUAL_OVERRIDE", "run_lease_ms": 30000}
            backend.publish(NODE_OVERRIDE_TOPIC, json.dumps(rejected_on), qos=1).wait_for_publish(timeout=5)
            if not events[command_ids[0]].wait(5):
                raise RuntimeError("unprovisioned ON acknowledgement not received")
            assert acknowledgements[command_ids[0]][0]["status"] == "REJECTED"

        if not retained:
            backend.publish(FLOW_POLICY_TOPIC, json.dumps(policy), qos=1).wait_for_publish(timeout=5)
            if not events[command_ids[1]].wait(5):
                raise RuntimeError("flow-policy ACCEPTED acknowledgement not received")
            assert acknowledgements[command_ids[1]] == [{
                "command_id": command_ids[1], "status": "ACCEPTED", "node_id": 1,
                "reason": "Authenticated flow policy provisioned"
            }]

        accepted_on = {"command_id": command_ids[2], "version": 1, "desired_state": "ON",
                       "source": "MANUAL_OVERRIDE", "run_lease_ms": 30000}
        backend.publish(NODE_OVERRIDE_TOPIC, json.dumps(accepted_on), qos=1).wait_for_publish(timeout=5)
        if not events[command_ids[2]].wait(5):
            raise RuntimeError("provisioned ON acknowledgement not received")
        expected_ack = [{
            "command_id": command_ids[2], "status": "ACCEPTED", "node_id": 1,
            "reason": "Node override accepted and queued"
        }]
        assert acknowledgements[command_ids[2]] == expected_ack
        if not lifecycle_event_received.wait(5):
            raise RuntimeError("RF dispatch lifecycle event was not received on telemetry topic")
        assert lifecycle_events == [{
            "command_id": command_ids[2], "status": "QUEUED", "node_id": 1,
            "reason": "RF_DISPATCHED"
        }]
        print(f"PASS command ACK lifecycle ({suffix}): one ACCEPTED ACK plus QUEUED RF event on telemetry topic")
    finally:
        backend.disconnect(); backend.loop_stop()
        if retained:
            cleaner = connected(client(f"qa-flow-policy-{suffix}-cleaner", os.environ["MQTT_BACKEND_USER"],
                                        os.environ["MQTT_BACKEND_PASS"]))
            try:
                cleaner.publish(FLOW_POLICY_TOPIC, "", qos=1, retain=True).wait_for_publish(timeout=5)
            finally:
                cleaner.disconnect(); cleaner.loop_stop()
        if process.poll() is None:
            stop(process)


def test_out_of_range_flow_policy_is_rejected(nvs_path, max_flow_lpm_x100):
    suffix = str(max_flow_lpm_x100)
    backend = connected(client(f"qa-flow-policy-range-{suffix}-backend", os.environ["MQTT_BACKEND_USER"],
                               os.environ["MQTT_BACKEND_PASS"]))
    command_ids = (f"policy-range-{suffix}", f"on-after-range-{suffix}")
    acknowledgements, events = {}, {}
    policy = {
        "command_id": command_ids[0], "version": 1, "node_id": 1,
        "policy_version": 1, "treatment_version_id": 101, "calibration_id": 1001,
        "min_flow_lpm_x100": 50, "max_off_flow_lpm_x100": 20,
        "max_flow_lpm_x100": max_flow_lpm_x100, "flow_start_timeout_ms": 3000,
        "run_lease_ms": 60000, "max_on_duration_ms": 300000,
    }
    process = runner("policy", nvs_path)
    try:
        for command_id in command_ids:
            event = threading.Event()
            events[command_id] = event
            def on_ack(_client, _userdata, message, expected=command_id):
                acknowledgements[expected] = json.loads(message.payload.decode("utf-8"))
                events[expected].set()
            backend.message_callback_add(f"aeroponics/device/{DEVICE_ID}/ack/{command_id}", on_ack)
            backend.subscribe(f"aeroponics/device/{DEVICE_ID}/ack/{command_id}", qos=1)
        time.sleep(0.3)
        wait_for_ready(process)
        backend.publish(FLOW_POLICY_TOPIC, json.dumps(policy), qos=1).wait_for_publish(timeout=5)
        if not events[command_ids[0]].wait(5):
            raise RuntimeError(f"out-of-range flow-policy {max_flow_lpm_x100} acknowledgement not received")
        assert acknowledgements[command_ids[0]]["status"] == "REJECTED"

        on_command = {"command_id": command_ids[1], "version": 1, "desired_state": "ON"}
        backend.publish(NODE_OVERRIDE_TOPIC, json.dumps(on_command), qos=1).wait_for_publish(timeout=5)
        if not events[command_ids[1]].wait(5):
            raise RuntimeError(f"ON after rejected flow-policy {max_flow_lpm_x100} acknowledgement not received")
        assert acknowledgements[command_ids[1]]["status"] == "REJECTED"
        print(f"PASS out-of-range flow policy ({max_flow_lpm_x100}) rejected; no ON authorization granted")
    finally:
        backend.disconnect(); backend.loop_stop()
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
    subscribe_event = threading.Event()
    foreign_command_delivered = threading.Event()

    def on_subscribe(_client, _userdata, _mid, reason_codes, _properties):
        subscribe_event.set()

    device.on_subscribe = on_subscribe
    device.on_message = lambda *_args: foreign_command_delivered.set()
    try:
        device.connect(HOST, PORT, keepalive=10); device.loop_start(); time.sleep(0.2)
        publish = device.publish(foreign_status, '{"status":"forged"}', qos=1)
        publish.wait_for_publish(timeout=3)
        subscribed, _ = device.subscribe(foreign_command, qos=1)
        if subscribed != mqtt.MQTT_ERR_SUCCESS or not subscribe_event.wait(3):
            raise RuntimeError("did not receive ACL subscription result")
        backend = connected(client("qa-acl-isolation-backend", os.environ["MQTT_BACKEND_USER"],
                                   os.environ["MQTT_BACKEND_PASS"]))
        try:
            command = backend.publish(foreign_command, '{"command_id":"acl-probe","version":1}', qos=1)
            command.wait_for_publish(timeout=3)
            time.sleep(1)
        finally:
            backend.disconnect(); backend.loop_stop()
        if delivered.is_set():
            raise RuntimeError("device A published into device B namespace")
        if foreign_command_delivered.is_set():
            raise RuntimeError("device A received a command from device B namespace")
    finally:
        device.disconnect(); device.loop_stop()
        observer.disconnect(); observer.loop_stop()
    print("PASS ACL gateway isolation (cross-device publish and command delivery denied)")


def main():
    required = ("MQTT_DEVICE_USER", "MQTT_DEVICE_PASS", "MQTT_BACKEND_USER", "MQTT_BACKEND_PASS")
    if any(not os.environ.get(key) for key in required):
        raise RuntimeError("required MQTT credentials are missing")
    if DEVICE_ID != os.environ["MQTT_DEVICE_USER"]:
        raise RuntimeError("MQTT_DEVICE_ID must equal MQTT_DEVICE_USER for ACL %u and MqttClient::begin()")
    subprocess.run(["pio", "run", "-e", "native-integration"], cwd=FIRMWARE, check=True)
    if not RUNNER.exists():
        raise RuntimeError("native integration runner was not built")
    with tempfile.TemporaryDirectory() as directory:
        nvs_path = Path(directory) / "production-nvs.bin"
        test_lwt(nvs_path)
        test_heartbeat(nvs_path)
        test_command_persistence(nvs_path)
        test_flow_policy_provisioning_after_reconnect(nvs_path, retained=False)
        test_flow_policy_provisioning_after_reconnect(nvs_path, retained=True)
        test_out_of_range_flow_policy_is_rejected(nvs_path, 601)
        test_out_of_range_flow_policy_is_rejected(nvs_path, 65535)
        test_acl_denial()
        test_acl_gateway_isolation()
    print("ALL PRODUCTION MQTT INTEGRATION GATES PASSED")


if __name__ == "__main__":
    import traceback
    try:
        main()
    except Exception as error:
        traceback.print_exc()
        print(f"FAIL production MQTT integration gate: {error}", file=sys.stderr)
        sys.exit(1)
