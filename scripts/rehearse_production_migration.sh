#!/usr/bin/env bash
set -euo pipefail

# Reproducible disposable migration rehearsal; requires Docker only.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
container="aero-migration-rehearsal-$$_${RANDOM}_${RANDOM}"
cleanup() { docker rm -f "$container" >/dev/null 2>&1 || true; }
trap cleanup EXIT

docker run -d --rm --name "$container" -e POSTGRES_PASSWORD=rehearsal -e POSTGRES_DB=aeroponics \
    timescale/timescaledb:latest-pg15 >/dev/null

# pg_isready only verifies that Postgres accepts connections. The image can
# still be running initdb/create-database work, so prove the requested DB is
# queryable before applying any fixture or migration.
readonly db_ready_timeout_s=90
readonly db_ready_deadline=$((SECONDS + db_ready_timeout_s))
until docker exec "$container" psql -v ON_ERROR_STOP=1 -U postgres -d aeroponics -c 'SELECT 1' >/dev/null 2>&1; do
    if (( SECONDS >= db_ready_deadline )); then
        echo "ERROR: timed out after ${db_ready_timeout_s}s waiting for queryable database aeroponics" >&2
        exit 1
    fi
    sleep 1
done
docker exec -i "$container" psql -v ON_ERROR_STOP=1 -U postgres -d aeroponics < "$ROOT/database/rehearsal/legacy_fixture.sql"
# Seed a pre-existing calibrated node before the first migration. This is the
# regression fixture for replay preserving live calibration state.
docker exec -i "$container" psql -v ON_ERROR_STOP=1 -U postgres -d aeroponics <<'SQL'
-- These two tables intentionally pre-date the migration and carry the legacy
-- 1..12 constraint. The rehearsal verifies that production replaces it.
CREATE TABLE group_node_assignments (
  id SERIAL PRIMARY KEY, group_id SMALLINT NOT NULL, node_id SMALLINT NOT NULL,
  season_id INT NOT NULL, effective_from TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  effective_to TIMESTAMPTZ, active BOOLEAN NOT NULL DEFAULT TRUE,
  CONSTRAINT group_node_assignments_node_id_check CHECK (node_id BETWEEN 1 AND 12)
);
CREATE TABLE sensor_calibrations (
  id SERIAL PRIMARY KEY, node_id SMALLINT NOT NULL, sensor_serial VARCHAR(64) NOT NULL,
  version_num INT NOT NULL, pulses_per_litre NUMERIC(10,2) NOT NULL,
  reference_volume_ml INT NOT NULL, trial_count INT NOT NULL, mean_pulses NUMERIC(10,2) NOT NULL,
  variance NUMERIC(10,4) NOT NULL, repeatability_pct NUMERIC(5,2) NOT NULL,
  status VARCHAR(16) NOT NULL,
  CONSTRAINT sensor_calibrations_node_id_check CHECK (node_id BETWEEN 1 AND 12)
);
CREATE TABLE node_registry (
  node_id SMALLINT PRIMARY KEY, display_name VARCHAR(50) NOT NULL,
  sensor_serial VARCHAR(64), active_sensor_calibration_id INT,
  calibration_status VARCHAR(16) NOT NULL DEFAULT 'UNCALIBRATED'
);
INSERT INTO sensor_calibrations
  (id, node_id, sensor_serial, version_num, pulses_per_litre, reference_volume_ml, trial_count, mean_pulses, variance, repeatability_pct, status)
VALUES (100, 4, 'YF-S201-PREEXISTING', 7, 452.25, 1000, 3, 452.25, 0.10, 0.15, 'ACTIVE');
INSERT INTO node_registry (node_id, display_name, sensor_serial, active_sensor_calibration_id, calibration_status)
VALUES (4, 'Node 04', 'YF-S201-PREEXISTING', 100, 'CALIBRATED');
SQL
docker exec -i "$container" psql -v ON_ERROR_STOP=1 -U postgres -d aeroponics < "$ROOT/database/001_production_domain_migration.sql"
# The migration must be safe to replay on the same schema, as required by
# deployment/rehearsal workflows. Keep this second execution in the disposable
# rehearsal so non-idempotent DDL fails the check immediately.
docker exec -i "$container" psql -v ON_ERROR_STOP=1 -U postgres -d aeroponics < "$ROOT/database/001_production_domain_migration.sql"

