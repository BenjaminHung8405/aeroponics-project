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

-- 8. Sensor Calibrations (Bảng quản lý phiên bản hiệu chuẩn cảm biến theo Serial & Node)
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

-- 9. Node registry (Danh mục 12 Node RF & calibration cache)
CREATE TABLE IF NOT EXISTS node_registry (
    node_id                       SMALLINT PRIMARY KEY CHECK (node_id BETWEEN 1 AND 12),
    display_name                  VARCHAR(50) NOT NULL,
    cached_group_id               SMALLINT CHECK (cached_group_id IS NULL OR cached_group_id BETWEEN 1 AND 4),
    sensor_serial                 VARCHAR(64) DEFAULT 'YF-S201-DEFAULT',
    calibration_version           INT NOT NULL DEFAULT 1,
    calibration_pulses_per_litre NUMERIC(10,2) NOT NULL DEFAULT 450.00 CHECK (calibration_pulses_per_litre > 0),
    last_seen_at                  TIMESTAMPTZ,
    health_status                 VARCHAR(16) NOT NULL DEFAULT 'OK' CHECK (health_status IN ('OK', 'STALE', 'FAULT')),
    created_at                    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at                    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Seed 12 nodes mặc định
INSERT INTO node_registry (node_id, display_name)
VALUES
  (1, 'Node 01'), (2, 'Node 02'), (3, 'Node 03'), (4, 'Node 04'),
  (5, 'Node 05'), (6, 'Node 06'), (7, 'Node 07'), (8, 'Node 08'),
  (9, 'Node 09'), (10, 'Node 10'), (11, 'Node 11'), (12, 'Node 12')
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
    time                 TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    command_id           UUID NOT NULL,
    season_id            INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
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

SELECT create_hypertable('pump_commands', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- 13. Pump state events (Desired, reported state & fail-safe audit)
CREATE TABLE IF NOT EXISTS pump_state_events (
    time           TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    season_id      INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    node_id        SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    group_id       SMALLINT CHECK (group_id BETWEEN 1 AND 4),
    desired_state  VARCHAR(8) NOT NULL CHECK (desired_state IN ('ON', 'OFF')),
    reported_state VARCHAR(8) NOT NULL CHECK (reported_state IN ('ON', 'OFF')),
    source         VARCHAR(16) NOT NULL DEFAULT 'SCHEDULE' CHECK (source IN ('SCHEDULE', 'MANUAL', 'FAIL_SAFE')),
    reason         TEXT
);

SELECT create_hypertable('pump_state_events', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- 14. Pump feedback events (Driver & load feedback từ node phần cứng)
CREATE TABLE IF NOT EXISTS pump_feedback_events (
    time            TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    season_id       INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    node_id         SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    driver_feedback VARCHAR(8) NOT NULL CHECK (driver_feedback IN ('ON', 'OFF')),
    load_feedback   VARCHAR(8) NOT NULL DEFAULT 'UNKNOWN' CHECK (load_feedback IN ('ON', 'OFF', 'UNKNOWN')),
    voltage_v       NUMERIC(6,2),
    current_ma      INT
);

SELECT create_hypertable('pump_feedback_events', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- 15. Flow events (Dữ liệu lưu lượng & cảnh báo định lượng theo Node, max 6 L/min)
CREATE TABLE IF NOT EXISTS flow_events (
    time                 TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    season_id            INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
    node_id              SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    group_id             SMALLINT CHECK (group_id BETWEEN 1 AND 4),
    litres_total         NUMERIC(10,3) NOT NULL DEFAULT 0.000,
    pulse_count          BIGINT NOT NULL DEFAULT 0,
    flow_rate_lpm        NUMERIC(6,2) NOT NULL DEFAULT 0.00 CHECK (flow_rate_lpm BETWEEN 0 AND 6),
    sample_window_ms     INT NOT NULL DEFAULT 1000 CHECK (sample_window_ms > 0),
    pulses_per_litre     NUMERIC(10,2) NOT NULL DEFAULT 450.00 CHECK (pulses_per_litre > 0),
    calibration_version INT NOT NULL DEFAULT 1,
    quality_flag         VARCHAR(16) NOT NULL DEFAULT 'OK',
    is_fault             BOOLEAN NOT NULL DEFAULT FALSE
);

SELECT create_hypertable('flow_events', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- 15. Measurement readings (Tuya PH-W218 on-demand & end-of-season)
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

CREATE INDEX IF NOT EXISTS idx_pump_state_events_season_node_time
    ON pump_state_events (season_id, node_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_pump_feedback_events_season_node_time
    ON pump_feedback_events (season_id, node_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_flow_events_season_node_time
    ON flow_events (season_id, node_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_measurement_readings_sensor_time
    ON measurement_readings (sensor_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_tuya_sessions_season
    ON tuya_measurement_sessions (season_id, started_at DESC);
