import {
  Injectable,
  Logger,
  NotFoundException,
  BadRequestException,
  OnModuleDestroy,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository, DataSource } from 'typeorm';
import { ConfigService } from '@nestjs/config';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { randomUUID } from 'crypto';

import {
  PumpCommand,
  PumpAction,
  CommandSource,
  PumpCommandOutcome,
} from './entities/pump_command.entity';
import { PumpFeedbackEvent } from './entities/pump_feedback_event.entity';
import { PumpStateEvent } from './entities/pump_state_event.entity';
import { FlowEvent } from '../flow/entities/flow_event.entity';
import {
  SensorCalibration,
  CalibrationStatusEnum,
} from '../node/entities/sensor_calibration.entity';
import {
  NodeRegistry,
  CalibrationStatus,
} from '../node/entities/node_registry.entity';
import { SeasonService } from '../season/season.service';
import { MqttService } from '../mqtt/mqtt.service';
import { MQTT_V1_PUBLISH } from '../mqtt/mqtt.constants';
import {
  PumpCommandSentEvent,
  PumpCommandAckedEvent,
  PumpCommandFeedbackEvent,
  PumpCommandFlowConfirmedEvent,
  PumpCommandFaultEvent,
} from './events/pump-command.events';
import { isModernNodeId } from '../node/node-topology';

const UUID_REGEX =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

export interface SendCommandOptions {
  runLeaseMs?: number;
  overrideDurationMs?: number;
  source?: CommandSource;
  gatewayId?: string;
}

export interface RfAckMetadata {
  latencyMs?: number;
  nodeTimestampMs?: string;
  gatewayTimestampMs?: string;
}

export interface PumpFeedbackData {
  driverFeedback: string;
  loadFeedback?: string;
  currentMa?: number;
  voltageV?: string;
  faultFlags?: number;
}

export interface FlowConfirmationData {
  flowRateLpm: number | string;
  litresTotal?: string;
  pulseCount?: string;
  deliveredVolumeMl?: number;
  sensorCalibrationId?: number;
}

@Injectable()
export class PumpCommandService implements OnModuleDestroy {
  private readonly logger = new Logger(PumpCommandService.name);

  // In-memory strictly monotonic rf_seq counter per node
  private readonly nodeSequences = new Map<number, number>();

  // In-memory active pending commands for deadman tracking
  private readonly pendingCommands = new Map<string, PumpCommand>();

  // In-memory sliding window replay cache: key = `${nodeId}:${rfSeq}` -> timestamp
  private readonly replayCache = new Map<string, number>();

  constructor(
    @InjectRepository(PumpCommand)
    private readonly commandRepo: Repository<PumpCommand>,
    @InjectRepository(PumpFeedbackEvent)
    private readonly feedbackRepo: Repository<PumpFeedbackEvent>,
    @InjectRepository(PumpStateEvent)
    private readonly stateRepo: Repository<PumpStateEvent>,
    @InjectRepository(FlowEvent)
    private readonly flowRepo: Repository<FlowEvent>,
  @InjectRepository(SensorCalibration)
  private readonly calibrationRepo: Repository<SensorCalibration>,
  @InjectRepository(NodeRegistry)
  private readonly nodeRegistryRepository: Repository<NodeRegistry>,
  private readonly seasonService: SeasonService,
  private readonly mqttService: MqttService,
  private readonly configService: ConfigService,
    private readonly eventEmitter: EventEmitter2,
    private readonly dataSource: DataSource,
  ) {}

  /**
   * S3-F1: Generate monotonic rf_seq per node for the current session.
   * Ensures cross-reboot isolation and strictly unique sequence per command.
   */
  getNextRfSeq(nodeId: number): number {
    let current = this.nodeSequences.get(nodeId);
    if (current === undefined) {
      // Seed with millisecond modulo to prevent reuse after restart
      current = Math.floor(Date.now() % 10000000);
    }
    current += 1;
    this.nodeSequences.set(nodeId, current);
    return current;
  }

