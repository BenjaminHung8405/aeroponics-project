import 'reflect-metadata';
import { plainToInstance } from 'class-transformer';
import { validate } from 'class-validator';
import { ControlSlotTargetType } from '../entities/control_slot.entity';
import { UpdateControlSlotDto } from './update-control-slot.dto';

describe('UpdateControlSlotDto', () => {
  it('accepts NODE targets through 15', async () => {
    const dto = plainToInstance(UpdateControlSlotDto, {
      target_type: ControlSlotTargetType.NODE,
      target_id: 15,
    });

    expect(await validate(dto)).toHaveLength(0);
  });

  it('rejects NODE targets above 15', async () => {
    const dto = plainToInstance(UpdateControlSlotDto, {
      target_type: ControlSlotTargetType.NODE,
      target_id: 16,
    });

    expect(await validate(dto)).not.toHaveLength(0);
  });

  it('rejects GROUP targets above 4', async () => {
    const dto = plainToInstance(UpdateControlSlotDto, {
      target_type: ControlSlotTargetType.GROUP,
      target_id: 5,
    });

    expect(await validate(dto)).not.toHaveLength(0);
  });

  it('allows an explicit clear payload', async () => {
    const dto = plainToInstance(UpdateControlSlotDto, {
      target_type: null,
      target_id: null,
    });

    expect(await validate(dto)).toHaveLength(0);
  });

  it.each([
    { target_type: ControlSlotTargetType.GROUP },
    { target_id: 2 },
    { target_type: null, target_id: 2 },
    { target_type: ControlSlotTargetType.NODE, target_id: null },
  ])('rejects partial target payload %o', async (payload) => {
    const dto = plainToInstance(UpdateControlSlotDto, payload);
    expect(await validate(dto)).not.toHaveLength(0);
  });
});
