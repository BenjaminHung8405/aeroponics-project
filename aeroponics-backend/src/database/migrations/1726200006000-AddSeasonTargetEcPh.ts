import { MigrationInterface, QueryRunner } from 'typeorm';

/**
 * Migration: AddSeasonTargetEcPh
 *
 * Adds target_ec and target_ph columns to seasons table to allow specifying
 * agronomic target thresholds for electrical conductivity (EC) and pH
 * for closed-loop aeroponics monitoring and Tuya PH-W218 sensor telemetry comparison.
 */
export class AddSeasonTargetEcPh1726200006000 implements MigrationInterface {
  public async up(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      ALTER TABLE seasons
      ADD COLUMN IF NOT EXISTS target_ec NUMERIC(4,2),
      ADD COLUMN IF NOT EXISTS target_ph NUMERIC(3,2);
    `);
  }

  public async down(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      ALTER TABLE seasons
      DROP COLUMN IF EXISTS target_ec,
      DROP COLUMN IF EXISTS target_ph;
    `);
  }
}
