#!/usr/bin/env python3
"""
AGU-Aeroponics IIoT Diagnostic & E2E Verification Tool
Replaces legacy Delphi TestSCI.exe with modern cross-platform Python implementation.

Usage:
  python3 tools/test_agu_rf_e2e.py --port /dev/ttyUSB0 --baud 38400 pump-on --node 1
  python3 tools/test_agu_rf_e2e.py --port /dev/ttyUSB0 --baud 38400 pump-off --node 1
  python3 tools/test_agu_rf_e2e.py --port /dev/ttyUSB0 --baud 38400 ping --node 1
  python3 tools/test_agu_rf_e2e.py --port /dev/ttyUSB0 --baud 38400 read-eeprom --addr 0x0000
  python3 tools/test_agu_rf_e2e.py --port /dev/ttyUSB0 --baud 38400 read-ram --addr 0x0008
  python3 tools/test_agu_rf_e2e.py --port /dev/ttyUSB0 --baud 9600 rf-setup
  python3 tools/test_agu_rf_e2e.py mock-node --port /dev/pts/2
"""

import argparse
import sys
import time
import struct
from typing import Optional

try:
    import serial
except ImportError:
    serial = None

# Opcode constants matching TestSCI.dpr
OP_READ_MEM_WORD   = 0x01
OP_WRITE_RAM       = 0x04
OP_PING            = 0x05
OP_PUMP_ON         = 0x06
OP_PUMP_OFF        = 0x07
OP_READ_EEPROM     = 0x08
OP_WRITE_EEPROM    = 0x09
OP_DEVICE_ID       = 0x0A
OP_READ_RAM_BURST  = 0x0E

ACK_BYTE = 0x5A

def calc_zero_sum_checksum(data: bytes) -> int:
    """Two's complement zero-sum checksum matching Delphi Read8BC."""
    s = sum(data) & 0xFF
    return (0x100 - s) & 0xFF

def verify_zero_sum_checksum(data: bytes, checksum: int) -> bool:
    """Verify sum(data) + checksum == 0 (mod 256)."""
    return ((sum(data) + checksum) & 0xFF) == 0

