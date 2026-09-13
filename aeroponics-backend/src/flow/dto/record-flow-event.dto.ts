import {
  IsInt,
  Min,
  Max,
  IsNumber,
  IsOptional,
  IsString,
  IsBoolean,
  IsEnum,
  IsUUID,
} from 'class-validator';
import { Type } from 'class-transformer';
import { FlowFaultCode } from '../entities/flow_event.entity';

export class RecordFlowEventDto {
  @IsInt()
  @Min(1)
  @Max(4)
  @Type(() => Number)
  node_id: number;

  @IsNumber()
  @Min(0)
  @Type(() => Number)
  flow_rate_lpm: number;

  @IsOptional()
  @IsString()
  litres_total?: string;

  @IsOptional()
  @IsString()
  pulse_count?: string;

  @IsOptional()
  @IsInt()
  @Min(0)
  @Type(() => Number)
  delivered_volume_ml?: number;

  @IsOptional()
  @IsInt()
  @Min(1)
  @Type(() => Number)
  sample_window_ms?: number;

  @IsOptional()
  @IsUUID()
  command_id?: string;

  @IsOptional()
  @IsInt()
  @Min(1)
  @Max(4)
  @Type(() => Number)
  group_id?: number;

  @IsOptional()
  @IsInt()
  @Type(() => Number)
  sensor_calibration_id?: number;

  @IsOptional()
  @IsBoolean()
  flow_confirmed?: boolean;

  @IsOptional()
  @IsString()
  flow_stability_pct?: string;

  @IsOptional()
  @IsString()
  quality_flag?: string;

  @IsOptional()
  @IsEnum(FlowFaultCode)
  fault_code?: FlowFaultCode;

  @IsOptional()
  @IsInt()
  @Type(() => Number)
  boot_session_id?: number;

  @IsOptional()
  @IsInt()
  @Type(() => Number)
  rf_seq?: number;

  @IsOptional()
  @IsString()
  node_timestamp_ms?: string;

  @IsOptional()
  @IsString()
  gateway_timestamp_ms?: string;
}
