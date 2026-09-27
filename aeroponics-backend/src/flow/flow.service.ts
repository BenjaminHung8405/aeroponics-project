import {
  Injectable,
  Logger,
  BadRequestException,
  NotFoundException,
  Inject,
  OnModuleInit,
  OnModuleDestroy,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository, DataSource, MoreThanOrEqual } from 'typeorm';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { Pool } from 'pg';
import { FlowEvent, FlowFaultCode } from './entities/flow_event.entity';
import {
  SensorCalibration,
  CalibrationStatusEnum,
} from '../node/entities/sensor_calibration.entity';
import {
  NodeRegistry,
  CalibrationStatus,
} from '../node/entities/node_registry.entity';
import { Season, SeasonStatus } from '../season/entities/season.entity';
import { UpdateCalibrationDto } from './dto/update-calibration.dto';
import { RecordFlowEventDto } from './dto/record-flow-event.dto';
import {
  FlowEventRecordedEvent,
  FlowCalibrationUpdatedEvent,
  FlowOverRangeAlertEvent,
} from './events/flow.events';
import { isModernNodeId } from '../node/node-topology';
import { WRITE_POOL } from '../database/database.module';

/** K2: Maximum number of flow events to buffer before an automatic flush. */
const BATCH_BUFFER_MAX = 50;

/** K2: Interval (ms) between automatic buffer flushes. */
const BATCH_FLUSH_INTERVAL_MS = 5000;

export interface FlowHistorySummary {
  total_events: number;
  total_delivered_volume_ml: number;
  total_litres: number;
  average_flow_rate_lpm: number;
  max_flow_rate_lpm: number;
  confirmed_events: number;
  fault_events: number;
  flow_confirmation_rate_pct: number;
}

export interface FlowHistoryResponse {
  node_id: number;
  hours: number;
  since: Date;
  summary: FlowHistorySummary;
  events: FlowEvent[];
}

export interface NodeCalibrationResponse {
  node_id: number;
  active_calibration: SensorCalibration | null;
  history: SensorCalibration[];
  total_versions: number;
}

@Injectable()
export class FlowService implements OnModuleInit, OnModuleDestroy {
  private readonly logger = new Logger(FlowService.name);

  /** K2: In-memory buffer for batch flow event inserts. */
  private readonly batchBuffer: FlowEvent[] = [];
  private batchTimer: ReturnType<typeof setInterval> | null = null;
  /** T2: Events buffered with emitAfterFlush=true waiting for post-flush WS broadcast. */
  private readonly pendingEmitEvents: FlowEvent[] = [];

  constructor(
    @InjectRepository(FlowEvent)
    private readonly flowRepo: Repository<FlowEvent>,
    @InjectRepository(SensorCalibration)
    private readonly calibrationRepo: Repository<SensorCalibration>,
    @InjectRepository(NodeRegistry)
    private readonly nodeRepo: Repository<NodeRegistry>,
    @InjectRepository(Season)
    private readonly seasonRepo: Repository<Season>,
    private readonly dataSource: DataSource,
    private readonly eventEmitter: EventEmitter2,
    @Inject(WRITE_POOL) private readonly writePool: Pool,
  ) {}

  // ─── Lifecycle Hooks ─────────────────────────────────────────────────

  onModuleInit(): void {
    this.startBatchTimer();
  }

  onModuleDestroy(): void {
    this.stopBatchTimer();
    if (this.batchBuffer.length > 0) {
      this.flushFlowEventBatch().catch((err) => {
        this.logger.error(`Batch flush on shutdown failed: ${err.message}`, err.stack);
      });
    }
  }

  // ─── K2: Batch Buffer ───────────────────────────────────────────────

  private startBatchTimer(): void {
    if (this.batchTimer) return;
    this.batchTimer = setInterval(() => {
      if (this.batchBuffer.length > 0) {
        this.flushFlowEventBatch().catch((err) => {
          this.logger.error(`Periodic batch flush failed: ${err.message}`, err.stack);
        });
      }
    }, BATCH_FLUSH_INTERVAL_MS);
    this.logger.log(`Batch buffer timer started (interval: ${BATCH_FLUSH_INTERVAL_MS}ms, max: ${BATCH_BUFFER_MAX}).`);
  }

