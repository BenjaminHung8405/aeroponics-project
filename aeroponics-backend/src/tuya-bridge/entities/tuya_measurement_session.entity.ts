import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  ManyToOne,
  JoinColumn,
  Index,
} from 'typeorm';
import { Season } from '../../season/entities/season.entity';

export enum TuyaSessionTriggerType {
  ON_DEMAND = 'ON_DEMAND',
  END_OF_SEASON = 'END_OF_SEASON',
}

export enum TuyaSessionStatus {
  PENDING = 'PENDING',
  COMPLETED = 'COMPLETED',
  FAILED = 'FAILED',
}

@Entity({ name: 'tuya_measurement_sessions' })
@Index('idx_tuya_sessions_season', ['season_id', 'started_at'])
export class TuyaMeasurementSession {
  @PrimaryGeneratedColumn('uuid')
  session_id: string;

  @Column({ type: 'varchar', length: 64, default: 'ph-w218-01' })
  sensor_id: string;

  @Column({
    type: 'varchar',
    length: 32,
    default: TuyaSessionTriggerType.ON_DEMAND,
  })
  trigger_type: TuyaSessionTriggerType;

  @Column({ type: 'int', nullable: true })
  season_id: number | null;

  @Column({ type: 'varchar', length: 64, nullable: true })
  triggered_by_user_id: string | null;

  @Column({
    type: 'varchar',
    length: 16,
    default: TuyaSessionStatus.PENDING,
  })
  status: TuyaSessionStatus;

  @CreateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  started_at: Date;

  @Column({ type: 'timestamptz', nullable: true })
  completed_at: Date | null;

  @Column({ type: 'text', nullable: true })
  error_message: string | null;

  @ManyToOne(() => Season, { onDelete: 'SET NULL' })
  @JoinColumn({ name: 'season_id' })
  season: Season | null;
}
