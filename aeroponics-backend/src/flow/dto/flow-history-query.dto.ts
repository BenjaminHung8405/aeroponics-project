import { IsOptional, IsInt, Min, Max } from 'class-validator';
import { Type } from 'class-transformer';

export class FlowHistoryQueryDto {
  /**
   * Number of hours of historical flow data to retrieve.
   * Default is 24 hours. Maximum allowed is 720 hours (30 days).
   */
  @IsOptional()
  @Type(() => Number)
  @IsInt({ message: 'hours must be an integer' })
  @Min(1, { message: 'hours must be at least 1' })
  @Max(720, { message: 'hours cannot exceed 720 (30 days)' })
  hours: number = 24;

  /**
   * Maximum number of raw flow event records to return.
   * Default is 500, maximum is 1000.
   */
  @IsOptional()
  @Type(() => Number)
  @IsInt({ message: 'limit must be an integer' })
  @Min(1, { message: 'limit must be at least 1' })
  @Max(1000, { message: 'limit cannot exceed 1000' })
  limit: number = 500;
}