  /**
   * Helper for testing to set deterministic sequence
   */
  setNodeSequence(nodeId: number, seq: number): void {
    this.nodeSequences.set(nodeId, seq);
  }

  /**
   * UC-BE-10: Validate that the node has an ACTIVE calibration before
   * a pump command is admitted. If validation fails, publish a REJECTED
   * ACK on the v1 node ack topic and throw BadRequestException.
   * No fallback to a hardcoded calibrationId is ever applied.
   */
  async validateCalibrationActive(nodeId: number): Promise<void> {
    const flowSensorEnabled = this.configService.get<boolean>(
      'FLOW_SENSOR_ENABLED',
      false,
    );
    if (!flowSensorEnabled) {
      // Flow sensor decoupled: bypass active calibration check in open-loop mode
      return;
    }

    const activeCal = await this.calibrationRepo.findOne({
      where: { node_id: nodeId, status: CalibrationStatusEnum.ACTIVE },
    });

    if (!activeCal) {
      await this.publishRejectedAck(nodeId);
      throw new BadRequestException(
        `UC-BE-10: Node #${nodeId} does not have an ACTIVE calibration. Pump command rejected.`,
      );
    }

    // Double-check node registry calibration_status is CALIBRATED.
    const registry = await this.nodeRegistryRepository.findOne({
      where: { node_id: nodeId },
    });
    if (!registry || registry.calibration_status !== CalibrationStatus.CALIBRATED) {
      await this.publishRejectedAck(nodeId);
      throw new BadRequestException(
        `UC-BE-10: Node #${nodeId} calibration_status is not CALIBRATED in node registry. Pump command rejected.`,
      );
    }
  }

  /**
   * Publish a REJECTED ACK for the given node. MQTT publish failures are
   * logged and swallowed so the admission decision is never masked by a
   * broker connectivity hiccup.
   */
  private async publishRejectedAck(nodeId: number): Promise<void> {
    try {
      await this.mqttService.publish(MQTT_V1_PUBLISH.NODE_ACK(nodeId), {
        status: 'REJECTED',
        reason: 'UC-BE-10: No ACTIVE calibration',
      });
    } catch (err: any) {
      this.logger.warn(
        `Failed to publish REJECTED ACK for node #${nodeId}: ${err.message}`,
      );
    }
  }

  /**
   * S3-F3: Check and record sequence for anti-replay verification.
   * Rejects duplicate rf_seq within configurable MQTT_ANTIREPLAY_WINDOW_MS.
   */
  checkAndRecordSequence(nodeId: number, rfSeq: number): boolean {
    const windowMs = this.configService.get<number>(
      'MQTT_ANTIREPLAY_WINDOW_MS',
      60000,
    );
    const now = Date.now();

    // Prune expired entries from cache to avoid memory leak
    for (const [key, timestamp] of this.replayCache.entries()) {
      if (now - timestamp > windowMs) {
        this.replayCache.delete(key);
      }
    }

    const key = `${nodeId}:${rfSeq}`;
    if (this.replayCache.has(key)) {
      this.logger.warn(
        `Anti-replay violation: Duplicate rf_seq ${rfSeq} detected for node ${nodeId} within window ${windowMs}ms. Dropping command frame.`,
      );
      return false;
    }

    this.replayCache.set(key, now);
    return true;
  }

