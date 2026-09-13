import {
  Injectable,
  Inject,
  Logger,
  HttpException,
  HttpStatus,
  BadGatewayException,
  RequestTimeoutException,
  Optional,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { ConfigService } from '@nestjs/config';
import { EventEmitter2 } from '@nestjs/event-emitter';
import {
  MeasurementReading,
  MeasurementTriggerType,
} from './entities/measurement_reading.entity';
import {
  TuyaMeasurementSession,
  TuyaSessionStatus,
  TuyaSessionTriggerType,
} from './entities/tuya_measurement_session.entity';
import { SeasonService } from '../season/season.service';
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

@Injectable()
export class TuyaBridgeService {
  private readonly logger = new Logger(TuyaBridgeService.name);

  // Concurrency & Rate Limiting state
  private isMeasuring = false;
  private lastMeasurementTimeMs = 0;

  constructor(
    @InjectRepository(TuyaMeasurementSession)
    private readonly sessionRepository: Repository<TuyaMeasurementSession>,
    @InjectRepository(MeasurementReading)
    private readonly readingRepository: Repository<MeasurementReading>,
    private readonly configService: ConfigService,
    private readonly seasonService: SeasonService,
    private readonly eventEmitter: EventEmitter2,
    @Optional()
    @Inject(TUYA_CLIENT_FACTORY)
    private readonly tuyaClientFactory?: TuyaDeviceFactory,
  ) {}

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
   * Reset cooldown and lock state (useful for unit testing).
   */
  resetStateForTesting(): void {
    this.isMeasuring = false;
    this.lastMeasurementTimeMs = 0;
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
