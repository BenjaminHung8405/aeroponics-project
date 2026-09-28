import { MigrationInterface, QueryRunner } from 'typeorm';

export class AddDeviceStatusTimeFields1726200011000 implements MigrationInterface {
  public async up(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      ALTER TABLE device_status
      ADD COLUMN IF NOT EXISTS time_source VARCHAR(16) DEFAULT NULL,
      ADD COLUMN IF NOT EXISTS last_sync_unix_time_utc BIGINT DEFAULT NULL;
    `);
  }

  public async down(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      ALTER TABLE device_status
      DROP COLUMN IF EXISTS last_sync_unix_time_utc,
      DROP COLUMN IF EXISTS time_source;
    `);
  }
}