  /**
   * S3-F1: Send pump command, publish via MQTT, and persist PENDING row.
   */
  async sendCommand(
    nodeId: number,
    groupId: number | null,
    action: PumpAction,
    treatmentVersionId: number | null,
    options?: SendCommandOptions,
  ): Promise<PumpCommand> {
    if (!isModernNodeId(nodeId)) {
      throw new BadRequestException(`Node ID must be between 1 and 15. Received: ${nodeId}`);
    }

    const activeSeason = await this.seasonService.getActive();
    if (!activeSeason) {
      throw new BadRequestException(
        'Cannot send pump command: No active season found. Please create an active season first.',
      );
    }

    await this.validateCalibrationActive(nodeId);

    const commandId = randomUUID();
    const rfSeq = this.getNextRfSeq(nodeId);
    const runLeaseMs = options?.runLeaseMs ?? 30000;
    const source = options?.source ?? CommandSource.MANUAL_OVERRIDE;
    const now = new Date();

    const command = this.commandRepo.create({
      time: now,
      command_id: commandId,
      season_id: activeSeason.id,
      node_id: nodeId,
      group_id: groupId,
      treatment_version_id: treatmentVersionId,
      action,
      rf_seq: rfSeq,
      run_lease_ms: runLeaseMs,
      source,
      outcome: PumpCommandOutcome.PENDING,
      retry_count: 0,
    });

    const savedCommand = await this.commandRepo.save(command);
    this.pendingCommands.set(commandId, savedCommand);

    // Record anti-replay sequence for outbound tracking
    this.checkAndRecordSequence(nodeId, rfSeq);

    // Schema matching Sprint 2 contract
    const mqttPayload = {
      command_id: commandId,
      version: 1,
      desired_state: action,
      source,
      run_lease_ms: runLeaseMs,
      ...(action === PumpAction.OFF && options?.overrideDurationMs
        ? { override_duration_ms: options.overrideDurationMs }
        : {}),
      rf_seq: rfSeq,
      node_id: nodeId,
      group_id: groupId,
      treatment_version_id: treatmentVersionId,
    };

    // Gateway credentials are scoped by the broker ACL to the device-specific
    // command namespace. Publishing to the legacy generic topic is accepted by
    // the backend client but cannot be subscribed to by the ESP32 gateway.
    const gatewayId = options?.gatewayId;
    if (!gatewayId) {
      throw new BadRequestException('deviceId query parameter is required for pump commands.');
    }
    const topic = `aeroponics/device/${gatewayId}/command/node/${nodeId}/override`;
    try {
      await this.mqttService.publish(topic, mqttPayload);
      this.logger.log(
        `Pump command sent for node #${nodeId}, action=${action}, rf_seq=${rfSeq}, cmd_id=${commandId}`,
      );
    } catch (err: any) {
      this.logger.warn(`MQTT publish failed for command ${commandId}: ${err.message}`);
      // Even if MQTT publish warns, command is recorded in DB for retry/audit
    }

    this.eventEmitter.emit(
      'pump.command.sent',
      new PumpCommandSentEvent(
        commandId,
        nodeId,
        groupId,
        action,
        rfSeq,
        runLeaseMs,
        now,
      ),
    );

    return savedCommand;
  }

  /**
   * S3-F2: Handle RF ACK from gateway/node.
   */
  async handleRfAck(
    commandId: string,
    acked: boolean,
    meta?: RfAckMetadata,
  ): Promise<PumpCommand> {
    if (!UUID_REGEX.test(commandId)) {
      this.logger.warn(`Rejected RF ACK for non-UUID command_id: "${commandId}"`);
      throw new BadRequestException(`Invalid command ID format: "${commandId}". Expected UUID.`);
    }

    const command = await this.commandRepo.findOne({
      where: { command_id: commandId },
    });
    if (!command) {
      throw new NotFoundException(`PumpCommand with ID "${commandId}" not found.`);
    }

    const now = new Date();
    if (acked) {
      command.outcome = PumpCommandOutcome.RF_ACKED;
      command.acked_at = now;
      if (meta?.latencyMs !== undefined) {
        command.command_to_ack_latency_ms = meta.latencyMs;
      } else if (command.time) {
        command.command_to_ack_latency_ms = now.getTime() - new Date(command.time).getTime();
      }
      if (meta?.nodeTimestampMs) command.node_timestamp_ms = meta.nodeTimestampMs;
      if (meta?.gatewayTimestampMs) command.gateway_timestamp_ms = meta.gatewayTimestampMs;
    } else {
      command.outcome = PumpCommandOutcome.FAULT_NO_ACK;
      command.fault_reason = 'RF ACK timeout or NACK';
      this.pendingCommands.delete(commandId);
    }

    const updated = await this.commandRepo.save(command);

    this.eventEmitter.emit(
      'pump.command.acked',
      new PumpCommandAckedEvent(
        command.command_id,
        command.node_id,
        command.outcome,
        now,
        command.command_to_ack_latency_ms,
      ),
    );

    return updated;
  }

