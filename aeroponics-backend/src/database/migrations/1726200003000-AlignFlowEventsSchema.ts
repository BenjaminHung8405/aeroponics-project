import { MigrationInterface, QueryRunner } from 'typeorm';

export class AlignFlowEventsSchema1726200003000 implements MigrationInterface {
  public async up(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      ALTER TABLE flow_events
      ADD COLUMN IF NOT EXISTS group_id SMALLINT CHECK (group_id BETWEEN 1 AND 4),
      ADD COLUMN IF NOT EXISTS litres_total NUMERIC(10,3) NOT NULL DEFAULT 0.000,
      ADD COLUMN IF NOT EXISTS flow_rate_lpm NUMERIC(6,2) NOT NULL DEFAULT 0.00,
      ADD COLUMN IF NOT EXISTS sample_window_ms INT NOT NULL DEFAULT 1000,
      ADD COLUMN IF NOT EXISTS sensor_calibration_id INT REFERENCES sensor_calibrations(id) ON DELETE RESTRICT,
      ADD COLUMN IF NOT EXISTS flow_confirmed BOOLEAN NOT NULL DEFAULT FALSE,
      ADD COLUMN IF NOT EXISTS flow_stability_pct NUMERIC(5,2),
      ADD COLUMN IF NOT EXISTS quality_flag VARCHAR(16) NOT NULL DEFAULT 'OK',
      ADD COLUMN IF NOT EXISTS fault_code VARCHAR(32) NOT NULL DEFAULT 'NONE',
      ADD COLUMN IF NOT EXISTS boot_session_id INT,
      ADD COLUMN IF NOT EXISTS rf_seq INT,
      ADD COLUMN IF NOT EXISTS node_timestamp_ms BIGINT,
      ADD COLUMN IF NOT EXISTS gateway_timestamp_ms BIGINT;
    `);

    // Ensure composite index exists for time-series range queries on (season_id, node_id, time DESC)
    await queryRunner.query(`
      CREATE INDEX IF NOT EXISTS idx_flow_events_season_node_time
      ON flow_events (season_id, node_id, time DESC);
    `);

    // Ensure index on command_id exists
    await queryRunner.query(`
      CREATE INDEX IF NOT EXISTS idx_flow_events_command_id
      ON flow_events (command_id);
    `);
  }

  public async down(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`DROP INDEX IF EXISTS idx_flow_events_command_id;`);
    await queryRunner.query(`DROP INDEX IF EXISTS idx_flow_events_season_node_time;`);
    await queryRunner.query(`
      ALTER TABLE flow_events
      DROP COLUMN IF EXISTS gateway_timestamp_ms,
      DROP COLUMN IF EXISTS node_timestamp_ms,
      DROP COLUMN IF EXISTS rf_seq,
      DROP COLUMN IF EXISTS boot_session_id,
      DROP COLUMN IF EXISTS fault_code,
      DROP COLUMN IF EXISTS quality_flag,
      DROP COLUMN IF EXISTS flow_stability_pct,
      DROP COLUMN IF EXISTS flow_confirmed,
      DROP COLUMN IF EXISTS sensor_calibration_id,
      DROP COLUMN IF EXISTS sample_window_ms,
      DROP COLUMN IF EXISTS flow_rate_lpm,
      DROP COLUMN IF EXISTS litres_total,
      DROP COLUMN IF EXISTS group_id;
    `);
  }
}
