import { Type } from 'class-transformer';
import { IsEnum, IsInt, IsOptional, Max, Min } from 'class-validator';
import { MeasurementTriggerType } from '../entities/measurement_reading.entity';

export class MeasurementHistoryQueryDto {
  @IsOptional()
  @Type(() => Number)
  @IsInt({ message: 'limit must be an integer' })
  @Min(1, { message: 'limit must be at least 1' })
  @Max(100, { message: 'limit cannot exceed 100' })
  limit: number = 20;

  @IsOptional()
  @Type(() => Number)
  @IsInt({ message: 'offset must be an integer' })
  @Min(0, { message: 'offset must be at least 0' })
  offset: number = 0;

  @IsOptional()
  @IsEnum(MeasurementTriggerType, {
    message: 'trigger_type must be either ON_DEMAND or END_OF_SEASON',
  })
  trigger_type?: MeasurementTriggerType;
}
