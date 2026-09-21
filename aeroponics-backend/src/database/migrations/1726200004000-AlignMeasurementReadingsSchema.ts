import { MigrationInterface, QueryRunner } from "typeorm";

/**
 * Migration: AlignMeasurementReadingsSchema
 *
 * This migration upgrades installations created from the legacy SQL baseline.
 * New installations are created by InitialBaselineMigration with the target
 * column names already in place, so this migration must be a no-op there.
 *
 * Changes applied:
 *  1. RENAME temperature        → temperature_c   (+ precision: numeric(5,2) → numeric(4,1))
 *  2. RENAME salinity           → salinity_ppm    (+ type:      numeric(6,3) → integer)
 *  3. RENAME orp_value          → orp_mv          (type was already integer — no change)
 *  4. RENAME turbidity          → turbidity_ntu   (+ precision: numeric(8,2) → numeric(5,2))
 *  5. ADD    battery_pct        integer, nullable
 *  6. ADD    calibrated_at      timestamptz, nullable
 */
export class AlignMeasurementReadingsSchema1726200004000 implements MigrationInterface {
  public async up(queryRunner: QueryRunner): Promise<void> {
    const columns = new Set(
      (
        await queryRunner.query(`
          SELECT column_name
          FROM information_schema.columns
          WHERE table_schema = current_schema()
            AND table_name = 'measurement_readings'
        `)
      ).map((row: { column_name: string }) => row.column_name),
    );

    if (columns.size === 0) {
      throw new Error(
        "measurement_readings is missing; the baseline migration must run before AlignMeasurementReadingsSchema",
      );
    }

    // Upgrade legacy installations, while remaining a no-op for the current
    // TypeORM baseline. If both names exist, fail rather than silently losing
    // one of the columns or merging data with unknown semantics.
    const renames = [
      ["temperature", "temperature_c"],
      ["salinity", "salinity_ppm"],
      ["orp_value", "orp_mv"],
      ["turbidity", "turbidity_ntu"],
    ] as const;

    for (const [legacyName, targetName] of renames) {
      if (columns.has(legacyName) && columns.has(targetName)) {
        throw new Error(
          `measurement_readings contains both legacy column "${legacyName}" and target column "${targetName}"`,
        );
      }
      if (columns.has(legacyName)) {
        await queryRunner.query(`
          ALTER TABLE measurement_readings
            RENAME COLUMN ${legacyName} TO ${targetName};
        `);
        columns.delete(legacyName);
        columns.add(targetName);
      }
    }

    // Apply the target types only when the target column exists. The baseline
    // already creates these types; legacy installations are converted here.
    if (columns.has("temperature_c")) {
      await queryRunner.query(`
        ALTER TABLE measurement_readings
          ALTER COLUMN temperature_c TYPE NUMERIC(4,1)
          USING ROUND(temperature_c::NUMERIC, 1)::NUMERIC(4,1);
      `);
    }
    if (columns.has("salinity_ppm")) {
      await queryRunner.query(`
        ALTER TABLE measurement_readings
          ALTER COLUMN salinity_ppm TYPE INTEGER
          USING ROUND(salinity_ppm)::INTEGER;
      `);
    }
    if (columns.has("turbidity_ntu")) {
      await queryRunner.query(`
        ALTER TABLE measurement_readings
          ALTER COLUMN turbidity_ntu TYPE NUMERIC(5,2)
          USING turbidity_ntu::NUMERIC(5,2);
      `);
    }

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
