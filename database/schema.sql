-- Kích hoạt extension TimescaleDB
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- ============================================================================
-- PHẦN 1: BẢNG QUAN HỆ (REGULAR POSTGRESQL TABLES)
-- ============================================================================

-- 1. Device registry: ESP32 identity
CREATE TABLE IF NOT EXISTS devices (
    device_id     VARCHAR(64) PRIMARY KEY,       -- MQTT clientId, phải unique
    display_name  VARCHAR(100),
    mqtt_username VARCHAR(64) NOT NULL UNIQUE,
    enabled       BOOLEAN NOT NULL DEFAULT TRUE,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_seen_at  TIMESTAMPTZ,
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 2. Relay configuration (4 relays, mỗi relay có profile Ngày/Đêm)
CREATE TABLE IF NOT EXISTS relay_profiles (
    relay_id          SMALLINT PRIMARY KEY CHECK (relay_id BETWEEN 1 AND 4),
    display_name      VARCHAR(50) NOT NULL DEFAULT '',  -- Vd: "Khu A", "Khu B"
    spray_day_s       INT NOT NULL DEFAULT 30   CHECK (spray_day_s BETWEEN 5 AND 300),
    cooldown_day_s    INT NOT NULL DEFAULT 300  CHECK (cooldown_day_s BETWEEN 30 AND 7200),
    spray_night_s     INT NOT NULL DEFAULT 30   CHECK (spray_night_s BETWEEN 5 AND 300),
    cooldown_night_s  INT NOT NULL DEFAULT 600  CHECK (cooldown_night_s BETWEEN 30 AND 7200),
    updated_at        TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Seed dữ liệu mặc định 4 relay
INSERT INTO relay_profiles (relay_id, display_name)
VALUES (1, 'Khu A'), (2, 'Khu B'), (3, 'Khu C'), (4, 'Khu D')
ON CONFLICT DO NOTHING;

-- ============================================================================
-- PHẦN 2: BẢNG DỮ LIỆU THỜI GIAN THỰC (TIMESCALEDB HYPERTABLE)
-- ============================================================================

-- 3. Relay events (state changes từ ESP32 telemetry)
CREATE TABLE IF NOT EXISTS relay_events (
    time              TIMESTAMPTZ NOT NULL DEFAULT NOW(),  -- TimescaleDB time column
    relay_id          SMALLINT NOT NULL CHECK (relay_id BETWEEN 1 AND 4),
    device_id         VARCHAR(64) NOT NULL,
    state             VARCHAR(32) NOT NULL,   -- SPRAYING | COOLING_DOWN | MANUAL_ON | MANUAL_OFF | FLUSH
    phase_remaining_s INT,
    mode              VARCHAR(8) CHECK (mode IN ('day', 'night')),
    override_active   BOOLEAN NOT NULL DEFAULT FALSE
);

SELECT create_hypertable('relay_events', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- 4. Sensor readings từ Tuya PH-W218
CREATE TABLE IF NOT EXISTS sensor_readings (
    time        TIMESTAMPTZ NOT NULL DEFAULT NOW(),  -- TimescaleDB time column
    sensor_id   VARCHAR(64) NOT NULL,
    ph_value    NUMERIC(4,2) CHECK (ph_value IS NULL OR (ph_value BETWEEN 0.00 AND 14.00)),
    ec_value    INT,               -- µS/cm
    tds_value   INT,               -- ppm
    temperature NUMERIC(5,2),      -- °C
    salinity    NUMERIC(6,3),      -- ppt
    orp_value   INT,               -- mV
    turbidity   NUMERIC(8,2)       -- NTU
);

SELECT create_hypertable('sensor_readings', 'time',
    chunk_time_interval => INTERVAL '1 hour',
    if_not_exists => TRUE
);

-- 5. Device status (upsert từ MQTT heartbeat)
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
-- PHẦN 3: INDEXING
-- ============================================================================

-- Index relay_events: lấy trạng thái mới nhất của từng relay
CREATE INDEX IF NOT EXISTS idx_relay_events_relay_time
    ON relay_events (relay_id, time DESC);

-- Index sensor_readings: lấy reading mới nhất + query chart
CREATE INDEX IF NOT EXISTS idx_sensor_readings_sensor_time
    ON sensor_readings (sensor_id, time DESC);