docker exec -i "$container" psql -v ON_ERROR_STOP=1 -U postgres -d aeroponics <<'SQL'
DO $$
DECLARE calibration_id INT; serial_value TEXT; status_value TEXT; constraint_count INT;
BEGIN
  SELECT active_sensor_calibration_id, sensor_serial, calibration_status
    INTO calibration_id, serial_value, status_value FROM node_registry WHERE node_id = 4;
  IF calibration_id <> 100 OR serial_value <> 'YF-S201-PREEXISTING' OR status_value <> 'CALIBRATED' THEN
    RAISE EXCEPTION 'pre-existing calibration state changed after migration replay';
  END IF;
  SELECT count(*) INTO constraint_count FROM pg_constraint
    WHERE conname IN ('node_registry_node_id_production_check', 'pump_commands_node_id_production_check',
      'pump_state_events_node_id_production_check', 'pump_feedback_events_node_id_production_check',
      'flow_events_node_id_production_check');
  IF constraint_count <> 5 THEN RAISE EXCEPTION 'expected exactly one production constraint per table, got %', constraint_count; END IF;
END $$;
DO $$
DECLARE constraint_count INT;
BEGIN
  SELECT count(*) INTO constraint_count
    FROM pg_constraint
   WHERE conname IN ('group_node_assignments_node_id_production_check',
                     'sensor_calibrations_node_id_production_check');
  IF constraint_count <> 2 THEN
    RAISE EXCEPTION 'expected production node constraints on both legacy tables, got %', constraint_count;
  END IF;
  IF EXISTS (SELECT 1 FROM group_node_assignments WHERE node_id > 4)
     OR EXISTS (SELECT 1 FROM sensor_calibrations WHERE node_id > 4) THEN
    RAISE EXCEPTION 'legacy node_id outside production scope remains';
  END IF;
END $$;
DO $$ BEGIN
  BEGIN
    INSERT INTO group_node_assignments (group_id, node_id, season_id) VALUES (1, 5, 1);
    RAISE EXCEPTION 'group_node_assignments accepted node_id = 5';
  EXCEPTION WHEN check_violation THEN NULL;
  END;
  BEGIN
    INSERT INTO sensor_calibrations
      (node_id, sensor_serial, version_num, pulses_per_litre, reference_volume_ml,
       trial_count, mean_pulses, variance, repeatability_pct, status)
      VALUES (5, 'invalid-node', 1, 1, 1000, 3, 1, 0, 0, 'ACTIVE');
    RAISE EXCEPTION 'sensor_calibrations accepted node_id = 5';
  EXCEPTION WHEN check_violation THEN NULL;
  END;
END $$;
INSERT INTO seasons (name, status) VALUES ('Legacy rehearsal', 'ACTIVE');
DO $$
DECLARE table_count INT; hypertable_count INT;
BEGIN
  IF (SELECT spray_day_s FROM relay_profiles WHERE relay_id = 1) <> 30
     OR (SELECT state FROM relay_events ORDER BY time LIMIT 1) <> 'ON'
     OR (SELECT value FROM sensor_readings WHERE sensor_id = 'legacy-ph') <> 6.20 THEN
    RAISE EXCEPTION 'legacy data was changed';
  END IF;
  SELECT count(*) INTO table_count FROM information_schema.tables
    WHERE table_schema = 'public' AND table_name IN ('devices','seasons','treatments','treatment_versions','timer_groups','group_treatment_assignments','group_node_assignments','sensor_calibrations','node_registry','device_status','tuya_measurement_sessions');
  IF table_count <> 11 THEN RAISE EXCEPTION 'expected 11 production regular tables, got %', table_count; END IF;
  SELECT count(*) INTO hypertable_count FROM timescaledb_information.hypertables
    WHERE hypertable_name IN ('measurement_readings','pump_commands','pump_state_events','pump_feedback_events','flow_events');
  IF hypertable_count <> 5 THEN RAISE EXCEPTION 'expected 5 hypertables, got %', hypertable_count; END IF;
