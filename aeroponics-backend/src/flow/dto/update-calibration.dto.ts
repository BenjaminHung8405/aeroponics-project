import {
  IsNumber,
  IsPositive,
  Min,
  Max,
  IsOptional,
  IsString,
  MaxLength,
  IsInt,
  ValidateIf,
} from 'class-validator';
import { Type, Transform } from 'class-transformer';

export class UpdateCalibrationDto {
  @ValidateIf((o) => o.pulses_per_litre === undefined || o.calibration_pulses_per_litre !== undefined)
  @IsNumber({}, { message: 'calibration_pulses_per_litre must be a number' })
  @IsPositive({ message: 'calibration_pulses_per_litre must be greater than 0' })
  @Min(0.0001, { message: 'calibration_pulses_per_litre must be greater than 0' })
  @Max(9999.9999, { message: 'calibration_pulses_per_litre must be less than 10000' })
  @IsOptional()
  @Type(() => Number)
  calibration_pulses_per_litre?: number;

  @ValidateIf((o) => o.calibration_pulses_per_litre === undefined)
  @IsNumber({}, { message: 'pulses_per_litre must be a number' })
  @IsPositive({ message: 'pulses_per_litre must be greater than 0' })
  @Min(0.0001, { message: 'pulses_per_litre must be greater than 0' })
  @Max(9999.9999, { message: 'pulses_per_litre must be less than 10000' })
  @IsOptional()
  @Type(() => Number)
  pulses_per_litre?: number;

  @IsOptional()
  @IsInt({ message: 'reference_volume_ml must be an integer' })
  @Min(1, { message: 'reference_volume_ml must be greater than 0' })
  @Type(() => Number)
  reference_volume_ml?: number = 1000;

  @IsOptional()
  @IsString()
  @MaxLength(64)
  @Transform(({ value }) => (typeof value === 'string' ? value.trim() : value))
  sensor_serial?: string;

  @IsOptional()
  @IsString()
  @MaxLength(100)
  @Transform(({ value }) => (typeof value === 'string' ? value.trim() : value))
  calibrated_by?: string;

  @IsOptional()
  @IsString()
  @MaxLength(255)
  notes?: string;

  /**
   * Helper to retrieve the effective pulses per litre
   */
  getEffectivePulsesPerLitre(): number {
    const val = this.pulses_per_litre ?? this.calibration_pulses_per_litre;
    return val as number;
  }
}