  /**
   * S3-F2: Handle pump hardware feedback (driver/load state).
   */
  async handlePumpFeedback(
    commandId: string,
    feedback: PumpFeedbackData,
  ): Promise<PumpCommand> {
    if (!UUID_REGEX.test(commandId)) {
      throw new BadRequestException(`Invalid command ID format: "${commandId}". Expected UUID.`);
    }

    const command = await this.commandRepo.findOne({
      where: { command_id: commandId },
    });
    if (!command) {
      throw new NotFoundException(`PumpCommand with ID "${commandId}" not found.`);
    }

    const now = new Date();
    command.feedback_at = now;

    const feedbackEvent = this.feedbackRepo.create({
      time: now,
      season_id: command.season_id,
      node_id: command.node_id,
      group_id: command.group_id,
      command_id: command.command_id,
      driver_feedback: feedback.driverFeedback,
      load_feedback: feedback.loadFeedback ?? 'UNKNOWN',
      driver_feedback_mismatch: feedback.driverFeedback !== command.action,
      fault_flags: feedback.faultFlags ?? 0,
      voltage_v: feedback.voltageV ?? null,
      current_ma: feedback.currentMa ?? null,
      rf_seq: command.rf_seq,
    });
    await this.feedbackRepo.save(feedbackEvent);

    const updated = await this.commandRepo.save(command);

    this.eventEmitter.emit(
      'pump.command.feedback',
      new PumpCommandFeedbackEvent(
        command.command_id,
        command.node_id,
        feedback.driverFeedback,
        feedback.loadFeedback ?? 'UNKNOWN',
        now,
      ),
    );

    return updated;
  }

  /**
   * S3-F2: Handle flow confirmation.
   * INVARIANT: Only allows outcome -> FLOW_CONFIRMED if command is already in RF_ACKED state.
   */
  async handleFlowConfirmed(
    commandId: string,
    flowData: FlowConfirmationData,
  ): Promise<PumpCommand> {
    if (!UUID_REGEX.test(commandId)) {
      throw new BadRequestException(`Invalid command ID format: "${commandId}". Expected UUID.`);
    }

    const command = await this.commandRepo.findOne({
      where: { command_id: commandId },
    });
    if (!command) {
      throw new NotFoundException(`PumpCommand with ID "${commandId}" not found.`);
    }

    // Strict invariant check: Flow confirmed without prior RF_ACKED must be rejected!
    if (command.outcome !== PumpCommandOutcome.RF_ACKED) {
      throw new BadRequestException(
        `Invalid state transition: Cannot confirm flow for command "${commandId}" with status "${command.outcome}". Command must be in RF_ACKED state first.`,
      );
    }

    const now = new Date();
    command.outcome = PumpCommandOutcome.FLOW_CONFIRMED;
    command.flow_confirmed_at = now;

    if (command.acked_at) {
      command.flow_start_latency_ms =
        now.getTime() - new Date(command.acked_at).getTime();
    }
    if (command.time) {
      command.execution_duration_ms =
        now.getTime() - new Date(command.time).getTime();
    }

    this.pendingCommands.delete(commandId);

    // Resolve calibration ID — UC-BE-10 forbids fallback to calibrationId = 1
    let calibrationId = flowData.sensorCalibrationId;
    if (!calibrationId) {
      const activeCal = await this.calibrationRepo.findOne({
        where: { node_id: command.node_id, status: CalibrationStatusEnum.ACTIVE },
      });
      calibrationId = activeCal?.id;
    }

    if (!calibrationId) {
      throw new BadRequestException(
        `Cannot confirm flow: Node #${command.node_id} does not have an ACTIVE calibration.`,
      );
    }

    const flowRateStr =
      typeof flowData.flowRateLpm === 'number'
        ? flowData.flowRateLpm.toFixed(2)
        : flowData.flowRateLpm;

    const flowEvent = this.flowRepo.create({
      time: now,
      season_id: command.season_id,
      node_id: command.node_id,
      group_id: command.group_id,
      command_id: command.command_id,
      litres_total: flowData.litresTotal ?? '0.000',
      pulse_count: flowData.pulseCount ?? '0',
      flow_rate_lpm: flowRateStr,
      delivered_volume_ml: flowData.deliveredVolumeMl ?? 0,
      sample_window_ms: 1000,
      sensor_calibration_id: calibrationId,
      flow_confirmed: true,
      flow_stability_pct: '100.00',
      quality_flag: 'OK',
      is_fault: false,
      rf_seq: command.rf_seq,
    });
    await this.flowRepo.save(flowEvent);

    const updated = await this.commandRepo.save(command);

    this.eventEmitter.emit(
      'pump.command.flow_confirmed',
      new PumpCommandFlowConfirmedEvent(
        command.command_id,
        command.node_id,
        flowRateStr,
        now,
      ),
    );

    return updated;
  }

