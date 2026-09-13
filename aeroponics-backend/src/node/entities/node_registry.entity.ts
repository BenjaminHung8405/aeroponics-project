import {
  Entity,
  PrimaryColumn,
  Column,
  CreateDateColumn,
  UpdateDateColumn,
  ManyToOne,
  JoinColumn,
} from 'typeorm';
import { SensorCalibration } from './sensor_calibration.entity';

export enum CalibrationStatus {
  UNCALIBRATED = 'UNCALIBRATED',
  CALIBRATED = 'CALIBRATED',
}

export enum ScheduleState {
  UNKNOWN = 'UNKNOWN',
  SPRAYING = 'SPRAYING',
  COOLING_DOWN = 'COOLING_DOWN',
  IDLE = 'IDLE',
  PAUSED = 'PAUSED',
}

export enum OverrideState {
  NONE = 'NONE',
  OVERRIDE_OFF = 'OVERRIDE_OFF',
  OVERRIDE_ON = 'OVERRIDE_ON',
}

export enum NodeHealthStatus {
  OK = 'OK',
  STALE = 'STALE',
  FAULT = 'FAULT',
  SAFE_OFF = 'SAFE_OFF',
}

@Entity({ name: 'node_registry' })
export class NodeRegistry {
  @PrimaryColumn({ type: 'smallint' })
  node_id: number;

  @Column({ type: 'varchar', length: 50 })
  display_name: string;

  @Column({ type: 'smallint', nullable: true })
  cached_group_id: number | null;

  @Column({ type: 'varchar', length: 64, nullable: true })
  sensor_serial: string | null;

  @Column({ type: 'int', nullable: true })
  active_sensor_calibration_id: number | null;

  @Column({
    type: 'varchar',
    length: 16,
    default: CalibrationStatus.UNCALIBRATED,
  })
  calibration_status: CalibrationStatus;

  @Column({
    type: 'varchar',
    length: 16,
    default: ScheduleState.UNKNOWN,
  })
  schedule_state: ScheduleState;

  @Column({
    type: 'varchar',
    length: 16,
    default: OverrideState.NONE,
  })
  override_state: OverrideState;

  @Column({ type: 'int', nullable: true })
  last_boot_session_id: number | null;

  @Column({ type: 'timestamptz', nullable: true })
  last_seen_at: Date | null;

  @Column({
    type: 'varchar',
    length: 16,
    default: NodeHealthStatus.OK,
  })
  health_status: NodeHealthStatus;

  @CreateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  created_at: Date;

  @UpdateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  updated_at: Date;

  @ManyToOne(() => SensorCalibration)
  @JoinColumn({ name: 'active_sensor_calibration_id' })
  active_sensor_calibration: SensorCalibration | null;
}
