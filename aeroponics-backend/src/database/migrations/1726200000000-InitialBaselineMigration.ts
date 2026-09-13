import { MigrationInterface, QueryRunner } from 'typeorm';

export class InitialBaselineMigration1726200000000 implements MigrationInterface {
  public async up(queryRunner: QueryRunner): Promise<void> {
    // 1. Extensions
    await queryRunner.query(`CREATE EXTENSION IF NOT EXISTS timescaledb;`);
    await queryRunner.query(`CREATE EXTENSION IF NOT EXISTS pgcrypto;`);

    // 2. Devices table
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS devices (
        device_id     VARCHAR(64) PRIMARY KEY,
        display_name  VARCHAR(100),
        mqtt_username VARCHAR(64) NOT NULL UNIQUE,
        enabled       BOOLEAN NOT NULL DEFAULT TRUE,
        created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        last_seen_at  TIMESTAMPTZ,
        updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
      );
    `);

    // 3. Seasons table
    await queryRunner.query(`
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
    `);

    // 4. Treatments table
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS treatments (
        id          SERIAL PRIMARY KEY,
        name        VARCHAR(100) NOT NULL,
        created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        is_archived BOOLEAN NOT NULL DEFAULT FALSE
      );
    `);

    // 5. Treatment versions
    await queryRunner.query(`
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
    `);

    // Immutability Trigger
    await queryRunner.query(`
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
    `);

    await queryRunner.query(`
      DROP TRIGGER IF EXISTS trg_treatment_version_immutable ON treatment_versions;
      CREATE TRIGGER trg_treatment_version_immutable
      BEFORE UPDATE ON treatment_versions
      FOR EACH ROW EXECUTE FUNCTION enforce_treatment_version_immutable();
    `);

