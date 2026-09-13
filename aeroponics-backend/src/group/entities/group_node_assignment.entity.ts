import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  ManyToOne,
  JoinColumn,
} from 'typeorm';
import { TimerGroup } from './timer_group.entity';
import { Season } from '../../season/entities/season.entity';

@Entity({ name: 'group_node_assignments' })
export class GroupNodeAssignment {
  @PrimaryGeneratedColumn({ type: 'int' })
  id: number;

  @Column({ type: 'smallint' })
  group_id: number;

  @Column({ type: 'smallint' })
  node_id: number;

  @Column({ type: 'int' })
  season_id: number;

  @CreateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  effective_from: Date;

  @Column({ type: 'timestamptz', nullable: true })
  effective_to: Date | null;

  @Column({ type: 'boolean', default: true })
  active: boolean;

  @ManyToOne(
    () => TimerGroup,
    (group) => group.node_assignments,
  )
  @JoinColumn({ name: 'group_id' })
  group: TimerGroup;

  @ManyToOne(
    () => Season,
    (season) => season.node_assignments,
  )
  @JoinColumn({ name: 'season_id' })
  season: Season;
}
