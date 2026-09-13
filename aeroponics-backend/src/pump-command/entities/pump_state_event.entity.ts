import {
  Entity,
  PrimaryColumn,
  Column,
  ManyToOne,
  JoinColumn,
  Index,
} from 'typeorm';
import { Season } from '../../season/entities/season.entity';

@Entity({ name: 'pump_state_events' })
@Index('idx_pump_state_events_season_node_time', ['season_id', 'node_id', 'time'])
export class PumpStateEvent {
  @PrimaryColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  time: Date;

  @Column({ type: 'int' })
  season_id: number;

  @Column({ type: 'smallint' })
  node_id: number;

  @Column({ type: 'smallint', nullable: true })
  group_id: number | null;

  @Column({ type: 'varchar', length: 8 })
  desired_state: string;

  @Column({ type: 'varchar', length: 8 })
  reported_state: string;

  @Column({ type: 'varchar', length: 32, default: 'SCHEDULE' })
  source: string;

  @Column({ type: 'varchar', length: 16, default: 'UNKNOWN' })
  schedule_state: string;

  @Column({ type: 'varchar', length: 16, default: 'NONE' })
  override_state: string;

  @Column({ type: 'varchar', length: 32, default: 'NONE' })
  resume_reason: string;

  @Column({ type: 'int', nullable: true })
  boot_session_id: number | null;

  @Column({ type: 'int', nullable: true })
  rf_seq: number | null;

  @Column({ type: 'bigint', nullable: true })
  node_timestamp_ms: string | null;

  @Column({ type: 'bigint', nullable: true })
  gateway_timestamp_ms: string | null;

  @Column({ type: 'text', nullable: true })
  reason: string | null;

  @ManyToOne(() => Season, { onDelete: 'RESTRICT' })
  @JoinColumn({ name: 'season_id' })
  season: Season;
}
