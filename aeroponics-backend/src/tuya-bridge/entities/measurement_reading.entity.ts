import {
  Entity,
  PrimaryColumn,
  Column,
  ManyToOne,
  JoinColumn,
  Index,
} from 'typeorm';
import { TuyaMeasurementSession } from './tuya_measurement_session.entity';

export enum MeasurementTriggerType {
  ON_DEMAND = 'ON_DEMAND',
  END_OF_SEASON = 'END_OF_SEASON',
}

@Entity({ name: 'measurement_readings' })
@Index('idx_measurement_readings_sensor_time', ['sensor_id', 'time'])
export class MeasurementReading {
  @PrimaryColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  time: Date;

  @Column({ type: 'uuid', nullable: true })
  session_id: string | null;

  @Column({ type: 'varchar', length: 64, default: 'ph-w218-01' })
  sensor_id: string;

  @Column({
    type: 'varchar',
    length: 32,
    default: MeasurementTriggerType.ON_DEMAND,
  })
  trigger_type: MeasurementTriggerType;

  @Column({ type: 'numeric', precision: 4, scale: 2, nullable: true })
  ph_value: string | null;

  @Column({ type: 'int', nullable: true })
  ec_value: number | null;

  @Column({ type: 'int', nullable: true })
  tds_value: number | null;

  @Column({ type: 'numeric', precision: 4, scale: 1, nullable: true })
  temperature_c: string | null;

  @Column({ type: 'int', nullable: true })
  salinity_ppm: number | null;

  @Column({ type: 'int', nullable: true })
  orp_mv: number | null;

  @Column({ type: 'numeric', precision: 5, scale: 2, nullable: true })
  turbidity_ntu: string | null;

  @Column({ type: 'int', nullable: true })
  battery_pct: number | null;

  @Column({ type: 'timestamptz', nullable: true })
  calibrated_at: Date | null;

  @Column({ type: 'varchar', length: 64, nullable: true })
  triggered_by_user_id: string | null;

  @ManyToOne(
    () => TuyaMeasurementSession,
    { onDelete: 'SET NULL' },
  )
  @JoinColumn({ name: 'session_id' })
  session: TuyaMeasurementSession | null;
}
