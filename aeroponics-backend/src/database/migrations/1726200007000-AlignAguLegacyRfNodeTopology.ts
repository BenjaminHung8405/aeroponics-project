import { MigrationInterface, QueryRunner } from 'typeorm';

/** Moves the deployed relational boundary to physical AGU RF IDs 4..7. */
export class AlignAguLegacyRfNodeTopology1726200007000 implements MigrationInterface {
  async up(queryRunner: QueryRunner): Promise<void> {
    const tables = [
      'group_node_assignments',
      'sensor_calibrations',
      'node_registry',
      'pump_commands',
      'pump_state_events',
      'pump_feedback_events',
      'flow_events',
    ];

    const legacyRows = await queryRunner.query(`
      SELECT 1 FROM node_registry WHERE node_id IN (1,2,3) LIMIT 1
    `);
    if (legacyRows.length > 0) {
      throw new Error('AGU RF migration blocked: remediate existing node IDs 1..3 before changing topology to 4..7.');
    }

    await queryRunner.query(`
      DO $$
      DECLARE c RECORD;
      BEGIN
        FOR c IN
          SELECT conrelid::regclass AS table_name, conname
          FROM pg_constraint
          WHERE contype = 'c'
            AND pg_get_constraintdef(oid) ILIKE '%node_id%'
            AND conrelid::regclass::text IN (${tables.map((t) => `'${t}'`).join(',')})
        LOOP
          EXECUTE format('ALTER TABLE %s DROP CONSTRAINT %I', c.table_name, c.conname);
        END LOOP;
      END $$;
    `);

    for (const table of tables) {
      await queryRunner.query(
        `ALTER TABLE ${table} ADD CONSTRAINT ${table}_node_id_agu_legacy_check CHECK (node_id IN (4,5,6,7))`,
      );
    }

    await queryRunner.query(`
      INSERT INTO node_registry (node_id, display_name)
      VALUES (4, 'Node 04'), (5, 'Node 05'), (6, 'Node 06'), (7, 'Node 07')
      ON CONFLICT (node_id) DO NOTHING
    `);
  }

  async down(queryRunner: QueryRunner): Promise<void> {
    for (const table of [
      'group_node_assignments', 'sensor_calibrations', 'node_registry',
      'pump_commands', 'pump_state_events', 'pump_feedback_events', 'flow_events',
    ]) {
      await queryRunner.query(`ALTER TABLE ${table} DROP CONSTRAINT IF EXISTS ${table}_node_id_agu_legacy_check`);
      await queryRunner.query(`ALTER TABLE ${table} ADD CONSTRAINT ${table}_node_id_legacy_check CHECK (node_id BETWEEN 1 AND 4)`);
    }
  }
}
