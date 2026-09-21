import { MigrationInterface, QueryRunner } from 'typeorm';

export class AddRfDiscoveryMetadata1726200008000 implements MigrationInterface {
  async up(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      ALTER TABLE node_registry
        ADD COLUMN IF NOT EXISTS rf_protocol varchar(24),
        ADD COLUMN IF NOT EXISTS last_scan_id varchar(96),
        ADD COLUMN IF NOT EXISTS last_rf_rtt_ms integer,
        ADD COLUMN IF NOT EXISTS last_discovered_at timestamptz,
        ADD COLUMN IF NOT EXISTS discovery_status varchar(24)
    `);
  }

  async down(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      ALTER TABLE node_registry
        DROP COLUMN IF EXISTS discovery_status,
        DROP COLUMN IF EXISTS last_discovered_at,
        DROP COLUMN IF EXISTS last_rf_rtt_ms,
        DROP COLUMN IF EXISTS last_scan_id,
        DROP COLUMN IF EXISTS rf_protocol
    `);
  }
}
