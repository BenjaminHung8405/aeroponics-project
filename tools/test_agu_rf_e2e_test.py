import unittest

from test_agu_rf_e2e import (
    ACK_BYTE,
    AguSerialClient,
    OP_PING,
    OP_PUMP_OFF,
    OP_PUMP_ON,
    OP_READ_RAM_BURST,
    RAM_BURST_DATA_SIZE,
    RAM_BURST_RESPONSE_LENGTH,
    RAM_BURST_RESPONSE_SIZE,
    calc_crc16_modbus,
    classify_frame,
    classify_ping_reply,
    diagnose_response,
    format_send_com_packet,
    group_id_for_node,
    validate_node_id,
    verify_crc16_modbus,
)

NODE_08 = 0x08
PING_ECHO = 0xA5


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


class FakeNodeSerial(FakeSerial):
    """Scripted legacy node: echoes PING, ACKs PUMP_ON/OFF for its own ID.

    `ping_reply` lets a test simulate a misbehaving node that returns a NACK
    (0x00) instead of the echo nonce, reproducing the observed failure.
    """

    def __init__(self, node_id=NODE_08, ping_reply=None, ack=True):
        super().__init__(b"")
        self.node_id = node_id
        self.ping_reply = ping_reply
        self.ack = ack

    def write(self, data):
        frame = bytes(data)
        self.writes.append(frame)
        self.response = bytearray()
        if len(frame) < 3 or not verify_crc16_modbus(frame):
            return len(data)
        opcode = frame[1]
        params = frame[2:-2]
        if opcode == OP_PING and len(params) == 2 and params[1] == self.node_id:
            self.response += bytes([params[0] if self.ping_reply is None else self.ping_reply])
        elif opcode in (OP_PUMP_ON, OP_PUMP_OFF) and params and params[0] == self.node_id:
            if self.ack:
                self.response += bytes([ACK_BYTE])
        return len(data)


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

    def test_ping_reply_verdicts_distinguish_fault_modes(self):
        # The failing monitor log showed response=0x00; the classifier must
        # name it as a NACK, not lump it in with a generic timeout or ACK.
        self.assertEqual("TIMEOUT", classify_ping_reply(None))
        self.assertEqual("ECHO_OK", classify_ping_reply(0xA5))
        self.assertEqual("ACK_NOT_ECHO", classify_ping_reply(0x5A))
        self.assertEqual("NACK", classify_ping_reply(0x00))
        self.assertEqual("UNEXPECTED_0x37", classify_ping_reply(0x37))

    def test_ping_accepts_only_matching_pong(self):
        client = self.client_with_response(bytes([0xA5]))
        self.assertTrue(client.send_ping(4, timeout_ms=10, max_retries=1))

    def test_pump_accepts_only_ack(self):
        client = self.client_with_response(bytes([ACK_BYTE]))
        self.assertTrue(client.send_pump_off(4, timeout_ms=10, max_retries=1))