  private stopBatchTimer(): void {
    if (this.batchTimer) {
      clearInterval(this.batchTimer);
      this.batchTimer = null;
    }
  }

  bufferFlowEvent(event: FlowEvent, emitAfterFlush: boolean = false): FlowEvent {
    this.batchBuffer.push(event);
    if (emitAfterFlush) {
      this.pendingEmitEvents.push(event);
    }

    if (this.batchBuffer.length >= BATCH_BUFFER_MAX) {
      this.flushFlowEventBatch().catch((err) => {
        this.logger.error(`Immediate batch flush failed: ${err.message}`, err.stack);
      });
    }

    return event;
  }

  async flushFlowEventBatch(): Promise<number> {
    if (this.batchBuffer.length === 0) return 0;

    const events = this.batchBuffer.splice(0, this.batchBuffer.length);
    if (events.length === 0) return 0;

    const columns = [
      'time', 'season_id', 'node_id', 'group_id', 'command_id',
      'litres_total', 'pulse_count', 'flow_rate_lpm', 'delivered_volume_ml',
      'sample_window_ms', 'sensor_calibration_id', 'flow_confirmed',
      'flow_stability_pct', 'quality_flag', 'is_fault', 'fault_code',
      'boot_session_id', 'rf_seq', 'node_timestamp_ms', 'gateway_timestamp_ms',
    ];

    const valuePlaceholders: string[] = [];
    const flatValues: unknown[] = [];
    let paramIndex = 1;

    for (const ev of events) {
      const rowPlaceholders: string[] = [];
      for (const _col of columns) {
        rowPlaceholders.push(`$${paramIndex++}`);
      }
      valuePlaceholders.push(`(${rowPlaceholders.join(', ')})`);

      flatValues.push(
        ev.time,
        ev.season_id,
        ev.node_id,
        ev.group_id,
        ev.command_id,
        ev.litres_total,
        ev.pulse_count,
        ev.flow_rate_lpm,
        ev.delivered_volume_ml,
        ev.sample_window_ms,
        ev.sensor_calibration_id,
        ev.flow_confirmed,
        ev.flow_stability_pct,
        ev.quality_flag,
        ev.is_fault,
        ev.fault_code,
        ev.boot_session_id,
        ev.rf_seq,
        ev.node_timestamp_ms,
        ev.gateway_timestamp_ms,
      );
    }

    const query = `
      INSERT INTO flow_events (${columns.join(', ')})
      VALUES ${valuePlaceholders.join(', ')}
      ON CONFLICT DO NOTHING
    `;

    try {
      const client = await this.writePool.connect();
      try {
        const result = await client.query(query, flatValues);
        const inserted = result.rowCount ?? events.length;
        this.logger.log(`Batch flush: ${inserted}/${events.length} flow events persisted.`);
        // T2: Emit deferred WS events now that batch is confirmed persisted.
        const emitEvents = events.filter((event) =>
          this.pendingEmitEvents.includes(event),
        );
        if (emitEvents.length > 0) {
          for (const ev of emitEvents) {
            this.eventEmitter.emit(
              'flow.event_recorded',
              new FlowEventRecordedEvent(ev, ev.time),
            );
            const pendingIndex = this.pendingEmitEvents.indexOf(ev);
            if (pendingIndex >= 0) {
              this.pendingEmitEvents.splice(pendingIndex, 1);
            }
          }
          this.logger.log(`T2: Emitted ${emitEvents.length} deferred WS events after batch flush.`);
        }
        return inserted;
      } finally {
        client.release();
      }
    } catch (err: any) {
      this.logger.error(`Batch INSERT failed: ${err.message}`, err.stack);
      // Re-buffer the events on failure so they are not lost.
      // pendingEmitEvents are untouched here — they will be emitted on the
      // next successful flush (only spliced in the success path above).
      this.batchBuffer.unshift(...events);
      throw err;
    }
  }

