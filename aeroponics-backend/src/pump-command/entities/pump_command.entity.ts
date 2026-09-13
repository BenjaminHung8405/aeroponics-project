import {
  Entity,
  PrimaryColumn,
  Column,
  ManyToOne,
  JoinColumn,
  Index,
} from 'typeorm';
import { Season } from '../../season/entities/season.entity';

export enum PumpAction {
  ON = 'ON',
  OFF = 'OFF',
}

export enum CommandSource {
  MANUAL_OVERRIDE = 'MANUAL_OVERRIDE',
  FAIL_SAFE = 'FAIL_SAFE',
  MANUAL = 'MANUAL',
  SCHEDULE = 'SCHEDULE',
}

export enum PumpCommandOutcome {
  PENDING = 'PENDING',
  RF_ACKED = 'RF_ACKED',
  FLOW_CONFIRMED = 'FLOW_CONFIRMED',
  FAULT_NO_ACK = 'FAULT_NO_ACK',
  FAULT_NO_FLOW = 'FAULT_NO_FLOW',
  FAULT_UNEXPECTED_FLOW = 'FAULT_UNEXPECTED_FLOW',
  FAULT_SENSOR = 'FAULT_SENSOR',
  TIMEOUT = 'TIMEOUT',
}

@Entity({ name: 'pump_commands' })
@Index('idx_pump_commands_command_id', ['command_id'])
@Index('idx_pump_commands_season_node_time', ['season_id', 'node_id', 'time'])
export class PumpCommand {
  @PrimaryColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  time: Date;

  @PrimaryColumn({ type: 'uuid' })
  command_id: string;

  @Column({ type: 'int' })
  season_id: number;

  @Column({ type: 'smallint' })
  node_id: number;

  @Column({ type: 'smallint', nullable: true })
  group_id: number | null;

  @Column({ type: 'int', nullable: true })
  treatment_version_id: number | null;

  @Column({
    type: 'varchar',
    length: 8,
  })
  action: PumpAction;

  @Column({ type: 'int' })
  rf_seq: number;

  @Column({ type: 'int', default: 30000 })
  run_lease_ms: number;

  @Column({
    type: 'varchar',
    length: 32,
    default: CommandSource.MANUAL_OVERRIDE,
  })
  source: CommandSource;

  @Column({ type: 'int', nullable: true })
  boot_session_id: number | null;

  @Column({ type: 'int', default: 0 })
  retry_count: number;

  @Column({
    type: 'varchar',
    length: 32,
    default: PumpCommandOutcome.PENDING,
  })
  outcome: PumpCommandOutcome;

  @Column({ type: 'timestamptz', nullable: true })
  acked_at: Date | null;

  @Column({ type: 'timestamptz', nullable: true })
  feedback_at: Date | null;

  @Column({ type: 'timestamptz', nullable: true })
  flow_confirmed_at: Date | null;

  @Column({ type: 'bigint', nullable: true })
  node_timestamp_ms: string | null;

  @Column({ type: 'bigint', nullable: true })
  gateway_timestamp_ms: string | null;

  @Column({ type: 'int', nullable: true })
  command_to_ack_latency_ms: number | null;

  @Column({ type: 'int', nullable: true })
  flow_start_latency_ms: number | null;

  @Column({ type: 'int', nullable: true })
  execution_duration_ms: number | null;

  @Column({ type: 'text', nullable: true })
  fault_reason: string | null;

  @ManyToOne(
    () => Season,
    (season) => season.pump_commands,
    { onDelete: 'RESTRICT' },
  )
  @JoinColumn({ name: 'season_id' })
  season: Season;
}
