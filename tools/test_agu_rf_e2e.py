#!/usr/bin/env python3
"""
AGU-Aeroponics IIoT Diagnostic & Control Tool
Replaces legacy Delphi TestSCI.exe with modern cross-platform Python implementation.
Supports auto-detection of CP2102 USB-to-UART bridge on macOS/Linux/Windows.

Usage:
  # Interactive mode:
  python3 tools/test_agu_rf_e2e.py
  ./AGU-Aeroponics/run.sh

  # Legacy TestSCI test6 pump cycle (Nodes 4, 5, 6, 7):
  python3 tools/test_agu_rf_e2e.py cycle
  python3 tools/test_agu_rf_e2e.py cycle --nodes 4,5,6,7 --on 3 --off 10

  # Direct commands:
  python3 tools/test_agu_rf_e2e.py pump-on --node 4
  python3 tools/test_agu_rf_e2e.py pump-off --node 4
  python3 tools/test_agu_rf_e2e.py ping --node 1
  python3 tools/test_agu_rf_e2e.py scan --start 1 --end 10
  python3 tools/test_agu_rf_e2e.py monitor
  python3 tools/test_agu_rf_e2e.py rf-setup
"""

import argparse
import glob
import os
import struct
import sys
import time
from typing import List, Optional

try:
    import serial
    import serial.tools.list_ports
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

def detect_default_port() -> str:
    """Auto-detect CP2102 or USB-serial port on macOS/Linux/Windows."""
    if serial is not None:
        ports = list(serial.tools.list_ports.comports())
        # Priority 1: CP2102 (VID:PID 10C4:EA60) or matching description / manufacturer
        for p in ports:
            desc = (p.description or "").upper()
            mfg = (p.manufacturer or "").upper()
            hwid = (p.hwid or "").upper()
            if (p.vid == 0x10C4 and p.pid == 0xEA60) or "CP210" in desc or "CP210" in hwid or "SILICON LABS" in mfg:
                return p.device

        # Priority 2: Any usbserial / ttyUSB
        for p in ports:
            dev_lower = p.device.lower()
            if "usbserial" in dev_lower or "ttyusb" in dev_lower:
                return p.device

    # Priority 3: Fallback based on OS
    if sys.platform == "darwin":
        cu_ports = sorted(glob.glob("/dev/cu.usbserial*"))
        if cu_ports:
            return cu_ports[0]
        return "/dev/cu.usbserial-0001"
    elif sys.platform == "win32":
        return "COM6"
    else:
        usb_ports = sorted(glob.glob("/dev/ttyUSB*"))
        if usb_ports:
            return usb_ports[0]
        return "/dev/ttyUSB0"

def calc_zero_sum_checksum(data: bytes) -> int:
    """Two's complement zero-sum checksum matching Delphi Read8BC."""
    s = sum(data) & 0xFF
    return (0x100 - s) & 0xFF

def verify_zero_sum_checksum(data: bytes, checksum: int) -> bool:
    """Verify sum(data) + checksum == 0 (mod 256)."""
    return ((sum(data) + checksum) & 0xFF) == 0

def format_send_com_packet(payload: bytes) -> bytes:
    """Delphi TSCI.SendCom framing matching TestSCI.dpr:
    [ frameLen = len(payload) + 1 ] [ payload[0] ... payload[N-1] ] [ checksum ]
    where (sum(all_bytes) & 0xFF) == 0.
    """
    flen = (len(payload) + 1) & 0xFF
    s = flen + sum(payload)
    cs = (0x100 - (s & 0xFF)) & 0xFF
    return bytes([flen]) + payload + bytes([cs])