  /**
   * Validate that the given nodeId is within modern control-plane scope [1..15].
   */
  private validateNodeId(nodeId: number): void {
    if (!isModernNodeId(nodeId)) {
      throw new BadRequestException(
        `Invalid node_id: ${nodeId}. Allowed modern node IDs are 1..15.`,
      );
    }
  }

  /**
   * S3-G1: Get flow history for a specific node within specified hours (default 24, max 720).
   * Calculates aggregated summary metrics (volume, average LPM, fault count, confirmation rate).
   */
  async getHistory(
    nodeId: number,
    hours: number = 24,
    limit: number = 500,
  ): Promise<FlowHistoryResponse> {
    this.validateNodeId(nodeId);

    if (hours === undefined || hours === null) {
      hours = 24;
    }

    if (!Number.isInteger(hours) || hours < 1 || hours > 720) {
      throw new BadRequestException(
        `Invalid hours parameter: ${hours}. Allowed query range is 1 to 720 hours (maximum 30 days).`,
      );
    }

    const since = new Date(Date.now() - hours * 3600 * 1000);

    // Efficient indexed range query on hypertable (season_id, node_id, time DESC)
    const events = await this.flowRepo.find({
      where: {
        node_id: nodeId,
        time: MoreThanOrEqual(since),
      },
      order: {
        time: 'DESC',
      },
      take: limit > 0 && limit <= 1000 ? limit : 500,
    });

    // Compute summary metrics over the queried window
    let totalVolumeMl = 0;
    let totalRateLpm = 0;
    let maxRateLpm = 0;
    let activeFlowCount = 0;
    let confirmedCount = 0;
    let faultCount = 0;

    for (const ev of events) {
      const vol = ev.delivered_volume_ml || 0;
      totalVolumeMl += vol;

      const rate = parseFloat(ev.flow_rate_lpm) || 0;
      if (rate > maxRateLpm) {
        maxRateLpm = rate;
      }
      if (rate > 0) {
        totalRateLpm += rate;
        activeFlowCount++;
      }

      if (ev.flow_confirmed) {
        confirmedCount++;
      }
      if (ev.is_fault) {
        faultCount++;
      }
    }

    const totalEvents = events.length;
    const avgRateLpm =
      activeFlowCount > 0
        ? parseFloat((totalRateLpm / activeFlowCount).toFixed(2))
        : 0;
    const confirmationRatePct =
      totalEvents > 0
        ? parseFloat(((confirmedCount / totalEvents) * 100).toFixed(2))
        : 0;

    const summary: FlowHistorySummary = {
      total_events: totalEvents,
      total_delivered_volume_ml: totalVolumeMl,
      total_litres: parseFloat((totalVolumeMl / 1000).toFixed(3)),
      average_flow_rate_lpm: avgRateLpm,
      max_flow_rate_lpm: parseFloat(maxRateLpm.toFixed(2)),
      confirmed_events: confirmedCount,
      fault_events: faultCount,
      flow_confirmation_rate_pct: confirmationRatePct,
    };

    return {
      node_id: nodeId,
      hours,
      since,
      summary,
      events,
    };
  }

  /**
   * S3-G1: Get active calibration and audit version history for a specific node.
   */
  async getCalibration(nodeId: number): Promise<NodeCalibrationResponse> {
    this.validateNodeId(nodeId);

    const activeCal = await this.calibrationRepo.findOne({
      where: {
        node_id: nodeId,
        status: CalibrationStatusEnum.ACTIVE,
      },
    });

    const history = await this.calibrationRepo.find({
      where: {
        node_id: nodeId,
      },
      order: {
        version_num: 'DESC',
      },
    });

    return {
      node_id: nodeId,
      active_calibration: activeCal || null,
      history,
      total_versions: history.length,
    };
  }

