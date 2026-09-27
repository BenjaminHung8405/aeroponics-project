import { ConflictException, BadRequestException } from '@nestjs/common';
import { ControlSlotService } from './control-slot.service';
import { ControlSlotTargetType } from './entities/control_slot.entity';

describe('ControlSlotService', () => {
  const repository = {
    find: jest.fn(),
    findOne: jest.fn(),
    create: jest.fn((value) => value),
    save: jest.fn((value) => Promise.resolve({ ...value, updated_at: new Date() })),
  } as any;
  let service: ControlSlotService;

  beforeEach(() => {
    jest.clearAllMocks();
    service = new ControlSlotService(repository);
    repository.find.mockResolvedValue([]);
    repository.findOne.mockResolvedValue(undefined);
  });

  it('returns four device-scoped slots including empty slots', async () => {
    const result = await service.getSlots('device-a');
    expect(result.map((slot) => slot.slot_index)).toEqual([1, 2, 3, 4]);
    expect(result.every((slot) => slot.target_type === null && slot.target_id === null)).toBe(true);
  });

  it('validates target-specific ranges', async () => {
    await expect(service.updateSlot('device-a', 1, { target_type: ControlSlotTargetType.NODE, target_id: 16 }, 'admin')).rejects.toBeInstanceOf(BadRequestException);
    await expect(service.updateSlot('device-a', 1, { target_type: ControlSlotTargetType.GROUP, target_id: 5 }, 'admin')).rejects.toBeInstanceOf(BadRequestException);
  });

  it('rejects duplicate targets within a device', async () => {
    repository.findOne.mockResolvedValueOnce(undefined).mockResolvedValueOnce({ slot_index: 2 });
    await expect(service.updateSlot('device-a', 1, { target_type: ControlSlotTargetType.NODE, target_id: 4 }, 'admin')).rejects.toBeInstanceOf(ConflictException);
  });

  it('supports clear payloads and records audit actor', async () => {
    repository.findOne.mockResolvedValue({ slot_index: 1, target_type: ControlSlotTargetType.NODE, target_id: 4 });
    await service.updateSlot('device-a', 1, { target_type: null, target_id: null }, 'admin');
    expect(repository.save).toHaveBeenCalledWith(expect.objectContaining({ target_type: null, target_id: null, updated_by: 'admin' }));
  });
});