class AguSerialClient:
    def __init__(self, port: str, baudrate: int = 38400, timeout: float = 0.5, stopbits: int = 2):
        if serial is None:
            raise RuntimeError("pyserial is not installed. Please run: pip install pyserial")
        sb = serial.STOPBITS_TWO if stopbits == 2 else serial.STOPBITS_ONE
        self.port = port
        self.baudrate = baudrate
        self.ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=sb,
            timeout=timeout
        )

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_pump_on(self, node_id: int, timeout_ms: int = 300, max_retries: int = 3) -> bool:
        cmd = format_send_com_packet(bytes([OP_PUMP_ON, node_id]))
        for attempt in range(1, max_retries + 1):
            self.ser.reset_input_buffer()
            retry_str = f" [Lần {attempt}/{max_retries}]" if max_retries > 1 and attempt > 1 else ""
            print(f"[TX] Pump ON -> Node {node_id}{retry_str} (Hex: {cmd.hex()})", end="", flush=True)
            self.ser.write(cmd)
            
            # Chờ max 300ms nhận byte ACK 0x5A
            start = time.time()
            timeout_sec = timeout_ms / 1000.0
            resp = b""
            while (time.time() - start) < timeout_sec:
                if self.ser.in_waiting:
                    resp = self.ser.read(1)
                    if resp and resp[0] == ACK_BYTE:
                        break
                time.sleep(0.005)
                
            elapsed_ms = (time.time() - start) * 1000
            if resp and resp[0] == ACK_BYTE:
                print(f" -> [RX] ACK 0x5A OK ({elapsed_ms:.1f}ms)")
                return True
            elif resp:
                print(f" -> [RX] Phản hồi: 0x{resp.hex().upper()} ({elapsed_ms:.1f}ms)")
            else:
                print(f" -> [RX] Timeout ({timeout_ms}ms, Chưa nhận ACK 0x5A)")
                
            if attempt < max_retries:
                time.sleep(0.05)
                
        print(f"[!] Bật bơm Node {node_id} thất bại sau {max_retries} lần thử.")
        return False

    def send_pump_off(self, node_id: int, timeout_ms: int = 300, max_retries: int = 3) -> bool:
        cmd = format_send_com_packet(bytes([OP_PUMP_OFF, node_id]))
        for attempt in range(1, max_retries + 1):
            self.ser.reset_input_buffer()
            retry_str = f" [Lần {attempt}/{max_retries}]" if max_retries > 1 and attempt > 1 else ""
            print(f"[TX] Pump OFF -> Node {node_id}{retry_str} (Hex: {cmd.hex()})", end="", flush=True)
            self.ser.write(cmd)
            
            start = time.time()
            timeout_sec = timeout_ms / 1000.0
            resp = b""
            while (time.time() - start) < timeout_sec:
                if self.ser.in_waiting:
                    resp = self.ser.read(1)
                    if resp and resp[0] == ACK_BYTE:
                        break
                time.sleep(0.005)
                
            elapsed_ms = (time.time() - start) * 1000
            if resp and resp[0] == ACK_BYTE:
                print(f" -> [RX] ACK 0x5A OK ({elapsed_ms:.1f}ms)")
                return True
            elif resp:
                print(f" -> [RX] Phản hồi: 0x{resp.hex().upper()} ({elapsed_ms:.1f}ms)")
            else:
                print(f" -> [RX] Timeout ({timeout_ms}ms, Chưa nhận ACK 0x5A)")
                
            if attempt < max_retries:
                time.sleep(0.05)
                
        print(f"[!] Tắt bơm Node {node_id} thất bại sau {max_retries} lần thử.")
        return False

    def run_cycle(self, nodes: List[int], on_time: float = 3.0, off_time: float = 10.0, iterations: int = 0):
        """Replicates legacy TestSCI test6 pump cycle loop."""
        print(f"\n==================================================")
        print(f"  Starting TestSCI Pump Cycle (test6)")
        print(f"  Nodes: {nodes} | ON: {on_time}s | OFF: {off_time}s")
        print(f"  Port: {self.port} @ {self.baudrate} baud")
        print(f"  Press Ctrl+C to stop.")
        print(f"==================================================\n")
        
        cycle = 0
        try:
            while True:
                cycle += 1
                iter_str = f"Cycle #{cycle}" if iterations == 0 else f"Cycle #{cycle}/{iterations}"
                print(f"--- [{iter_str}] Phase 1: Turn ON Pumps ---")
                for node_id in nodes:
                    self.send_pump_on(node_id, timeout_ms=300)
                
                print(f"[*] Sleeping {on_time:.1f}s while pumps are ON...")
                time.sleep(on_time)

                print(f"--- [{iter_str}] Phase 2: Turn OFF Pumps ---")
                for node_id in nodes:
                    self.send_pump_off(node_id, timeout_ms=300)

                if iterations > 0 and cycle >= iterations:
                    print(f"\n[+] Completed {cycle} cycles.")
                    break

                print(f"[*] Sleeping {off_time:.1f}s while pumps are OFF...")
                time.sleep(off_time)
        except KeyboardInterrupt:
            print("\n\n[!] Cycle interrupted by user. Ensuring all pumps are turned OFF...")
            for node_id in nodes:
                try:
                    self.send_pump_off(node_id, timeout_ms=200, max_retries=1)
                except Exception:
                    pass
            print("[✓] All safety OFF commands sent.")

    def send_ping(self, node_id: int, value: int = 0xA5, timeout_ms: int = 300, max_retries: int = 3) -> bool:
        cmd = format_send_com_packet(bytes([OP_PING, value, node_id]))
        for attempt in range(1, max_retries + 1):
            self.ser.reset_input_buffer()
            retry_str = f" [Lần {attempt}/{max_retries}]" if max_retries > 1 and attempt > 1 else ""
            print(f"[TX] Ping Node {node_id}{retry_str} with 0x{value:02X} (Hex: {cmd.hex()})", end="", flush=True)
            start = time.time()
            self.ser.write(cmd)
            
            timeout_sec = timeout_ms / 1000.0
            resp = b""
            while (time.time() - start) < timeout_sec:
                if self.ser.in_waiting:
                    resp = self.ser.read(1)
                    if resp and resp[0] == value:
                        break
                time.sleep(0.005)
                
            elapsed_ms = (time.time() - start) * 1000
            if resp and resp[0] == value:
                print(f" -> [RX] Pong 0x{resp[0]:02X} OK ({elapsed_ms:.1f}ms RTT)")
                return True
            elif resp:
                print(f" -> [RX] Ping response: 0x{resp[0]:02X} ({elapsed_ms:.1f}ms)")
                return True
            else:
                print(f" -> [RX] Timeout ({timeout_ms}ms, No response)")
                
            if attempt < max_retries:
                time.sleep(0.05)
                
        return False

    def scan_nodes(self, start_id: int = 1, end_id: int = 10, value: int = 0xA5) -> List[int]:
        print(f"\n[*] Scanning nodes {start_id} to {end_id} via RF/Serial on {self.port}...")
        found = []
        for nid in range(start_id, end_id + 1):
            cmd = format_send_com_packet(bytes([OP_PING, value, nid]))
            self.ser.reset_input_buffer()
            self.ser.write(cmd)
            time.sleep(0.04)
            resp = self.ser.read(1)
            if resp:
                print(f"  [+] Node #{nid:2d}: ONLINE (Response 0x{resp[0]:02X})")
                found.append(nid)
            else:
                print(f"  [-] Node #{nid:2d}: No response")
            time.sleep(0.01)
        print(f"[*] Scan complete. Found {len(found)} responsive node(s): {found}\n")
        return found

    def read_eeprom(self, addr: int) -> Optional[int]:
        hi = (addr >> 8) & 0xFF
        lo = addr & 0xFF
        cmd = format_send_com_packet(bytes([OP_READ_EEPROM, hi, lo]))
        print(f"[TX] Read EEPROM @ 0x{addr:04X} (Hex: {cmd.hex()})", end="", flush=True)
        self.ser.reset_input_buffer()
        self.ser.write(cmd)
        resp = self.ser.read(1)
        if resp:
            val = resp[0]
            print(f" -> [RX] EEPROM[0x{addr:04X}] = 0x{val:02X} ({val})")
            return val
        print(" -> [RX] Timeout")
        return None

    def write_eeprom(self, addr: int, val: int) -> bool:
        hi = (addr >> 8) & 0xFF
        lo = addr & 0xFF
        cmd = format_send_com_packet(bytes([OP_WRITE_EEPROM, hi, lo, val]))
        print(f"[TX] Write EEPROM @ 0x{addr:04X} = 0x{val:02X} (Hex: {cmd.hex()})")
        for attempt in range(1, 6):
            self.ser.reset_input_buffer()
            self.ser.write(cmd)
            resp = self.ser.read(1)
            if resp and resp[0] == ACK_BYTE:
                print(f"  [RX] Attempt {attempt}: ACK 0x5A OK")
                return True
            time.sleep(0.02)
        print("  [RX] Write EEPROM Failed: No ACK")
        return False

    def read_ram_burst(self, addr: int) -> Optional[bytes]:
        lo = addr & 0xFF
        hi = (addr >> 8) & 0xFF
        cmd = format_send_com_packet(bytes([OP_READ_RAM_BURST, lo, hi, 0x08, 0x01]))
        print(f"[TX] Read RAM Burst 8B @ 0x{addr:04X} (Hex: {cmd.hex()})")
        self.ser.reset_input_buffer()
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

    def read_device_id(self) -> Optional[int]:
        cmd = format_send_com_packet(bytes([OP_DEVICE_ID, 0x00]))
        print(f"[TX] Query Device ID (Hex: {cmd.hex()})")
        self.ser.reset_input_buffer()
        self.ser.write(cmd)
        raw = self.ser.read(4)
        if len(raw) >= 3 and raw[0] == 0xFF and raw[1] == 0x5A:
            dev_id = raw[2]
            print(f"[RX] Current Device ID = 0x{dev_id:02X} ({dev_id})")
            return dev_id
        print(f"[RX] Failed to read Device ID. Raw: {raw.hex()}")
        return None

    def set_device_id(self, new_id: int) -> bool:
        cmd = format_send_com_packet(bytes([OP_DEVICE_ID, 0x01, new_id]))
        print(f"[TX] Set Device ID = 0x{new_id:02X} ({new_id}) (Hex: {cmd.hex()})")
        self.ser.write(cmd)
        time.sleep(0.05)
        resp = self.ser.read(self.ser.in_waiting or 1)
        print(f"[RX] Set ID response: {resp.hex() if resp else 'None'}")
        return True

    def monitor(self, duration: Optional[float] = None):
        print(f"\n[*] Monitoring serial stream on {self.port} @ {self.baudrate} baud...")
        print("[*] Press Ctrl+C to stop.\n")
        start = time.time()
        try:
            while True:
                if duration and (time.time() - start) > duration:
                    break
                n = self.ser.in_waiting
                if n > 0:
                    chunk = self.ser.read(n)
                    t_str = time.strftime("%H:%M:%S")
                    hex_str = " ".join(f"{b:02X}" for b in chunk)
                    ascii_str = "".join(chr(b) if 32 <= b <= 126 else "." for b in chunk)
                    print(f"[{t_str}] ({len(chunk)}B) {hex_str:<30} | {ascii_str}")
                time.sleep(0.02)
        except KeyboardInterrupt:
            print("\n[*] Monitor stopped.")

    def _detect_hc12_baud(self) -> int:
        """Find the baudrate where HC-12 responds to AT in AT mode."""
        for b in [9600, self.baudrate, 38400, 115200]:
            try:
                self.ser.baudrate = b
                time.sleep(0.04)
                self.ser.reset_input_buffer()
                self.ser.write(b"AT\r\n")
                time.sleep(0.12)
                resp = self.ser.read(self.ser.in_waiting or 32)
                if b"OK" in resp:
                    return b
            except Exception:
                pass
        return 9600

    def hc12_command(self, cmd_str: str) -> str:
        """Send an AT command to HC-12 module with automatic baud detection."""
        cmd_bytes = cmd_str.strip().encode("ascii") + b"\r\n"
        orig_baud = self.ser.baudrate
        at_baud = self._detect_hc12_baud()
        try:
            self.ser.baudrate = at_baud
            time.sleep(0.03)
            self.ser.reset_input_buffer()
            self.ser.write(cmd_bytes)
            time.sleep(0.15)
            resp = self.ser.read(self.ser.in_waiting or 128)
            if resp:
                return resp.decode("ascii", errors="replace").strip()
        finally:
            self.ser.baudrate = orig_baud
        return ""

    def hc12_query(self):
        """Query current parameters of HC-12 module (AT+RX, AT+RC, etc.)."""
        print("\n=== Querying HC-12 Configuration ===")
        print("[!] Note: Chân SET của module HC-12 phải được cắm vào GND để nhận lệnh AT.\n")
        
        # Test AT handshake
        at_resp = self.hc12_command("AT")
        if not at_resp:
            print("[X] Không nhận được phản hồi từ HC-12!")
            print("    -> Hãy kiểm tra lại: chân SET đã cắm chắc vào GND chưa?")
            print("    -> TX/RX có bị cắm nhầm không?")
            return False

        print(f"[✓] Handshake: {at_resp}")
        
        rx_resp = self.hc12_command("AT+RX")
        if rx_resp:
            print(f"[✓] Current Settings (AT+RX):\n{rx_resp}")
        
        rc_resp = self.hc12_command("AT+RC")
        if rc_resp:
            print(f"[✓] Current Channel: {rc_resp}")
        
        rb_resp = self.hc12_command("AT+RB")
        if rb_resp:
            print(f"[✓] Current Baudrate: {rb_resp}")
            
        print("\n[!] Sau khi cài đặt xong, hãy RÚT chân SET ra khỏi GND (thả nổi) để truyền dữ liệu không dây.")
        return True

    def hc12_set_channel(self, channel: int):
        """Set HC-12 radio channel (001 to 127)."""
        if not (1 <= channel <= 127):
            print(f"[X] Channel {channel} không hợp lệ! Phải từ 1 đến 127.")
            return False
            
        ch_str = f"C{channel:03d}"
        print(f"\n[*] Đang gửi lệnh cài kênh: AT+{ch_str} ...")
        resp = self.hc12_command(f"AT+{ch_str}")
        if resp:
            print(f"[✓] Phản hồi từ module: {resp}")
            print(f"[✓] Đã cài đặt kênh HC-12 thành công về KÊNH {channel:03d}!")
            print("[!] QUAN TRỌNG: Hãy RÚT chân SET ra khỏi GND để chuyển sang chế độ truyền sóng vô tuyến!")
            return True
        else:
            print("[X] Không nhận được phản hồi từ HC-12!")
            print("    -> Hãy đảm bảo chân SET của HC-12 đang được nối vào GND.")
            return False

    def hc12_set_baud(self, baud: int):
        """Set HC-12 UART baud rate."""
        valid_bauds = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200]
        if baud not in valid_bauds:
            print(f"[X] Baudrate {baud} không hợp lệ! Hỗ trợ: {valid_bauds}")
            return False
            
        print(f"\n[*] Đang gửi lệnh cài baudrate: AT+B{baud} ...")
        resp = self.hc12_command(f"AT+B{baud}")
        if resp:
            print(f"[✓] Phản hồi từ module: {resp}")
            print(f"[✓] Đã cài đặt baudrate thành công về {baud}!")
            return True
        else:
            print("[X] Không nhận được phản hồi từ HC-12!")
            return False

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