  /**
   * S3-G1: Update calibration with immutable version audit trail.
   * Atomically supersedes previous active calibration and records new version.
   */
  async updateCalibration(
    nodeId: number,
    dto: UpdateCalibrationDto,
    operator: string = 'operator',
  ): Promise<SensorCalibration> {
    this.validateNodeId(nodeId);

    const pulsesPerLitre = dto.getEffectivePulsesPerLitre();
    if (!pulsesPerLitre || pulsesPerLitre <= 0 || pulsesPerLitre >= 10000) {
      throw new BadRequestException(
        `Calibration pulses per litre (${pulsesPerLitre}) must be greater than 0 and less than 10000.`,
      );
    }

    const node = await this.nodeRepo.findOne({
      where: { node_id: nodeId },
    });
    if (!node) {
      throw new NotFoundException(`Node #${nodeId} not found in registry.`);
    }

    const refVol = dto.reference_volume_ml ?? 1000;
    const serial =
      dto.sensor_serial ?? node.sensor_serial ?? `SEN-N${nodeId}-FLOW`;
    const calibratedBy = operator || dto.calibrated_by || 'operator';
    const now = new Date();

    let savedCalibration!: SensorCalibration;
    let previousCalId: number | null = null;

    await this.dataSource.transaction(async (manager) => {
      // 1. Find previous active calibration for audit tracking
      const previousActive = await manager.findOne(SensorCalibration, {
        where: {
          node_id: nodeId,
          status: CalibrationStatusEnum.ACTIVE,
        },
      });

      if (previousActive) {
        previousCalId = previousActive.id;
      }

      // 2. Determine next strictly monotonic version number for this node
      const maxVersionRecord = await manager.findOne(SensorCalibration, {
        where: { node_id: nodeId },
        order: { version_num: 'DESC' },
      });
      const newVersionNum = maxVersionRecord
        ? maxVersionRecord.version_num + 1
        : 1;

      // 3. Mark previous active calibrations as SUPERSEDED (Immutable audit)
      await manager
        .createQueryBuilder()
        .update(SensorCalibration)
        .set({ status: CalibrationStatusEnum.SUPERSEDED })
        .where('node_id = :nodeId AND status = :activeStatus', {
          nodeId,
          activeStatus: CalibrationStatusEnum.ACTIVE,
        })
        .execute();

      // 4. Create new SensorCalibration record
      const meanPulses = (pulsesPerLitre * (refVol / 1000)).toFixed(2);
      const newCal = manager.create(SensorCalibration, {
        node_id: nodeId,
        sensor_serial: serial,
        version_num: newVersionNum,
        pulses_per_litre: pulsesPerLitre.toFixed(4),
        reference_volume_ml: refVol,
        trial_count: 3,
        mean_pulses: meanPulses,
        variance: '0.0000',
        repeatability_pct: '1.00',
        status: CalibrationStatusEnum.ACTIVE,
        calibrated_by: calibratedBy,
        calibrated_at: now,
      });

      savedCalibration = await manager.save(newCal);

      // 5. Update NodeRegistry pointer to active calibration
      await manager
        .createQueryBuilder()
        .update(NodeRegistry)
        .set({
          active_sensor_calibration_id: savedCalibration.id,
          calibration_status: CalibrationStatus.CALIBRATED,
          sensor_serial: serial,
          updated_at: now,
        })
        .where('node_id = :nodeId', { nodeId })
        .execute();
    });

    this.logger.log(
      `Node #${nodeId} calibration updated: version ${savedCalibration.version_num}, ${savedCalibration.pulses_per_litre} pulses/L by ${calibratedBy}.`,
    );

    // Emit event for WebSocket and audit listeners
    this.eventEmitter.emit(
      'flow.calibration_updated',
      new FlowCalibrationUpdatedEvent(
        nodeId,
        savedCalibration,
        previousCalId,
        calibratedBy,
        now,
      ),
    );

    return savedCalibration;
  }