class AguSerialClient:
    def __init__(self, port: str, baudrate: int = 38400, timeout: float = 1.0):
        if serial is None:
            raise RuntimeError("pyserial is not installed. Please run: pip install pyserial")
        self.ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_TWO,
            timeout=timeout
        )

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_pump_on(self, node_id: int) -> bool:
        cmd = bytes([OP_PUMP_ON, node_id])
        print(f"[TX] Pump ON -> Node {node_id} (Hex: {cmd.hex()})")
        self.ser.write(cmd)
        resp = self.ser.read(1)
        if resp:
            print(f"[RX] Response: 0x{resp.hex().upper()}")
            return True
        print("[RX] Timeout: No response")
        return False

    def send_pump_off(self, node_id: int) -> bool:
        cmd = bytes([OP_PUMP_OFF, node_id])
        print(f"[TX] Pump OFF -> Node {node_id} (Hex: {cmd.hex()})")
        self.ser.write(cmd)
        resp = self.ser.read(1)
        if resp:
            print(f"[RX] Response: 0x{resp.hex().upper()}")
            return True
        print("[RX] Timeout: No response")
        return False

    def send_ping(self, node_id: int, value: int = 0xA5) -> bool:
        cmd = bytes([OP_PING, value, node_id])
        print(f"[TX] Ping Node {node_id} with 0x{value:02X} (Hex: {cmd.hex()})")
        start = time.time()
        self.ser.write(cmd)
        resp = self.ser.read(1)
        elapsed_ms = (time.time() - start) * 1000
        if resp and resp[0] == value:
            print(f"[RX] Pong 0x{resp[0]:02X} OK ({elapsed_ms:.1f}ms RTT)")
            return True
        print(f"[RX] Ping failed! Expected 0x{value:02X}, received {resp.hex() if resp else 'None'}")
        return False

    def read_eeprom(self, addr: int) -> Optional[int]:
        hi = (addr >> 8) & 0xFF
        lo = addr & 0xFF
        cmd = bytes([OP_READ_EEPROM, hi, lo])
        print(f"[TX] Read EEPROM @ 0x{addr:04X} (Hex: {cmd.hex()})")
        self.ser.write(cmd)
        resp = self.ser.read(1)
        if resp:
            val = resp[0]
            print(f"[RX] EEPROM[0x{addr:04X}] = 0x{val:02X} ({val})")
            return val
        print("[RX] Read EEPROM Timeout")
        return None

    def write_eeprom(self, addr: int, val: int) -> bool:
        hi = (addr >> 8) & 0xFF
        lo = addr & 0xFF
        cmd = bytes([OP_WRITE_EEPROM, hi, lo, val])
        print(f"[TX] Write EEPROM @ 0x{addr:04X} = 0x{val:02X} (Hex: {cmd.hex()})")
        for attempt in range(1, 6):
            self.ser.write(cmd)
            resp = self.ser.read(1)
            if resp and resp[0] == ACK_BYTE:
                print(f"[RX] Attempt {attempt}: ACK 0x5A OK")
                return True
            time.sleep(0.02)
        print("[RX] Write EEPROM Failed: No ACK")
        return False

    def read_ram_burst(self, addr: int) -> Optional[bytes]:
        lo = addr & 0xFF
        hi = (addr >> 8) & 0xFF
        cmd = bytes([OP_READ_RAM_BURST, lo, hi, 0x08, 0x01])
        print(f"[TX] Read RAM Burst 8B @ 0x{addr:04X} (Hex: {cmd.hex()})")
        self.ser.write(cmd)
        raw = self.ser.read(9)
        if len(raw) == 9:
            data = raw[:8]
            cs = raw[8]
            if verify_zero_sum_checksum(data, cs):
                hex_str = " ".join(f"{b:02X}" for b in data)
                print(f"[RX] RAM[0x{addr:04X}..0x{addr+7:04X}]: {hex_str} (CS: 0x{cs:02X} VALID)")
                return data
            else:
                print(f"[RX] RAM Checksum mismatch! Raw: {raw.hex()}")
                return None
        print(f"[RX] RAM Timeout (received {len(raw)}/9 bytes)")
        return None

    def rf_setup(self):
        print("=== AGU RF Module AT Setup ===")
        commands = [
            ("AT\r\n", "Handshake"),
            ("AT+B38400\r\n", "Baudrate 38400"),
            ("AT+UN2\r\n", "UART Format (8N2)"),
            ("AT+A123\r\n", "Network ID 123"),
            ("AT+C001\r\n", "Channel 001 (433MHz)"),
        ]
        for at_cmd, desc in commands:
            print(f"[TX] Sending: {at_cmd.strip()} ({desc})")
            self.ser.write(at_cmd.encode('ascii'))
            time.sleep(0.5)
            resp = self.ser.read(self.ser.in_waiting or 32)
            print(f"[RX] Response: {resp.decode('ascii', errors='replace').strip()}")
        print("=== Setup completed ===")

def run_mock_node(port: str, baudrate: int = 38400, node_id: int = 1):
    """Simulates an AGU ATmega8 node responding to commands over serial."""
    if serial is None:
        raise RuntimeError("pyserial is not installed.")
    ser = serial.Serial(port=port, baudrate=baudrate, timeout=0.1)
    print(f"[Mock Node {node_id}] Listening on {port} at {baudrate} baud...")
    pump_state = 0
    eeprom_mock = bytearray(512)

    try:
        while True:
            b = ser.read(1)
            if not b:
                continue
            op = b[0]
            if op == OP_PUMP_ON:
                target = ser.read(1)
                if target and target[0] == node_id:
                    pump_state = 1
                    ser.write(bytes([ACK_BYTE]))
                    print(f"[Node {node_id}] Pump turned ON")
            elif op == OP_PUMP_OFF:
                target = ser.read(1)
                if target and target[0] == node_id:
                    pump_state = 0
                    ser.write(bytes([ACK_BYTE]))
                    print(f"[Node {node_id}] Pump turned OFF")
            elif op == OP_PING:
                args = ser.read(2)
                if len(args) == 2 and args[1] == node_id:
                    ser.write(bytes([args[0]]))
                    print(f"[Node {node_id}] Echoed ping 0x{args[0]:02X}")
            elif op == OP_READ_EEPROM:
                addr_bytes = ser.read(2)
                if len(addr_bytes) == 2:
                    addr = (addr_bytes[0] << 8) | addr_bytes[1]
                    val = eeprom_mock[addr % 512]
                    ser.write(bytes([val]))
            elif op == OP_WRITE_EEPROM:
                w_bytes = ser.read(3)
                if len(w_bytes) == 3:
                    addr = (w_bytes[0] << 8) | w_bytes[1]
                    val = w_bytes[2]
                    eeprom_mock[addr % 512] = val
                    ser.write(bytes([ACK_BYTE]))
                    print(f"[Node {node_id}] Wrote EEPROM[0x{addr:04X}] = 0x{val:02X}")
    except KeyboardInterrupt:
        print("\n[Mock Node] Stopped.")
    finally:
        ser.close()

