-- Kích hoạt extension TimescaleDB & pgcrypto
CREATE EXTENSION IF NOT EXISTS timescaledb;
CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- ============================================================================
-- PHẦN 1: BẢNG QUAN HỆ PRODUCTION (REGULAR POSTGRESQL TABLES)
-- ============================================================================

-- 1. Device registry: ESP32 Gateway identity
CREATE TABLE IF NOT EXISTS devices (
    device_id     VARCHAR(64) PRIMARY KEY,       -- MQTT clientId (Gateway)
    display_name  VARCHAR(100),
    mqtt_username VARCHAR(64) NOT NULL UNIQUE,
    enabled       BOOLEAN NOT NULL DEFAULT TRUE,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_seen_at  TIMESTAMPTZ,
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 2. Season management (Mùa vụ tối đa 120 ngày)
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

-- 3. Treatment profiles (Cấu hình công thức tưới)
CREATE TABLE IF NOT EXISTS treatments (
    id          SERIAL PRIMARY KEY,
    name        VARCHAR(100) NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    is_archived BOOLEAN NOT NULL DEFAULT FALSE
);

-- 4. Treatment versions (Phiên bản cấu hình spray/cooldown với lifecycle & immutability)
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

-- 5. Timer groups (4 Timer Groups cố định: Group 1 -> Group 4)
CREATE TABLE IF NOT EXISTS timer_groups (
    group_id    SMALLINT PRIMARY KEY CHECK (group_id BETWEEN 1 AND 4),
    name        VARCHAR(50) NOT NULL,
    status      VARCHAR(16) NOT NULL DEFAULT 'UNASSIGNED' CHECK (status IN ('UNASSIGNED', 'ACTIVE', 'PAUSED', 'ENDED')),
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

INSERT INTO timer_groups (group_id, name)
VALUES
  (1, 'Group 1'), (2, 'Group 2'), (3, 'Group 3'), (4, 'Group 4')
ON CONFLICT (group_id) DO NOTHING;

-- 6. Group Treatment Assignments (Lịch sử gán Treatment Version cho Timer Group)
CREATE TABLE IF NOT EXISTS group_treatment_assignments (
    id                   SERIAL PRIMARY KEY,
    group_id             SMALLINT NOT NULL REFERENCES timer_groups(group_id),
    treatment_version_id INT NOT NULL REFERENCES treatment_versions(id),
    season_id            INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    assigned_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    unassigned_at        TIMESTAMPTZ,
    active               BOOLEAN NOT NULL DEFAULT TRUE
);

-- 7. Group Node Assignments (Source of truth cho LỊCH SỬ gán Node 1..12 vào Group 1..4 theo mùa vụ)
-- PRODUCTION ACCEPTANCE SCOPE (Baseline 2026-08-22): Node IDs 1..4 only.
-- Schema allows up to 12 for future backlog; application layer MUST reject IDs > 4 in production paths.
CREATE TABLE IF NOT EXISTS group_node_assignments (
    id             SERIAL PRIMARY KEY,
    group_id       SMALLINT NOT NULL REFERENCES timer_groups(group_id),
    -- Schema capacity: 1..12. Production enforcement: application must reject node_id > 4.
    node_id        SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 4), -- Production scope: 1..4; backlog nodes require a separate schema.
    season_id      INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    effective_from TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    effective_to   TIMESTAMPTZ,
    active         BOOLEAN NOT NULL DEFAULT TRUE,
    CONSTRAINT group_node_assignment_lifecycle_check
        CHECK ((active AND effective_to IS NULL) OR (NOT active AND effective_to IS NOT NULL))
);

-- 8. Sensor Calibrations (Bảng quản lý phiên bản hiệu chuẩn cảm biến theo Serial & Node)
-- PRODUCTION ACCEPTANCE SCOPE (Baseline 2026-08-22): Node IDs 1..4 only. Schema capacity: 1..12.
CREATE TABLE IF NOT EXISTS sensor_calibrations (
    id                   SERIAL PRIMARY KEY,
    -- Schema capacity: 1..12. Production enforcement: application must reject node_id > 4.
    node_id              SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 4), -- Production scope: 1..4; backlog nodes require a separate schema.
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

-- 9. Node registry. A node is explicitly UNCALIBRATED until an audited ACTIVE
-- calibration for its physical sensor serial is selected below.
-- Baseline 2026-08-22: 4 active remote nodes (Node 01 .. Node 04) with autonomous MEGA8 schedule.
-- PRODUCTION ACCEPTANCE SCOPE: Node IDs 1..4 only. Schema capacity up to 12 (backlog).
CREATE TABLE IF NOT EXISTS node_registry (
    -- Schema capacity: 1..12. Production enforcement: application must reject node_id > 4.
    node_id                      SMALLINT PRIMARY KEY CHECK (node_id BETWEEN 1 AND 4), -- Production scope: 1..4; backlog nodes require a separate schema.
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

-- Seed 4 primary nodes for baseline 2026-08-22 (support up to 12)
INSERT INTO node_registry (node_id, display_name)
VALUES
  (1, 'Node 01'), (2, 'Node 02'), (3, 'Node 03'), (4, 'Node 04')
ON CONFLICT (node_id) DO NOTHING;

-- 10. Device status (upsert từ MQTT Gateway heartbeat)
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

-- 11. Tuya Measurement Sessions (Audit session đo PH-W218 on-demand & end-of-season, KHÔNG poll 10s)
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

-- ============================================================================
-- PHẦN 2: BẢNG DỮ LIỆU THỜI GIAN THỰC PRODUCTION (TIMESCALEDB HYPERTABLES)
-- ============================================================================

-- 12. Pump command history & RF lifecycle outcomes
CREATE TABLE IF NOT EXISTS pump_commands (
    time                       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    command_id                 UUID NOT NULL,
    season_id                  INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    node_id                    SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 4), -- Production scope: 1..4; backlog nodes require a separate schema.
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

SELECT create_hypertable('pump_commands', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- 13. Pump state events (Desired, reported state, MEGA8 schedule state & temporary override audit)
CREATE TABLE IF NOT EXISTS pump_state_events (
    time                 TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    season_id            INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    node_id              SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 4), -- Production scope: 1..4; backlog nodes require a separate schema.
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

SELECT create_hypertable('pump_state_events', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- 14. Pump feedback events (Driver & load feedback từ node phần cứng)
CREATE TABLE IF NOT EXISTS pump_feedback_events (
    time                     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    season_id                INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    node_id                  SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 4), -- Production scope: 1..4; backlog nodes require a separate schema.
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

SELECT create_hypertable('pump_feedback_events', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- 15. Flow events. An event is accepted only with the exact approved
-- calibration ID selected for the reporting node; no common fallback exists.
CREATE TABLE IF NOT EXISTS flow_events (
    time                  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    season_id             INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    node_id               SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 4), -- Production scope: 1..4; backlog nodes require a separate schema.
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

SELECT create_hypertable('flow_events', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

CREATE OR REPLACE FUNCTION assert_node_active_calibration() RETURNS TRIGGER AS $$
BEGIN
    IF NEW.calibration_status = 'UNCALIBRATED' THEN
        RETURN NEW;
    END IF;
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

CREATE TRIGGER trg_pump_commands_require_active_calibration
BEFORE INSERT OR UPDATE OF action, node_id ON pump_commands
FOR EACH ROW EXECUTE FUNCTION assert_pump_on_calibration();

-- 15. Measurement readings (Tuya PH-W218 on-demand & end-of-season)
-- Column names align with TypeORM MeasurementReading entity & migration 1726200004000.
CREATE TABLE IF NOT EXISTS measurement_readings (
    time                 TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    session_id           UUID REFERENCES tuya_measurement_sessions(session_id) ON DELETE SET NULL,
    sensor_id            VARCHAR(64) NOT NULL DEFAULT 'ph-w218-01',
    trigger_type         VARCHAR(32) NOT NULL DEFAULT 'ON_DEMAND' CHECK (trigger_type IN ('ON_DEMAND', 'END_OF_SEASON')),
    ph_value             NUMERIC(4,2) CHECK (ph_value IS NULL OR (ph_value BETWEEN 0.00 AND 14.00)),
    ec_value             INT,               -- µS/cm
    tds_value            INT,               -- ppm
    temperature_c        NUMERIC(4,1),      -- °C  (renamed from temperature)
    salinity_ppm         INT,               -- ppm (renamed from salinity, type: numeric→int)
    orp_mv               INT,               -- mV  (renamed from orp_value)
    turbidity_ntu        NUMERIC(5,2),      -- NTU (renamed from turbidity)
    battery_pct          INT,               -- % battery
    calibrated_at        TIMESTAMPTZ,       -- last calibration timestamp
    triggered_by_user_id VARCHAR(64)
);

SELECT create_hypertable('measurement_readings', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- ============================================================================
-- PHẦN 3: INDEXING
-- ============================================================================

CREATE INDEX IF NOT EXISTS idx_group_node_assignments_group_active
    ON group_node_assignments (group_id, active, effective_from DESC);

CREATE INDEX IF NOT EXISTS idx_group_node_assignments_node_active
    ON group_node_assignments (node_id, active, effective_from DESC);

CREATE UNIQUE INDEX IF NOT EXISTS uq_group_node_assignments_one_current_node
    ON group_node_assignments (season_id, node_id)
    WHERE active AND effective_to IS NULL;

CREATE UNIQUE INDEX IF NOT EXISTS uq_group_treatment_assignments_one_current_group
    ON group_treatment_assignments (season_id, group_id)
    WHERE active AND unassigned_at IS NULL;

CREATE INDEX IF NOT EXISTS idx_pump_commands_season_node_time
    ON pump_commands (season_id, node_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_pump_commands_command_id
    ON pump_commands (command_id);

CREATE INDEX IF NOT EXISTS idx_pump_state_events_season_node_time
    ON pump_state_events (season_id, node_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_pump_feedback_events_season_node_time
    ON pump_feedback_events (season_id, node_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_flow_events_season_node_time
    ON flow_events (season_id, node_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_flow_events_command_id
    ON flow_events (command_id);

CREATE INDEX IF NOT EXISTS idx_measurement_readings_sensor_time
    ON measurement_readings (sensor_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_tuya_sessions_season
    ON tuya_measurement_sessions (season_id, started_at DESC);

-- ============================================================================
-- PHẦN 4: ANALYTICS & REPORTING VIEWS (SPEC-TELEMETRY-ANALYTICS-001)
-- ============================================================================

-- 1. Command Performance & Latency Analytics View
CREATE OR REPLACE VIEW v_command_performance_analytics AS
SELECT
    c.season_id,
    c.node_id,
    COUNT(*) AS total_commands,
    COUNT(*) FILTER (WHERE c.action = 'ON') AS total_on_commands,
    COUNT(*) FILTER (WHERE c.acked_at IS NOT NULL) AS acked_commands,
    COUNT(*) FILTER (WHERE c.outcome = 'FLOW_CONFIRMED') AS flow_confirmed_commands,
    ROUND(
        (COUNT(*) FILTER (WHERE c.outcome = 'FLOW_CONFIRMED')::NUMERIC /
         NULLIF(COUNT(*) FILTER (WHERE c.action = 'ON'), 0)) * 100.0, 2
    ) AS confirmation_rate_pct,
    ROUND(AVG(c.command_to_ack_latency_ms)::NUMERIC, 1) AS avg_cmd_to_ack_latency_ms,
    PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY c.command_to_ack_latency_ms) AS p95_cmd_to_ack_latency_ms,
    ROUND(AVG(c.flow_start_latency_ms)::NUMERIC, 1) AS avg_flow_start_latency_ms,
    SUM(c.retry_count) AS total_retries,
    COUNT(*) FILTER (WHERE c.outcome IN ('FAULT_TIMEOUT', 'FAULT_NO_ACK')) AS timeout_count
FROM pump_commands c
GROUP BY c.season_id, c.node_id;

-- 2. Flow Stability & Volume Delivery Analytics View
CREATE OR REPLACE VIEW v_flow_stability_and_volume_analytics AS
SELECT
    f.season_id,
    f.node_id,
    COUNT(*) AS total_flow_events,
    ROUND(SUM(f.delivered_volume_ml)::NUMERIC / 1000.0, 3) AS total_delivered_litres,
    ROUND(AVG(f.flow_rate_lpm)::NUMERIC, 2) AS avg_flow_rate_lpm,
    ROUND(AVG(f.flow_stability_pct)::NUMERIC, 2) AS avg_flow_stability_pct,
    COUNT(*) FILTER (WHERE f.flow_confirmed = TRUE) AS flow_confirmed_count,
    COUNT(*) FILTER (WHERE f.is_fault = TRUE) AS fault_event_count
FROM flow_events f
GROUP BY f.season_id, f.node_id;

-- 3. Schedule vs Override Mismatch Analytics View
CREATE OR REPLACE VIEW v_schedule_override_mismatch_analytics AS
SELECT
    s.season_id,
    s.node_id,
    COUNT(*) AS total_state_events,
    COUNT(*) FILTER (WHERE s.override_state != 'NONE') AS override_events_count,
    COUNT(*) FILTER (WHERE s.override_state = 'OVERRIDE_OFF') AS override_off_count,
    COUNT(*) FILTER (WHERE s.override_state = 'OVERRIDE_ON') AS override_on_count,
    COUNT(*) FILTER (WHERE s.resume_reason = 'OVERRIDE_EXPIRED') AS expired_resumes_count,
    COUNT(*) FILTER (WHERE s.resume_reason = 'CYCLE_BOUNDARY') AS cycle_boundary_resumes_count
FROM pump_state_events s
GROUP BY s.season_id, s.node_id;