    // 6. Timer Groups
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS timer_groups (
        group_id    SMALLINT PRIMARY KEY CHECK (group_id BETWEEN 1 AND 4),
        name        VARCHAR(50) NOT NULL,
        status      VARCHAR(16) NOT NULL DEFAULT 'UNASSIGNED' CHECK (status IN ('UNASSIGNED', 'ACTIVE', 'PAUSED', 'ENDED')),
        created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
      );
    `);

    await queryRunner.query(`
      INSERT INTO timer_groups (group_id, name)
      VALUES (1, 'Group 1'), (2, 'Group 2'), (3, 'Group 3'), (4, 'Group 4')
      ON CONFLICT (group_id) DO NOTHING;
    `);

    // 7. Group Treatment Assignments
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS group_treatment_assignments (
        id                   SERIAL PRIMARY KEY,
        group_id             SMALLINT NOT NULL REFERENCES timer_groups(group_id),
        treatment_version_id INT NOT NULL REFERENCES treatment_versions(id),
        season_id            INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
        assigned_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        unassigned_at        TIMESTAMPTZ,
        active               BOOLEAN NOT NULL DEFAULT TRUE
      );
    `);

    // 8. Group Node Assignments
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS group_node_assignments (
        id             SERIAL PRIMARY KEY,
        group_id       SMALLINT NOT NULL REFERENCES timer_groups(group_id),
        node_id        SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 4),
        season_id      INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
        effective_from TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        effective_to   TIMESTAMPTZ,
        active         BOOLEAN NOT NULL DEFAULT TRUE,
        CONSTRAINT group_node_assignment_lifecycle_check
            CHECK ((active AND effective_to IS NULL) OR (NOT active AND effective_to IS NOT NULL))
      );
    `);

    // 9. Sensor Calibrations
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS sensor_calibrations (
        id                   SERIAL PRIMARY KEY,
        node_id              SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 4),
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
    `);

    // 10. Node Registry
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS node_registry (
        node_id                      SMALLINT PRIMARY KEY CHECK (node_id BETWEEN 1 AND 4),
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
    `);

    await queryRunner.query(`
      INSERT INTO node_registry (node_id, display_name)
      VALUES (1, 'Node 01'), (2, 'Node 02'), (3, 'Node 03'), (4, 'Node 04')
      ON CONFLICT (node_id) DO NOTHING;
    `);

    // 11. Device Status
    await queryRunner.query(`
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
    `);

    // 12. Tuya Measurement Sessions
    await queryRunner.query(`
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
    `);

    // 13. Pump Commands
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS pump_commands (
        time                       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        command_id                 UUID NOT NULL,
        season_id                  INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
        node_id                    SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 4),
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
    `);

    // 14. Pump State Events
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS pump_state_events (
        time                 TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        season_id            INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
        node_id              SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 4),
        group_id             SMALLINT CHECK (group_id BETWEEN 1 AND 4),
        desired_state        VARCHAR(8) NOT NULL CHECK (desired_state IN ('ON', 'OFF')),
        reported_state       VARCHAR(8) NOT NULL CHECK (reported_state IN ('ON', 'OFF')),
        source               VARCHAR(32) NOT NULL DEFAULT 'SCHEDULE'
            CHECK (source IN ('SCHEDULE', 'MANUAL_OVERRIDE', 'FAIL_SAFE', 'MANUAL')),
        schedule_state       VARCHAR(16) NOT NULL DEFAULT 'UNKNOWN'
            CHECK (schedule_state IN ('UNKNOWN', 'SPRAYING', 'COOLING_DOWN', 'IDLE', 'PAUSED')),
        override_state       VARCHAR(16) NOT NULL DEFAULT 'NONE'
            CHECK (override_state IN ('NONE', 'OVERRIDE_OFF', 'OVERRIDE_ON')),
        driver_feedback      SMALLINT,
        load_feedback        SMALLINT,
        current_ma           INT,
        flow_confirmed       BOOLEAN NOT NULL DEFAULT FALSE,
        fault_flags          INT NOT NULL DEFAULT 0,
        command_id           UUID,
        node_timestamp_ms    BIGINT,
        gateway_timestamp_ms BIGINT
      );
    `);

    // 15. Flow Events
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS flow_events (
        time                 TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        season_id            INT NOT NULL REFERENCES seasons(id) ON DELETE RESTRICT,
        node_id              SMALLINT NOT NULL CHECK (node_id BETWEEN 1 AND 4),
        command_id           UUID,
        flow_lpm             NUMERIC(6,3) NOT NULL,
        delivered_volume_ml  INT NOT NULL,
        pulse_count          INT NOT NULL,
        calibration_version  INT,
        pulses_per_litre     NUMERIC(10,2),
        is_fault             BOOLEAN NOT NULL DEFAULT FALSE,
        fault_type           VARCHAR(32)
      );
    `);

    // 16. Measurement Readings (Tuya on-demand)
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS measurement_readings (
        time                  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        session_id            UUID REFERENCES tuya_measurement_sessions(session_id) ON DELETE SET NULL,
        sensor_id             VARCHAR(64) NOT NULL DEFAULT 'ph-w218-01',
        trigger_type          VARCHAR(32) NOT NULL DEFAULT 'ON_DEMAND'
            CHECK (trigger_type IN ('ON_DEMAND', 'END_OF_SEASON')),
        ph_value              NUMERIC(4,2),
        ec_value              INT,
        tds_value             INT,
        temperature_c         NUMERIC(4,1),
        salinity_ppm          INT,
        orp_mv                INT,
        turbidity_ntu         NUMERIC(5,2),
        battery_pct           INT,
        calibrated_at         TIMESTAMPTZ,
        triggered_by_user_id  VARCHAR(64)
      );
    `);

    // Try creating hypertables if timescaledb is available
    await queryRunner.query(`
      DO $$
      BEGIN
        IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'timescaledb') THEN
          PERFORM create_hypertable('pump_commands', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);
          PERFORM create_hypertable('pump_state_events', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);
          PERFORM create_hypertable('flow_events', 'time', chunk_time_interval => INTERVAL '1 day', if_not_exists => TRUE);
          PERFORM create_hypertable('measurement_readings', 'time', chunk_time_interval => INTERVAL '7 days', if_not_exists => TRUE);
        END IF;
      END $$;
    `);
  }

  public async down(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`DROP TABLE IF EXISTS measurement_readings CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS flow_events CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS pump_state_events CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS pump_commands CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS tuya_measurement_sessions CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS device_status CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS node_registry CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS sensor_calibrations CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS group_node_assignments CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS group_treatment_assignments CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS timer_groups CASCADE;`);
    await queryRunner.query(`DROP TRIGGER IF EXISTS trg_treatment_version_immutable ON treatment_versions;`);
    await queryRunner.query(`DROP FUNCTION IF EXISTS enforce_treatment_version_immutable();`);
    await queryRunner.query(`DROP TABLE IF EXISTS treatment_versions CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS treatments CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS seasons CASCADE;`);
    await queryRunner.query(`DROP TABLE IF EXISTS devices CASCADE;`);
  }
}
