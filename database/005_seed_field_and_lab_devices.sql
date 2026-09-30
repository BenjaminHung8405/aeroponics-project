-- =============================================================================
-- Migration 005: Seed Field and Lab ESP32 Gateway Devices
-- =============================================================================
-- Registers the 2 production & development ESP32 Gateways:
--  1. esp32_field: ESP32 Thực địa (Field Gateway deployed at farm)
--  2. esp32_lab:   ESP32 Phòng LAB (Bench/Lab testbed Gateway)
--
-- Ensures idempotent insertion and populates initial control_slots (1..4)
-- for both devices if not already configured.

BEGIN;

INSERT INTO devices (
  device_id,
  display_name,
  mqtt_username,
  enabled,
  created_at,
  updated_at
) VALUES
  ('esp32_field', 'ESP32 Thực địa (Field)', 'esp32_field', TRUE, NOW(), NOW()),
  ('esp32_lab',   'ESP32 Phòng LAB (Lab)',   'esp32_lab',   TRUE, NOW(), NOW()),
  ('aero_s3_b81f3fb9a09c', 'ESP32 Thực địa (b9a09c)', 'aero_s3_b81f3fb9a09c', TRUE, NOW(), NOW())
ON CONFLICT (device_id) DO UPDATE SET
  display_name = EXCLUDED.display_name,
  enabled = EXCLUDED.enabled,
  updated_at = NOW();

-- Seed default control slots 1..4 for each device (pointing to nodes 1..4)
INSERT INTO control_slots (device_id, slot_index, target_type, target_id, updated_by)
VALUES
  ('esp32_field', 1, 'NODE', 1, 'system_seed'),
  ('esp32_field', 2, 'NODE', 2, 'system_seed'),
  ('esp32_field', 3, 'NODE', 3, 'system_seed'),
  ('esp32_field', 4, 'NODE', 4, 'system_seed'),
  ('esp32_lab',   1, 'NODE', 1, 'system_seed'),
  ('esp32_lab',   2, 'NODE', 2, 'system_seed'),
  ('esp32_lab',   3, 'NODE', 3, 'system_seed'),
  ('esp32_lab',   4, 'NODE', 4, 'system_seed'),
  ('aero_s3_b81f3fb9a09c', 1, 'NODE', 1, 'system_seed'),
  ('aero_s3_b81f3fb9a09c', 2, 'NODE', 2, 'system_seed'),
  ('aero_s3_b81f3fb9a09c', 3, 'NODE', 3, 'system_seed'),
  ('aero_s3_b81f3fb9a09c', 4, 'NODE', 4, 'system_seed')
ON CONFLICT (device_id, slot_index) DO NOTHING;


COMMIT;
