import {
  Entity,
  PrimaryColumn,
  Column,
  CreateDateColumn,
  UpdateDateColumn,
  OneToMany,
} from 'typeorm';
import { GroupTreatmentAssignment } from './group_treatment_assignment.entity';
import { GroupNodeAssignment } from './group_node_assignment.entity';

export enum TimerGroupStatus {
  UNASSIGNED = 'UNASSIGNED',
  ACTIVE = 'ACTIVE',
  PAUSED = 'PAUSED',
  ENDED = 'ENDED',
}

@Entity({ name: 'timer_groups' })
export class TimerGroup {
  @PrimaryColumn({ type: 'smallint' })
  group_id: number;

  @Column({ type: 'varchar', length: 50 })
  name: string;

  @Column({
    type: 'varchar',
    length: 16,
    default: TimerGroupStatus.UNASSIGNED,
  })
  status: TimerGroupStatus;

  @CreateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  created_at: Date;

  @UpdateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  updated_at: Date;

  @OneToMany(
    () => GroupTreatmentAssignment,
    (assignment) => assignment.group,
  )
  treatment_assignments: GroupTreatmentAssignment[];

  @OneToMany(
    () => GroupNodeAssignment,
    (assignment) => assignment.group,
  )
  node_assignments: GroupNodeAssignment[];
}
