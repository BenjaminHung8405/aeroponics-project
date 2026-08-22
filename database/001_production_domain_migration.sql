-- ============================================================================
-- Migration: 001_production_domain_migration.sql
-- Description: Idempotent migration script for Aeroponics Lean production domain
-- ============================================================================

CREATE EXTENSION IF NOT EXISTS timescaledb;
CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- 1. Devices table (ESP32 Gateway identity)
CREATE TABLE IF NOT EXISTS devices (
    device_id     VARCHAR(64) PRIMARY KEY,
    display_name  VARCHAR(100),
    mqtt_username VARCHAR(64) NOT NULL UNIQUE,
    enabled       BOOLEAN NOT NULL DEFAULT TRUE,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_seen_at  TIMESTAMPTZ,
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 2. Seasons table
CREATE TABLE IF NOT EXISTS seasons (
    id          SERIAL PRIMARY KEY,
    name        VARCHAR(100) NOT NULL,
    started_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    ended_at    TIMESTAMPTZ,
    status      VARCHAR(16) NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE', 'ENDED')),
    notes       TEXT,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 3. Treatments & Versions
CREATE TABLE IF NOT EXISTS treatments (
    id          SERIAL PRIMARY KEY,
    name        VARCHAR(100) NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    is_archived BOOLEAN NOT NULL DEFAULT FALSE
);

CREATE TABLE IF NOT EXISTS treatment_versions (
    id               SERIAL PRIMARY KEY,
    treatment_id     INT NOT NULL REFERENCES treatments(id) ON DELETE CASCADE,
    version_num      INT NOT NULL DEFAULT 1,
    status           VARCHAR(16) NOT NULL DEFAULT 'DRAFT' CHECK (status IN ('DRAFT', 'PUBLISHED', 'ARCHIVED')),
    spray_day_s      INT NOT NULL CHECK (spray_day_s BETWEEN 5 AND 300),
    cooldown_day_s   INT NOT NULL CHECK (cooldown_day_s BETWEEN 30 AND 7200),
    spray_night_s    INT NOT NULL CHECK (spray_night_s BETWEEN 5 AND 300),
    cooldown_night_s INT NOT NULL CHECK (cooldown_night_s BETWEEN 30 AND 7200),
    created_by       VARCHAR(100),
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    published_at     TIMESTAMPTZ,
    CONSTRAINT uq_treatment_version UNIQUE (treatment_id, version_num)
);

ALTER TABLE treatment_versions ADD COLUMN IF NOT EXISTS status VARCHAR(16) NOT NULL DEFAULT 'DRAFT';
ALTER TABLE treatment_versions ADD COLUMN IF NOT EXISTS created_by VARCHAR(100);

CREATE OR REPLACE FUNCTION enforce_treatment_version_immutable()
RETURNS TRIGGER AS $$
BEGIN
    IF OLD.status = 'PUBLISHED' THEN
        IF NEW.treatment_id IS DISTINCT FROM OLD.treatment_id OR NEW.version_num IS DISTINCT FROM OLD.version_num OR
           NEW.spray_day_s IS DISTINCT FROM OLD.spray_day_s OR NEW.cooldown_day_s IS DISTINCT FROM OLD.cooldown_day_s OR
           NEW.spray_night_s IS DISTINCT FROM OLD.spray_night_s OR NEW.cooldown_night_s IS DISTINCT FROM OLD.cooldown_night_s OR
           NEW.created_by IS DISTINCT FROM OLD.created_by OR NEW.created_at IS DISTINCT FROM OLD.created_at THEN
            RAISE EXCEPTION 'Published treatment versions are immutable; create a new version.';
        END IF;
        IF NEW.status NOT IN ('PUBLISHED', 'ARCHIVED') THEN
            RAISE EXCEPTION 'Invalid published treatment lifecycle transition.';
        END IF;
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_treatment_version_immutable ON treatment_versions;
CREATE TRIGGER trg_treatment_version_immutable
BEFORE UPDATE ON treatment_versions
FOR EACH ROW EXECUTE FUNCTION enforce_treatment_version_immutable();

-- 4. Timer groups
CREATE TABLE IF NOT EXISTS timer_groups (
    group_id    SMALLINT PRIMARY KEY CHECK (group_id BETWEEN 1 AND 4),
    name        VARCHAR(50) NOT NULL,
    status      VARCHAR(16) NOT NULL DEFAULT 'UNASSIGNED' CHECK (status IN ('UNASSIGNED', 'ACTIVE', 'PAUSED', 'ENDED')),
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

INSERT INTO timer_groups (group_id, name)
VALUES (1, 'Group 1'), (2, 'Group 2'), (3, 'Group 3'), (4, 'Group 4')
ON CONFLICT (group_id) DO NOTHING;

-- 5. Group Treatment Assignments
CREATE TABLE IF NOT EXISTS group_treatment_assignments (
    id                   SERIAL PRIMARY KEY,
    group_id             SMALLINT NOT NULL REFERENCES timer_groups(group_id),
    treatment_version_id INT NOT NULL REFERENCES treatment_versions(id),
    season_id            INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    assigned_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    unassigned_at        TIMESTAMPTZ,
    active               BOOLEAN NOT NULL DEFAULT TRUE
);

-- 6. Group Node Assignments
CREATE TABLE IF NOT EXISTS group_node_assignments (
    id             SERIAL PRIMARY KEY,
    group_id       SMALLINT NOT NULL REFERENCES timer_groups(group_id),
    node_id        SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    season_id      INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    effective_from TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    effective_to   TIMESTAMPTZ,
    active         BOOLEAN NOT NULL DEFAULT TRUE,
    CONSTRAINT group_node_assignment_lifecycle_check
        CHECK ((active AND effective_to IS NULL) OR (NOT active AND effective_to IS NOT NULL))
);

-- 7. Sensor Calibrations
CREATE TABLE IF NOT EXISTS sensor_calibrations (
    id                   SERIAL PRIMARY KEY,
    node_id              SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    sensor_serial        VARCHAR(64) NOT NULL,
    version_num          INT NOT NULL DEFAULT 1,
    pulses_per_litre     NUMERIC(10,2) NOT NULL CHECK (pulses_per_litre > 0),
    reference_volume_ml  INT NOT NULL CHECK (reference_volume_ml > 0),
    trial_count          INT NOT NULL DEFAULT 3 CHECK (trial_count >= 3),
    mean_pulses          NUMERIC(10,2) NOT NULL,
    variance             NUMERIC(10,4) NOT NULL DEFAULT 0.0,
    repeatability_pct    NUMERIC(5,2) NOT NULL CHECK (repeatability_pct <= 5.00),
    operating_conditions JSONB,
    status               VARCHAR(16) NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('DRAFT', 'ACTIVE', 'SUPERSEDED', 'REJECTED')),
    calibrated_by        VARCHAR(100),
    calibrated_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT uq_sensor_calibration UNIQUE (node_id, sensor_serial, version_num)
);

-- 8. Node registry. Existing nodes begin UNCALIBRATED until an audited ACTIVE
-- calibration for their real sensor serial is explicitly selected.
-- Baseline 2026-08-22: 4 active remote nodes (Node 01 .. Node 04) with autonomous MEGA8 schedule.
CREATE TABLE IF NOT EXISTS node_registry (
    node_id                      SMALLINT PRIMARY KEY CHECK (node_id BETWEEN 1 AND 12),
    display_name                 VARCHAR(50) NOT NULL,
    cached_group_id              SMALLINT CHECK (cached_group_id IS NULL OR cached_group_id BETWEEN 1 AND 4),
    sensor_serial                VARCHAR(64),
    active_sensor_calibration_id INT REFERENCES sensor_calibrations(id) ON DELETE RESTRICT,
    calibration_status           VARCHAR(16) NOT NULL DEFAULT 'UNCALIBRATED'
        CHECK (calibration_status IN ('UNCALIBRATED', 'CALIBRATED')),
    schedule_state               VARCHAR(16) NOT NULL DEFAULT 'UNKNOWN'
        CHECK (schedule_state IN ('UNKNOWN', 'SPRAYING', 'COOLING_DOWN', 'IDLE', 'PAUSED')),
    override_state               VARCHAR(16) NOT NULL DEFAULT 'NONE'
        CHECK (override_state IN ('NONE', 'OVERRIDE_OFF', 'OVERRIDE_ON')),
    last_boot_session_id         INT,
    last_seen_at                 TIMESTAMPTZ,
    health_status                VARCHAR(16) NOT NULL DEFAULT 'OK' CHECK (health_status IN ('OK', 'STALE', 'FAULT')),
    created_at                   TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at                   TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT node_registry_calibration_state_check CHECK (
        (calibration_status = 'UNCALIBRATED' AND active_sensor_calibration_id IS NULL)
        OR (calibration_status = 'CALIBRATED' AND active_sensor_calibration_id IS NOT NULL)
    )
);

ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS cached_group_id SMALLINT CHECK (cached_group_id IS NULL OR cached_group_id BETWEEN 1 AND 4);
ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS sensor_serial VARCHAR(64);
ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS active_sensor_calibration_id INT REFERENCES sensor_calibrations(id) ON DELETE RESTRICT;
ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS calibration_status VARCHAR(16) NOT NULL DEFAULT 'UNCALIBRATED'
    CHECK (calibration_status IN ('UNCALIBRATED', 'CALIBRATED'));
ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS schedule_state VARCHAR(16) NOT NULL DEFAULT 'UNKNOWN'
    CHECK (schedule_state IN ('UNKNOWN', 'SPRAYING', 'COOLING_DOWN', 'IDLE', 'PAUSED'));
ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS override_state VARCHAR(16) NOT NULL DEFAULT 'NONE'
    CHECK (override_state IN ('NONE', 'OVERRIDE_OFF', 'OVERRIDE_ON'));
ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS last_boot_session_id INT;
ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS created_at TIMESTAMPTZ NOT NULL DEFAULT NOW();
ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW();
ALTER TABLE node_registry ALTER COLUMN sensor_serial DROP DEFAULT;
ALTER TABLE node_registry ALTER COLUMN sensor_serial DROP NOT NULL;
UPDATE node_registry SET sensor_serial = NULL
WHERE sensor_serial = 'YF-S201-DEFAULT';
UPDATE node_registry SET active_sensor_calibration_id = NULL, calibration_status = 'UNCALIBRATED';
ALTER TABLE node_registry DROP COLUMN IF EXISTS calibration_version;
ALTER TABLE node_registry DROP COLUMN IF EXISTS calibration_pulses_per_litre;
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'node_registry'::regclass
          AND conname = 'node_registry_calibration_state_check'
    ) THEN
        ALTER TABLE node_registry ADD CONSTRAINT node_registry_calibration_state_check CHECK (
            (calibration_status = 'UNCALIBRATED' AND active_sensor_calibration_id IS NULL)
            OR (calibration_status = 'CALIBRATED' AND active_sensor_calibration_id IS NOT NULL)
        );
    END IF;
END $$;

-- Seed 4 primary nodes for baseline 2026-08-22
INSERT INTO node_registry (node_id, display_name)
VALUES
  (1, 'Node 01'), (2, 'Node 02'), (3, 'Node 03'), (4, 'Node 04')
ON CONFLICT (node_id) DO NOTHING;

-- 8. Device status table
CREATE TABLE IF NOT EXISTS device_status (
    device_id    VARCHAR(64) PRIMARY KEY,
    status       VARCHAR(16) NOT NULL DEFAULT 'offline',
    uptime_s     BIGINT DEFAULT 0,
    rssi_dbm     SMALLINT,
    free_heap_b  INT,
    ntp_synced   BOOLEAN DEFAULT FALSE,
    rtc_valid    BOOLEAN DEFAULT FALSE,
    last_seen_at TIMESTAMPTZ
);

-- 9. Tuya Measurement Sessions
CREATE TABLE IF NOT EXISTS tuya_measurement_sessions (
    session_id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    sensor_id            VARCHAR(64) NOT NULL DEFAULT 'ph-w218-01',
    trigger_type         VARCHAR(32) NOT NULL DEFAULT 'ON_DEMAND' CHECK (trigger_type IN ('ON_DEMAND', 'END_OF_SEASON')),
    season_id            INT REFERENCES seasons(id) ON DELETE SET NULL,
    triggered_by_user_id VARCHAR(64),
    status               VARCHAR(16) NOT NULL DEFAULT 'PENDING' CHECK (status IN ('PENDING', 'COMPLETED', 'FAILED')),
    started_at           TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    completed_at         TIMESTAMPTZ,
    error_message        TEXT
);

-- 10. Measurement readings hypertable
CREATE TABLE IF NOT EXISTS measurement_readings (
    time                 TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    session_id           UUID REFERENCES tuya_measurement_sessions(session_id) ON DELETE SET NULL,
    sensor_id            VARCHAR(64) NOT NULL DEFAULT 'ph-w218-01',
    trigger_type         VARCHAR(32) NOT NULL DEFAULT 'ON_DEMAND' CHECK (trigger_type IN ('ON_DEMAND', 'END_OF_SEASON')),
    ph_value             NUMERIC(4,2) CHECK (ph_value IS NULL OR (ph_value BETWEEN 0.00 AND 14.00)),
    ec_value             INT,               -- µS/cm
    tds_value            INT,               -- ppm
    temperature          NUMERIC(5,2),      -- °C
    salinity             NUMERIC(6,3),      -- ppt
    orp_value            INT,               -- mV
    turbidity            NUMERIC(8,2),      -- NTU
    triggered_by_user_id VARCHAR(64)
);

SELECT create_hypertable('measurement_readings', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);

ALTER TABLE measurement_readings ADD COLUMN IF NOT EXISTS session_id UUID REFERENCES tuya_measurement_sessions(session_id) ON DELETE SET NULL;
ALTER TABLE measurement_readings ADD COLUMN IF NOT EXISTS trigger_type VARCHAR(32) NOT NULL DEFAULT 'ON_DEMAND' CHECK (trigger_type IN ('ON_DEMAND', 'END_OF_SEASON'));

-- 11. Pump commands hypertable
CREATE TABLE IF NOT EXISTS pump_commands (
    time                       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    command_id                 UUID NOT NULL,
    season_id                  INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    node_id                    SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    group_id                   SMALLINT CHECK (group_id BETWEEN 1 AND 4),
    treatment_version_id       INT,
    action                     VARCHAR(8) NOT NULL CHECK (action IN ('ON', 'OFF')),
    rf_seq                     INT NOT NULL,
    run_lease_ms               INT NOT NULL DEFAULT 30000,
    source                     VARCHAR(32) NOT NULL DEFAULT 'MANUAL_OVERRIDE'
        CHECK (source IN ('MANUAL_OVERRIDE', 'FAIL_SAFE', 'MANUAL', 'SCHEDULE')),
    boot_session_id            INT,
    retry_count                INT NOT NULL DEFAULT 0,
    outcome                    VARCHAR(32) NOT NULL DEFAULT 'PENDING',
    acked_at                   TIMESTAMPTZ,
    feedback_at                TIMESTAMPTZ,
    flow_confirmed_at          TIMESTAMPTZ,
    node_timestamp_ms          BIGINT,
    gateway_timestamp_ms       BIGINT,
    command_to_ack_latency_ms  INT,
    flow_start_latency_ms      INT,
    execution_duration_ms      INT,
    fault_reason               TEXT
);

SELECT create_hypertable('pump_commands', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);

ALTER TABLE pump_commands ADD COLUMN IF NOT EXISTS source VARCHAR(32) NOT NULL DEFAULT 'MANUAL_OVERRIDE';
ALTER TABLE pump_commands ADD COLUMN IF NOT EXISTS boot_session_id INT;
ALTER TABLE pump_commands ADD COLUMN IF NOT EXISTS retry_count INT NOT NULL DEFAULT 0;
ALTER TABLE pump_commands ADD COLUMN IF NOT EXISTS node_timestamp_ms BIGINT;
ALTER TABLE pump_commands ADD COLUMN IF NOT EXISTS gateway_timestamp_ms BIGINT;
ALTER TABLE pump_commands ADD COLUMN IF NOT EXISTS command_to_ack_latency_ms INT;
ALTER TABLE pump_commands ADD COLUMN IF NOT EXISTS flow_start_latency_ms INT;
ALTER TABLE pump_commands ADD COLUMN IF NOT EXISTS execution_duration_ms INT;

-- 12. Pump state & feedback events hypertables
CREATE TABLE IF NOT EXISTS pump_state_events (
    time                 TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    season_id            INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    node_id              SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    group_id             SMALLINT CHECK (group_id BETWEEN 1 AND 4),
    desired_state        VARCHAR(8) NOT NULL CHECK (desired_state IN ('ON', 'OFF')),
    reported_state       VARCHAR(8) NOT NULL CHECK (reported_state IN ('ON', 'OFF')),
    source               VARCHAR(32) NOT NULL DEFAULT 'SCHEDULE'
        CHECK (source IN ('SCHEDULE', 'MANUAL_OVERRIDE', 'FAIL_SAFE', 'MANUAL')),
    schedule_state       VARCHAR(16) NOT NULL DEFAULT 'UNKNOWN'
        CHECK (schedule_state IN ('UNKNOWN', 'SPRAYING', 'COOLING_DOWN', 'IDLE', 'PAUSED')),
    override_state       VARCHAR(16) NOT NULL DEFAULT 'NONE'
        CHECK (override_state IN ('NONE', 'OVERRIDE_OFF', 'OVERRIDE_ON')),
    resume_reason        VARCHAR(32) NOT NULL DEFAULT 'NONE'
        CHECK (resume_reason IN ('NONE', 'OVERRIDE_EXPIRED', 'CYCLE_BOUNDARY', 'MANUAL_RESUME', 'FAIL_SAFE_RESUME')),
    boot_session_id      INT,
    rf_seq               INT,
    node_timestamp_ms    BIGINT,
    gateway_timestamp_ms BIGINT,
    reason               TEXT
);

SELECT create_hypertable('pump_state_events', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);

ALTER TABLE pump_state_events ADD COLUMN IF NOT EXISTS schedule_state VARCHAR(16) NOT NULL DEFAULT 'UNKNOWN';
ALTER TABLE pump_state_events ADD COLUMN IF NOT EXISTS override_state VARCHAR(16) NOT NULL DEFAULT 'NONE';
ALTER TABLE pump_state_events ADD COLUMN IF NOT EXISTS resume_reason VARCHAR(32) NOT NULL DEFAULT 'NONE';
ALTER TABLE pump_state_events ADD COLUMN IF NOT EXISTS boot_session_id INT;
ALTER TABLE pump_state_events ADD COLUMN IF NOT EXISTS rf_seq INT;
ALTER TABLE pump_state_events ADD COLUMN IF NOT EXISTS node_timestamp_ms BIGINT;
ALTER TABLE pump_state_events ADD COLUMN IF NOT EXISTS gateway_timestamp_ms BIGINT;

CREATE TABLE IF NOT EXISTS pump_feedback_events (
    time                     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    season_id                INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    node_id                  SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    group_id                 SMALLINT CHECK (group_id IS NULL OR group_id BETWEEN 1 AND 4),
    command_id               UUID,
    driver_feedback          VARCHAR(8) NOT NULL CHECK (driver_feedback IN ('ON', 'OFF')),
    load_feedback            VARCHAR(8) NOT NULL DEFAULT 'UNKNOWN' CHECK (load_feedback IN ('ON', 'OFF', 'UNKNOWN')),
    driver_feedback_mismatch BOOLEAN NOT NULL DEFAULT FALSE,
    fault_flags              INT NOT NULL DEFAULT 0,
    voltage_v                NUMERIC(6,2),
    current_ma               INT,
    boot_session_id          INT,
    rf_seq                   INT,
    node_timestamp_ms        BIGINT,
    gateway_timestamp_ms     BIGINT
);

SELECT create_hypertable('pump_feedback_events', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);

ALTER TABLE pump_feedback_events ADD COLUMN IF NOT EXISTS group_id SMALLINT CHECK (group_id IS NULL OR group_id BETWEEN 1 AND 4);
ALTER TABLE pump_feedback_events ADD COLUMN IF NOT EXISTS command_id UUID;
ALTER TABLE pump_feedback_events ADD COLUMN IF NOT EXISTS driver_feedback_mismatch BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE pump_feedback_events ADD COLUMN IF NOT EXISTS fault_flags INT NOT NULL DEFAULT 0;
ALTER TABLE pump_feedback_events ADD COLUMN IF NOT EXISTS boot_session_id INT;
ALTER TABLE pump_feedback_events ADD COLUMN IF NOT EXISTS rf_seq INT;
ALTER TABLE pump_feedback_events ADD COLUMN IF NOT EXISTS node_timestamp_ms BIGINT;
ALTER TABLE pump_feedback_events ADD COLUMN IF NOT EXISTS gateway_timestamp_ms BIGINT;

-- 13. Flow events hypertable. Flow must reference the active, approved
-- calibration rather than a universal coefficient.
CREATE TABLE IF NOT EXISTS flow_events (
    time                  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    season_id             INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    node_id               SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    group_id              SMALLINT CHECK (group_id BETWEEN 1 AND 4),
    command_id            UUID,
    litres_total          NUMERIC(10,3) NOT NULL DEFAULT 0.000,
    pulse_count           BIGINT NOT NULL DEFAULT 0,
    flow_rate_lpm         NUMERIC(6,2) NOT NULL DEFAULT 0.00 CHECK (flow_rate_lpm BETWEEN 0 AND 6),
    delivered_volume_ml   INT NOT NULL DEFAULT 0,
    sample_window_ms      INT NOT NULL DEFAULT 1000 CHECK (sample_window_ms > 0),
    sensor_calibration_id INT NOT NULL REFERENCES sensor_calibrations(id) ON DELETE RESTRICT,
    flow_confirmed        BOOLEAN NOT NULL DEFAULT FALSE,
    flow_stability_pct    NUMERIC(5,2),
    quality_flag          VARCHAR(16) NOT NULL DEFAULT 'OK',
    is_fault              BOOLEAN NOT NULL DEFAULT FALSE,
    fault_code            VARCHAR(32) NOT NULL DEFAULT 'NONE'
        CHECK (fault_code IN ('NONE', 'NO_FLOW_FAULT', 'UNEXPECTED_FLOW_FAULT', 'OVER_RANGE_FAULT', 'SENSOR_FAULT')),
    boot_session_id       INT,
    rf_seq                INT,
    node_timestamp_ms     BIGINT,
    gateway_timestamp_ms  BIGINT
);

SELECT create_hypertable('flow_events', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);

ALTER TABLE flow_events ADD COLUMN IF NOT EXISTS command_id UUID;
ALTER TABLE flow_events ADD COLUMN IF NOT EXISTS delivered_volume_ml INT NOT NULL DEFAULT 0;
ALTER TABLE flow_events ADD COLUMN IF NOT EXISTS flow_confirmed BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE flow_events ADD COLUMN IF NOT EXISTS flow_stability_pct NUMERIC(5,2);
ALTER TABLE flow_events ADD COLUMN IF NOT EXISTS fault_code VARCHAR(32) NOT NULL DEFAULT 'NONE';
ALTER TABLE flow_events ADD COLUMN IF NOT EXISTS boot_session_id INT;
ALTER TABLE flow_events ADD COLUMN IF NOT EXISTS rf_seq INT;
ALTER TABLE flow_events ADD COLUMN IF NOT EXISTS node_timestamp_ms BIGINT;
ALTER TABLE flow_events ADD COLUMN IF NOT EXISTS gateway_timestamp_ms BIGINT;

ALTER TABLE flow_events ADD COLUMN IF NOT EXISTS sensor_calibration_id INT REFERENCES sensor_calibrations(id) ON DELETE RESTRICT;
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM flow_events WHERE sensor_calibration_id IS NULL) THEN
        RAISE EXCEPTION 'migration aborted: flow events without an audited calibration reference require resolution';
    END IF;
