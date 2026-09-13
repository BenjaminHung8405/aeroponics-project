import {
  Entity,
  PrimaryColumn,
  Column,
  CreateDateColumn,
  UpdateDateColumn,
} from 'typeorm';

@Entity({ name: 'devices' })
export class Device {
  @PrimaryColumn({ type: 'varchar', length: 64 })
  device_id: string;

  @Column({ type: 'varchar', length: 100, nullable: true })
  display_name: string | null;

  @Column({ type: 'varchar', length: 64, unique: true })
  mqtt_username: string;

  @Column({ type: 'boolean', default: true })
  enabled: boolean;

  @CreateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  created_at: Date;

  @Column({ type: 'timestamptz', nullable: true })
  last_seen_at: Date | null;

  @UpdateDateColumn({ type: 'timestamptz', default: () => 'CURRENT_TIMESTAMP' })
  updated_at: Date;
}
