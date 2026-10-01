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

  /** Active reference the gateway trusted: DS1307_RTC | SYSTEM_NTP | BACKEND | INVALID. */
  @Column({ type: 'varchar', length: 16, nullable: true })
  time_source: string | null;

  /** UTC epoch seconds of the last authoritative sync, if the firmware reports one. */
  @Column({ type: 'bigint', nullable: true })
  last_sync_unix_time_utc: string | null;

  @Column({ type: 'timestamptz', nullable: true })
  last_seen_at: Date | null;

  /** Closed-loop: the schedule state the gateway reports it actually runs. */
  @Column({ type: 'jsonb', nullable: true })
  reported_schedule_state: Record<string, any> | null;

  /** IN_SYNC | IN_SYNC_PENDING_BOUNDARY | SYNCING | DRIFTED | DRIFTED_LATCHED | UNCONFIRMED */
  @Column({ type: 'varchar', length: 32, nullable: true })
  schedule_sync_state: string | null;

  @Column({ type: 'timestamptz', nullable: true })
  schedule_sync_updated_at: Date | null;

  @Column({ type: 'jsonb', nullable: true })
  schedule_sync_details: Record<string, any> | null;

  /** Monotonic command versions persisted per gateway. */
  @Column({ type: 'bigint', default: 0 })
  assignment_config_version: string;

  @Column({ type: 'bigint', default: 0 })
  command_envelope_version: string;
}
