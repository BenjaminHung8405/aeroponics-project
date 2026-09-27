import {
  IsEnum,
  IsInt,
  IsOptional,
  IsIn,
  Max,
  Min,
  Validate,
  ValidateIf,
  ValidatorConstraint,
  ValidatorConstraintInterface,
  ValidationArguments,
} from 'class-validator';
import { Type } from 'class-transformer';
import { CommandSource, PumpAction } from '../entities/pump_command.entity';
import { MODERN_NODE_IDS } from '../../node/node-topology';

export enum PumpTargetType {
  NODE = 'NODE',
  GROUP = 'GROUP',
}

@ValidatorConstraint({ name: 'pumpTargetPair', async: false })
class PumpTargetPairConstraint implements ValidatorConstraintInterface {
  validate(_value: unknown, args: ValidationArguments): boolean {
    const dto = args.object as SendPumpCommandDto;
    const hasNode = dto.node_id !== undefined && dto.node_id !== null;
    const hasGroup = dto.group_id !== undefined && dto.group_id !== null;
    if (dto.target_type === PumpTargetType.NODE) return hasNode && !hasGroup;
    if (dto.target_type === PumpTargetType.GROUP) return hasGroup && !hasNode;
    return !hasNode && !hasGroup;
  }

  defaultMessage(): string {
    return 'target_type must match exactly one target: NODE + node_id or GROUP + group_id.';
  }
}

export class SendPumpCommandDto {
  @IsEnum(PumpAction, {
    message: 'action must be either ON or OFF',
  })
  @Validate(PumpTargetPairConstraint)
  action: PumpAction;

  @IsOptional()
  @IsEnum(PumpTargetType)
  target_type?: PumpTargetType;

  @IsOptional()
  @ValidateIf((dto: SendPumpCommandDto) => dto.target_type !== PumpTargetType.GROUP)
  @Type(() => Number)
  @IsInt()
  @IsIn(MODERN_NODE_IDS)
  node_id?: number;

  @IsOptional()
  @ValidateIf((dto: SendPumpCommandDto) => dto.target_type !== PumpTargetType.NODE)
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
