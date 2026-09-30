import {
  Injectable,
  NotFoundException,
  BadRequestException,
  Logger,
  Inject,
  Optional,
  forwardRef,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository, DataSource } from 'typeorm';
import { ConfigService } from '@nestjs/config';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { randomUUID } from 'crypto';

import {
  NodeRegistry,
  NodeHealthStatus,
  CalibrationStatus,
  OverrideState,
  ScheduleState,
} from './entities/node_registry.entity';
import {
  SensorCalibration,
  CalibrationStatusEnum,
} from './entities/sensor_calibration.entity';
import { UpdateNodeCalibrationDto } from './dto/update-node-calibration.dto';
import { NodeTelemetryDto } from './dto/node-telemetry.dto';
import { ClaimNodeDto } from './dto/claim-node.dto';
import { MqttService } from '../mqtt/mqtt.service';
import { MQTT_PUBLISH_TEMPLATES } from '../mqtt/mqtt.constants';
import {
  NodeTelemetryReceivedEvent,
  NodeStalenessAlertEvent,
  NodeHealthChangedEvent,
  NodeFaultResetEvent,
  NodeCalibrationUpdatedEvent,
} from './events/node.events';
import { AGU_LEGACY_NODE_IDS, isAguLegacyNodeId, isModernNodeId } from './node-topology';

export interface DiscoveredRfNode {
  node_id: number;
  online: boolean | null;
  rtt_ms: number | null;
  protocol: string;
  is_assigned: boolean;
  current_slot?: number;
  boot_session_id?: number;
  failure_reason?: string;
}

export interface RfScanResponse {
  scan_id: string;
  duration_ms: number;
  status: 'COMPLETED' | 'PARTIAL' | 'FAILED' | 'TIMEOUT';
  error?: string;
  error_code?: string;
  nodes: DiscoveredRfNode[];
}

export interface NodeStatusResponse {
  node_id: number;
  display_name: string;
  cached_group_id: number | null;
  sensor_serial: string | null;
  calibration_status: CalibrationStatus;
  schedule_state: string;
  override_state: string;
  last_boot_session_id: number | null;
  last_seen_at: Date | null;
  health_status: NodeHealthStatus;
  is_stale: boolean;
  stale_for_ms: number;
  rf_protocol: string | null;
  last_scan_id: string | null;
  last_rf_rtt_ms: number | null;
  last_discovered_at: Date | null;
  discovery_status: string | null;
  active_calibration: {
    id: number;
    version_num: number;
    pulses_per_litre: string;
    reference_volume_ml: number;
    calibrated_at: Date;
    calibrated_by: string | null;
  } | null;
}

@Injectable()
export class NodeService {
  private readonly logger = new Logger(NodeService.name);
  private readonly staleThresholdMs: number;
  private readonly activeScans = new Set<string>();
  private readonly rfScanResultTimeoutMs: number;

  constructor(
    @InjectRepository(NodeRegistry)
    private readonly nodeRegistryRepository: Repository<NodeRegistry>,
    @InjectRepository(SensorCalibration)
    private readonly calibrationRepository: Repository<SensorCalibration>,
    private readonly configService: ConfigService,
    private readonly dataSource: DataSource,
    private readonly eventEmitter: EventEmitter2,
    @Optional()
    @Inject(forwardRef(() => MqttService))
    private readonly mqttService?: MqttService,
  ) {
    this.staleThresholdMs = Number(
      this.configService.get<number>('STALE_THRESHOLD_MS', 120000),
    );
    this.rfScanResultTimeoutMs = Number(
      this.configService.get<number>('RF_SCAN_RESULT_TIMEOUT_MS', 10000),
    );
  }

  /**
   * Get configured staleness threshold in milliseconds.
   */
  getStaleThresholdMs(): number {
    return this.staleThresholdMs;
  }

  /**
   * Register or update a node display name in node_registry.
   */
  async register(
    nodeId: number,
    displayName?: string,
  ): Promise<NodeRegistry> {
    this.validateNodeId(nodeId);

    let node = await this.nodeRegistryRepository.findOne({
      where: { node_id: nodeId },
    });

    if (!node) {
      node = this.nodeRegistryRepository.create({
        node_id: nodeId,
        display_name: displayName ?? `Node 0${nodeId}`,
        health_status: NodeHealthStatus.OK,
        calibration_status: CalibrationStatus.UNCALIBRATED,
      });
    } else if (displayName) {
      node.display_name = displayName;
    }

    return this.nodeRegistryRepository.save(node);
  }