END $$;

INSERT INTO treatments (name) VALUES ('Rehearsal');
INSERT INTO treatment_versions (treatment_id, version_num, spray_day_s, cooldown_day_s, spray_night_s, cooldown_night_s)
  VALUES (1, 1, 30, 300, 30, 300);
-- Calibration is fail-closed: an uncalibrated node can neither run ON nor
-- write a flow confirmation. A versioned ACTIVE calibration is traceable.
DO $$ BEGIN
  BEGIN
    INSERT INTO pump_commands (command_id, season_id, node_id, action, rf_seq)
      VALUES ('00000000-0000-0000-0000-000000000001', 1, 1, 'ON', 1);
    RAISE EXCEPTION 'uncalibrated node accepted pump ON';
  EXCEPTION WHEN raise_exception THEN
    IF position('requires an ACTIVE sensor calibration' IN SQLERRM) = 0 THEN RAISE; END IF;
  END;
  BEGIN
    INSERT INTO flow_events (season_id, node_id, sensor_calibration_id)
      VALUES (1, 1, 1);
    RAISE EXCEPTION 'uncalibrated node accepted flow event';
  EXCEPTION WHEN foreign_key_violation OR raise_exception THEN
    IF position('requires its selected ACTIVE sensor calibration' IN SQLERRM) = 0 THEN RAISE; END IF;
  END;
END $$;
INSERT INTO sensor_calibrations
  (node_id, sensor_serial, version_num, pulses_per_litre, reference_volume_ml, trial_count, mean_pulses, variance, repeatability_pct, status)
  VALUES (1, 'YF-S201-NODE-01', 1, 452.25, 1000, 3, 452.25, 0.10, 0.15, 'ACTIVE');
UPDATE node_registry
  SET sensor_serial = 'YF-S201-NODE-01', active_sensor_calibration_id = (SELECT id FROM sensor_calibrations WHERE node_id = 1 AND sensor_serial = 'YF-S201-NODE-01' AND version_num = 1), calibration_status = 'CALIBRATED'
  WHERE node_id = 1;
INSERT INTO pump_commands (command_id, season_id, node_id, action, rf_seq)
  VALUES ('00000000-0000-0000-0000-000000000002', 1, 1, 'ON', 2);
INSERT INTO flow_events (season_id, node_id, sensor_calibration_id, flow_rate_lpm)
  VALUES (1, 1, (SELECT id FROM sensor_calibrations
                 WHERE node_id = 1 AND sensor_serial = 'YF-S201-NODE-01'
                   AND version_num = 1 AND status = 'ACTIVE'), 1.25);
DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM flow_events event
    JOIN sensor_calibrations calibration ON calibration.id = event.sensor_calibration_id
    WHERE event.node_id = 1 AND calibration.sensor_serial = 'YF-S201-NODE-01'
      AND calibration.version_num = 1 AND calibration.status = 'ACTIVE'
  ) THEN RAISE EXCEPTION 'versioned calibration reference was not persisted'; END IF;
END $$;
-- A calibration becoming SUPERSEDED or REJECTED immediately invalidates ON.
-- The selected reference remains in node_registry for audit history, but it
-- must never authorize a new pump command or flow event.
UPDATE sensor_calibrations SET status = 'SUPERSEDED' WHERE node_id = 1 AND sensor_serial = 'YF-S201-NODE-01' AND version_num = 1;
DO $$ BEGIN
  BEGIN
    INSERT INTO pump_commands (command_id, season_id, node_id, action, rf_seq)
      VALUES ('00000000-0000-0000-0000-000000000003', 1, 1, 'ON', 3);
    RAISE EXCEPTION 'superseded calibration accepted pump ON';
  EXCEPTION WHEN raise_exception THEN
    IF position('requires an ACTIVE sensor calibration' IN SQLERRM) = 0 THEN RAISE; END IF;
  END;
