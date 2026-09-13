import { IsEnum, IsOptional, IsString, MaxLength } from 'class-validator';
import { MeasurementTriggerType } from '../entities/measurement_reading.entity';

export class TriggerMeasurementDto {
  @IsEnum(MeasurementTriggerType, {
    message: 'trigger_type must be either ON_DEMAND or END_OF_SEASON',
  })
  @IsOptional()
  trigger_type?: MeasurementTriggerType = MeasurementTriggerType.ON_DEMAND;

  @IsString()
  @IsOptional()
  @MaxLength(500)
  notes?: string;
}