  /**
   * Update node health status with fault-latch protection.
   * Transition from FAULT -> OK is rejected unless explicitReset is true.
   */
  async updateHealth(
    nodeId: number,
    newStatus: NodeHealthStatus,
    options?: { explicitReset?: boolean; reason?: string },
  ): Promise<NodeRegistry> {
    this.validateNodeId(nodeId);

    const node = await this.nodeRegistryRepository.findOne({
      where: { node_id: nodeId },
    });
    if (!node) {
      throw new NotFoundException(`Node #${nodeId} not found in registry.`);
    }

    // Safety Invariant: Cannot recover directly from FAULT to OK without explicit reset
    if (
      node.health_status === NodeHealthStatus.FAULT &&
      newStatus === NodeHealthStatus.OK &&
      !options?.explicitReset
    ) {
      throw new BadRequestException(
        `Cannot transition Node #${nodeId} from FAULT to OK without an explicit fault-reset command.`,
      );
    }

    const previousStatus = node.health_status;
    if (previousStatus !== newStatus) {
      node.health_status = newStatus;
      const savedNode = await this.nodeRegistryRepository.save(node);

      this.logger.log(
        `Node #${nodeId} health changed: ${previousStatus} -> ${newStatus} (${options?.reason ?? 'Manual/System update'})`,
      );

      this.eventEmitter.emit(
        'node.health_changed',
        new NodeHealthChangedEvent(
          nodeId,
          previousStatus,
          newStatus,
          options?.reason ?? 'Health state updated',
          new Date(),
        ),
      );

      return savedNode;
    }

    return node;
  }

  /**
   * Explicitly reset a node fault latch, restoring health status to OK.
   */
  async resetFault(
    nodeId: number,
    resetBy: string = 'operator',
  ): Promise<NodeRegistry> {
    this.validateNodeId(nodeId);

    const updatedNode = await this.updateHealth(nodeId, NodeHealthStatus.OK, {
      explicitReset: true,
      reason: `Explicit fault reset by ${resetBy}`,
    });

    this.eventEmitter.emit(
      'node.fault_reset',
      new NodeFaultResetEvent(nodeId, resetBy, new Date()),
    );

    return updatedNode;
  }

  /**
   * Ingest node telemetry: update last_seen_at, states, and check for staleness alert.
   */
  async handleTelemetry(
    nodeId: number,
    telemetry: NodeTelemetryDto,
  ): Promise<NodeRegistry> {
    this.validateNodeId(nodeId);

    let node = await this.nodeRegistryRepository.findOne({
      where: { node_id: nodeId },
    });

    if (!node) {
      node = await this.register(nodeId);
    }

    const now = new Date();

    // Staleness check: if node was previously disconnected longer than STALE_THRESHOLD_MS
    if (node.last_seen_at) {
      const msSinceLastSeen = now.getTime() - new Date(node.last_seen_at).getTime();
      if (msSinceLastSeen > this.staleThresholdMs) {
        this.logger.warn(
          `Node #${nodeId} reconnected after being stale for ${msSinceLastSeen}ms (threshold: ${this.staleThresholdMs}ms).`,
        );

        this.eventEmitter.emit(
          'staleness_alert',
          new NodeStalenessAlertEvent(
            nodeId,
            node.last_seen_at,
            msSinceLastSeen,
            now,
          ),
        );
      }
    }

    node.last_seen_at = now;

    if (telemetry.schedule_state !== undefined) {
      node.schedule_state = telemetry.schedule_state;
    }
    if (telemetry.override_state !== undefined) {
      node.override_state = telemetry.override_state;
    }
    if (telemetry.boot_session_id !== undefined) {
      node.last_boot_session_id = telemetry.boot_session_id;
    }
    if (telemetry.sensor_serial !== undefined) {
      node.sensor_serial = telemetry.sensor_serial;
    }

    // Auto-recover from STALE to OK if node is not currently in FAULT or SAFE_OFF
    if (node.health_status === NodeHealthStatus.STALE) {
      node.health_status = NodeHealthStatus.OK;
    }

    const savedNode = await this.nodeRegistryRepository.save(node);

    this.eventEmitter.emit(
      'node.telemetry',
      new NodeTelemetryReceivedEvent(nodeId, telemetry, now),
    );

    return savedNode;
  }

