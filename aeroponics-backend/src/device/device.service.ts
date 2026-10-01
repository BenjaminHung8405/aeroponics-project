import {
  Injectable,
  Logger,
  NotFoundException,
  OnModuleInit,
  OnModuleDestroy,
  Inject,
  forwardRef,
  Optional,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { EventEmitter2, OnEvent } from '@nestjs/event-emitter';
import { Device } from './entities/device.entity';
import { DeviceStatus } from './entities/device_status.entity';
import { DeviceStatusResponseDto } from './dto/device-status-response.dto';
import { UpdateDeviceDto } from './dto/update-device.dto';
import { NodeService } from '../node/node.service';
import { ClockSyncService } from '../mqtt/clock-sync.service';
import { ScheduleStateSyncService } from '../mqtt/schedule-state-sync.service';


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
    @Optional()
    @Inject(forwardRef(() => ClockSyncService))
    private readonly clockSyncService?: ClockSyncService,
    @Optional()
    @InjectRepository(Device)
    private readonly deviceRepository?: Repository<Device>,
    @Optional()
    @Inject(forwardRef(() => ScheduleStateSyncService))
    private readonly scheduleStateSyncService?: ScheduleStateSyncService,
  ) {}

  onModuleInit(): void {
    // Automated staleness detection check loop (every 15s)
    this.stalenessTimer = setInterval(async () => {
      try {
        if (this.nodeService) {
          await this.nodeService.checkStaleness();
        }
        await this.checkDeviceStaleness();
        if (this.scheduleStateSyncService) {
          await this.scheduleStateSyncService.checkScheduleSyncFreshness();
        }
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
    // Ignore the retired single-gateway placeholder from old databases.
    const list = (await this.deviceStatusRepository.find()).filter(
      (status) => status.device_id !== 'esp32_device',
    );
    let devicesMap = new Map<string, Device>();
    if (this.deviceRepository) {
      try {
        const devs = await this.deviceRepository.find();
        devicesMap = new Map(
          devs
            .filter((device) => device.device_id !== 'esp32_device')
            .map((device) => [device.device_id, device]),
        );
      } catch {
        // Ignored
      }
    }

    if (list.length === 0) {
      if (devicesMap.size > 0) {
        return Array.from(devicesMap.values()).map((d) => ({
          device_id: d.device_id,
          display_name: d.display_name ?? null,
          enabled: d.enabled,
          status: 'offline',
          uptime_s: 0,
          rssi_dbm: null,
          free_heap_b: null,
          ntp_synced: false,
          rtc_valid: false,
          time_source: null,
          last_sync_unix_time_utc: null,
        last_seen_at: d.last_seen_at ?? null,
        syncState: 'UNCONFIRMED',
        reportedScheduleState: null,
        scheduleSyncUpdatedAt: null,
        scheduleSyncDetails: null,
      }));
      }

      return [];
    }

    return list.map((s) => {
      const dev = devicesMap.get(s.device_id);
      return {
        device_id: s.device_id,
        display_name: dev?.display_name ?? null,
        enabled: dev?.enabled ?? true,
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
        syncState: s.schedule_sync_state ?? 'UNCONFIRMED',
        reportedScheduleState: s.reported_schedule_state ?? null,
        scheduleSyncUpdatedAt: s.schedule_sync_updated_at ?? null,
        scheduleSyncDetails: s.schedule_sync_details ?? null,
      };
    });
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
      syncState: status.schedule_sync_state ?? 'UNCONFIRMED',
      reportedScheduleState: status.reported_schedule_state ?? null,
      scheduleSyncUpdatedAt: status.schedule_sync_updated_at ?? null,
      scheduleSyncDetails: status.schedule_sync_details ?? null,
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

  async syncDeviceClock(
    deviceId: string,
  ): Promise<{ success: boolean; device_id: string; timestamp: number }> {
    const status = await this.deviceStatusRepository.findOne({
      where: { device_id: deviceId },
    });

    if (!status) {
      throw new NotFoundException(
        `Device status not found for device: ${deviceId}`,
      );
    }

    const timestamp = Math.floor(Date.now() / 1000);
    if (this.clockSyncService) {
      await this.clockSyncService.pushTimeToDevice(deviceId);
    }

    return {
      success: true,
      device_id: deviceId,
      timestamp,
    };
  }

  @OnEvent('device.status_changed')
  async handleDeviceStatusChanged(event: { deviceId: string }): Promise<void> {
    if (event?.deviceId) {
      await this.ensureDeviceRegistered(event.deviceId);
    }
  }

  async ensureDeviceRegistered(
    deviceId: string,
    displayName?: string,
  ): Promise<Device | null> {
    if (!this.deviceRepository) return null;
    let dev = await this.deviceRepository.findOne({ where: { device_id: deviceId } });
    if (!dev) {
      const defaultName =
        displayName ??
        (deviceId.startsWith('aero_s3_')
          ? `ESP32 (${deviceId.slice(-6)})`
          : deviceId);

      dev = this.deviceRepository.create({
        device_id: deviceId,
        display_name: defaultName,
        mqtt_username: deviceId,
        enabled: true,
        last_seen_at: new Date(),
      });

      try {
        dev = await this.deviceRepository.save(dev);
        this.logger.log(
          `Auto-provisioned new device in database: ${deviceId} ("${defaultName}")`,
        );
        this.eventEmitter.emit('device.registered', { deviceId });
      } catch (err: any) {
        // Race condition handler if concurrent events save the device
        dev = await this.deviceRepository.findOne({ where: { device_id: deviceId } });
      }
    }
    return dev;
  }

  async updateDevice(
    deviceId: string,
    dto: UpdateDeviceDto,
  ): Promise<Device> {
    if (!this.deviceRepository) {
      throw new NotFoundException(`Device repository unavailable.`);
    }

    let dev = await this.deviceRepository.findOne({ where: { device_id: deviceId } });
    if (!dev) {
      // Auto-provision if updating a recognized device not yet saved
      dev = await this.ensureDeviceRegistered(deviceId);
      if (!dev) {
        throw new NotFoundException(`Device not found: ${deviceId}`);
      }
    }

    if (dto.display_name !== undefined) {
      dev.display_name = dto.display_name;
    }
    if (dto.enabled !== undefined) {
      dev.enabled = dto.enabled;
    }

    const saved = await this.deviceRepository.save(dev);
    this.logger.log(
      `Updated device ${deviceId}: display_name="${saved.display_name}", enabled=${saved.enabled}`,
    );
    return saved;
  }
}
