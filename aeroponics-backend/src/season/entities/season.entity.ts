import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  UpdateDateColumn,
  OneToMany,
  Index,
} from 'typeorm';
import { GroupTreatmentAssignment } from '../../group/entities/group_treatment_assignment.entity';
import { GroupNodeAssignment } from '../../group/entities/group_node_assignment.entity';
import { PumpCommand } from '../../pump-command/entities/pump_command.entity';
import { FlowEvent } from '../../flow/entities/flow_event.entity';

export enum SeasonStatus {
  ACTIVE = 'ACTIVE',
  ENDED = 'ENDED',
}

@Entity({ name: 'seasons' })
export class Season {
  @PrimaryGeneratedColumn({ type: 'int' })
  id: number;

  @Column({ type: 'varchar', length: 100 })
  name: string;

  @Column({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  started_at: Date;

  @Column({ type: 'timestamptz', nullable: true })
  ended_at: Date | null;

  @Index('uq_seasons_one_active', {
    unique: true,
    where: "status = 'ACTIVE'",
  })
  @Column({
    type: 'varchar',
    length: 16,
    default: SeasonStatus.ACTIVE,
  })
  status: SeasonStatus;

  @Column({ type: 'text', nullable: true })
  notes: string | null;

  @Column({
    type: 'decimal',
    precision: 4,
    scale: 2,
    nullable: true,
    transformer: {
      to: (value?: number | null) => value,
      from: (value?: string | null) => (value != null ? parseFloat(value) : null),
    },
  })
  target_ec: number | null;

  @Column({
    type: 'decimal',
    precision: 3,
    scale: 2,
    nullable: true,
    transformer: {
      to: (value?: number | null) => value,
      from: (value?: string | null) => (value != null ? parseFloat(value) : null),
    },
  })
  target_ph: number | null;

  @CreateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  created_at: Date;

  @UpdateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  updated_at: Date;

  @OneToMany(
    () => GroupTreatmentAssignment,
    (assignment) => assignment.season,
  )
  treatment_assignments: GroupTreatmentAssignment[];

  @OneToMany(
    () => GroupNodeAssignment,
    (assignment) => assignment.season,
  )
  node_assignments: GroupNodeAssignment[];

  @OneToMany(
    () => PumpCommand,
    (cmd) => cmd.season,
  )
  pump_commands: PumpCommand[];

  @OneToMany(
    () => FlowEvent,
    (event) => event.season,
  )
  flow_events: FlowEvent[];
}