  /**
   * S3-F2: Handle command fault.
   */
  async handleFault(
    commandId: string,
    reason: string,
    faultOutcome?: PumpCommandOutcome,
  ): Promise<PumpCommand> {
    if (!UUID_REGEX.test(commandId)) {
      throw new BadRequestException(`Invalid command ID format: "${commandId}". Expected UUID.`);
    }

    const command = await this.commandRepo.findOne({
      where: { command_id: commandId },
    });
    if (!command) {
      throw new NotFoundException(`PumpCommand with ID "${commandId}" not found.`);
    }

    const now = new Date();
    command.outcome = faultOutcome ?? PumpCommandOutcome.FAULT_NO_FLOW;
    command.fault_reason = reason;
    this.pendingCommands.delete(commandId);

    const updated = await this.commandRepo.save(command);

    this.eventEmitter.emit(
      'pump.command.fault',
      new PumpCommandFaultEvent(
        command.command_id,
        command.node_id,
        command.outcome,
        reason,
        now,
      ),
    );

    return updated;
  }

  /**
   * S3-F4: Paginate commands for a node (default 50, max 200).
   */
  async getNodeCommands(
    nodeId: number,
    limit = 50,
    offset = 0,
  ): Promise<PumpCommand[]> {
    if (!isModernNodeId(nodeId)) {
      throw new BadRequestException(`Node ID must be between 1 and 15. Received: ${nodeId}`);
    }

    return this.commandRepo.find({
      where: { node_id: nodeId },
      order: { time: 'DESC' },
      take: limit,
      skip: offset,
    });
  }

  /**
   * S3-F2 & QA Rule S3-DEADMAN-08: Deadman/lease cancel on module destroy.
   * Marks all PENDING commands with FAULT_BACKEND_DISCONNECT.
   */
  async onModuleDestroy(): Promise<void> {
    this.logger.log(
      'PumpCommandService shutting down: Executing Deadman Timer cancellation on pending commands...',
    );

    try {
      const pendingCommands = await this.commandRepo.find({
        where: { outcome: PumpCommandOutcome.PENDING },
      });

      for (const cmd of pendingCommands) {
        cmd.outcome = PumpCommandOutcome.FAULT_BACKEND_DISCONNECT;
        cmd.fault_reason = 'Deadman cancellation: Backend disconnected / service shutdown';
        await this.commandRepo.save(cmd);
      }
    } catch (err: any) {
      this.logger.error(`Error cancelling pending commands during deadman shutdown: ${err.message}`);
    }

    this.pendingCommands.clear();
    this.replayCache.clear();
  }
}