END $$;
UPDATE sensor_calibrations SET status = 'REJECTED' WHERE node_id = 1 AND sensor_serial = 'YF-S201-NODE-01' AND version_num = 1;
DO $$ BEGIN
  BEGIN
    INSERT INTO pump_commands (command_id, season_id, node_id, action, rf_seq)
      VALUES ('00000000-0000-0000-0000-000000000004', 1, 1, 'ON', 4);
    RAISE EXCEPTION 'rejected calibration accepted pump ON';
  EXCEPTION WHEN raise_exception THEN
    IF position('requires an ACTIVE sensor calibration' IN SQLERRM) = 0 THEN RAISE; END IF;
  END;
END $$;
UPDATE sensor_calibrations SET status = 'ACTIVE' WHERE node_id = 1 AND sensor_serial = 'YF-S201-NODE-01' AND version_num = 1;
-- Task R5-M Rehearsal: Verify 4-node baseline & MEGA8 schedule/override/timestamp/analytics columns
DO $$
DECLARE
  node_count INT;
  cmd_latency INT;
  flow_count INT;
BEGIN
  -- Verify baseline 4 nodes seeded
  SELECT count(*) INTO node_count FROM node_registry WHERE node_id BETWEEN 1 AND 4;
  IF node_count < 4 THEN
    RAISE EXCEPTION 'expected at least 4 baseline nodes seeded, got %', node_count;
  END IF;

  -- Setup node 2 and 3 calibrations
  INSERT INTO sensor_calibrations
    (node_id, sensor_serial, version_num, pulses_per_litre, reference_volume_ml, trial_count, mean_pulses, variance, repeatability_pct, status)
  VALUES
    (2, 'YF-S201-NODE-02', 1, 450.00, 1000, 3, 450.00, 0.05, 0.10, 'ACTIVE'),
    (3, 'YF-S201-NODE-03', 1, 455.50, 1000, 3, 455.50, 0.08, 0.12, 'ACTIVE')
  ON CONFLICT DO NOTHING;

  UPDATE node_registry
    SET sensor_serial = 'YF-S201-NODE-02', active_sensor_calibration_id = (SELECT id FROM sensor_calibrations WHERE node_id = 2 AND version_num = 1), calibration_status = 'CALIBRATED', schedule_state = 'SPRAYING', override_state = 'NONE', last_boot_session_id = 101
    WHERE node_id = 2;

  UPDATE node_registry
    SET sensor_serial = 'YF-S201-NODE-03', active_sensor_calibration_id = (SELECT id FROM sensor_calibrations WHERE node_id = 3 AND version_num = 1), calibration_status = 'CALIBRATED', schedule_state = 'COOLING_DOWN', override_state = 'OVERRIDE_OFF', last_boot_session_id = 102
    WHERE node_id = 3;

  -- Insert comprehensive pump_commands with analytics & dual timestamps
  INSERT INTO pump_commands (
    command_id, season_id, node_id, group_id, action, rf_seq, run_lease_ms, source,
    boot_session_id, retry_count, outcome, acked_at, feedback_at, flow_confirmed_at,
    node_timestamp_ms, gateway_timestamp_ms, command_to_ack_latency_ms, flow_start_latency_ms, execution_duration_ms
  ) VALUES (
    '00000000-0000-0000-0000-000000000010', 1, 2, 1, 'ON', 10, 5000, 'MANUAL_OVERRIDE',
    101, 0, 'COMPLETED', NOW(), NOW(), NOW(),
    123456789, 123456889, 178, 400, 5000
  );

  -- Insert pump_state_events with MEGA8 schedule and override states
  INSERT INTO pump_state_events (
    season_id, node_id, group_id, desired_state, reported_state, source,
    schedule_state, override_state, resume_reason, boot_session_id, rf_seq,
    node_timestamp_ms, gateway_timestamp_ms, reason
  ) VALUES
    (1, 2, 1, 'ON', 'ON', 'MANUAL_OVERRIDE', 'SPRAYING', 'OVERRIDE_ON', 'NONE', 101, 10, 123456789, 123456889, 'Manual override active'),
    (1, 3, 2, 'OFF', 'OFF', 'MANUAL_OVERRIDE', 'COOLING_DOWN', 'OVERRIDE_OFF', 'OVERRIDE_EXPIRED', 102, 11, 123457000, 123457100, 'Override expired auto-resumed schedule');

  -- Insert pump_feedback_events with multi-tier classification & timestamps
  INSERT INTO pump_feedback_events (
    season_id, node_id, group_id, command_id, driver_feedback, load_feedback,
    driver_feedback_mismatch, fault_flags, voltage_v, current_ma,
    boot_session_id, rf_seq, node_timestamp_ms, gateway_timestamp_ms
  ) VALUES (
    1, 2, 1, '00000000-0000-0000-0000-000000000010', 'ON', 'ON',
    FALSE, 0, 12.05, 1850,
    101, 10, 123456850, 123456950
  );

  -- Insert flow_events with confirmation, volume, stability and fault code
  INSERT INTO flow_events (
    season_id, node_id, group_id, command_id, litres_total, pulse_count, flow_rate_lpm,
    delivered_volume_ml, sample_window_ms, sensor_calibration_id, flow_confirmed,
    flow_stability_pct, quality_flag, is_fault, fault_code,
    boot_session_id, rf_seq, node_timestamp_ms, gateway_timestamp_ms
  ) VALUES (
    1, 2, 1, '00000000-0000-0000-0000-000000000010', 0.208, 94, 2.50,
    208, 1000, (SELECT id FROM sensor_calibrations WHERE node_id = 2 AND version_num = 1), TRUE,
    98.50, 'OK', FALSE, 'NONE',
    101, 10, 123457200, 123457300
  );

  -- Assert analytics querying
  SELECT command_to_ack_latency_ms INTO cmd_latency
    FROM pump_commands WHERE command_id = '00000000-0000-0000-0000-000000000010';
  IF cmd_latency <> 178 THEN
    RAISE EXCEPTION 'expected command_to_ack_latency_ms = 178, got %', cmd_latency;
  END IF;

  SELECT count(*) INTO flow_count
    FROM flow_events WHERE flow_confirmed = TRUE AND delivered_volume_ml > 0;
  IF flow_count < 1 THEN
    RAISE EXCEPTION 'expected at least 1 confirmed flow event with volume';
  END IF;
