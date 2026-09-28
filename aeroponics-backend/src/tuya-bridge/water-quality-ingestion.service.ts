import { Injectable, Logger } from '@nestjs/common';
import { OnEvent, EventEmitter2 } from '@nestjs/event-emitter';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';

import { MQTT_EVENTS } from '../mqtt/mqtt.constants';
import {
  MeasurementReading,
  MeasurementTriggerType,
} from './entities/measurement_reading.entity';
import { SeasonService } from '../season/season.service';

export interface WaterQualityPayload {
  sensor_id?: string;
  ph?: number | null;
  ec?: number | null;
  tds?: number | null;
  temperature_c?: number | null;
  orp?: number | null;
  salinity?: number | null;
  specific_gravity?: number | null;
  conductivity_factor?: number | null;
  humidity?: number | null;
  timestamp?: string;
}

export interface WaterQualityStatusPayload {
  status: 'online' | 'offline';
  sensor_id?: string;
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
    private readonly seasonService: SeasonService,
    private readonly eventEmitter: EventEmitter2,
  ) {}

  @OnEvent(MQTT_EVENTS.WATER_QUALITY_STATUS)
  handleStatusEvent(event: {
    sensorId: string;
    payload: WaterQualityStatusPayload;
    receivedAt?: Date;
  }): void {
    const status = event.payload?.status || 'offline';
    this.currentStatus = status;
    this.logger.log(`Water quality sensor [${event.sensorId}] status: ${status}`);

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
    const shouldPersist = this.shouldPersistReading(phVal, ecVal);
    if (shouldPersist) {
      try {
        const reading = this.readingRepository.create({
          time: data.timestamp ? new Date(data.timestamp) : new Date(),
          sensor_id: sensorId,
          trigger_type: MeasurementTriggerType.ON_DEMAND,
          ph_value: phVal != null ? phVal.toFixed(2) : null,
          ec_value: ecVal,
          tds_value: data.tds ?? null,
          temperature_c:
            data.temperature_c != null ? data.temperature_c.toFixed(1) : null,
          salinity_ppm: data.salinity ?? null,
          orp_mv: data.orp ?? null,
          battery_pct: null,
          triggered_by_user_id: 'mqtt_stream',
        });
        await this.readingRepository.save(reading);

        this.lastSavedReading = {
          ph: phVal,
          ec: ecVal,
          savedAtMs: Date.now(),
        };

        this.logger.debug(
          `Persisted water quality reading to TimescaleDB: pH=${reading.ph_value}, EC=${reading.ec_value}, Temp=${reading.temperature_c}`,
        );
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