def parse_nodes_list(s: str) -> List[int]:
    """Parse comma-separated node list like '4,5,6,7'."""
    return [int(x.strip()) for x in s.split(",") if x.strip()]

def interactive_menu(client: AguSerialClient):
    """Interactive CLI menu for controlling CP2102 / AGU Aeroponics."""
    while True:
        print("\n========================================================")
        print("          AGU-Aeroponics CP2102 Control Console         ")
        print(f" Port: {client.port} | Baud: {client.baudrate} (8N2)")
        print("========================================================")
        print(" 1) Run TestSCI Pump Cycle (Test6: Nodes 4,5,6,7)")
        print(" 2) Turn Pump ON")
        print(" 3) Turn Pump OFF")
        print(" 4) Ping Node")
        print(" 5) Scan / Probe Nodes (1..10)")
        print(" 6) Read EEPROM")
        print(" 7) Write EEPROM")
        print(" 8) Read RAM Burst (8 Bytes)")
        print(" 9) Query / Set Device ID")
        print("10) HC-12 Config (Set Channel, Baud, Query AT+RX)")
        print("11) Raw Serial Monitor")
        print(" 0) Exit")
        print("--------------------------------------------------------")
        
        try:
            choice = input("Select option [0-11]: ").strip()
        except (KeyboardInterrupt, EOFError):
            print("\nExiting.")
            break

        if choice == "1":
            nodes_input = input("Enter nodes to cycle (default: 4,5,6,7): ").strip()
            nodes = parse_nodes_list(nodes_input) if nodes_input else [4, 5, 6, 7]
            on_s = input("ON duration in seconds (default: 3): ").strip()
            on_time = float(on_s) if on_s else 3.0
            off_s = input("OFF duration in seconds (default: 10): ").strip()
            off_time = float(off_s) if off_s else 10.0
            client.run_cycle(nodes, on_time, off_time)

        elif choice == "2":
            n_str = input("Node ID to turn ON (e.g. 4 or 4,5,6,7): ").strip()
            if n_str:
                for nid in parse_nodes_list(n_str):
                    client.send_pump_on(nid)

        elif choice == "3":
            n_str = input("Node ID to turn OFF (e.g. 4 or 4,5,6,7): ").strip()
            if n_str:
                for nid in parse_nodes_list(n_str):
                    client.send_pump_off(nid)

        elif choice == "4":
            n_str = input("Node ID to ping (default: 1): ").strip()
            nid = int(n_str) if n_str else 1
            client.send_ping(nid)

        elif choice == "5":
            s_str = input("Start Node ID (default: 1): ").strip()
            start_id = int(s_str) if s_str else 1
            e_str = input("End Node ID (default: 10): ").strip()
            end_id = int(e_str) if e_str else 10
            client.scan_nodes(start_id, end_id)

        elif choice == "6":
            addr_str = input("Address in hex (e.g. 0x0000): ").strip()
            if addr_str:
                addr = int(addr_str, 0)
                client.read_eeprom(addr)

        elif choice == "7":
            addr_str = input("Address in hex (e.g. 0x0000): ").strip()
            val_str = input("Value (hex or dec, e.g. 0xA5 or 123): ").strip()
            if addr_str and val_str:
                client.write_eeprom(int(addr_str, 0), int(val_str, 0))

        elif choice == "8":
            addr_str = input("Address in hex (e.g. 0x0008): ").strip()
            if addr_str:
                client.read_ram_burst(int(addr_str, 0))

        elif choice == "9":
            sub = input("[1] Query ID or [2] Set ID: ").strip()
            if sub == "1":
                client.read_device_id()
            elif sub == "2":
                new_id_str = input("New ID (1..255): ").strip()
                if new_id_str:
                    client.set_device_id(int(new_id_str, 0))

        elif choice == "10":
            print("\n[1] Query Settings (AT+RX)")
            print("[2] Set Channel (1..127)")
            print("[3] Set Baudrate")
            print("[4] Legacy APC220 RF Setup")
            sub_hc = input("Choose [1-4]: ").strip()
            if sub_hc == "1":
                client.hc12_query()
            elif sub_hc == "2":
                ch_in = input("Enter channel number (1..127, e.g. 1 or 7): ").strip()
                if ch_in:
                    client.hc12_set_channel(int(ch_in))
            elif sub_hc == "3":
                b_in = input("Enter baudrate (e.g. 38400, 9600): ").strip()
                if b_in:
                    client.hc12_set_baud(int(b_in))
            elif sub_hc == "4":
                client.rf_setup()

        elif choice == "11":
            client.monitor()

        elif choice == "0":
            print("Goodbye!")
            break

