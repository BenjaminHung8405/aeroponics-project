import { Injectable, Logger } from '@nestjs/common';
import { OnEvent, EventEmitter2 } from '@nestjs/event-emitter';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';

import { MQTT_EVENTS } from '../mqtt/mqtt.constants';
import {
  MeasurementReading,
  MeasurementTriggerType,
} from './entities/measurement_reading.entity';
import {
  TuyaMeasurementSession,
  TuyaSessionStatus,
} from './entities/tuya_measurement_session.entity';
import { SeasonService } from '../season/season.service';
import { MeasurementRecordedEvent } from './events/tuya-bridge.events';

export interface WaterQualityPayload {
  sensor_id?: string;
  session_id?: string | null;
  trigger_type?: string | null;
  ph?: number | null;
  ec?: number | null;
  tds?: number | null;
  temperature_c?: number | null;
  orp?: number | null;
  salinity?: number | null;
  turbidity?: number | null;
  battery?: number | null;
  specific_gravity?: number | null;
  conductivity_factor?: number | null;
  humidity?: number | null;
  timestamp?: string;
}

export interface WaterQualityStatusPayload {
  status: 'online' | 'offline' | 'error';
  sensor_id?: string;
  session_id?: string | null;
  error?: string | null;
  timestamp?: string;
}

@Injectable()
export class WaterQualityIngestionService {
  private readonly logger = new Logger(WaterQualityIngestionService.name);

  // Deadband state tracking
  private lastSavedReading: {
    ph: number | null;
    ec: number | null;
    savedAtMs: number;
  } = {
    ph: null,
    ec: null,
    savedAtMs: 0,
  };

  // Sensor online status
  private currentStatus: 'online' | 'offline' = 'online';

  constructor(
    @InjectRepository(MeasurementReading)
    private readonly readingRepository: Repository<MeasurementReading>,
    @InjectRepository(TuyaMeasurementSession)
    private readonly sessionRepository: Repository<TuyaMeasurementSession>,
    private readonly seasonService: SeasonService,
    private readonly eventEmitter: EventEmitter2,
  ) {}

  @OnEvent(MQTT_EVENTS.WATER_QUALITY_STATUS)
  handleStatusEvent(event: {
    sensorId: string;
    payload: WaterQualityStatusPayload;
    receivedAt?: Date;
  }): void {
    const rawStatus = event.payload?.status || 'offline';
    const status = rawStatus === 'error' ? 'offline' : rawStatus;
    this.currentStatus = status as 'online' | 'offline';
    this.logger.log(`Water quality sensor [${event.sensorId}] status: ${rawStatus}`);

    // If edge bridge reported an error for an in-flight session, notify listeners
    if (event.payload?.session_id && rawStatus === 'error') {
      this.eventEmitter.emit(`measurement.session.${event.payload.session_id}`, {
        error: event.payload.error || 'Sensor query failed at local edge bridge',
      });
    } else if (rawStatus === 'error') {
      this.logger.warn(
        `Water quality sensor [${event.sensorId}] reported an error without a session_id; an on-demand request cannot be correlated.`,
      );
    } else if (rawStatus === 'offline') {
      this.logger.warn(
        `Water quality sensor [${event.sensorId}] is offline; in-flight measurements will end at their configured timeout.`,
      );
    }

    // Emit event for WebSocket broadcasting
    this.eventEmitter.emit('water_quality.status', {
      sensor_id: event.sensorId,
      status,
      timestamp: event.payload?.timestamp || new Date().toISOString(),
    });
  }

