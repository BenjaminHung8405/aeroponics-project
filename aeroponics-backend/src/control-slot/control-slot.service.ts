import {
  BadRequestException,
  ConflictException,
  Injectable,
  Optional,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { QueryFailedError } from 'typeorm';
import {
  ControlSlot,
  ControlSlotTargetType,
} from './entities/control_slot.entity';
import { UpdateControlSlotDto } from './dto/update-control-slot.dto';
import { MqttService } from '../mqtt/mqtt.service';

export interface ControlSlotResponse {
  slot_index: number;
  target_type: ControlSlotTargetType | null;
  target_id: number | null;
  updated_at: Date | null;
  updated_by: string | null;
}

/** Payload pushed to firmware on every slot change */
export interface ControlSlotFirmwarePayload {
  slots: { idx: number; type: string | null; id: number | null }[];
}

@Injectable()
export class ControlSlotService {
  constructor(
    @InjectRepository(ControlSlot)
    private readonly repository: Repository<ControlSlot>,
    @Optional() private readonly mqttService?: MqttService,
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

    // Push full slot table to firmware as a retained MQTT message so the HMI
    // map is always current — even if the gateway was offline during the update.
    await this.pushSlotConfigToFirmware(deviceId).catch(() => {
      // Non-fatal: firmware will pick up the retained message on next reconnect.
    });

    return this.toResponse(saved, slotIndex);
  }

  /**
   * Build and publish full 4-slot configuration as a retained MQTT message.
   *
   * Topic:   `aeroponics/device/<deviceId>/config/control_slots`
   * Payload: `{ "slots": [{ "idx": 1, "type": "NODE", "id": 7 }, ...] }`
   *
   * The `retain: true` flag ensures the firmware always receives the latest
   * mapping even when it reconnects after an outage.
   */
  async pushSlotConfigToFirmware(deviceId: string): Promise<void> {
    if (!this.mqttService?.isConnected()) return;

    const payload = await this.buildSlotPayload(deviceId);
    const topic = `aeroponics/device/${deviceId}/config/control_slots`;
    await this.mqttService.publish(topic, payload, { qos: 1, retain: true });
  }

  async buildSlotPayload(deviceId: string): Promise<ControlSlotFirmwarePayload> {
    const responses = await this.getSlots(deviceId);
    return {
      slots: responses.map((r) => ({
        idx: r.slot_index,
        type: r.target_type ?? null,
        id: r.target_id ?? null,
      })),
    };
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
