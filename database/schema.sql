-- Kích hoạt extension TimescaleDB
CREATE EXTENSION IF NOT EXISTS timescaledb;

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
    notes       TEXT
);

-- 3. Treatment profiles (Cấu hình công thức tưới)
CREATE TABLE IF NOT EXISTS treatments (
    id          SERIAL PRIMARY KEY,
    name        VARCHAR(100) NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    is_archived BOOLEAN NOT NULL DEFAULT FALSE
);

-- 4. Treatment versions (Phiên bản cấu hình spray/cooldown)
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

-- 5. Group assignments (Gán Treatment Version và Node vào 4 Timer Groups)
CREATE TABLE IF NOT EXISTS group_assignments (
    id                   SERIAL PRIMARY KEY,
    group_id             SMALLINT NOT NULL CHECK (group_id BETWEEN 1 AND 4),
    treatment_version_id INT REFERENCES treatment_versions(id),
    node_ids             INT[] NOT NULL DEFAULT '{}',
    season_id            INT REFERENCES seasons(id),
    assigned_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    active               BOOLEAN NOT NULL DEFAULT TRUE
);

-- 6. Node registry (Danh mục 12 Node RF & calibration)
CREATE TABLE IF NOT EXISTS node_registry (
    node_id                       SMALLINT PRIMARY KEY CHECK (node_id BETWEEN 1 AND 12),
    display_name                  VARCHAR(50) NOT NULL,
    group_id                      SMALLINT CHECK (group_id IS NULL OR group_id BETWEEN 1 AND 4),
    calibration_pulses_per_litre NUMERIC(10,2) NOT NULL DEFAULT 450.00,
    last_seen_at                  TIMESTAMPTZ,
    health_status                 VARCHAR(16) NOT NULL DEFAULT 'OK' CHECK (health_status IN ('OK', 'STALE', 'FAULT'))
);

-- Seed 12 nodes mặc định
INSERT INTO node_registry (node_id, display_name)
VALUES 
  (1, 'Node 01'), (2, 'Node 02'), (3, 'Node 03'), (4, 'Node 04'),
  (5, 'Node 05'), (6, 'Node 06'), (7, 'Node 07'), (8, 'Node 08'),
  (9, 'Node 09'), (10, 'Node 10'), (11, 'Node 11'), (12, 'Node 12')
ON CONFLICT DO NOTHING;

-- 7. Device status (upsert từ MQTT Gateway heartbeat)
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

-- ============================================================================
-- PHẦN 2: BẢNG DỮ LIỆU THỜI GIAN THỰC PRODUCTION (TIMESCALEDB HYPERTABLES)
-- ============================================================================

-- 8. Pump command history & RF lifecycle outcomes
CREATE TABLE IF NOT EXISTS pump_commands (
    time              TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    command_id        UUID NOT NULL,
    node_id           SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    group_id          SMALLINT CHECK (group_id BETWEEN 1 AND 4),
    action            VARCHAR(8) NOT NULL CHECK (action IN ('ON', 'OFF')),
    rf_seq            INT NOT NULL,
    outcome           VARCHAR(32) NOT NULL DEFAULT 'PENDING', -- PENDING | RF_ACKED | FLOW_CONFIRMED | FAULT_NO_ACK | FAULT_NO_FLOW | FAULT_UNEXPECTED_FLOW | TIMEOUT
    acked_at          TIMESTAMPTZ,
    flow_confirmed_at TIMESTAMPTZ,
    fault_reason      TEXT
);

SELECT create_hypertable('pump_commands', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- 9. Flow events (Dữ liệu lưu lượng & cảnh báo theo Node)
CREATE TABLE IF NOT EXISTS flow_events (
    time           TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    node_id        SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 12),
    litres_total   NUMERIC(10,3) NOT NULL DEFAULT 0.000,
    pulse_count    BIGINT NOT NULL DEFAULT 0,
    flow_rate_lpm  NUMERIC(6,2) NOT NULL DEFAULT 0.00,
    is_fault       BOOLEAN NOT NULL DEFAULT FALSE
);

SELECT create_hypertable('flow_events', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- 10. Measurement readings (Tuya PH-W218 on-demand & end-of-season)
CREATE TABLE IF NOT EXISTS measurement_readings (
    time                 TIMESTAMPTZ NOT NULL DEFAULT NOW(),
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
-- PHẦN 3: PROTOTYPE RIG COMPATIBILITY (DEPRECATED FOR PRODUCTION)
-- ============================================================================

CREATE TABLE IF NOT EXISTS relay_profiles (
    relay_id          SMALLINT PRIMARY KEY CHECK (relay_id BETWEEN 1 AND 4),
    display_name      VARCHAR(50) NOT NULL DEFAULT '',
    spray_day_s       INT NOT NULL DEFAULT 30,
    cooldown_day_s    INT NOT NULL DEFAULT 300,
    spray_night_s     INT NOT NULL DEFAULT 30,
    cooldown_night_s  INT NOT NULL DEFAULT 600,
    updated_at        TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS relay_events (
    time              TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    relay_id          SMALLINT NOT NULL CHECK (relay_id BETWEEN 1 AND 4),
    device_id         VARCHAR(64) NOT NULL,
    state             VARCHAR(32) NOT NULL,
    phase_remaining_s INT,
    mode              VARCHAR(8),
    override_active   BOOLEAN NOT NULL DEFAULT FALSE
);

SELECT create_hypertable('relay_events', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- Deprecated table for continuous sensor readings (replaced by measurement_readings)
CREATE TABLE IF NOT EXISTS sensor_readings (
    time        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    sensor_id   VARCHAR(64) NOT NULL,
    ph_value    NUMERIC(4,2),
    ec_value    INT,
    tds_value   INT,
    temperature NUMERIC(5,2),
    salinity    NUMERIC(6,3),
    orp_value   INT,
    turbidity   NUMERIC(8,2)
);

SELECT create_hypertable('sensor_readings', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- ============================================================================
-- PHẦN 4: INDEXING
-- ============================================================================

CREATE INDEX IF NOT EXISTS idx_pump_commands_node_time
    ON pump_commands (node_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_flow_events_node_time
    ON flow_events (node_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_measurement_readings_sensor_time
    ON measurement_readings (sensor_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_relay_events_relay_time
    ON relay_events (relay_id, time DESC);
