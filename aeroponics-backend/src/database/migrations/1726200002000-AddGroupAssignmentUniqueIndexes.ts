import { MigrationInterface, QueryRunner } from 'typeorm';

export class AddGroupAssignmentUniqueIndexes1726200002000 implements MigrationInterface {
  public async up(queryRunner: QueryRunner): Promise<void> {
    // 1. Enforce single active assignment per node per season at the DB level
    await queryRunner.query(`
      CREATE UNIQUE INDEX IF NOT EXISTS uq_group_node_active_per_season
      ON group_node_assignments (season_id, node_id)
      WHERE active = true AND effective_to IS NULL;
    `);

    // 2. Enforce single active treatment assignment per group per season at the DB level
    await queryRunner.query(`
      CREATE UNIQUE INDEX IF NOT EXISTS uq_group_treatment_active_per_season
      ON group_treatment_assignments (season_id, group_id)
      WHERE active = true AND unassigned_at IS NULL;
    `);
  }

  public async down(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`DROP INDEX IF EXISTS uq_group_treatment_active_per_season;`);
    await queryRunner.query(`DROP INDEX IF EXISTS uq_group_node_active_per_season;`);
  }
}