END $$;
ALTER TABLE flow_events ALTER COLUMN sensor_calibration_id SET NOT NULL;
ALTER TABLE flow_events DROP COLUMN IF EXISTS calibration_version;
ALTER TABLE flow_events DROP COLUMN IF EXISTS pulses_per_litre;

CREATE OR REPLACE FUNCTION assert_node_active_calibration() RETURNS TRIGGER AS $$
BEGIN
    IF NEW.calibration_status = 'UNCALIBRATED' THEN RETURN NEW; END IF;
    IF NEW.sensor_serial IS NULL OR NOT EXISTS (
        SELECT 1 FROM sensor_calibrations calibration
        WHERE calibration.id = NEW.active_sensor_calibration_id
          AND calibration.node_id = NEW.node_id
          AND calibration.sensor_serial = NEW.sensor_serial
          AND calibration.status = 'ACTIVE'
    ) THEN
        RAISE EXCEPTION 'node % requires an ACTIVE calibration for its own sensor serial', NEW.node_id;
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_node_registry_active_calibration ON node_registry;
CREATE TRIGGER trg_node_registry_active_calibration
BEFORE INSERT OR UPDATE OF sensor_serial, active_sensor_calibration_id, calibration_status
ON node_registry FOR EACH ROW EXECUTE FUNCTION assert_node_active_calibration();

