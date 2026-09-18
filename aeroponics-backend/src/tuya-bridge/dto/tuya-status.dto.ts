import { IsBoolean, IsOptional, IsString } from 'class-validator';

export class ToggleTuyaBridgeDto {
  @IsBoolean()
  enabled: boolean;

  @IsOptional()
  @IsString()
  reason?: string;
}

export interface TuyaBridgeStatusResponse {
  enabled: boolean;
  static_enabled: boolean;
  runtime_enabled: boolean;
  sensor_id: string;
  device_ip?: string;
  masked_device_id?: string;
  cooldown_remaining_s: number;
  is_measuring: boolean;
  last_measurement_time: string | null;
  reason: string | null;
  updated_at: string | null;
  updated_by: string | null;
}