  /**
   * Ingest node snapshot: update last_seen_at, override state, health status, RTT, etc.
   */
  async handleSnapshot(
    nodeId: number,
    snapshot: any,
    deviceId?: string,
    receivedAt?: Date,
  ): Promise<NodeRegistry> {
    this.validateNodeId(nodeId);

    let node = await this.nodeRegistryRepository.findOne({
      where: { node_id: nodeId },
    });

    if (!node) {
      node = await this.register(nodeId);
    }

    const previousHealth = node.health_status;
    const now = receivedAt || new Date();
    node.rf_protocol = isAguLegacyNodeId(nodeId) ? 'AGU_LEGACY_SCI' : 'MODERN_RF';

    const isNodeHealthy =
      snapshot.health_status === 'ONLINE' ||
      snapshot.health_status === 'OK' ||
      snapshot.last_ping_ok === true;

    // TECHNICAL DEBT FIX: Only advance last_seen_at when the node physically responded!
    // A snapshot reporting LIVENESS_LOST / STALE / OFFLINE must NOT reset last_seen_at to 'now'.
    if (isNodeHealthy) {
      node.last_seen_at = now;
      node.last_discovered_at = now;
    }

    if (snapshot.source === 'SCHEDULE') {
      node.override_state = OverrideState.NONE;
    } else if (snapshot.override_state === 'ON_LEASE') {
      node.override_state = OverrideState.OVERRIDE_ON;
    } else if (snapshot.override_state === 'OFF_PAUSE') {
      node.override_state = OverrideState.OVERRIDE_OFF;
    } else if (snapshot.override_state === 'NONE') {
      node.override_state = OverrideState.NONE;
    }

    if (snapshot.schedule_state) {
      if (snapshot.schedule_state === 'SPRAYING') {
        node.schedule_state = ScheduleState.SPRAYING;
      } else if (snapshot.schedule_state === 'COOLING_DOWN' || snapshot.schedule_state === 'COOLDOWN') {
        node.schedule_state = ScheduleState.COOLING_DOWN;
      } else if (snapshot.schedule_state === 'IDLE') {
        node.schedule_state = ScheduleState.IDLE;
      } else if (snapshot.schedule_state === 'PAUSED') {
        node.schedule_state = ScheduleState.PAUSED;
      }
    } else if (snapshot.source === 'SCHEDULE') {
      node.schedule_state =
        (snapshot.desired_state === 'ON' || snapshot.reported_state === 'ON')
          ? ScheduleState.SPRAYING
          : ScheduleState.COOLING_DOWN;
    }

    if (snapshot.health_status === 'ONLINE') {
      if (node.health_status !== NodeHealthStatus.FAULT) {
        node.health_status = NodeHealthStatus.OK;
      }
    } else if (snapshot.health_status === 'STALE') {
      node.health_status = NodeHealthStatus.STALE;
    } else if (snapshot.health_status === 'FAULT') {
      node.health_status = NodeHealthStatus.FAULT;
    } else if (snapshot.health_status === 'SAFE_OFF') {
      node.health_status = NodeHealthStatus.SAFE_OFF;
    } else if (snapshot.health_status === 'OFFLINE') {
      node.health_status = NodeHealthStatus.STALE;
    }

    if (typeof snapshot.ping_rtt_ms === 'number' && snapshot.ping_rtt_ms > 0) {
      node.last_rf_rtt_ms = snapshot.ping_rtt_ms;
    }
    if (typeof snapshot.boot_session_id === 'number' && snapshot.boot_session_id > 0) {
      node.last_boot_session_id = snapshot.boot_session_id;
    }

    // Normalize firmware health_status → discovery_status values expected by UI.
    const HEALTH_TO_DISCOVERY: Record<string, string> = {
      ONLINE:     'ONLINE',
      OK:         'ONLINE',
      DISCOVERED: 'DISCOVERED',
      STALE:      'STALE',
      FAULT:      'FAULT',
      SAFE_OFF:   'SAFE_OFF',
      OFFLINE:    'OFFLINE',
    };
    node.discovery_status =
      HEALTH_TO_DISCOVERY[snapshot.health_status] ??
      (snapshot.health_status ? String(snapshot.health_status) : 'ONLINE');

    const savedNode = await this.nodeRegistryRepository.save(node);

    this.eventEmitter.emit(
      'node.telemetry',
      new NodeTelemetryReceivedEvent(nodeId, {
        schedule_state: node.schedule_state,
        override_state: node.override_state,
        boot_session_id: node.last_boot_session_id ?? undefined,
        rssi_dbm: undefined,
        battery_mv: undefined,
        flow_pulse_count: undefined,
        sensor_serial: node.sensor_serial ?? undefined,
        health: node.health_status,
        discovery_status: node.discovery_status,
        is_stale: !isNodeHealthy,
      }, node.last_seen_at ?? now),
    );

    if (previousHealth !== node.health_status) {
      this.eventEmitter.emit(
        'node.health_changed',
        new NodeHealthChangedEvent(
          nodeId,
          previousHealth,
          node.health_status,
          snapshot.transition_reason || 'SNAPSHOT_UPDATE',
          now,
        ),
      );
    }

    return savedNode;
  }

