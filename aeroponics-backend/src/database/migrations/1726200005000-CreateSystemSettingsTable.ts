import { MigrationInterface, QueryRunner } from 'typeorm';

/**
 * Migration: CreateSystemSettingsTable
 *
 * Provides persistent key-value configuration storage for dynamic system flags,
 * primarily enabling/disabling the Tuya Water Quality Bridge (PH-W218 8-in-1 sensor)
 * at runtime to preserve sensor probe longevity until late-season/harvest experiments.
 */
export class CreateSystemSettingsTable1726200005000
  implements MigrationInterface
{
  public async up(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS system_settings (
        key         VARCHAR(64) PRIMARY KEY,
        value       JSONB NOT NULL,
        description TEXT,
        updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        updated_by  VARCHAR(64)
      );
    `);

    await queryRunner.query(`
      INSERT INTO system_settings (key, value, description)
      VALUES (
        'tuya_bridge_enabled',
        '{"enabled": false, "reason": "Bảo vệ đầu dò cảm biến pH/EC/ORP cho thí nghiệm/vụ cuối"}'::jsonb,
        'Trạng thái kích hoạt Tuya PH-W218 Bridge'
      )
      ON CONFLICT (key) DO NOTHING;
    `);
  }

  public async down(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      DROP TABLE IF EXISTS system_settings;
    `);
  }
}