  /**
   * Record a normalized flow event from MQTT telemetry or pump command confirmation.
   * Enforces S2-FLOW-04 safety rule (>6 L/min triggers OVER_RANGE_FAULT).
   *
   * @param dto         Flow event data transfer object.
   * @param emitAfterFlush  T2: When true the WS `node_flow` broadcast is deferred
   *                        until the batch is confirmed flushed to the DB (aligns
   *                        with §2.3 diagram timing). When false (default) the event
   *                        is saved and broadcast immediately for real-time UX.
   */
  async recordFlowEvent(dto: RecordFlowEventDto, emitAfterFlush: boolean = false): Promise<FlowEvent> {
    this.validateNodeId(dto.node_id);

    // Look up active season
    const activeSeason = await this.seasonRepo.findOne({
      where: { status: SeasonStatus.ACTIVE },
    });
    const seasonId = activeSeason?.id ?? 1;

    // UC-BE-10: require an ACTIVE calibration for the node. Never fall back
    // to a hardcoded calibrationId when none exists.
    const activeCal = await this.calibrationRepo.findOne({
      where: {
        node_id: dto.node_id,
        status: CalibrationStatusEnum.ACTIVE,
      },
    });
    if (!activeCal) {
      throw new BadRequestException(
        `UC-BE-10: Node #${dto.node_id} does not have an ACTIVE calibration. Flow event rejected.`,
      );
    }
    dto.sensor_calibration_id = activeCal.id;
    const calibrationId = activeCal.id;

    const now = new Date();
    let isFault = Boolean(
      dto.flow_rate_lpm > 6.0 ||
        (dto.fault_code && dto.fault_code !== FlowFaultCode.NONE),
    );
    let faultCode = dto.fault_code ?? FlowFaultCode.NONE;

    // Over-range safety check: >6 L/min triggers OVER_RANGE_FAULT
    if (dto.flow_rate_lpm > 6.0) {
      isFault = true;
      faultCode = FlowFaultCode.OVER_RANGE_FAULT;
      this.logger.error(
        `Node #${dto.node_id} reported excessive flow: ${dto.flow_rate_lpm} L/min (threshold: 6.0 L/min). OVER_RANGE_FAULT latched.`,
      );
      this.eventEmitter.emit(
        'flow.over_range',
        new FlowOverRangeAlertEvent(dto.node_id, dto.flow_rate_lpm, 6.0, faultCode, now),
      );
    }

    const event = this.flowRepo.create({
      time: now,
      season_id: seasonId,
      node_id: dto.node_id,
      group_id: dto.group_id ?? null,
      command_id: dto.command_id ?? null,
      litres_total: dto.litres_total ?? '0.000',
      pulse_count: dto.pulse_count ?? '0',
      flow_rate_lpm: dto.flow_rate_lpm.toFixed(2),
      delivered_volume_ml: dto.delivered_volume_ml ?? 0,
      sample_window_ms: dto.sample_window_ms ?? 1000,
      sensor_calibration_id: calibrationId,
      flow_confirmed: dto.flow_confirmed ?? false,
      flow_stability_pct: dto.flow_stability_pct ?? null,
      quality_flag: dto.quality_flag ?? (isFault ? 'FAULT' : 'OK'),
      is_fault: isFault,
      fault_code: faultCode,
      boot_session_id: dto.boot_session_id ?? null,
      rf_seq: dto.rf_seq ?? null,
      node_timestamp_ms: dto.node_timestamp_ms ?? null,
      gateway_timestamp_ms: dto.gateway_timestamp_ms ?? null,
    });

    if (emitAfterFlush) {
      // T2: Buffer the event for batch insert; WS emit deferred until flush.
      this.bufferFlowEvent(event, true);
      return event;
    }

    // Default path: save immediately and broadcast WS event right away.
    const saved = await this.flowRepo.save(event);

    this.eventEmitter.emit(
      'flow.event_recorded',
      new FlowEventRecordedEvent(saved, now),
    );

    return saved;
  }
}
