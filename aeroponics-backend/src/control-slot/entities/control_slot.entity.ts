import {
  Entity,
  Column,
  CreateDateColumn,
  UpdateDateColumn,
  PrimaryColumn,
  Index,
} from 'typeorm';

export enum ControlSlotTargetType {
  NODE = 'NODE',
  GROUP = 'GROUP',
}

@Entity({ name: 'control_slots' })
@Index('uq_control_slots_device_target', ['device_id', 'target_type', 'target_id'], {
  unique: true,
  where: 'target_type IS NOT NULL AND target_id IS NOT NULL',
})
export class ControlSlot {
  @PrimaryColumn({ type: 'varchar', length: 64 })
  device_id: string;

  @PrimaryColumn({ type: 'smallint' })
  slot_index: number;

  @Column({ type: 'varchar', length: 8, nullable: true })
  target_type: ControlSlotTargetType | null;

  @Column({ type: 'smallint', nullable: true })
  target_id: number | null;

  @CreateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  created_at: Date;

  @UpdateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  updated_at: Date;

  @Column({ type: 'varchar', length: 100, nullable: true })
  updated_by: string | null;
}
