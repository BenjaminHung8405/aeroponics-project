import { MigrationInterface, QueryRunner } from 'typeorm';

export class CreateControlSlots1726200009000 implements MigrationInterface {
  async up(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query(`
      CREATE TABLE IF NOT EXISTS control_slots (
        device_id   VARCHAR(64) NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
        slot_index  SMALLINT NOT NULL CHECK (slot_index BETWEEN 1 AND 4),
        target_type VARCHAR(8),
        target_id   SMALLINT,
        created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        updated_by  VARCHAR(100),
        PRIMARY KEY (device_id, slot_index),
        CONSTRAINT control_slots_target_type_check CHECK (target_type IS NULL OR target_type IN ('NODE', 'GROUP')),
        CONSTRAINT control_slots_target_pair_check CHECK ((target_type IS NULL AND target_id IS NULL) OR (target_type IS NOT NULL AND target_id IS NOT NULL)),
        CONSTRAINT control_slots_target_id_check CHECK (
          target_id IS NULL OR (target_id BETWEEN 1 AND 15 AND (target_type <> 'GROUP' OR target_id <= 4))
        )
      );
    `);
    await queryRunner.query(`
      CREATE UNIQUE INDEX IF NOT EXISTS uq_control_slots_device_target
      ON control_slots (device_id, target_type, target_id)
      WHERE target_type IS NOT NULL AND target_id IS NOT NULL;
    `);
  }

  async down(queryRunner: QueryRunner): Promise<void> {
    await queryRunner.query('DROP TABLE IF EXISTS control_slots CASCADE;');
  }
}
