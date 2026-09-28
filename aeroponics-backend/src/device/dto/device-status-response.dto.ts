export class DeviceStatusResponseDto {
  device_id: string;
  status: string;
  uptime_s: number;
  rssi_dbm: number | null;
  free_heap_b: number | null;
  ntp_synced: boolean;
  rtc_valid: boolean;
  time_source?: string | null;
  last_sync_unix_time_utc?: number | null;
  last_seen_at: Date | null;
}
