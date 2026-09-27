import {
  BadRequestException,
  ConflictException,
  Injectable,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { QueryFailedError } from 'typeorm';
import {
  ControlSlot,
  ControlSlotTargetType,
} from './entities/control_slot.entity';
import { UpdateControlSlotDto } from './dto/update-control-slot.dto';

export interface ControlSlotResponse {
  slot_index: number;
  target_type: ControlSlotTargetType | null;
  target_id: number | null;
  updated_at: Date | null;
  updated_by: string | null;
}

@Injectable()
export class ControlSlotService {
  constructor(
    @InjectRepository(ControlSlot)
    private readonly repository: Repository<ControlSlot>,
  ) {}

  async getSlots(deviceId: string): Promise<ControlSlotResponse[]> {
    const rows = await this.repository.find({
      where: { device_id: deviceId },
      order: { slot_index: 'ASC' },
    });
    const byIndex = new Map(rows.map((row) => [row.slot_index, row]));
    return [1, 2, 3, 4].map((slotIndex) => this.toResponse(byIndex.get(slotIndex), slotIndex));
  }

  async updateSlot(
    deviceId: string,
    slotIndex: number,
    dto: UpdateControlSlotDto,
    updatedBy: string,
  ): Promise<ControlSlotResponse> {
    this.validateSlotIndex(slotIndex);
    const targetType = dto.target_type ?? null;
    const targetId = dto.target_id ?? null;

    if (targetType === null || targetId === null) {
      if (targetType !== null || targetId !== null) {
        throw new BadRequestException('target_type and target_id must both be provided or both be null.');
      }
    } else {
      this.validateTarget(targetType, targetId);
    }

    const existing = await this.repository.findOne({ where: { device_id: deviceId, slot_index: slotIndex } });
    if (targetType !== null && targetId !== null) {
      const duplicate = await this.repository.findOne({
        where: { device_id: deviceId, target_type: targetType, target_id: targetId },
      });
      if (duplicate && duplicate.slot_index !== slotIndex) {
        throw new ConflictException(`Target ${targetType}:${targetId} is already assigned to slot ${duplicate.slot_index}.`);
      }
    }

    const row = existing ?? this.repository.create({ device_id: deviceId, slot_index: slotIndex });
    row.target_type = targetType;
    row.target_id = targetId;
    row.updated_by = updatedBy;
    let saved: ControlSlot;
    try {
      saved = await this.repository.save(row);
    } catch (error) {
      if (error instanceof QueryFailedError && (error as QueryFailedError & { driverError?: { code?: string } }).driverError?.code === '23505') {
        throw new ConflictException(`Target ${targetType}:${targetId} is already assigned to another slot.`);
      }
      throw error;
    }
    return this.toResponse(saved, slotIndex);
  }

  private toResponse(row: ControlSlot | undefined, slotIndex: number): ControlSlotResponse {
    return {
      slot_index: slotIndex,
      target_type: row?.target_type ?? null,
      target_id: row?.target_id ?? null,
      updated_at: row?.updated_at ?? null,
      updated_by: row?.updated_by ?? null,
    };
  }

  private validateSlotIndex(slotIndex: number): void {
    if (!Number.isInteger(slotIndex) || slotIndex < 1 || slotIndex > 4) {
      throw new BadRequestException('slotIndex must be an integer between 1 and 4.');
    }
  }

  private validateTarget(targetType: ControlSlotTargetType, targetId: number): void {
    if (!Number.isInteger(targetId) || targetId < 1 || (targetType === ControlSlotTargetType.NODE && targetId > 15) || (targetType === ControlSlotTargetType.GROUP && targetId > 4)) {
      throw new BadRequestException(
        targetType === ControlSlotTargetType.NODE
          ? 'NODE target_id must be an integer between 1 and 15.'
          : 'GROUP target_id must be an integer between 1 and 4.',
      );
    }
  }
}
