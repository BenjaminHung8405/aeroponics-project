import {
  Injectable,
  Logger,
  BadRequestException,
  NotFoundException,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository, DataSource, MoreThanOrEqual } from 'typeorm';
import { EventEmitter2 } from '@nestjs/event-emitter';
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
import { AGU_LEGACY_NODE_IDS, isAguLegacyNodeId } from '../node/node-topology';

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
export class FlowService {
  private readonly logger = new Logger(FlowService.name);

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
  ) {}

  /**
   * Validate that the given nodeId is within valid production scope [1..4].
   */
  private validateNodeId(nodeId: number): void {
    if (!Number.isInteger(nodeId) || !isAguLegacyNodeId(nodeId)) {
      throw new BadRequestException(
        `Invalid node_id: ${nodeId}. Allowed physical RF IDs are ${AGU_LEGACY_NODE_IDS.join(', ')}.`,
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
   */
  async recordFlowEvent(dto: RecordFlowEventDto): Promise<FlowEvent> {
    this.validateNodeId(dto.node_id);

    // Look up active season
    const activeSeason = await this.seasonRepo.findOne({
      where: { status: SeasonStatus.ACTIVE },
    });
    const seasonId = activeSeason?.id ?? 1;

    // Resolve active calibration
    let calibrationId = dto.sensor_calibration_id;
    if (!calibrationId) {
      const activeCal = await this.calibrationRepo.findOne({
        where: {
          node_id: dto.node_id,
          status: CalibrationStatusEnum.ACTIVE,
        },
      });
      calibrationId = activeCal?.id ?? 1;
    }

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

    const saved = await this.flowRepo.save(event);

    this.eventEmitter.emit(
      'flow.event_recorded',
      new FlowEventRecordedEvent(saved, now),
    );

    return saved;
  }
}
