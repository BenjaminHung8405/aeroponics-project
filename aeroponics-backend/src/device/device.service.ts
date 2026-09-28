import {
  Injectable,
  Logger,
  NotFoundException,
  OnModuleInit,
  OnModuleDestroy,
  Inject,
  forwardRef,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { DeviceStatus } from './entities/device_status.entity';
import { DeviceStatusResponseDto } from './dto/device-status-response.dto';
import { NodeService } from '../node/node.service';

@Injectable()
export class DeviceService implements OnModuleInit, OnModuleDestroy {
  private readonly logger = new Logger(DeviceService.name);
  private stalenessTimer: NodeJS.Timeout | null = null;

  constructor(
    @InjectRepository(DeviceStatus)
    private readonly deviceStatusRepository: Repository<DeviceStatus>,
    private readonly eventEmitter: EventEmitter2,
    @Inject(forwardRef(() => NodeService))
    private readonly nodeService: NodeService,
  ) {}

  onModuleInit(): void {
    // Automated staleness detection check loop (every 15s)
    this.stalenessTimer = setInterval(async () => {
      try {
        if (this.nodeService) {
          await this.nodeService.checkStaleness();
        }
        await this.checkDeviceStaleness();
      } catch (err: any) {
        this.logger.error(
          `Periodic staleness detection encountered an error: ${err.message}`,
        );
      }
    }, 15000);
  }

  onModuleDestroy(): void {
    if (this.stalenessTimer) {
      clearInterval(this.stalenessTimer);
      this.stalenessTimer = null;
    }
  }

  async getAllDevicesStatus(): Promise<DeviceStatusResponseDto[]> {
    const list = await this.deviceStatusRepository.find();
    if (list.length === 0) {
      return [
        {
          device_id: 'esp32_device',
          status: 'offline',
          uptime_s: 0,
          rssi_dbm: null,
          free_heap_b: null,
          ntp_synced: false,
          rtc_valid: false,
          time_source: null,
          last_sync_unix_time_utc: null,
          last_seen_at: null,
        },
      ];
    }

    return list.map((s) => ({
      device_id: s.device_id,
      status: s.status,
      uptime_s: Number(s.uptime_s || 0),
      rssi_dbm: s.rssi_dbm,
      free_heap_b: s.free_heap_b,
      ntp_synced: s.ntp_synced,
      rtc_valid: s.rtc_valid,
      time_source: s.time_source ?? null,
      last_sync_unix_time_utc: s.last_sync_unix_time_utc
        ? Number(s.last_sync_unix_time_utc)
        : null,
      last_seen_at: s.last_seen_at,
    }));
  }

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
      time_source: status.time_source ?? null,
      last_sync_unix_time_utc: status.last_sync_unix_time_utc
        ? Number(status.last_sync_unix_time_utc)
        : null,
      last_seen_at: status.last_seen_at,
    };
  }

  async checkDeviceStaleness(timeoutMs: number = 30000): Promise<void> {
    const onlineDevices = await this.deviceStatusRepository.find({
      where: { status: 'online' },
    });

    const now = Date.now();
    for (const dev of onlineDevices) {
      const lastSeen = dev.last_seen_at ? new Date(dev.last_seen_at).getTime() : 0;
      if (now - lastSeen > timeoutMs) {
        this.logger.warn(
          `Gateway device "${dev.device_id}" heartbeat timed out (last seen ${Math.floor((now - lastSeen) / 1000)}s ago). Marking offline.`,
        );
        dev.status = 'offline';
        await this.deviceStatusRepository.save(dev);

        this.eventEmitter.emit('device.status_changed', {
          deviceId: dev.device_id,
          status: 'offline',
          uptime_s: Number(dev.uptime_s || 0),
          rssi_dbm: dev.rssi_dbm,
          free_heap_b: dev.free_heap_b,
          ntpSynced: dev.ntp_synced,
          rtcValid: dev.rtc_valid,
          timeSource: dev.time_source ?? null,
          lastSyncUnixTimeUtc: dev.last_sync_unix_time_utc ?? null,
          lastSeenAt: dev.last_seen_at ? dev.last_seen_at.toISOString() : new Date().toISOString(),
          reason: 'HEARTBEAT_TIMEOUT',
        });
      }
    }
  }
}
