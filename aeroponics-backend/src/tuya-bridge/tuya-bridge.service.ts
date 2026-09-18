import {
  Injectable,
  Inject,
  Logger,
  HttpException,
  HttpStatus,
  BadGatewayException,
  RequestTimeoutException,
  Optional,
  OnModuleInit,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { ConfigService } from '@nestjs/config';
import { EventEmitter2, OnEvent } from '@nestjs/event-emitter';
import {
  MeasurementReading,
  MeasurementTriggerType,
} from './entities/measurement_reading.entity';
import {
  TuyaMeasurementSession,
  TuyaSessionStatus,
  TuyaSessionTriggerType,
} from './entities/tuya_measurement_session.entity';
import { SystemSetting } from '../system-setting/entities/system_setting.entity';
import { SeasonService } from '../season/season.service';
import { SeasonEndedEvent } from '../season/events/season.events';
import {
  ITuyaDevice,
  TuyaDeviceFactory,
  TUYA_CLIENT_FACTORY,
} from './tuya-client.interface';
import { parseDps } from './dp-parser';
import {
  MeasurementFailedEvent,
  MeasurementRecordedEvent,
} from './events/tuya-bridge.events';
import {
  MeasurementHistoryResponse,
  MeasurementReadingResponse,
} from './dto/measurement-response.dto';
import {
  ToggleTuyaBridgeDto,
  TuyaBridgeStatusResponse,
} from './dto/tuya-status.dto';

@Injectable()
export class TuyaBridgeService implements OnModuleInit {
  private readonly logger = new Logger(TuyaBridgeService.name);

  // Concurrency & Rate Limiting state
  private isMeasuring = false;
  private lastMeasurementTimeMs = 0;

  // Persistent Runtime Enable/Disable state (Probe Protection Mode)
  private runtimeEnabled = false;
  private runtimeReason: string | null = null;
  private runtimeUpdatedAt: Date | null = null;
  private runtimeUpdatedBy: string | null = null;

  constructor(
    @InjectRepository(TuyaMeasurementSession)
    private readonly sessionRepository: Repository<TuyaMeasurementSession>,
    @InjectRepository(MeasurementReading)
    private readonly readingRepository: Repository<MeasurementReading>,
    @InjectRepository(SystemSetting)
    private readonly settingRepository: Repository<SystemSetting>,
    private readonly configService: ConfigService,
    private readonly seasonService: SeasonService,
    private readonly eventEmitter: EventEmitter2,
    @Optional()
    @Inject(TUYA_CLIENT_FACTORY)
    private readonly tuyaClientFactory?: TuyaDeviceFactory,
  ) {}

  async onModuleInit(): Promise<void> {
    try {
      const setting = await this.settingRepository.findOne({
        where: { key: 'tuya_bridge_enabled' },
      });
      if (setting && setting.value) {
        this.runtimeEnabled = Boolean(setting.value.enabled);
        this.runtimeReason = setting.value.reason || null;
        this.runtimeUpdatedAt = setting.updated_at;
        this.runtimeUpdatedBy = setting.updated_by;
      } else {
        const defaultSetting = this.settingRepository.create({
          key: 'tuya_bridge_enabled',
          value: {
            enabled: false,
            reason: 'Bảo vệ đầu dò cảm biến pH/EC/ORP cho thí nghiệm/vụ cuối',
          },
          description: 'Trạng thái kích hoạt Tuya PH-W218 Bridge',
        });
        await this.settingRepository.save(defaultSetting);
        this.runtimeEnabled = false;
        this.runtimeReason = defaultSetting.value.reason;
      }
      this.logger.log(
        `Tuya Bridge initialized: static_enabled=${this.isStaticConfigEnabled()}, runtime_enabled=${this.runtimeEnabled} (Active=${this.isBridgeEnabled()})`,
      );
    } catch (err) {
      this.logger.warn(
        `Could not load Tuya Bridge runtime setting: ${(err as Error).message}`,
      );
    }
  }

  /**
   * Helper to inspect static environment variable configuration.
   */
  isStaticConfigEnabled(): boolean {
    return this.configService.get<boolean>('TUYA_BRIDGE_ENABLED') ?? false;
  }

  /**
   * Master Gate Check: Bridge is active only if static config is true AND runtime toggle is enabled.
   */
  isBridgeEnabled(): boolean {
    return this.isStaticConfigEnabled() && this.runtimeEnabled;
  }

  /**
   * Get detailed bridge operational status, sensor identity, and probe protection information.
   */
  async getStatus(): Promise<TuyaBridgeStatusResponse> {
    const staticEnabled = this.isStaticConfigEnabled();
    const sensorId =
      this.configService.get<string>('TUYA_SENSOR_ID') || 'ph-w218-01';
    const deviceIp = this.configService.get<string>('TUYA_DEVICE_IP');
    const deviceId = this.configService.get<string>('TUYA_DEVICE_ID');
    const cooldownWindowMs =
      this.configService.get<number>('TUYA_COOLDOWN_WINDOW_MS') ?? 60000;
    const now = Date.now();
    const elapsed = now - this.lastMeasurementTimeMs;
    const cooldownRemainingS =
      this.lastMeasurementTimeMs > 0 && elapsed < cooldownWindowMs
        ? Math.ceil((cooldownWindowMs - elapsed) / 1000)
        : 0;

    let maskedDeviceId: string | undefined;
    if (deviceId && deviceId.length > 4) {
      maskedDeviceId = `***${deviceId.slice(-4)}`;
    }

    return {
      enabled: staticEnabled && this.runtimeEnabled,
      static_enabled: staticEnabled,
      runtime_enabled: this.runtimeEnabled,
      sensor_id: sensorId,
      device_ip: deviceIp,
      masked_device_id: maskedDeviceId,
      cooldown_remaining_s: cooldownRemainingS,
      is_measuring: this.isMeasuring,
      last_measurement_time:
        this.lastMeasurementTimeMs > 0
          ? new Date(this.lastMeasurementTimeMs).toISOString()
          : null,
      reason: this.runtimeReason,
      updated_at: this.runtimeUpdatedAt
        ? this.runtimeUpdatedAt.toISOString()
        : null,
      updated_by: this.runtimeUpdatedBy,
    };
  }

  /**
   * Operator/Admin dynamic runtime toggle to switch between Probe Protection Mode and Active Measurement Mode.
   */
  async setBridgeEnabled(
    dto: ToggleTuyaBridgeDto,
    operatorUserId?: string,
  ): Promise<TuyaBridgeStatusResponse> {
    const reason =
      dto.reason ||
      (dto.enabled
        ? 'Kích hoạt đo lường cho thí nghiệm / vụ cuối'
        : 'Tắt thiết bị để bảo vệ đầu dò cảm biến pH/EC/ORP');

    this.runtimeEnabled = dto.enabled;
    this.runtimeReason = reason;
    this.runtimeUpdatedAt = new Date();
    this.runtimeUpdatedBy = operatorUserId || null;

    try {
      let setting = await this.settingRepository.findOne({
        where: { key: 'tuya_bridge_enabled' },
      });
      if (!setting) {
        setting = this.settingRepository.create({
          key: 'tuya_bridge_enabled',
          value: { enabled: dto.enabled, reason },
          description: 'Trạng thái kích hoạt Tuya PH-W218 Bridge',
          updated_at: this.runtimeUpdatedAt,
          updated_by: this.runtimeUpdatedBy,
        });
      } else {
        setting.value = { enabled: dto.enabled, reason };
        setting.updated_at = this.runtimeUpdatedAt;
        setting.updated_by = this.runtimeUpdatedBy;
      }
      await this.settingRepository.save(setting);
    } catch (err) {
      this.logger.error(
        `Failed to persist Tuya bridge status to DB: ${(err as Error).message}`,
      );
    }

    this.logger.log(
      `Tuya Bridge toggle by [${operatorUserId || 'operator'}]: enabled=${dto.enabled} (reason: "${reason}")`,
    );

    return this.getStatus();
  }

  /**
   * Listen to Season Ended Event to gracefully snapshot water quality if enabled, or skip if probe is in storage.
   */
  @OnEvent('season.ended')
  async handleSeasonEnded(event: SeasonEndedEvent): Promise<void> {
    if (!this.isBridgeEnabled()) {
      this.logger.log(
        `Season #${event?.seasonId} ended, but Tuya Bridge is currently disabled (Probe in storage). Skipping end-of-season snapshot safely.`,
      );
      return;
    }

    try {
      this.logger.log(
        `Season #${event?.seasonId} ended and Tuya Bridge is enabled. Initiating automatic end-of-season water quality snapshot...`,
      );
      await this.measureOnDemand(
        'system:season_ended',
        MeasurementTriggerType.END_OF_SEASON,
      );
    } catch (err) {
      this.logger.error(
        `Automatic end-of-season Tuya measurement failed: ${(err as Error).message}`,
      );
    }
  }

  /**
   * S3-H1: Measure water quality on-demand via Tuya PH-W218.
   * STRICT HARD RULE S3-TUYA-ON-DEMAND-04: Zero polling loop, zero periodic timer.
   * Concurrency lock & 60s cooldown to prevent hardware socket congestion (HTTP 429).
   */
  async measureOnDemand(
    triggeredByUserId?: string | null,
    triggerType: MeasurementTriggerType = MeasurementTriggerType.ON_DEMAND,
  ): Promise<MeasurementReadingResponse> {
    const cooldownWindowMs =
      this.configService.get<number>('TUYA_COOLDOWN_WINDOW_MS') ?? 60000;
    const now = Date.now();

    // 0. Circuit Breaker Check (Probe Protection Mode)
    if (!this.isBridgeEnabled()) {
      const staticEnabled = this.isStaticConfigEnabled();
      let reasonDetail =
        this.runtimeReason ||
        'Bảo vệ đầu dò cảm biến pH/EC/ORP cho thí nghiệm/vụ cuối';
      if (!staticEnabled) {
        reasonDetail =
          'TUYA_BRIDGE_ENABLED bị vô hiệu hoá ở cấu hình môi trường hệ thống (static config)';
      }
      throw new HttpException(
        {
          statusCode: HttpStatus.CONFLICT,
          errorCode: 'TUYA_BRIDGE_DISABLED',
          message: `Bộ đo chất lượng nước Tuya PH-W218 đang ở chế độ TẮT (${reasonDetail}). Vui lòng kích hoạt thiết bị trước khi thực hiện đo.`,
        },
        HttpStatus.CONFLICT,
      );
    }

    // 1. Concurrency Check (Max 1 in-flight measurement)
    if (this.isMeasuring) {
      throw new HttpException(
        'Measurement session is already in progress. Concurrent measurements are rejected.',
        HttpStatus.TOO_MANY_REQUESTS,
      );
    }

    // 2. Cooldown Check (60s rate limit window)
    const elapsedSinceLast = now - this.lastMeasurementTimeMs;
    if (this.lastMeasurementTimeMs > 0 && elapsedSinceLast < cooldownWindowMs) {
      const remainingSeconds = Math.ceil(
        (cooldownWindowMs - elapsedSinceLast) / 1000,
      );
      throw new HttpException(
        `Measurement rate limit exceeded. Please wait ${remainingSeconds}s before triggering again.`,
        HttpStatus.TOO_MANY_REQUESTS,
      );
    }

    // Acquire lock
    this.isMeasuring = true;

    const sensorId =
      this.configService.get<string>('TUYA_SENSOR_ID') || 'ph-w218-01';
    const timeoutMs =
      this.configService.get<number>('TUYA_ON_DEMAND_TIMEOUT_MS') ?? 5000;

    // Determine active season if available
    let activeSeasonId: number | null = null;
    try {
      const activeSeason = await this.seasonService.getActive();
      if (activeSeason) {
        activeSeasonId = activeSeason.id;
      }
    } catch (err) {
      this.logger.warn(`Could not resolve active season: ${(err as Error).message}`);
    }

    // 3. Create Tuya Measurement Session in PENDING state
    let session = this.sessionRepository.create({
      sensor_id: sensorId,
      trigger_type:
        triggerType === MeasurementTriggerType.END_OF_SEASON
          ? TuyaSessionTriggerType.END_OF_SEASON
          : TuyaSessionTriggerType.ON_DEMAND,
      season_id: activeSeasonId,
      triggered_by_user_id: triggeredByUserId || null,
      status: TuyaSessionStatus.PENDING,
      started_at: new Date(),
    });
    session = await this.sessionRepository.save(session);

    let device: ITuyaDevice | null = null;

    try {
      // 4. Instantiate Tuya client (HARD RULE S3-TUYA-01: Never log local key!)
      const deviceIp = this.configService.get<string>('TUYA_DEVICE_IP');
      const deviceId = this.configService.get<string>('TUYA_DEVICE_ID');
      const localKey = this.configService.get<string>('TUYA_LOCAL_KEY');

      if (!deviceId || !localKey) {
        throw new BadGatewayException(
          'Tuya credentials not configured (TUYA_DEVICE_ID or TUYA_LOCAL_KEY missing).',
        );
      }

      device = this.createDevice({
        id: deviceId,
        key: localKey,
        ip: deviceIp,
        version: '3.3',
        issueGetOnConnect: false,
        issueRefreshOnConnect: false,
      });

      this.logger.log(
        `Initiating Tuya on-demand measurement session ${session.session_id} for sensor ${sensorId}...`,
      );

      // 5. Connect and fetch DPS with timeout protection
      const rawDps = await this.fetchWithTimeout(device, timeoutMs);

      // 6. Parse DPS with graceful missing property handling
      const parsed = parseDps(rawDps);

      // 7. Persist reading in TimescaleDB hypertable
      let reading = this.readingRepository.create({
        time: new Date(),
        session_id: session.session_id,
        sensor_id: sensorId,
        trigger_type: triggerType,
        ph_value: parsed.ph_value,
        ec_value: parsed.ec_value,
        tds_value: parsed.tds_value,
        temperature_c: parsed.temperature_c,
        salinity_ppm: parsed.salinity_ppm,
        orp_mv: parsed.orp_mv,
        turbidity_ntu: parsed.turbidity_ntu,
        battery_pct: parsed.battery_pct,
        calibrated_at: null,
        triggered_by_user_id: triggeredByUserId || null,
      });
      reading = await this.readingRepository.save(reading);

      // 8. Update Session to COMPLETED
      session.status = TuyaSessionStatus.COMPLETED;
      session.completed_at = new Date();
      await this.sessionRepository.save(session);

      // Update cooldown timestamp
      this.lastMeasurementTimeMs = Date.now();

      // Emit event for downstream broadcast (WebSocket / Alert)
      this.eventEmitter.emit(
        'measurement.recorded',
        new MeasurementRecordedEvent(reading, session),
      );

      this.logger.log(
        `Measurement completed successfully: pH=${reading.ph_value}, Temp=${reading.temperature_c}°C, EC=${reading.ec_value}`,
      );

      return this.toReadingResponse(reading);
    } catch (error) {
      const err = error as Error;
      // HARD RULE S3-TUYA-01: Sanitize error message to ensure no local keys are leaked
      const sanitizedError = this.sanitizeErrorMessage(err.message || 'Unknown error');

      this.logger.error(
        `Tuya measurement failed for session ${session.session_id}: ${sanitizedError}`,
      );

      // Update Session to FAILED
      session.status = TuyaSessionStatus.FAILED;
      session.completed_at = new Date();
      session.error_message = sanitizedError;
      await this.sessionRepository.save(session);

      this.eventEmitter.emit(
        'measurement.failed',
        new MeasurementFailedEvent(
          session.session_id,
          sensorId,
          sanitizedError,
          triggeredByUserId,
        ),
      );

      if (err instanceof HttpException) {
        throw err;
      }

      const lowerMsg = err.message?.toLowerCase() || '';
      if (lowerMsg.includes('timeout') || lowerMsg.includes('timed out')) {
        throw new RequestTimeoutException(
          `Tuya device measurement timed out after ${timeoutMs}ms.`,
        );
      }

      throw new BadGatewayException(
        `Tuya sensor communication failed: ${sanitizedError}`,
      );
    } finally {
      // Clean up connection immediately (HARD RULE S3-TUYA-07)
      if (device) {
        try {
          device.disconnect();
        } catch (disconnectErr) {
          this.logger.warn(
            `Error disconnecting Tuya device: ${(disconnectErr as Error).message}`,
          );
        }
      }
      this.isMeasuring = false;
    }
  }

  /**
   * S3-H2: Get latest water quality measurement reading.
   */
  async getLatest(): Promise<MeasurementReadingResponse | null> {
    const sensorId =
      this.configService.get<string>('TUYA_SENSOR_ID') || 'ph-w218-01';

    const latest = await this.readingRepository.findOne({
      where: { sensor_id: sensorId },
      order: { time: 'DESC' },
    });

    if (!latest) {
      return null;
    }

    return this.toReadingResponse(latest);
  }

  /**
   * S3-H2: Get historical measurement readings with pagination and optional trigger_type filter.
   */
  async getHistory(
    limit: number = 20,
    offset: number = 0,
    triggerType?: MeasurementTriggerType,
  ): Promise<MeasurementHistoryResponse> {
    const sensorId =
      this.configService.get<string>('TUYA_SENSOR_ID') || 'ph-w218-01';

    const query = this.readingRepository
      .createQueryBuilder('reading')
      .where('reading.sensor_id = :sensorId', { sensorId });

    if (triggerType) {
      query.andWhere('reading.trigger_type = :triggerType', { triggerType });
    }

    query
      .orderBy('reading.time', 'DESC')
      .skip(offset)
      .take(limit);

    const [readings, total] = await query.getManyAndCount();

    return {
      readings: readings.map((r) => this.toReadingResponse(r)),
      total,
      limit,
      offset,
    };
  }

  /**
   * Reset cooldown, lock, and runtime enable state (useful for unit testing).
   */
  resetStateForTesting(runtimeEnabled = true): void {
    this.isMeasuring = false;
    this.lastMeasurementTimeMs = 0;
    this.runtimeEnabled = runtimeEnabled;
  }

  private createDevice(options: any): ITuyaDevice {
    if (this.tuyaClientFactory) {
      return this.tuyaClientFactory(options);
    }

    // Default to dynamic require for tuyapi
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const TuyAPI = require('tuyapi');
    return new TuyAPI(options);
  }

  private async fetchWithTimeout(
    device: ITuyaDevice,
    timeoutMs: number,
  ): Promise<Record<string, any>> {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        reject(new Error(`Tuya operation timed out after ${timeoutMs}ms`));
      }, timeoutMs);

      (async () => {
        try {
          await device.connect();
          const response = await device.get({ schema: true });
          clearTimeout(timer);

          if (response && typeof response === 'object' && 'dps' in response) {
            resolve(response.dps);
          } else if (response && typeof response === 'object') {
            resolve(response);
          } else {
            resolve({});
          }
        } catch (err) {
          clearTimeout(timer);
          reject(err);
        }
      })();
    });
  }

  private sanitizeErrorMessage(msg: string): string {
    const localKey = this.configService.get<string>('TUYA_LOCAL_KEY');
    if (localKey && localKey.length > 0 && msg.includes(localKey)) {
      return msg.replace(new RegExp(localKey, 'g'), '[REDACTED_KEY]');
    }
    return msg;
  }

  private toReadingResponse(
    reading: MeasurementReading,
  ): MeasurementReadingResponse {
    return {
      time: reading.time,
      session_id: reading.session_id,
      sensor_id: reading.sensor_id,
      trigger_type: reading.trigger_type,
      ph_value: reading.ph_value,
      ec_value: reading.ec_value,
      tds_value: reading.tds_value,
      temperature_c: reading.temperature_c,
      salinity_ppm: reading.salinity_ppm,
      orp_mv: reading.orp_mv,
      turbidity_ntu: reading.turbidity_ntu,
      battery_pct: reading.battery_pct,
      calibrated_at: reading.calibrated_at,
      triggered_by_user_id: reading.triggered_by_user_id,
    };
  }
}