CREATE OR REPLACE FUNCTION assert_flow_event_calibration() RETURNS TRIGGER AS $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM node_registry node
        JOIN sensor_calibrations calibration ON calibration.id = NEW.sensor_calibration_id
        WHERE node.node_id = NEW.node_id
          AND node.calibration_status = 'CALIBRATED'
          AND node.active_sensor_calibration_id = calibration.id
          AND calibration.node_id = NEW.node_id
          AND calibration.sensor_serial = node.sensor_serial
          AND calibration.status = 'ACTIVE'
    ) THEN
        RAISE EXCEPTION 'flow event for node % requires its selected ACTIVE sensor calibration', NEW.node_id;
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_flow_events_require_active_calibration ON flow_events;
CREATE TRIGGER trg_flow_events_require_active_calibration
BEFORE INSERT OR UPDATE OF node_id, sensor_calibration_id
ON flow_events FOR EACH ROW EXECUTE FUNCTION assert_flow_event_calibration();

CREATE OR REPLACE FUNCTION assert_pump_on_calibration() RETURNS TRIGGER AS $$
BEGIN
    IF NEW.action = 'ON' AND NOT EXISTS (
        SELECT 1 FROM node_registry node
        JOIN sensor_calibrations calibration
          ON calibration.id = node.active_sensor_calibration_id
        WHERE node.node_id = NEW.node_id
          AND node.calibration_status = 'CALIBRATED'
          AND calibration.node_id = NEW.node_id
          AND calibration.sensor_serial = node.sensor_serial
          AND calibration.status = 'ACTIVE'
    ) THEN
        RAISE EXCEPTION 'pump ON for node % requires an ACTIVE sensor calibration', NEW.node_id;
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_pump_commands_require_active_calibration ON pump_commands;
CREATE TRIGGER trg_pump_commands_require_active_calibration
BEFORE INSERT OR UPDATE OF action, node_id ON pump_commands
FOR EACH ROW EXECUTE FUNCTION assert_pump_on_calibration();

