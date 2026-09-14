import { MigrationInterface, QueryRunner } from 'typeorm';

/**
 * Migration: AlignMeasurementReadingsSchema
 *
 * Root cause: DB was initialised from schema.sql which had stale column names
 * and was missing two columns entirely. This migration aligns the live
 * measurement_readings hypertable with the TypeORM MeasurementReading entity.
 *
 * Changes applied:
 *  1. RENAME temperature        → temperature_c   (+ precision: numeric(5,2) → numeric(4,1))
 *  2. RENAME salinity           → salinity_ppm    (+ type:      numeric(6,3) → integer)
 *  3. RENAME orp_value          → orp_mv          (type was already integer — no change)
 *  4. RENAME turbidity          → turbidity_ntu   (+ precision: numeric(8,2) → numeric(5,2))
 *  5. ADD    battery_pct        integer, nullable
 *  6. ADD    calibrated_at      timestamptz, nullable
 */
export class AlignMeasurementReadingsSchema1726200004000
  implements MigrationInterface
{
  public async up(queryRunner: QueryRunner): Promise<void> {
    // ── Step 1: Rename temperature → temperature_c ──────────────────────────
    // numeric(5,2) → numeric(4,1): safe cast, existing data rounded to 1 d.p.
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        RENAME COLUMN temperature TO temperature_c;
    `);
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        ALTER COLUMN temperature_c TYPE NUMERIC(4,1)
        USING ROUND(temperature_c::NUMERIC, 1)::NUMERIC(4,1);
    `);

    // ── Step 2: Rename salinity → salinity_ppm ──────────────────────────────
    // numeric(6,3) → integer: fractional ppt values are rounded to whole ppm.
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        RENAME COLUMN salinity TO salinity_ppm;
    `);
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        ALTER COLUMN salinity_ppm TYPE INTEGER
        USING ROUND(salinity_ppm)::INTEGER;
    `);

    // ── Step 3: Rename orp_value → orp_mv ───────────────────────────────────
    // Type (integer) is identical — rename only.
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        RENAME COLUMN orp_value TO orp_mv;
    `);

    // ── Step 4: Rename turbidity → turbidity_ntu ────────────────────────────
    // numeric(8,2) → numeric(5,2): reduces max integer digits 6→3 (NTU values
    // from PH-W218 are always < 1000 NTU, so no data loss in practice).
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        RENAME COLUMN turbidity TO turbidity_ntu;
    `);
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        ALTER COLUMN turbidity_ntu TYPE NUMERIC(5,2)
        USING turbidity_ntu::NUMERIC(5,2);
    `);

    // ── Step 5 & 6: Add missing columns ─────────────────────────────────────
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        ADD COLUMN IF NOT EXISTS battery_pct    INTEGER,
        ADD COLUMN IF NOT EXISTS calibrated_at  TIMESTAMPTZ;
    `);
  }

  public async down(queryRunner: QueryRunner): Promise<void> {
    // Remove added columns
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        DROP COLUMN IF EXISTS calibrated_at,
        DROP COLUMN IF EXISTS battery_pct;
    `);

    // Revert turbidity_ntu → turbidity (numeric(8,2))
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        ALTER COLUMN turbidity_ntu TYPE NUMERIC(8,2)
        USING turbidity_ntu::NUMERIC(8,2);
    `);
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        RENAME COLUMN turbidity_ntu TO turbidity;
    `);

    // Revert orp_mv → orp_value
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        RENAME COLUMN orp_mv TO orp_value;
    `);

    // Revert salinity_ppm → salinity (numeric(6,3))
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        ALTER COLUMN salinity_ppm TYPE NUMERIC(6,3)
        USING salinity_ppm::NUMERIC(6,3);
    `);
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        RENAME COLUMN salinity_ppm TO salinity;
    `);

    // Revert temperature_c → temperature (numeric(5,2))
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        ALTER COLUMN temperature_c TYPE NUMERIC(5,2)
        USING temperature_c::NUMERIC(5,2);
    `);
    await queryRunner.query(`
      ALTER TABLE measurement_readings
        RENAME COLUMN temperature_c TO temperature;
    `);
  }
}
