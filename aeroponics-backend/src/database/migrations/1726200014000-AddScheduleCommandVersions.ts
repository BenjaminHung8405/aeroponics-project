import { MigrationInterface, QueryRunner } from 'typeorm';

export class AddScheduleCommandVersions1726200014000 implements MigrationInterface {
  public async up(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      ALTER TABLE device_status
      ADD COLUMN IF NOT EXISTS assignment_config_version BIGINT NOT NULL DEFAULT 0,
      ADD COLUMN IF NOT EXISTS command_envelope_version BIGINT NOT NULL DEFAULT 0;
    `);
  }

  public async down(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      ALTER TABLE device_status
      DROP COLUMN IF EXISTS command_envelope_version,
      DROP COLUMN IF EXISTS assignment_config_version;
    `);
  }
}
