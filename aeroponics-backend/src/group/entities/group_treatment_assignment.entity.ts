import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  ManyToOne,
  JoinColumn,
} from 'typeorm';
import { TimerGroup } from './timer_group.entity';
import { TreatmentVersion } from '../../treatment/entities/treatment_version.entity';
import { Season } from '../../season/entities/season.entity';

@Entity({ name: 'group_treatment_assignments' })
export class GroupTreatmentAssignment {
  @PrimaryGeneratedColumn({ type: 'int' })
  id: number;

  @Column({ type: 'smallint' })
  group_id: number;

  @Column({ type: 'int' })
  treatment_version_id: number;

  @Column({ type: 'int' })
  season_id: number;

  @CreateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  assigned_at: Date;

  @Column({ type: 'timestamptz', nullable: true })
  unassigned_at: Date | null;

  @Column({ type: 'boolean', default: true })
  active: boolean;

  @ManyToOne(
    () => TimerGroup,
    (group) => group.treatment_assignments,
  )
  @JoinColumn({ name: 'group_id' })
  group: TimerGroup;

  @ManyToOne(() => TreatmentVersion)
  @JoinColumn({ name: 'treatment_version_id' })
  treatment_version: TreatmentVersion;

  @ManyToOne(
    () => Season,
    (season) => season.treatment_assignments,
  )
  @JoinColumn({ name: 'season_id' })
  season: Season;
}
