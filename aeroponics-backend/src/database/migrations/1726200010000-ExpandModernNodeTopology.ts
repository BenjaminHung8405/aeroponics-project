import { MigrationInterface, QueryRunner } from 'typeorm';

/**
 * Expands the modern control-plane node range to 1..15.
 * AGU legacy adapter validation remains 4..7 in application code.
 * Rollback is intentionally blocked while rows outside the legacy range exist.
 */
export class ExpandModernNodeTopology1726200010000 implements MigrationInterface {
  private readonly tables = [
    'group_node_assignments',
    'sensor_calibrations',
    'node_registry',
    'pump_commands',
    'pump_state_events',
    'pump_feedback_events',
    'flow_events',
  ];

  async up(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      DO $$
      DECLARE c RECORD;
      BEGIN
        FOR c IN
          SELECT conrelid::regclass AS table_name, conname
          FROM pg_constraint
          WHERE contype = 'c'
            AND pg_get_constraintdef(oid) ILIKE '%node_id%'
            AND conrelid::regclass::text IN (${this.tables.map((table) => `'${table}'`).join(',')})
        LOOP
          EXECUTE format('ALTER TABLE %s DROP CONSTRAINT %I', c.table_name, c.conname);
        END LOOP;
      END $$;
    `);

    for (const table of this.tables) {
      await queryRunner.query(
        `ALTER TABLE ${table} ADD CONSTRAINT ${table}_node_id_modern_check CHECK (node_id BETWEEN 1 AND 15)`,
      );
    }

  }

  async down(queryRunner: QueryRunner): Promise<void> {
    const modernRows = await queryRunner.query(`
      SELECT table_name, row_count
      FROM (
        ${this.tables
          .map(
            (table) =>
              `SELECT '${table}' AS table_name, count(*)::int AS row_count FROM ${table} WHERE node_id NOT IN (4,5,6,7)`,
          )
          .join(' UNION ALL ')}
      ) violations
      WHERE row_count > 0
    `);
    if (modernRows.length > 0) {
      const summary = modernRows
        .map((row: { table_name: string; row_count: number }) => `${row.table_name}=${row.row_count}`)
        .join(', ');
      throw new Error(`Cannot roll back modern node topology while non-legacy rows exist (${summary}).`);
    }

    for (const table of this.tables) {
      await queryRunner.query(`ALTER TABLE ${table} DROP CONSTRAINT IF EXISTS ${table}_node_id_modern_check`);
      await queryRunner.query(
        `ALTER TABLE ${table} ADD CONSTRAINT ${table}_node_id_agu_legacy_check CHECK (node_id IN (4,5,6,7))`,
      );
    }
  }
}