-- Existing deployments may not infer an event's season safely. Backfill only
-- when exactly one active season exists; otherwise abort for audited resolution.
DO $$
DECLARE
    event_table TEXT;
    active_season INT;
    has_unassigned_events BOOLEAN;
BEGIN
    SELECT id INTO active_season FROM seasons WHERE status = 'ACTIVE';
    IF (SELECT count(*) FROM seasons WHERE status = 'ACTIVE') <> 1 THEN
        FOREACH event_table IN ARRAY ARRAY['pump_commands', 'pump_state_events', 'pump_feedback_events', 'flow_events'] LOOP
            EXECUTE format('ALTER TABLE %I ADD COLUMN IF NOT EXISTS season_id INT', event_table);
            EXECUTE format('SELECT EXISTS (SELECT 1 FROM %I WHERE season_id IS NULL)', event_table)
                INTO has_unassigned_events;
            IF has_unassigned_events THEN
                RAISE EXCEPTION 'migration aborted: cannot infer season_id for %; resolve event history first', event_table;
            END IF;
        END LOOP;
    ELSE
        FOREACH event_table IN ARRAY ARRAY['pump_commands', 'pump_state_events', 'pump_feedback_events', 'flow_events'] LOOP
            EXECUTE format('ALTER TABLE %I ADD COLUMN IF NOT EXISTS season_id INT', event_table);
            EXECUTE format('UPDATE %I SET season_id = $1 WHERE season_id IS NULL', event_table) USING active_season;
        END LOOP;
    END IF;
    FOREACH event_table IN ARRAY ARRAY['pump_commands', 'pump_state_events', 'pump_feedback_events', 'flow_events'] LOOP
        EXECUTE format('ALTER TABLE %I ALTER COLUMN season_id SET NOT NULL', event_table);
        EXECUTE format('ALTER TABLE %I DROP CONSTRAINT IF EXISTS %I', event_table, event_table || '_season_id_fkey');
        EXECUTE format('ALTER TABLE %I ADD CONSTRAINT %I FOREIGN KEY (season_id) REFERENCES seasons(id) ON DELETE RESTRICT',
                       event_table, event_table || '_season_id_fkey');
    END LOOP;
