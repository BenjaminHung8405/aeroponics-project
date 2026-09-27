import unittest

from test_agu_rf_e2e import (
    ACK_BYTE,
    AguSerialClient,
    diagnose_response,
    format_send_com_packet,
    group_id_for_node,
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
        # CRC16-Modbus SendComCRC16 envelope: [len=payloadLen+2][payload][crc_lo][crc_hi].
        self.assertEqual(frame, bytes.fromhex("04 06 04 32 62"))

    def test_only_valid_physical_ids_are_allowed(self):
        for node_id in range(1, 16):
            validate_node_id(node_id)
        for node_id in (0, 16, 255):
            with self.assertRaises(ValueError):
                validate_node_id(node_id)

    def test_group_address_mapping_follows_shared_bit_field(self):
        # groupID = 0x10 | (nodeID & 0x0C): 0x01..0x03 -> $10, 0x04..0x07 -> $14, ...
        self.assertEqual(group_id_for_node(1), 0x10)
        self.assertEqual(group_id_for_node(4), 0x14)
        self.assertEqual(group_id_for_node(8), 0x18)
        self.assertEqual(group_id_for_node(0x0F), 0x1C)

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
