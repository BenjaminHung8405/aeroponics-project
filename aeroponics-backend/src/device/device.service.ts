import { Injectable, NotFoundException } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { DeviceStatus } from './entities/device_status.entity';
import { DeviceStatusResponseDto } from './dto/device-status-response.dto';

@Injectable()
export class DeviceService {
  constructor(
    @InjectRepository(DeviceStatus)
    private readonly deviceStatusRepository: Repository<DeviceStatus>,
  ) {}

  async getDeviceStatus(deviceId: string): Promise<DeviceStatusResponseDto> {
    const status = await this.deviceStatusRepository.findOne({
      where: { device_id: deviceId },
    });

    if (!status) {
      throw new NotFoundException(
        `Device status not found for device: ${deviceId}`,
      );
    }

    return {
      device_id: status.device_id,
      status: status.status,
      uptime_s: Number(status.uptime_s || 0),
      rssi_dbm: status.rssi_dbm,
      free_heap_b: status.free_heap_b,
      ntp_synced: status.ntp_synced,
      rtc_valid: status.rtc_valid,
      last_seen_at: status.last_seen_at,
    };
  }
}
