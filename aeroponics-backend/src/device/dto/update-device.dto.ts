import { IsBoolean, IsOptional, IsString, MaxLength } from 'class-validator';

export class UpdateDeviceDto {
  @IsOptional()
  @IsString()
  @MaxLength(100)
  display_name?: string;

  @IsOptional()
  @IsBoolean()
  enabled?: boolean;
}