def main():
    parser = argparse.ArgumentParser(description="AGU-Aeroponics E2E Diagnostic & Control Tool")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial port (e.g. COM6, /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=38400, help="Baudrate (default: 38400)")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # pump-on
    p_on = subparsers.add_parser("pump-on", help="Turn ON pump for a node")
    p_on.add_argument("--node", type=int, default=1, help="Node ID (1..4)")

    # pump-off
    p_off = subparsers.add_parser("pump-off", help="Turn OFF pump for a node")
    p_off.add_argument("--node", type=int, default=1, help="Node ID (1..4)")

    # ping
    p_ping = subparsers.add_parser("ping", help="Ping node and measure latency")
    p_ping.add_argument("--node", type=int, default=1, help="Node ID (1..4)")
    p_ping.add_argument("--value", type=lambda x: int(x, 0), default=0xA5, help="Ping payload byte (hex or dec)")

    # read-eeprom
    p_reep = subparsers.add_parser("read-eeprom", help="Read byte from EEPROM")
    p_reep.add_argument("--addr", type=lambda x: int(x, 0), required=True, help="16-bit address (e.g. 0x0000)")

    # write-eeprom
    p_weep = subparsers.add_parser("write-eeprom", help="Write byte to EEPROM")
    p_weep.add_argument("--addr", type=lambda x: int(x, 0), required=True, help="16-bit address")
    p_weep.add_argument("--val", type=lambda x: int(x, 0), required=True, help="Value byte (0..255)")

    # read-ram
    p_rram = subparsers.add_parser("read-ram", help="Read 8-byte burst from RAM with checksum")
    p_rram.add_argument("--addr", type=lambda x: int(x, 0), required=True, help="RAM start address")

    # rf-setup
    subparsers.add_parser("rf-setup", help="Send AT commands to configure RF module (Baud, Channel, Network ID)")

    # mock-node
    p_mock = subparsers.add_parser("mock-node", help="Run simulated ATmega8 node responder")
    p_mock.add_argument("--node", type=int, default=1, help="Node ID to simulate")

    # test-checksum (offline)
    subparsers.add_parser("test-checksum", help="Run unit test on two's complement checksum")

    args = parser.parse_args()

    if args.command == "test-checksum":
        test_data = bytes([10, 20, 30, 40, 50, 60, 70, 80])
        cs = calc_zero_sum_checksum(test_data)
        assert cs == 0x98, f"Expected 0x98, got 0x{cs:02X}"
        assert verify_zero_sum_checksum(test_data, cs), "Verification failed"
        print("Offline checksum self-test: PASSED!")
        return

    if args.command == "mock-node":
        run_mock_node(args.port, args.baud, args.node)
        return

    client = AguSerialClient(args.port, args.baud)
    try:
        if args.command == "pump-on":
            client.send_pump_on(args.node)
        elif args.command == "pump-off":
            client.send_pump_off(args.node)
        elif args.command == "ping":
            client.send_ping(args.node, args.value)
        elif args.command == "read-eeprom":
            client.read_eeprom(args.addr)
        elif args.command == "write-eeprom":
            client.write_eeprom(args.addr, args.val)
        elif args.command == "read-ram":
            client.read_ram_burst(args.addr)
        elif args.command == "rf-setup":
            client.rf_setup()
    finally:
        client.close()

if __name__ == "__main__":
    main()
