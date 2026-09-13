import { MigrationInterface, QueryRunner } from 'typeorm';

export class EnhanceConstraintsAndEnums1726200001000 implements MigrationInterface {
  public async up(queryRunner: QueryRunner): Promise<void> {
    // 1. Enforce single active season at the database level via partial unique index
    await queryRunner.query(`
      CREATE UNIQUE INDEX IF NOT EXISTS uq_seasons_one_active
      ON seasons (status)
      WHERE status = 'ACTIVE';
    `);

    // 2. Enhance node_registry health_status check constraint to include SAFE_OFF
    await queryRunner.query(`
      ALTER TABLE node_registry DROP CONSTRAINT IF EXISTS node_registry_health_status_check;
      ALTER TABLE node_registry
      ADD CONSTRAINT node_registry_health_status_check
      CHECK (health_status IN ('OK', 'STALE', 'FAULT', 'SAFE_OFF'));
    `);

    // 3. Ensure sensor_calibrations pulses_per_litre has NUMERIC(10,4) precision
    await queryRunner.query(`
      ALTER TABLE sensor_calibrations
      ALTER COLUMN pulses_per_litre TYPE NUMERIC(10,4);
    `);

    // 4. Ensure TimescaleDB hypertable unique constraint on (command_id, time)
    await queryRunner.query(`
      CREATE UNIQUE INDEX IF NOT EXISTS uq_pump_commands_id_time
      ON pump_commands (command_id, time);
    `);
  }

  public async down(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`DROP INDEX IF EXISTS uq_pump_commands_id_time;`);
    await queryRunner.query(`
      ALTER TABLE sensor_calibrations
      ALTER COLUMN pulses_per_litre TYPE NUMERIC(10,2);
    `);
    await queryRunner.query(`
      ALTER TABLE node_registry DROP CONSTRAINT IF EXISTS node_registry_health_status_check;
      ALTER TABLE node_registry
      ADD CONSTRAINT node_registry_health_status_check
      CHECK (health_status IN ('OK', 'STALE', 'FAULT'));
    `);
    await queryRunner.query(`DROP INDEX IF EXISTS uq_seasons_one_active;`);
  }
}