  @OnEvent(MQTT_EVENTS.WATER_QUALITY_TELEMETRY)
  async handleTelemetryEvent(event: {
    sensorId: string;
    payload: WaterQualityPayload;
    receivedAt?: Date;
  }): Promise<void> {
    const data = event.payload;
    if (!data) return;

    const sensorId = event.sensorId || data.sensor_id || 'ph-w218-01';
    const phVal = data.ph ?? null;
    const ecVal = data.ec ?? null;

    // Fetch active season to check agronomic target thresholds
    let activeSeasonId: number | null = null;
    let targetPh: number | null = null;
    let targetEc: number | null = null;
    let isPhOutOfRange = false;
    let isEcOutOfRange = false;

    try {
      const activeSeason = await this.seasonService.getActive();
      if (activeSeason) {
        activeSeasonId = activeSeason.id;
        targetPh = (activeSeason as any).target_ph != null ? Number((activeSeason as any).target_ph) : null;
        targetEc = (activeSeason as any).target_ec != null ? Number((activeSeason as any).target_ec) : null;

        if (phVal != null && targetPh != null) {
          // Warning if pH deviates by > 0.4 from agronomic recipe target
          isPhOutOfRange = Math.abs(phVal - targetPh) > 0.4;
        }

        if (ecVal != null && targetEc != null) {
          // target_ec is typically in mS/cm (e.g. 1.8), while ecVal is in µS/cm (e.g. 1800)
          const targetEcUs = targetEc < 10 ? targetEc * 1000 : targetEc;
          isEcOutOfRange = Math.abs(ecVal - targetEcUs) > 300;
        }
      }
    } catch (err: any) {
      this.logger.warn(`Could not fetch active season for targets: ${err.message}`);
    }

    // 1. Evaluate Deadband for TimescaleDB Persistence
    // If this is an explicit on-demand measurement session (session_id present), always persist
    const isExplicitSession = Boolean(data.session_id);
    const shouldPersist = isExplicitSession || this.shouldPersistReading(phVal, ecVal);
    if (shouldPersist) {
      try {
        let triggerType = MeasurementTriggerType.ON_DEMAND;
        if (data.trigger_type === 'END_OF_SEASON') {
          triggerType = MeasurementTriggerType.END_OF_SEASON;
        }

        const reading = this.readingRepository.create({
          time: data.timestamp ? new Date(data.timestamp) : new Date(),
          session_id: data.session_id || null,
          sensor_id: sensorId,
          trigger_type: triggerType,
          ph_value: phVal != null ? phVal.toFixed(2) : null,
          ec_value: ecVal,
          tds_value: data.tds ?? null,
          temperature_c:
            data.temperature_c != null ? data.temperature_c.toFixed(1) : null,
          salinity_ppm: data.salinity ?? null,
          orp_mv: data.orp ?? null,
          turbidity_ntu:
            data.turbidity != null ? data.turbidity.toFixed(2) : null,
          battery_pct: data.battery ?? null,
          triggered_by_user_id: isExplicitSession ? 'operator' : 'mqtt_stream',
        });
        const savedReading = await this.readingRepository.save(reading);

        this.lastSavedReading = {
          ph: phVal,
          ec: ecVal,
          savedAtMs: Date.now(),
        };

        let session: TuyaMeasurementSession | null = null;
        if (data.session_id) {
          session = await this.sessionRepository.findOne({
            where: { session_id: data.session_id },
          });
          if (session) {
            session.status = TuyaSessionStatus.COMPLETED;
            session.completed_at = new Date();
            await this.sessionRepository.save(session);
          }
        }

        this.logger.log(
          `Persisted water quality reading to TimescaleDB: pH=${savedReading.ph_value}, EC=${savedReading.ec_value}, Temp=${savedReading.temperature_c} (Session: ${data.session_id || 'stream'})`,
        );

        // Emit general recorded event
        this.eventEmitter.emit(
          'measurement.recorded',
          new MeasurementRecordedEvent(savedReading, session),
        );

        // Emit session-specific event to unblock in-flight HTTP request
        if (data.session_id) {
          this.eventEmitter.emit(`measurement.session.${data.session_id}`, {
            reading: savedReading,
            session,
          });
        }
      } catch (saveErr: any) {
        this.logger.error(
          `Failed to save water quality reading: ${saveErr.message}`,
          saveErr.stack,
        );
      }
    }

    // 2. Broadcast realtime telemetry via WebSocket (always emitted regardless of deadband)
    const broadcastPayload = {
      sensor_id: sensorId,
      time: data.timestamp || new Date().toISOString(),
      ph: phVal,
      ec: ecVal,
      tds: data.tds ?? null,
      temperature_c: data.temperature_c ?? null,
      orp: data.orp ?? null,
      salinity: data.salinity ?? null,
      turbidity: data.turbidity ?? null,
      battery: data.battery ?? null,
      specific_gravity: data.specific_gravity ?? null,
      conductivity_factor: data.conductivity_factor ?? null,
      humidity: data.humidity ?? null,
      season_id: activeSeasonId,
      target_ph: targetPh,
      target_ec: targetEc,
      is_ph_out_of_range: isPhOutOfRange,
      is_ec_out_of_range: isEcOutOfRange,
      status: this.currentStatus,
    };

    this.eventEmitter.emit('water_quality.telemetry', broadcastPayload);
  }

  /**
   * Deadband evaluation rule:
   * Only persist to DB if:
   * - |ΔpH| >= 0.05
   * - |ΔEC| >= 15 µS/cm
   * - Δt >= 5 minutes (300,000 ms)
   */
  private shouldPersistReading(
    ph: number | null,
    ec: number | null,
  ): boolean {
    const now = Date.now();
    const elapsedMs = now - this.lastSavedReading.savedAtMs;

    // If never saved or > 5 minutes elapsed, persist heartbeat sample
    if (this.lastSavedReading.savedAtMs === 0 || elapsedMs >= 300_000) {
      return true;
    }

    // Check pH threshold (0.05)
    if (ph != null && this.lastSavedReading.ph != null) {
      if (Math.abs(ph - this.lastSavedReading.ph) >= 0.05) {
        return true;
      }
    } else if (ph != null && this.lastSavedReading.ph === null) {
      return true;
    }

    // Check EC threshold (15 µS/cm)
    if (ec != null && this.lastSavedReading.ec != null) {
      if (Math.abs(ec - this.lastSavedReading.ec) >= 15) {
        return true;
      }
    } else if (ec != null && this.lastSavedReading.ec === null) {
      return true;
    }

    return false;
  }
}