class Node08LegacyRfCommsTests(unittest.TestCase):
    """ESP32 gateway <-> physical legacy node 0x08 wire contract.

    These are the golden frames the deployed gateway emits for node 8, plus
    the response contract it must enforce. Every vector here is derivable from
    the Delphi reference (SCIW32.pas TSCI.SendCom / TestSCI.dpr) and the
    CRC16-Modbus envelope documented in docs/ATMEGA8_INTEGRATION_BOUNDARY.md,
    so the suite runs offline with no RF hardware attached.
    """

    def client_with_response(self, response):
        client = AguSerialClient.__new__(AguSerialClient)
        client.ser = FakeSerial(response)
        client.port = "fake"
        client.baudrate = 38400
        return client

    def test_node08_control_frames_match_deployed_legacy_wire_contract(self):
        self.assertEqual(
            bytes.fromhex("04 06 08 32 67"),
            format_send_com_packet(bytes([OP_PUMP_ON, NODE_08])),
        )
        self.assertEqual(
            bytes.fromhex("04 07 08 33 F7"),
            format_send_com_packet(bytes([OP_PUMP_OFF, NODE_08])),
        )
        self.assertEqual(
            bytes.fromhex("05 05 A5 08 6A 7F"),
            format_send_com_packet(bytes([OP_PING, PING_ECHO, NODE_08])),
        )
        self.assertEqual(
            bytes.fromhex("07 0E 00 00 08 08 6E 6B"),
            format_send_com_packet(
                bytes([OP_READ_RAM_BURST, 0x00, 0x00, RAM_BURST_DATA_SIZE, NODE_08])
            ),
        )

    def test_node08_frames_were_cross_checked_against_documented_examples(self):
        # docs/ATMEGA8_INTEGRATION_BOUNDARY.md gives node-9 anchors:
        #   pump ON  = 04 06 09 F3 A7 ; pump OFF = 04 07 09 F2 37
        # The codec shares one framing path, so node 8 must land on the same
        # algorithm with only the address byte differing.
        self.assertEqual(
            bytes.fromhex("04 06 09 F3 A7"),
            format_send_com_packet(bytes([OP_PUMP_ON, 0x09])),
        )
        self.assertEqual(
            bytes.fromhex("04 07 09 F2 37"),
            format_send_com_packet(bytes([OP_PUMP_OFF, 0x09])),
        )

    def test_node08_every_control_frame_passes_crc16_verification(self):
        frames = [
            format_send_com_packet(bytes([OP_PUMP_ON, NODE_08])),
            format_send_com_packet(bytes([OP_PUMP_OFF, NODE_08])),
            format_send_com_packet(bytes([OP_PING, PING_ECHO, NODE_08])),
        ]
        for frame in frames:
            with self.subTest(frame=frame.hex(" ")):
                self.assertTrue(verify_crc16_modbus(frame))
                # Self-describing envelope: length byte + 1 == total frame size.
                self.assertEqual(frame[0] + 1, len(frame))
                # A single flipped bit anywhere must break CRC verification.
                for index in range(len(frame)):
                    corrupted = bytearray(frame)
                    corrupted[index] ^= 0x01
                    self.assertFalse(verify_crc16_modbus(bytes(corrupted)))

    def test_node08_ping_requires_echo_not_generic_ack(self):
        # A node that answers PING with the generic 0x5A ACK instead of echoing
        # the nonce is not a valid liveness proof and must not be accepted.
        ack_client = self.client_with_response(bytes([ACK_BYTE]))
        self.assertFalse(ack_client.send_ping(NODE_08, timeout_ms=10, max_retries=1))

        echo_client = self.client_with_response(bytes([PING_ECHO]))
        self.assertTrue(echo_client.send_ping(NODE_08, timeout_ms=10, max_retries=1))

    def test_node08_ping_sends_echo_nonce_on_the_wire(self):
        client = self.client_with_response(bytes([PING_ECHO]))
        self.assertTrue(client.send_ping(NODE_08, timeout_ms=10, max_retries=1))
        expected = format_send_com_packet(bytes([OP_PING, PING_ECHO, NODE_08]))
        self.assertEqual([expected], client.ser.writes)

    def test_node08_pump_off_accepts_only_ack_byte(self):
        ack_client = self.client_with_response(bytes([ACK_BYTE]))
        self.assertTrue(ack_client.send_pump_off(NODE_08, timeout_ms=10, max_retries=1))
        expected = format_send_com_packet(bytes([OP_PUMP_OFF, NODE_08]))
        self.assertEqual([expected], ack_client.ser.writes)

        echo_client = self.client_with_response(bytes([PING_ECHO]))
        self.assertFalse(echo_client.send_pump_off(NODE_08, timeout_ms=10, max_retries=1))

    def test_node08_pump_on_accepts_only_ack_byte(self):
        ack_client = self.client_with_response(bytes([ACK_BYTE]))
        self.assertTrue(ack_client.send_pump_on(NODE_08, timeout_ms=10, max_retries=1))

        echo_client = self.client_with_response(bytes([PING_ECHO]))
        self.assertFalse(echo_client.send_pump_on(NODE_08, timeout_ms=10, max_retries=1))

    def test_node08_burst_reply_framing_is_independently_verifiable(self):
        data = bytes([0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80])
        body = bytes([RAM_BURST_RESPONSE_LENGTH]) + data
        crc = calc_crc16_modbus(body)
        reply = body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])

        self.assertEqual(RAM_BURST_RESPONSE_SIZE, len(reply))
        crc_ok, framing_ok = classify_frame(reply, "burst")
        self.assertTrue(crc_ok)
        self.assertTrue(framing_ok)

        corrupted = bytearray(reply)
        corrupted[3] ^= 0xFF
        crc_ok, framing_ok = classify_frame(bytes(corrupted), "burst")
        self.assertFalse(crc_ok)
        self.assertTrue(framing_ok)  # Length intact; only the CRC verdict fails.

    def test_node08_retry_reuses_the_identical_frame(self):
        # The host must retransmit byte-for-byte identical frames so a delayed
        # node reply cannot be mistaken for a different command's response.
        client = self.client_with_response(b"")
        client.send_pump_off(NODE_08, timeout_ms=1, max_retries=3)
        expected = format_send_com_packet(bytes([OP_PUMP_OFF, NODE_08]))
        self.assertEqual(3, len(client.ser.writes))
        for written in client.ser.writes:
            self.assertEqual(expected, written)


    def test_node08_ping_nack_is_surfaced_as_failure(self):
        # Reproduces the observed monitor line: response=0x00, result=1
        # (UNEXPECTED_RESPONSE) after all three attempts.
        client = self.client_with_response(b"")
        client.ser = FakeNodeSerial(NODE_08, ping_reply=0x00)
        self.assertFalse(client.send_ping(NODE_08, timeout_ms=10, max_retries=3))
        expected = format_send_com_packet(bytes([OP_PING, PING_ECHO, NODE_08]))
        self.assertEqual(3, len(client.ser.writes))
        for written in client.ser.writes:
            self.assertEqual(expected, written)

    def test_node08_link_test_passes_on_actuator_ack_with_ping_diagnostic(self):
        # The link test passes based on PUMP_ON/OFF ACKs even when PING 0x05
        # is unsupported (returns NACK), matching the deployed firmware.
        client = self.client_with_response(b"")
        client.ser = FakeNodeSerial(NODE_08, ping_reply=0x00)
        self.assertTrue(client.link_test(NODE_08, on_seconds=0.02, ping_interval=0.01,
                                         timeout_ms=10, max_retries=3))
        self.assertIn(format_send_com_packet(bytes([OP_PUMP_ON, NODE_08])),
                      client.ser.writes)
        self.assertIn(format_send_com_packet(bytes([OP_PUMP_OFF, NODE_08])),
                      client.ser.writes)

    def test_node08_link_test_passes_with_full_echo(self):
        client = self.client_with_response(b"")
        client.ser = FakeNodeSerial(NODE_08)
        self.assertTrue(client.link_test(NODE_08, on_seconds=0.02, ping_interval=0.01,
                                         timeout_ms=10, max_retries=3))

    def test_node08_link_test_fails_when_pump_on_is_not_acked(self):
        client = self.client_with_response(b"")
        client.ser = FakeNodeSerial(NODE_08, ack=False)
        self.assertFalse(client.link_test(NODE_08, on_seconds=0.02, ping_interval=0.01,
                                          timeout_ms=1, max_retries=3))

    def test_node08_link_test_require_ping_flag_fails_on_unsupported_firmware(self):
        client = self.client_with_response(b"")
        client.ser = FakeNodeSerial(NODE_08, ping_reply=0x00)
        self.assertFalse(client.link_test(NODE_08, on_seconds=0.02, ping_interval=0.01,
                                          timeout_ms=10, max_retries=3,
                                          require_ping=True))

    def test_node08_link_test_ignores_transactions_addressed_to_other_nodes(self):
        client = self.client_with_response(b"")
        client.ser = FakeNodeSerial(node_id=7)
        self.assertFalse(client.link_test(NODE_08, on_seconds=0.02, ping_interval=0.01,
                                          timeout_ms=1, max_retries=3))


if __name__ == "__main__":
    unittest.main()
