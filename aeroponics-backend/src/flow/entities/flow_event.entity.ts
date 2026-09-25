import {
  Entity,
  PrimaryColumn,
  Column,
  ManyToOne,
  JoinColumn,
  Index,
} from 'typeorm';
import { Season } from '../../season/entities/season.entity';
import { SensorCalibration } from '../../node/entities/sensor_calibration.entity';

export enum FlowFaultCode {
  NONE = 'NONE',
  NO_FLOW_FAULT = 'NO_FLOW_FAULT',
  UNEXPECTED_FLOW_FAULT = 'UNEXPECTED_FLOW_FAULT',
  OVER_RANGE_FAULT = 'OVER_RANGE_FAULT',
  SENSOR_FAULT = 'SENSOR_FAULT',
}

@Entity({ name: 'flow_events' })
@Index('idx_flow_events_command_id', ['command_id'])
@Index('idx_flow_events_season_node_time', ['season_id', 'node_id', 'time'])
// K3: TimescaleDB-aware index hint for range queries on (node_id, time).
// TimescaleDB automatically creates a primary key index on the hypertable
// partition key (`time`). This additional composite index improves
// query plans that filter by node_id with time range predicates.
@Index('idx_flow_events_node_time', ['node_id', 'time'])
export class FlowEvent {
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

  @Column({ type: 'numeric', precision: 10, scale: 3, default: '0.000' })
  litres_total: string;

  @Column({ type: 'bigint', default: '0' })
  pulse_count: string;

  @Column({ type: 'numeric', precision: 6, scale: 2, default: '0.00' })
  flow_rate_lpm: string;

  @Column({ type: 'int', default: 0 })
  delivered_volume_ml: number;

  @Column({ type: 'int', default: 1000 })
  sample_window_ms: number;

  @Column({ type: 'int' })
  sensor_calibration_id: number;

  @Column({ type: 'boolean', default: false })
  flow_confirmed: boolean;

  @Column({ type: 'numeric', precision: 5, scale: 2, nullable: true })
  flow_stability_pct: string | null;

  @Column({ type: 'varchar', length: 16, default: 'OK' })
  quality_flag: string;

  @Column({ type: 'boolean', default: false })
  is_fault: boolean;

  @Column({
    type: 'varchar',
    length: 32,
    default: FlowFaultCode.NONE,
  })
  fault_code: FlowFaultCode;

  @Column({ type: 'int', nullable: true })
  boot_session_id: number | null;

  @Column({ type: 'int', nullable: true })
  rf_seq: number | null;

  @Column({ type: 'bigint', nullable: true })
  node_timestamp_ms: string | null;

  @Column({ type: 'bigint', nullable: true })
  gateway_timestamp_ms: string | null;

  @ManyToOne(
    () => Season,
    (season) => season.flow_events,
    { onDelete: 'RESTRICT' },
  )
  @JoinColumn({ name: 'season_id' })
  season: Season;

  @ManyToOne(() => SensorCalibration, { onDelete: 'RESTRICT' })
  @JoinColumn({ name: 'sensor_calibration_id' })
  sensor_calibration: SensorCalibration;
}
