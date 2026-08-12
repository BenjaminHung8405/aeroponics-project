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
    spray_day_s      INT NOT NULL CHECK (spray_day_s BETWEEN 5 AND 300),
    cooldown_day_s   INT NOT NULL CHECK (cooldown_day_s BETWEEN 30 AND 7200),
    spray_night_s    INT NOT NULL CHECK (spray_night_s BETWEEN 5 AND 300),
    cooldown_night_s INT NOT NULL CHECK (cooldown_night_s BETWEEN 30 AND 7200),
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    published_at     TIMESTAMPTZ
);

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
    season_id            INT REFERENCES seasons(id) ON DELETE CASCADE,
    assigned_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    unassigned_at        TIMESTAMPTZ,
    active               BOOLEAN NOT NULL DEFAULT TRUE
);

-- 6. Group Node Assignments
CREATE TABLE IF NOT EXISTS group_node_assignments (
    id             SERIAL PRIMARY KEY,
    group_id       SMALLINT NOT NULL REFERENCES timer_groups(group_id),
    node_id        SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    season_id      INT REFERENCES seasons(id) ON DELETE CASCADE,
    effective_from TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    effective_to   TIMESTAMPTZ,
    active         BOOLEAN NOT NULL DEFAULT TRUE
);

-- 7. Node registry
CREATE TABLE IF NOT EXISTS node_registry (
    node_id                       SMALLINT PRIMARY KEY CHECK (node_id BETWEEN 1 AND 12),
    display_name                  VARCHAR(50) NOT NULL,
    cached_group_id               SMALLINT CHECK (cached_group_id IS NULL OR cached_group_id BETWEEN 1 AND 4),
    calibration_pulses_per_litre NUMERIC(10,2) NOT NULL DEFAULT 450.00,
    calibration_version           INT NOT NULL DEFAULT 1,
    last_seen_at                  TIMESTAMPTZ,
    health_status                 VARCHAR(16) NOT NULL DEFAULT 'OK' CHECK (health_status IN ('OK', 'STALE', 'FAULT')),
    created_at                    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at                    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS cached_group_id SMALLINT CHECK (cached_group_id IS NULL OR cached_group_id BETWEEN 1 AND 4);
ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS calibration_version INT NOT NULL DEFAULT 1;
ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS created_at TIMESTAMPTZ NOT NULL DEFAULT NOW();
ALTER TABLE node_registry ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW();

INSERT INTO node_registry (node_id, display_name)
VALUES
  (1, 'Node 01'), (2, 'Node 02'), (3, 'Node 03'), (4, 'Node 04'),
  (5, 'Node 05'), (6, 'Node 06'), (7, 'Node 07'), (8, 'Node 08'),
  (9, 'Node 09'), (10, 'Node 10'), (11, 'Node 11'), (12, 'Node 12')
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
    time                 TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    command_id           UUID NOT NULL,
    node_id              SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    group_id             SMALLINT CHECK (group_id BETWEEN 1 AND 4),
    treatment_version_id INT,
    action               VARCHAR(8) NOT NULL CHECK (action IN ('ON', 'OFF')),
    rf_seq               INT NOT NULL,
    run_lease_ms         INT NOT NULL DEFAULT 30000,
    outcome              VARCHAR(32) NOT NULL DEFAULT 'PENDING',
    acked_at             TIMESTAMPTZ,
    feedback_at          TIMESTAMPTZ,
    flow_confirmed_at    TIMESTAMPTZ,
    fault_reason         TEXT
);

SELECT create_hypertable('pump_commands', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);

-- 12. Pump state & feedback events hypertables
CREATE TABLE IF NOT EXISTS pump_state_events (
    time           TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    node_id        SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    group_id       SMALLINT CHECK (group_id BETWEEN 1 AND 4),
    desired_state  VARCHAR(8) NOT NULL CHECK (desired_state IN ('ON', 'OFF')),
    reported_state VARCHAR(8) NOT NULL CHECK (reported_state IN ('ON', 'OFF')),
    source         VARCHAR(16) NOT NULL DEFAULT 'SCHEDULE' CHECK (source IN ('SCHEDULE', 'MANUAL', 'FAIL_SAFE')),
    reason         TEXT
);

SELECT create_hypertable('pump_state_events', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);

CREATE TABLE IF NOT EXISTS pump_feedback_events (
    time            TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    node_id         SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    driver_feedback VARCHAR(8) NOT NULL CHECK (driver_feedback IN ('ON', 'OFF')),
    load_feedback   VARCHAR(8) NOT NULL DEFAULT 'UNKNOWN' CHECK (load_feedback IN ('ON', 'OFF', 'UNKNOWN')),
    voltage_v       NUMERIC(6,2),
    current_ma      INT
);

SELECT create_hypertable('pump_feedback_events', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);

-- 13. Flow events hypertable
CREATE TABLE IF NOT EXISTS flow_events (
    time                 TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    node_id              SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    group_id             SMALLINT CHECK (group_id BETWEEN 1 AND 4),
    litres_total         NUMERIC(10,3) NOT NULL DEFAULT 0.000,
    pulse_count          BIGINT NOT NULL DEFAULT 0,
    flow_rate_lpm        NUMERIC(6,2) NOT NULL DEFAULT 0.00,
    sample_window_ms     INT NOT NULL DEFAULT 1000,
    pulses_per_litre     NUMERIC(10,2) NOT NULL DEFAULT 450.00,
    calibration_version INT NOT NULL DEFAULT 1,
    quality_flag         VARCHAR(16) NOT NULL DEFAULT 'OK',
    is_fault             BOOLEAN NOT NULL DEFAULT FALSE
);

SELECT create_hypertable('flow_events', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_group_node_assignments_group_active ON group_node_assignments (group_id, active, effective_from DESC);
CREATE INDEX IF NOT EXISTS idx_group_node_assignments_node_active ON group_node_assignments (node_id, active, effective_from DESC);
CREATE INDEX IF NOT EXISTS idx_pump_commands_node_time ON pump_commands (node_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_pump_state_events_node_time ON pump_state_events (node_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_pump_feedback_events_node_time ON pump_feedback_events (node_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_flow_events_node_time ON flow_events (node_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_measurement_readings_sensor_time ON measurement_readings (sensor_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_tuya_sessions_season ON tuya_measurement_sessions (season_id, started_at DESC);
