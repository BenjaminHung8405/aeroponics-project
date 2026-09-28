import {
  Injectable,
  Logger,
  OnModuleInit,
  OnModuleDestroy,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { DeviceStatus } from '../device/entities/device_status.entity';
import { MqttService } from './mqtt.service';
import { MQTT_PUBLISH_TEMPLATES } from './mqtt.constants';

/** Push authoritative UTC epoch to every online gateway. */
const CLOCK_SYNC_INTERVAL_MS = 6 * 60 * 60 * 1000; // 6 hours
/** Asia/Ho_Chi_Minh has no DST, so a fixed offset is exact. */
const ICT_TZ_OFFSET_S = 7 * 3600;

/**
 * Backend-to-gateway authoritative time-set.
 *
 * The DS1307 crystal drifts roughly +/-20 s/day versus the retired DS3231's
 * +/-2 ppm, so the backend acts as a secondary reference and pushes the current
 * UTC epoch on a 6-hour cadence. A gateway suppresses the equivalent NTP
 * override for one full interval after a push, so this and SNTP cannot fight.
 */
@Injectable()
export class ClockSyncService implements OnModuleInit, OnModuleDestroy {
  private readonly logger = new Logger(ClockSyncService.name);
  private timer: NodeJS.Timeout | null = null;
  private syncCount = 0;

  constructor(
    @InjectRepository(DeviceStatus)
    private readonly deviceStatusRepo: Repository<DeviceStatus>,
    private readonly mqttService: MqttService,
  ) {}

  onModuleInit(): void {
    this.timer = setInterval(() => {
      this.syncAll().catch((err: any) => {
        this.logger.error(
          `Scheduled clock sync failed: ${err.message ?? err}`,
        );
      });
    }, CLOCK_SYNC_INTERVAL_MS);
    this.logger.log(
      `Clock sync scheduled every ${CLOCK_SYNC_INTERVAL_MS / 3600000}h.`,
    );
  }

  onModuleDestroy(): void {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  }

  /**
   * Manual one-shot push to a single gateway, e.g. after an operator replaces
   * the coin cell or observes a large drift.
   */
  async pushTimeToDevice(deviceId: string): Promise<void> {
    await this.publishClockCommand(deviceId, this.currentUnixUtc());
  }

  /**
   * Push authoritative time to every online gateway.
   * @returns the number of gateways that accepted the publish.
   */
  async syncAll(): Promise<number> {
    const gateways = await this.deviceStatusRepo.find({
      where: { status: 'online' },
    });

    if (gateways.length === 0) {
      this.logger.debug('Clock sync skipped: no online gateways.');
      return 0;
    }

    const nowUtc = this.currentUnixUtc();
    let synced = 0;

    for (const gateway of gateways) {
      try {
        await this.publishClockCommand(gateway.device_id, nowUtc);
        ++synced;
      } catch (err: any) {
        this.logger.warn(
          `Clock push to gateway "${gateway.device_id}" failed: ${err.message ?? err}`,
        );
      }
    }

    this.logger.log(
      `Clock sync #${++this.syncCount}: pushed epoch ${nowUtc} to ${synced}/${gateways.length} gateway(s).`,
    );
    return synced;
  }

  private currentUnixUtc(): number {
    return Math.floor(Date.now() / 1000);
  }

  private async publishClockCommand(
    deviceId: string,
    unixTimeUtc: number,
  ): Promise<void> {
    const localDate = new Date((unixTimeUtc + ICT_TZ_OFFSET_S) * 1000);
    await this.mqttService.publish(
      MQTT_PUBLISH_TEMPLATES.GATEWAY_CLOCK(deviceId),
      {
        // One command_id per epoch keeps the gateway's 60s dedup window from
        // suppressing a genuinely new correction.
        command_id: `clock-sync-${unixTimeUtc}`,
        version: 1,
        unix_time_utc: unixTimeUtc,
        tz_offset_s: ICT_TZ_OFFSET_S,
        local_time: localDate.toISOString().slice(0, 19),
      },
    );
  }
}
