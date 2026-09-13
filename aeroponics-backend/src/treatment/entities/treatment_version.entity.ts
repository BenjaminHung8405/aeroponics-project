import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  ManyToOne,
  JoinColumn,
  Unique,
} from 'typeorm';
import { Treatment } from './treatment.entity';

export enum TreatmentVersionStatus {
  DRAFT = 'DRAFT',
  PUBLISHED = 'PUBLISHED',
  ARCHIVED = 'ARCHIVED',
}

export const TREATMENT_BOUNDS = {
  MIN_SPRAY_DAY_S: 5,
  MAX_SPRAY_DAY_S: 300,
  MIN_COOLDOWN_DAY_S: 30,
  MAX_COOLDOWN_DAY_S: 7200,
  MIN_SPRAY_NIGHT_S: 5,
  MAX_SPRAY_NIGHT_S: 300,
  MIN_COOLDOWN_NIGHT_S: 30,
  MAX_COOLDOWN_NIGHT_S: 7200,
} as const;

@Entity({ name: 'treatment_versions' })
@Unique('uq_treatment_version', ['treatment_id', 'version_num'])
export class TreatmentVersion {
  @PrimaryGeneratedColumn({ type: 'int' })
  id: number;

  @Column({ type: 'int' })
  treatment_id: number;

  @Column({ type: 'int', default: 1 })
  version_num: number;

  @Column({
    type: 'varchar',
    length: 16,
    default: TreatmentVersionStatus.DRAFT,
  })
  status: TreatmentVersionStatus;

  @Column({ type: 'int' })
  spray_day_s: number;

  @Column({ type: 'int' })
  cooldown_day_s: number;

  @Column({ type: 'int' })
  spray_night_s: number;

  @Column({ type: 'int' })
  cooldown_night_s: number;

  @Column({ type: 'varchar', length: 100, nullable: true })
  created_by: string | null;

  @CreateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  created_at: Date;

  @Column({ type: 'timestamptz', nullable: true })
  published_at: Date | null;

  @ManyToOne(
    () => Treatment,
    (treatment) => treatment.versions,
    { onDelete: 'CASCADE' },
  )
  @JoinColumn({ name: 'treatment_id' })
  treatment: Treatment;
}