END $$;

DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM group_treatment_assignments WHERE season_id IS NULL)
       OR EXISTS (SELECT 1 FROM group_node_assignments WHERE season_id IS NULL) THEN
        RAISE EXCEPTION 'migration aborted: assignment history without season_id requires audited resolution';
    END IF;
    IF EXISTS (SELECT 1 FROM group_treatment_assignments WHERE active AND unassigned_at IS NULL
               GROUP BY season_id, group_id HAVING count(*) > 1) THEN
        RAISE EXCEPTION 'migration aborted: duplicate current treatment assignments require audited resolution';
    END IF;
    IF EXISTS (SELECT 1 FROM group_node_assignments WHERE active AND effective_to IS NULL
               GROUP BY season_id, node_id HAVING count(*) > 1) THEN
        RAISE EXCEPTION 'migration aborted: duplicate current node assignments require audited resolution';
    END IF;
END $$;

ALTER TABLE group_treatment_assignments ALTER COLUMN season_id SET NOT NULL;
ALTER TABLE group_node_assignments ALTER COLUMN season_id SET NOT NULL;

-- Indexes
CREATE INDEX IF NOT EXISTS idx_group_node_assignments_group_active ON group_node_assignments (group_id, active, effective_from DESC);
CREATE INDEX IF NOT EXISTS idx_group_node_assignments_node_active ON group_node_assignments (node_id, active, effective_from DESC);
DO $$
BEGIN
    IF EXISTS (
        SELECT 1 FROM group_node_assignments
        WHERE active AND effective_to IS NULL
        GROUP BY season_id, node_id HAVING count(*) > 1
    ) THEN
        RAISE EXCEPTION 'migration aborted: duplicate active group_node_assignments require audited resolution';
    END IF;
END $$;
CREATE UNIQUE INDEX IF NOT EXISTS uq_group_node_assignments_one_current_node
    ON group_node_assignments (season_id, node_id) WHERE active AND effective_to IS NULL;
CREATE UNIQUE INDEX IF NOT EXISTS uq_group_treatment_assignments_one_current_group
    ON group_treatment_assignments (season_id, group_id) WHERE active AND unassigned_at IS NULL;
CREATE INDEX IF NOT EXISTS idx_pump_commands_season_node_time ON pump_commands (season_id, node_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_pump_commands_command_id ON pump_commands (command_id);
CREATE INDEX IF NOT EXISTS idx_pump_state_events_season_node_time ON pump_state_events (season_id, node_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_pump_feedback_events_season_node_time ON pump_feedback_events (season_id, node_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_flow_events_season_node_time ON flow_events (season_id, node_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_flow_events_command_id ON flow_events (command_id);
CREATE INDEX IF NOT EXISTS idx_measurement_readings_sensor_time ON measurement_readings (sensor_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_tuya_sessions_season ON tuya_measurement_sessions (season_id, started_at DESC);
