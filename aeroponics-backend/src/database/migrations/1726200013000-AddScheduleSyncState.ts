import { MigrationInterface, QueryRunner } from 'typeorm';

export class AddScheduleSyncState1726200013000 implements MigrationInterface {
  public async up(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      ALTER TABLE device_status
      ADD COLUMN IF NOT EXISTS reported_schedule_state JSONB DEFAULT NULL,
      ADD COLUMN IF NOT EXISTS schedule_sync_state VARCHAR(32) DEFAULT NULL,
      ADD COLUMN IF NOT EXISTS schedule_sync_updated_at TIMESTAMPTZ DEFAULT NULL,
      ADD COLUMN IF NOT EXISTS schedule_sync_details JSONB DEFAULT NULL;
    `);
  }

  public async down(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      ALTER TABLE device_status
      DROP COLUMN IF EXISTS schedule_sync_details,
      DROP COLUMN IF EXISTS schedule_sync_updated_at,
      DROP COLUMN IF EXISTS schedule_sync_state,
      DROP COLUMN IF EXISTS reported_schedule_state;
    `);
  }
}
