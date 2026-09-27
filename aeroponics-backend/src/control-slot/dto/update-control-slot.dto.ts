import {
  IsEnum,
  Validate,
  ValidateIf,
  ValidatorConstraint,
  ValidatorConstraintInterface,
  ValidationArguments,
} from 'class-validator';
import { Type } from 'class-transformer';
import { ControlSlotTargetType } from '../entities/control_slot.entity';

@ValidatorConstraint({ name: 'controlSlotTargetId', async: false })
class ControlSlotTargetIdConstraint implements ValidatorConstraintInterface {
  validate(value: unknown, args: ValidationArguments): boolean {
    if (value === null || value === undefined) return true;
    if (!Number.isInteger(value) || (value as number) < 1) return false;

    const dto = args.object as UpdateControlSlotDto;
    if (dto.target_type === ControlSlotTargetType.NODE) return (value as number) <= 15;
    if (dto.target_type === ControlSlotTargetType.GROUP) return (value as number) <= 4;
    return false;
  }

  defaultMessage(args: ValidationArguments): string {
    const dto = args.object as UpdateControlSlotDto;
    return dto.target_type === ControlSlotTargetType.GROUP
      ? 'GROUP target_id must be an integer between 1 and 4.'
      : 'NODE target_id must be an integer between 1 and 15.';
  }
}

@ValidatorConstraint({ name: 'controlSlotTargetPair', async: false })
class ControlSlotTargetPairConstraint implements ValidatorConstraintInterface {
  validate(_value: unknown, args: ValidationArguments): boolean {
    const dto = args.object as UpdateControlSlotDto;
    const typeIsEmpty = dto.target_type === null || dto.target_type === undefined;
    const idIsEmpty = dto.target_id === null || dto.target_id === undefined;
    if (dto.target_type === undefined && dto.target_id === undefined) return false;
    return typeIsEmpty === idIsEmpty;
  }

  defaultMessage(): string {
    return 'target_type and target_id must both be provided or both be null.';
  }
}

export class UpdateControlSlotDto {
  @ValidateIf((dto: UpdateControlSlotDto) => dto.target_type !== null && dto.target_type !== undefined)
  @IsEnum(ControlSlotTargetType, { message: 'target_type must be NODE or GROUP.' })
  target_type?: ControlSlotTargetType | null;

  @Type(() => Number)
  @Validate(ControlSlotTargetPairConstraint)
  @Validate(ControlSlotTargetIdConstraint)
  target_id?: number | null;
}