  /**
   * Check for staleness across nodes. If any node exceeds threshold, transition to STALE and emit alert.
   */
  async checkStaleness(nodeId?: number): Promise<NodeRegistry[]> {
    const nodes = nodeId
      ? await this.nodeRegistryRepository.find({ where: { node_id: nodeId } })
      : await this.nodeRegistryRepository.find();

    const now = Date.now();
    const staleNodes: NodeRegistry[] = [];

    for (const node of nodes) {
      if (!node.last_seen_at) continue;

      const msSinceLastSeen = now - new Date(node.last_seen_at).getTime();
      if (
        msSinceLastSeen > this.staleThresholdMs &&
        node.health_status === NodeHealthStatus.OK
      ) {
        node.health_status = NodeHealthStatus.STALE;
        node.discovery_status = 'STALE';
        const saved = await this.nodeRegistryRepository.save(node);
        staleNodes.push(saved);

        this.logger.warn(
          `Node #${node.node_id} marked STALE (silent for ${msSinceLastSeen}ms).`,
        );

        this.eventEmitter.emit(
          'staleness_alert',
          new NodeStalenessAlertEvent(
            node.node_id,
            node.last_seen_at,
            msSinceLastSeen,
            new Date(now),
          ),
        );

        this.eventEmitter.emit(
          'node.health_changed',
          new NodeHealthChangedEvent(
            node.node_id,
            NodeHealthStatus.OK,
            NodeHealthStatus.STALE,
            'STALENESS_TIMEOUT',
            new Date(now),
          ),
        );
      }
    }

    return staleNodes;
  }

  /**
   * Get detail and status for a specific node.
   */
  async getNodeStatus(nodeId: number): Promise<NodeStatusResponse> {
    this.validateNodeId(nodeId);

    const node = await this.nodeRegistryRepository.findOne({
      where: { node_id: nodeId },
      relations: ['active_sensor_calibration'],
    });

    if (!node) {
      throw new NotFoundException(`Node #${nodeId} not found in registry.`);
    }

    return this.buildNodeStatusResponse(node);
  }

  /** Get status for all registered modern nodes. */
  async getAllNodesStatus(): Promise<NodeStatusResponse[]> {
    const nodes = await this.nodeRegistryRepository.find({
      relations: ['active_sensor_calibration'],
      order: { node_id: 'ASC' },
    });

    return nodes.map((node) => this.buildNodeStatusResponse(node));
  }

