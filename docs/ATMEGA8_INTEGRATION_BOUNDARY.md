# ATmega8 Integration Boundary

> **Status:** Current integration constraint / source of truth
> **Applies to:** All ESP32, RF, MQTT, telemetry, safety and QA documentation

## 1. Hardware and Firmware Boundary

The ATmega8 devices are **preloaded legacy nodes**. Their firmware source is not
available in this repository and must not be modified or treated as part of the
ESP32 build. The firmware version running on each physical node is **UNKNOWN**
unless the deployed firmware exposes a verified version query.

The only controllable software component in this repository is the ESP32
Gateway. It sends commands supported by the preloaded node firmware over RF and
parses responses that are actually observed and validated.

The southbound protocol is **AGU-Aeroponics legacy SCI**, not the historical
HMAC production-frame design. ESP32 uses `AguLegacyCodec` and
`AguLegacyRfHost` to serialize and transact the deployed protocol:

```text
[Length][Opcode][Params...][Zero-sum checksum]
```

The supported control path is the verified AGU transaction set: `PING (0x05)`,
`PUMP_ON (0x06)`, `PUMP_OFF (0x07)`, with legacy ACK `0x5A`. Optional memory,
ID and RAM-poll opcodes (`0x01`, `0x04`, `0x08`, `0x09`, `0x0A`, `0x0E`) require
separate black-box verification before use. AGU frames contain no HMAC,
`boot_session_id`, RF sequence or production `command_id`.

## 2. What Is Not Assumed

Until black-box RF/hardware evidence exists, the system must not claim that an
ATmega8 node provides:

- autonomous scheduling, EEPROM schedule persistence, or automatic resume;
- a lease/deadman timer, fault latch, or Safe-OFF after RF loss;
- HMAC, session, sequence, anti-replay, or authenticated legacy frames;
- unsolicited telemetry, heartbeat, or asynchronous fault reports;
- driver feedback, current sensing, flow sensing, or semantic fault fields;
- any firmware version, build result, unit-test result, or simulator result.

Physical wiring is not evidence that the preloaded firmware reads or reports a
signal. A repository build or host-side test validates gateway/model code only;
it does not validate the firmware currently programmed into a node.

## 3. Control and Evidence Rules

1. ESP32 may send `PUMP_ON`, `PUMP_OFF`, `PING`, or another command only when
   its support and byte-level format have been verified for the deployed node.
2. A legacy RF ACK proves only that the transaction was accepted. It does not
   prove GPIO actuation, pump operation, current, flow, or Safe-OFF.
3. ESP32 may send `PUMP_OFF` while RF communication is available. When RF is
   unavailable, ESP32 cannot force a remote state change; physical interlock
   or independently verified node behavior is required for that safety claim.
4. Telemetry published by ESP32 is gateway-observed data derived from a valid
   response. It is not evidence of unsolicited node telemetry.
5. Unsupported or unverified fields are represented as `UNKNOWN` or
   `NOT_AVAILABLE`, never inferred from the requested command.
6. An ON command must be rejected or explicitly marked unsafe if the deployment
   requires a node-side timeout guarantee that has not been independently
   verified.

## 4. Verification Status Vocabulary

- **IMPLEMENTED:** observed in the deployed integration or directly verified
  by an independent hardware/protocol test.
- **GATEWAY_ONLY:** implemented/tested in ESP32 or host code; not evidence of
  ATmega8 behavior.
- **UNVERIFIED:** possible, but not proven for the preloaded firmware.
- **NOT_SUPPORTED:** outside the current integration boundary.

All acceptance reports must separate these statuses and must not report a
gateway/model test as an ATmega8 firmware acceptance result.
