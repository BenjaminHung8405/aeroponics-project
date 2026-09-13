import {
  Entity,
  PrimaryColumn,
  Column,
  ManyToOne,
  JoinColumn,
  Index,
} from 'typeorm';
import { Season } from '../../season/entities/season.entity';

@Entity({ name: 'pump_feedback_events' })
@Index('idx_pump_feedback_events_season_node_time', ['season_id', 'node_id', 'time'])
export class PumpFeedbackEvent {
  @PrimaryColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  time: Date;

  @Column({ type: 'int' })
  season_id: number;

  @Column({ type: 'smallint' })
  node_id: number;

  @Column({ type: 'smallint', nullable: true })
  group_id: number | null;

  @Column({ type: 'uuid', nullable: true })
  command_id: string | null;

  @Column({ type: 'varchar', length: 8 })
  driver_feedback: string;

  @Column({ type: 'varchar', length: 8, default: 'UNKNOWN' })
  load_feedback: string;

  @Column({ type: 'boolean', default: false })
  driver_feedback_mismatch: boolean;

  @Column({ type: 'int', default: 0 })
  fault_flags: number;

  @Column({ type: 'numeric', precision: 6, scale: 2, nullable: true })
  voltage_v: string | null;

  @Column({ type: 'int', nullable: true })
  current_ma: number | null;

  @Column({ type: 'int', nullable: true })
  boot_session_id: number | null;

  @Column({ type: 'int', nullable: true })
  rf_seq: number | null;

  @Column({ type: 'bigint', nullable: true })
  node_timestamp_ms: string | null;

  @Column({ type: 'bigint', nullable: true })
  gateway_timestamp_ms: string | null;

  @ManyToOne(() => Season, { onDelete: 'RESTRICT' })
  @JoinColumn({ name: 'season_id' })
  season: Season;
}
