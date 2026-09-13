import {
  IsInt,
  IsNotEmpty,
  IsOptional,
  IsString,
  Max,
  MaxLength,
  Min,
} from 'class-validator';
import { Transform } from 'class-transformer';
import { TREATMENT_BOUNDS } from '../entities/treatment_version.entity';

export class CreateTreatmentDto {
  @IsString()
  @IsNotEmpty()
  @MaxLength(100)
  @Transform(({ value }: { value: unknown }) =>
    typeof value === 'string' ? value.trim() : value,
  )
  name: string;

  @IsOptional()
  @IsInt()
  @Min(TREATMENT_BOUNDS.MIN_SPRAY_DAY_S)
  @Max(TREATMENT_BOUNDS.MAX_SPRAY_DAY_S)
  spray_day_s?: number;

  @IsOptional()
  @IsInt()
  @Min(TREATMENT_BOUNDS.MIN_COOLDOWN_DAY_S)
  @Max(TREATMENT_BOUNDS.MAX_COOLDOWN_DAY_S)
  cooldown_day_s?: number;

  @IsOptional()
  @IsInt()
  @Min(TREATMENT_BOUNDS.MIN_SPRAY_NIGHT_S)
  @Max(TREATMENT_BOUNDS.MAX_SPRAY_NIGHT_S)
  spray_night_s?: number;

  @IsOptional()
  @IsInt()
  @Min(TREATMENT_BOUNDS.MIN_COOLDOWN_NIGHT_S)
  @Max(TREATMENT_BOUNDS.MAX_COOLDOWN_NIGHT_S)
  cooldown_night_s?: number;

  @IsOptional()
  @IsString()
  @MaxLength(100)
  @Transform(({ value }: { value: unknown }) =>
    typeof value === 'string' ? value.trim() : value,
  )
  created_by?: string;
}
