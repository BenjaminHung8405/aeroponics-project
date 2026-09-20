import { IsISO8601, IsOptional, IsString, MaxLength } from 'class-validator';
import { Transform } from 'class-transformer';

export class EndSeasonDto {
  @IsOptional()
  @IsISO8601()
  end_date?: string;

  @IsOptional()
  @IsString()
  @MaxLength(1000)
  @Transform(({ value }: { value: unknown }) =>
    typeof value === 'string' ? value.trim() : value,
  )
  notes?: string;
}