def main():
    default_port = detect_default_port()

    parser = argparse.ArgumentParser(description="AGU-Aeroponics E2E Diagnostic & Control Tool (CP2102)")
    parser.add_argument("--port", default=default_port, help=f"Serial port (detected: {default_port})")
    parser.add_argument("--baud", type=int, default=38400, help="Baudrate (default: 38400)")
    parser.add_argument("--stopbits", type=int, choices=[1, 2], default=2, help="Stop bits: 1 or 2 (default: 2 for 8N2)")
    subparsers = parser.add_subparsers(dest="command", required=False)

    # interactive
    subparsers.add_parser("interactive", help="Launch interactive control menu")

    # cycle / test6
    p_cycle = subparsers.add_parser("cycle", aliases=["test6"], help="Run legacy TestSCI continuous pump cycle")
    p_cycle.add_argument("--nodes", default="4,5,6,7", help="Comma-separated node IDs (default: 4,5,6,7)")
    p_cycle.add_argument("--on", type=float, default=3.0, help="Pump ON duration in seconds (default: 3.0)")
    p_cycle.add_argument("--off", type=float, default=10.0, help="Pump OFF duration in seconds (default: 10.0)")
    p_cycle.add_argument("--count", type=int, default=0, help="Number of cycles (default: 0 = infinite)")

    # pump-on
    p_on = subparsers.add_parser("pump-on", help="Turn ON pump for a node")
    p_on.add_argument("--node", type=int, default=None, help="Single Node ID")
    p_on.add_argument("--nodes", default=None, help="Comma-separated Node IDs (e.g. 4,5,6,7)")

    # pump-off
    p_off = subparsers.add_parser("pump-off", help="Turn OFF pump for a node")
    p_off.add_argument("--node", type=int, default=None, help="Single Node ID")
    p_off.add_argument("--nodes", default=None, help="Comma-separated Node IDs (e.g. 4,5,6,7)")

    # ping
    p_ping = subparsers.add_parser("ping", help="Ping node and measure latency")
    p_ping.add_argument("--node", type=int, default=1, help="Node ID (default: 1)")
    p_ping.add_argument("--value", type=lambda x: int(x, 0), default=0xA5, help="Ping payload byte (default: 0xA5)")

    # scan
    p_scan = subparsers.add_parser("scan", aliases=["probe"], help="Scan / probe range of nodes")
    p_scan.add_argument("--start", type=int, default=1, help="Start Node ID (default: 1)")
    p_scan.add_argument("--end", type=int, default=10, help="End Node ID (default: 10)")

    # get-id / set-id
    subparsers.add_parser("get-id", help="Read device ID (Opcode 0x0A)")
    p_setid = subparsers.add_parser("set-id", help="Set device ID (Opcode 0x0A)")
    p_setid.add_argument("--id", type=lambda x: int(x, 0), required=True, help="New device ID")

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

    # monitor
    p_mon = subparsers.add_parser("monitor", help="Monitor raw serial stream from CP2102")
    p_mon.add_argument("--duration", type=float, default=None, help="Duration in seconds (default: infinite)")

    # rf-setup
    subparsers.add_parser("rf-setup", help="Send AT commands to configure RF module (Baud, Channel, Network ID)")

    # hc12
    p_hc12 = subparsers.add_parser("hc12", help="Configure HC-12 wireless module (Channel, Baud, Query)")
    p_hc12.add_argument("--query", "-q", action="store_true", help="Query current HC-12 configuration (AT+RX)")
    p_hc12.add_argument("--channel", "-c", type=int, help="Set channel (1..127, e.g. 1 or 7)")
    p_hc12.add_argument("--baud-rate", "-b", type=int, help="Set baudrate (e.g. 9600, 38400)")

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

    # If no command given, or command is 'interactive', launch interactive menu
    client = AguSerialClient(args.port, args.baud, stopbits=args.stopbits)
    try:
        if args.command is None or args.command == "interactive":
            interactive_menu(client)
        elif args.command in ("cycle", "test6"):
            nodes = parse_nodes_list(args.nodes)
            client.run_cycle(nodes, args.on, args.off, args.count)
        elif args.command == "pump-on":
            target_nodes = parse_nodes_list(args.nodes) if args.nodes else ([args.node] if args.node is not None else [1])
            for nid in target_nodes:
                client.send_pump_on(nid)
        elif args.command == "pump-off":
            target_nodes = parse_nodes_list(args.nodes) if args.nodes else ([args.node] if args.node is not None else [1])
            for nid in target_nodes:
                client.send_pump_off(nid)
        elif args.command == "ping":
            client.send_ping(args.node, args.value)
        elif args.command in ("scan", "probe"):
            client.scan_nodes(args.start, args.end)
        elif args.command == "get-id":
            client.read_device_id()
        elif args.command == "set-id":
            client.set_device_id(args.id)
        elif args.command == "read-eeprom":
            client.read_eeprom(args.addr)
        elif args.command == "write-eeprom":
            client.write_eeprom(args.addr, args.val)
        elif args.command == "read-ram":
            client.read_ram_burst(args.addr)
        elif args.command == "monitor":
            client.monitor(args.duration)
        elif args.command == "hc12":
            if args.channel is not None:
                client.hc12_set_channel(args.channel)
            elif args.baud_rate is not None:
                client.hc12_set_baud(args.baud_rate)
            else:
                client.hc12_query()
        elif args.command == "rf-setup":
            client.rf_setup()
    finally:
        client.close()

if __name__ == "__main__":
    main()
