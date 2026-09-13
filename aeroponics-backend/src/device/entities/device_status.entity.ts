import {
  Entity,
  PrimaryColumn,
  Column,
} from 'typeorm';

@Entity({ name: 'device_status' })
export class DeviceStatus {
  @PrimaryColumn({ type: 'varchar', length: 64 })
  device_id: string;

  @Column({ type: 'varchar', length: 16, default: 'offline' })
  status: string;

  @Column({ type: 'bigint', default: 0 })
  uptime_s: string;

  @Column({ type: 'smallint', nullable: true })
  rssi_dbm: number | null;

  @Column({ type: 'int', nullable: true })
  free_heap_b: number | null;

  @Column({ type: 'boolean', default: false })
  ntp_synced: boolean;

  @Column({ type: 'boolean', default: false })
  rtc_valid: boolean;

  @Column({ type: 'timestamptz', nullable: true })
  last_seen_at: Date | null;
}
