import { IsEnum, IsInt, IsOptional, IsString } from 'class-validator';
import { Type } from 'class-transformer';
import { ScheduleState, OverrideState } from '../entities/node_registry.entity';

export class NodeTelemetryDto {
  @IsOptional()
  @IsEnum(ScheduleState)
  schedule_state?: ScheduleState;

  @IsOptional()
  @IsEnum(OverrideState)
  override_state?: OverrideState;

  @IsOptional()
  @IsInt()
  @Type(() => Number)
  boot_session_id?: number;

  @IsOptional()
  @IsInt()
  @Type(() => Number)
  rssi_dbm?: number;

  @IsOptional()
  @IsInt()
  @Type(() => Number)
  battery_mv?: number;

  @IsOptional()
  @IsInt()
  @Type(() => Number)
  flow_pulse_count?: number;

  @IsOptional()
  @IsString()
  sensor_serial?: string;
}