  /**
   * Update flow sensor calibration for a node.
   * Creates a new versioned SensorCalibration record and updates active reference.
   */
  async updateCalibration(
    nodeId: number,
    dto: UpdateNodeCalibrationDto,
  ): Promise<NodeStatusResponse> {
    this.validateNodeId(nodeId);

    const node = await this.nodeRegistryRepository.findOne({
      where: { node_id: nodeId },
    });
    if (!node) {
      throw new NotFoundException(`Node #${nodeId} not found in registry.`);
    }

    const pulsesPerLitre = dto.getEffectivePulsesPerLitre();
    if (!pulsesPerLitre || pulsesPerLitre <= 0 || pulsesPerLitre >= 10000) {
      throw new BadRequestException(
        `Calibration pulses per litre (${pulsesPerLitre}) must be greater than 0 and less than 10000.`,
      );
    }

    const refVol = dto.reference_volume_ml ?? 1000;
    const serial =
      dto.sensor_serial ?? node.sensor_serial ?? `SEN-N${nodeId}-FLOW`;
    const now = new Date();

    let newCalibrationId = 0;
    let newVersionNum = 1;

    await this.dataSource.transaction(async (manager) => {
      // Find latest calibration version for this node and serial
      const latestCal = await manager.findOne(SensorCalibration, {
        where: { node_id: nodeId, sensor_serial: serial },
        order: { version_num: 'DESC' },
      });

      newVersionNum = latestCal ? latestCal.version_num + 1 : 1;

      // Supersede previous active calibration for this node
      await manager
        .createQueryBuilder()
        .update(SensorCalibration)
        .set({ status: CalibrationStatusEnum.SUPERSEDED })
        .where('node_id = :nodeId AND status = :activeStatus', {
          nodeId,
          activeStatus: CalibrationStatusEnum.ACTIVE,
        })
        .execute();

      // Create new SensorCalibration record
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
        calibrated_by: dto.calibrated_by ?? 'operator',
        calibrated_at: now,
      });

      const savedCal = await manager.save(newCal);
      newCalibrationId = savedCal.id;

      // Update NodeRegistry with active calibration reference
      await manager
        .createQueryBuilder()
        .update(NodeRegistry)
        .set({
          active_sensor_calibration_id: newCalibrationId,
          calibration_status: CalibrationStatus.CALIBRATED,
          sensor_serial: serial,
          updated_at: now,
        })
        .where('node_id = :nodeId', { nodeId })
        .execute();
    });

    this.logger.log(
      `Node #${nodeId} calibration updated: version #${newVersionNum}, ${pulsesPerLitre.toFixed(4)} pulses/L.`,
    );

    this.eventEmitter.emit(
      'node.calibration_updated',
      new NodeCalibrationUpdatedEvent(
        nodeId,
        newCalibrationId,
        pulsesPerLitre.toFixed(4),
        newVersionNum,
        now,
      ),
    );

    return this.getNodeStatus(nodeId);
  }

  private buildNodeStatusResponse(node: NodeRegistry): NodeStatusResponse {
    const now = Date.now();
    const msSinceLastSeen = node.last_seen_at
      ? now - new Date(node.last_seen_at).getTime()
      : Infinity;

    const isStale =
      node.last_seen_at === null || msSinceLastSeen > this.staleThresholdMs;

    return {
      node_id: node.node_id,
      display_name: node.display_name,
      cached_group_id: node.cached_group_id,
      sensor_serial: node.sensor_serial,
      calibration_status: node.calibration_status,
      schedule_state: node.schedule_state,
      override_state: node.override_state,
      last_boot_session_id: node.last_boot_session_id,
      last_seen_at: node.last_seen_at,
      health_status: node.health_status,
      is_stale: isStale,
      stale_for_ms: node.last_seen_at ? msSinceLastSeen : -1,
      rf_protocol: node.rf_protocol,
      last_scan_id: node.last_scan_id,
      last_rf_rtt_ms: node.last_rf_rtt_ms,
      last_discovered_at: node.last_discovered_at,
      discovery_status: node.discovery_status,
      active_calibration: node.active_sensor_calibration
        ? {
            id: node.active_sensor_calibration.id,
            version_num: node.active_sensor_calibration.version_num,
            pulses_per_litre:
              node.active_sensor_calibration.pulses_per_litre,
            reference_volume_ml:
              node.active_sensor_calibration.reference_volume_ml,
            calibrated_at: node.active_sensor_calibration.calibrated_at,
            calibrated_by: node.active_sensor_calibration.calibrated_by,
          }
        : null,
    };
  }

  /**
   * Trigger active RF probe sweep on Gateway and collect discovered nodes.
   */
  async scanRfNodes(deviceId: string): Promise<RfScanResponse> {
    const scanId = `scan_${Date.now()}`;
    if (!this.mqttService || !this.mqttService.isConnected()) {
      throw new BadRequestException('MQTT_UNAVAILABLE: gateway connection is offline.');
    }
    if (this.activeScans.has(deviceId)) {
      throw new BadRequestException('SCAN_IN_PROGRESS: gateway already has an RF scan running.');
    }
    this.activeScans.add(deviceId);
    this.eventEmitter.emit('rf_scan.started', { scan_id: scanId, device_id: deviceId });

    const scanPromise = new Promise<{
      duration_ms: number;
      status: RfScanResponse['status'];
      error?: string;
      error_code?: string;
      nodes: any[];
    }>((resolve) => {
      const timeout = setTimeout(() => {
        this.eventEmitter.removeListener('gateway.scan_results', listener);
        this.logger.warn(`Scan request "${scanId}" timed out waiting for gateway.`);
        resolve({
          duration_ms: this.rfScanResultTimeoutMs,
          status: 'TIMEOUT',
          error: 'GATEWAY_TIMEOUT',
          error_code: 'SCAN_RESULT_CORRELATION_TIMEOUT',
          nodes: [],
        });
      }, this.rfScanResultTimeoutMs);

      const listener = (event: { deviceId: string; payload: any }) => {
        if (event.deviceId !== deviceId || event.payload?.scan_id !== scanId) {
          this.logger.warn(
            `Ignoring RF scan result correlation mismatch: expected device=${deviceId} scan=${scanId}, ` +
            `received device=${event.deviceId} scan=${String(event.payload?.scan_id ?? '(missing)')}`,
          );
          return;
        }
        if (event.deviceId === deviceId && event.payload?.scan_id === scanId) {
          clearTimeout(timeout);
          this.eventEmitter.removeListener('gateway.scan_results', listener);
          resolve({
            duration_ms: event.payload?.duration_ms ?? 1000,
            status: String(event.payload?.status ?? 'COMPLETED') as RfScanResponse['status'],
            error: event.payload?.error ? String(event.payload.error) : undefined,
            error_code: event.payload?.error_code ? String(event.payload.error_code) : undefined,
            nodes: event.payload?.nodes ?? [],
          });
        }
      };

      this.eventEmitter.on('gateway.scan_results', listener);
    });

    try {
      const scanTopic = MQTT_PUBLISH_TEMPLATES.GATEWAY_SCAN(deviceId);
      this.logger.log(
        `Publishing RF scan command: device=${deviceId} scan_id=${scanId} topic=${scanTopic} envelope_version=1`,
      );
      await this.mqttService.publish(
        scanTopic,
        { scan_id: scanId, command_id: scanId, version: 1 },
      );
    } catch (err: any) {
      this.logger.error(`Failed to publish scan command to gateway: ${err.message}`);
      const response = {
        scan_id: scanId,
        duration_ms: 0,
        status: 'FAILED' as const,
        error: 'MQTT_PUBLISH_FAILED',
        error_code: 'MQTT_PUBLISH_FAILED',
        nodes: [],
      };
      this.activeScans.delete(deviceId);
      this.eventEmitter.emit('rf_scan.failed', { ...response, device_id: deviceId });
      return response;
    }

    const result = await scanPromise;
    const assignedNodes = await this.nodeRegistryRepository.find();
    const assignedIds = new Set(assignedNodes.map((n) => n.node_id));

    const scanTime = new Date();
    const scannedById = new Map((result.nodes || []).map((node: any) => [Number(node.node_id), node]));
    const formattedNodes: DiscoveredRfNode[] = AGU_LEGACY_NODE_IDS.map((nodeId) => {
      const node = scannedById.get(nodeId) ?? {
        node_id: nodeId,
        online: result.status === 'TIMEOUT' ? null : false,
        failure_reason: result.status === 'TIMEOUT' ? 'GATEWAY_RESULT_TIMEOUT' : 'MISSING_RESULT',
      };
      return {
      node_id: Number(node.node_id),
      // Preserve null as UNKNOWN. Boolean(null) would incorrectly turn a
      // gateway-level timeout into an OFFLINE node in the API/UI.
      online: node.online == null ? null : Boolean(node.online),
      rtt_ms: node.rtt_ms == null ? null : Number(node.rtt_ms),
      protocol: String(node.protocol ?? 'MODERN_RF'),
      is_assigned: assignedIds.has(Number(node.node_id)),
      current_slot: assignedIds.has(Number(node.node_id)) ? Number(node.node_id) : undefined,
      boot_session_id: node.boot_session_id == null ? undefined : Number(node.boot_session_id),
      failure_reason: node.failure_reason == null ? undefined : String(node.failure_reason),
      };
    });

    for (const scanned of formattedNodes) {
      if (!isAguLegacyNodeId(scanned.node_id)) continue;
      let node = await this.nodeRegistryRepository.findOne({ where: { node_id: scanned.node_id } });
      if (!node) node = await this.register(scanned.node_id);
      node.last_scan_id = scanId;
      node.last_discovered_at = scanTime;
      node.rf_protocol = scanned.protocol;
      node.last_rf_rtt_ms = scanned.rtt_ms;
      if (scanned.online === null) continue;
      node.discovery_status = scanned.online ? 'DISCOVERED' : 'STALE';
      if (scanned.online) {
        node.last_seen_at = scanTime;
        if (node.health_status !== NodeHealthStatus.FAULT) node.health_status = NodeHealthStatus.OK;
      } else if (node.health_status !== NodeHealthStatus.FAULT) {
        node.health_status = NodeHealthStatus.STALE;
      }
      await this.nodeRegistryRepository.save(node);
    }
    const registered = await this.nodeRegistryRepository.find();
    for (const node of registered) {
      if (!isAguLegacyNodeId(node.node_id) || formattedNodes.some((item) => item.node_id === node.node_id)) continue;
      if (node.health_status !== NodeHealthStatus.FAULT) {
        node.health_status = NodeHealthStatus.STALE;
        node.discovery_status = 'STALE';
        await this.nodeRegistryRepository.save(node);
      }
    }

    const response = {
      scan_id: scanId,
      duration_ms: result.duration_ms,
      status: result.status as RfScanResponse['status'],
      error: result.error,
      error_code: result.error_code,
      nodes: formattedNodes,
    };
    this.activeScans.delete(deviceId);
    this.eventEmitter.emit(result.status === 'FAILED' || result.status === 'TIMEOUT' ? 'rf_scan.failed' : 'rf_scan.completed', {
      ...response,
      device_id: deviceId,
    });
    return response;
  }

  /**
   * Fixed physical IDs cannot be claimed or reassigned in production.
   */
  async claimNode(
    dto: ClaimNodeDto,
    deviceId: string,
  ): Promise<NodeStatusResponse> {
    throw new BadRequestException('NODE_ID_FIXED: claim/SET_ID is disabled; use RF discovery with physical IDs 4..7.');

    if (!this.mqttService || !this.mqttService.isConnected()) {
      throw new BadRequestException('Cannot claim node: MQTT gateway connection is offline.');
    }

    const commandId = randomUUID();

    const ackPromise = new Promise<{ status: string; reason?: string }>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.eventEmitter.removeListener('mqtt.command.ack', listener);
        reject(new BadRequestException('Gateway claim request timed out.'));
      }, 4500);

      const listener = (event: { commandId?: string; payload?: any }) => {
        const cmd = event.commandId ?? event.payload?.command_id;
        if (cmd === commandId) {
          clearTimeout(timeout);
          this.eventEmitter.removeListener('mqtt.command.ack', listener);
          resolve({
            status: event.payload?.status ?? 'COMPLETED',
            reason: event.payload?.reason,
          });
        }
      };

      this.eventEmitter.on('mqtt.command.ack', listener);
    });

    await this.mqttService.publish(
      MQTT_PUBLISH_TEMPLATES.GATEWAY_CLAIM(deviceId),
      {
        command_id: commandId,
        from_node_id: dto.fromNodeId,
        to_node_id: dto.toNodeId,
      },
    );

    const outcome = await ackPromise;
    if (outcome.status === 'REJECTED' || outcome.status === 'FAILED') {
      throw new BadRequestException(
        `Node claim failed: ${outcome.reason ?? 'Unknown error from gateway'}`,
      );
    }

    // Persist/update node in database
    let node = await this.nodeRegistryRepository.findOne({
      where: { node_id: dto.toNodeId },
    });
    if (!node) {
      node = await this.register(dto.toNodeId);
    }
    node.last_seen_at = new Date();
    node.health_status = NodeHealthStatus.OK;
    await this.nodeRegistryRepository.save(node);

    this.logger.log(`Node #${dto.toNodeId} successfully claimed from RF ID #${dto.fromNodeId}.`);
    return this.getNodeStatus(dto.toNodeId);
  }

  private validateNodeId(nodeId: number): void {
    if (!isModernNodeId(nodeId)) {
      throw new BadRequestException('Node ID must be between 1 and 15.');
    }
  }
}
