import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  Unique,
} from 'typeorm';

export enum CalibrationStatusEnum {
  DRAFT = 'DRAFT',
  ACTIVE = 'ACTIVE',
  SUPERSEDED = 'SUPERSEDED',
  REJECTED = 'REJECTED',
}

@Entity({ name: 'sensor_calibrations' })
@Unique('uq_sensor_calibration', ['node_id', 'sensor_serial', 'version_num'])
export class SensorCalibration {
  @PrimaryGeneratedColumn({ type: 'int' })
  id: number;

  @Column({ type: 'smallint' })
  node_id: number;

  @Column({ type: 'varchar', length: 64 })
  sensor_serial: string;

  @Column({ type: 'int', default: 1 })
  version_num: number;

  @Column({
    type: 'numeric',
    precision: 10,
    scale: 4,
  })
  pulses_per_litre: string;

  @Column({ type: 'int' })
  reference_volume_ml: number;

  @Column({ type: 'int', default: 3 })
  trial_count: number;

  @Column({
    type: 'numeric',
    precision: 10,
    scale: 2,
  })
  mean_pulses: string;

  @Column({
    type: 'numeric',
    precision: 10,
    scale: 4,
    default: '0.0000',
  })
  variance: string;

  @Column({
    type: 'numeric',
    precision: 5,
    scale: 2,
  })
  repeatability_pct: string;

  @Column({ type: 'jsonb', nullable: true })
  operating_conditions: Record<string, any> | null;

  @Column({
    type: 'varchar',
    length: 16,
    default: CalibrationStatusEnum.ACTIVE,
  })
  status: CalibrationStatusEnum;

  @Column({ type: 'varchar', length: 100, nullable: true })
  calibrated_by: string | null;

  @CreateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  calibrated_at: Date;
}