END $$;

-- Partial uniqueness: two current assignments for one season/node must fail.
INSERT INTO group_node_assignments (group_id, node_id, season_id) VALUES (1, 1, 1);
DO $$ BEGIN
  BEGIN
    INSERT INTO group_node_assignments (group_id, node_id, season_id) VALUES (2, 1, 1);
    RAISE EXCEPTION 'partial unique index did not reject duplicate';
  EXCEPTION WHEN unique_violation THEN NULL;
  END;
END $$;
-- Season guard: a second active season makes un-attributed event history unsafe.
INSERT INTO seasons (name, status) VALUES ('Ambiguous', 'ACTIVE');
ALTER TABLE flow_events ALTER COLUMN season_id DROP NOT NULL;
INSERT INTO flow_events (season_id, node_id, sensor_calibration_id)
  VALUES (NULL, 1, (SELECT active_sensor_calibration_id FROM node_registry WHERE node_id = 1));
DO $$
BEGIN
  IF (SELECT count(*) FROM seasons WHERE status = 'ACTIVE') <> 2
     OR NOT EXISTS (SELECT 1 FROM flow_events WHERE season_id IS NULL) THEN
    RAISE EXCEPTION 'season-attribution safety fixture invalid';
  END IF;
END $$;
SQL

echo 'PASS disposable production migration rehearsal: legacy preserved, 11 tables, 5 hypertables, R5-M 4-node schema columns, calibration fail-closed, indexes and season guard verified'
