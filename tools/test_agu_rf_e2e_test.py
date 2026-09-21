import unittest

from test_agu_rf_e2e import (
    ACK_BYTE,
    AguSerialClient,
    diagnose_response,
    format_send_com_packet,
    validate_node_id,
)


class FakeSerial:
    def __init__(self, response=b""):
        self.response = bytearray(response)
        self.writes = []
        self.is_open = True

    @property
    def in_waiting(self):
        return len(self.response)

    def reset_input_buffer(self):
        # Preserve the scripted response; real callers use this to discard
        # stale bytes before a transaction.
        pass

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def read(self, size=1):
        data = bytes(self.response[:size])
        del self.response[:size]
        return data

    def close(self):
        self.is_open = False


class AguDiagnosticTests(unittest.TestCase):
    def client_with_response(self, response):
        client = AguSerialClient.__new__(AguSerialClient)
        client.ser = FakeSerial(response)
        client.port = "fake"
        client.baudrate = 38400
        return client

    def test_pump_frame_contains_length_payload_and_checksum(self):
        frame = format_send_com_packet(bytes([0x06, 4]))
        self.assertEqual(frame, bytes.fromhex("03 06 04 F3"))

    def test_only_valid_physical_ids_are_allowed(self):
        for node_id in (4, 5, 6, 7):
            validate_node_id(node_id)
        for node_id in (1, 2, 3, 8):
            with self.assertRaises(ValueError):
                validate_node_id(node_id)

    def test_ping_rejects_hc12_error_byte(self):
        client = self.client_with_response(b"E")
        self.assertFalse(client.send_ping(4, timeout_ms=1, max_retries=1))
        self.assertIn("AT_MODE_RESPONSE", diagnose_response(b"E"))

    def test_ping_accepts_only_matching_pong(self):
        client = self.client_with_response(bytes([0xA5]))
        self.assertTrue(client.send_ping(4, timeout_ms=10, max_retries=1))

    def test_pump_accepts_only_ack(self):
        client = self.client_with_response(bytes([ACK_BYTE]))
        self.assertTrue(client.send_pump_off(4, timeout_ms=10, max_retries=1))


if __name__ == "__main__":
    unittest.main()
