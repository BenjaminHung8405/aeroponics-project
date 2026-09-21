import { IsEnum, IsInt, IsOptional, IsIn, Max, Min } from 'class-validator';
import { Type } from 'class-transformer';
import { CommandSource, PumpAction } from '../entities/pump_command.entity';
import { AGU_LEGACY_NODE_IDS } from '../../node/node-topology';

export class SendPumpCommandDto {
  @IsEnum(PumpAction, {
    message: 'action must be either ON or OFF',
  })
  action: PumpAction;

  @IsOptional()
  @Type(() => Number)
  @IsInt()
  @IsIn(AGU_LEGACY_NODE_IDS)
  node_id?: number;

  @IsOptional()
  @Type(() => Number)
  @IsInt()
  @Min(1)
  @Max(4)
  group_id?: number;

  @IsOptional()
  @Type(() => Number)
  @IsInt()
  @Min(1000)
  @Max(300000)
  run_lease_ms?: number = 30000;

  @IsOptional()
  @Type(() => Number)
  @IsInt()
  @Min(1000)
  @Max(86400000)
  override_duration_ms?: number;

  @IsOptional()
  @IsEnum(CommandSource)
  source?: CommandSource = CommandSource.MANUAL_OVERRIDE;
}
