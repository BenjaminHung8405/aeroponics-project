import { Injectable, Logger, Optional } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { OnEvent, EventEmitter2 } from '@nestjs/event-emitter';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';

import { MQTT_EVENTS } from './mqtt.constants';
import { DeviceStatus } from '../device/entities/device_status.entity';
import { NodeService } from '../node/node.service';
import { NodeHealthStatus } from '../node/entities/node_registry.entity';
import { FlowService } from '../flow/flow.service';
import { RecordFlowEventDto } from '../flow/dto/record-flow-event.dto';
import { PumpCommandService } from '../pump-command/pump-command.service';
import { PumpCommand } from '../pump-command/entities/pump_command.entity';
import { FlowEvent } from '../flow/entities/flow_event.entity';
import { CommandAcceptedEvent } from '../pump-command/events/pump-command.events';
import { MqttService } from './mqtt.service';
import { ClockSyncService } from './clock-sync.service';
import { isModernNodeId } from '../node/node-topology';

const UUID_REGEX =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

@Injectable()
export class MqttRouterService {
  private readonly logger = new Logger(MqttRouterService.name);

  constructor(
    @InjectRepository(DeviceStatus)
    private readonly deviceStatusRepo: Repository<DeviceStatus>,
    private readonly nodeService: NodeService,
    private readonly flowService: FlowService,
    private readonly pumpCommandService: PumpCommandService,
    private readonly mqttService: MqttService,
    private readonly configService: ConfigService,
    private readonly eventEmitter: EventEmitter2,
    @Optional()
    private readonly clockSyncService?: ClockSyncService,
  ) {}

  @OnEvent(MQTT_EVENTS.DEVICE_STATUS)
  async handleDeviceStatusEvent(event: {
    deviceId: string;
    payload: any;
    receivedAt?: Date;
  }): Promise<void> {
    await this.handleGatewayHeartbeat(event.deviceId, event.payload, event.receivedAt);
  }

  @OnEvent(MQTT_EVENTS.GATEWAY_HEARTBEAT)
  async handleGatewayHeartbeatEvent(event: {
    gatewayId: string;
    payload: any;
    receivedAt?: Date;
  }): Promise<void> {
    await this.handleGatewayHeartbeat(event.gatewayId, event.payload, event.receivedAt);
  }

  @OnEvent(MQTT_EVENTS.NODE_TELEMETRY)
  async handleNodeTelemetryEvent(event: {
    nodeId: number;
    payload: any;
    topic?: string;
  }): Promise<void> {
    if (!isModernNodeId(event.nodeId)) return;
    if (event.topic?.startsWith('aeroponics/v1/')) {
      await this.mapV1ToDeviceAlias(event.nodeId, 'telemetry', event.payload);
    }
    await this.handleNodeTelemetry(event.nodeId, event.payload);
  }

  @OnEvent(MQTT_EVENTS.NODE_FLOW)
  async handleNodeFlowEvent(event: {
    nodeId: number;
    payload: any;
    topic?: string;
  }): Promise<void> {
    if (!isModernNodeId(event.nodeId)) return;
    if (event.topic?.startsWith('aeroponics/v1/')) {
      await this.mapV1ToDeviceAlias(event.nodeId, 'flow', event.payload);
    }
    await this.handleNodeFlow(event.nodeId, event.payload);
  }

  @OnEvent(MQTT_EVENTS.NODE_ACK)
  async handleNodeAckEvent(event: {
    nodeId: number;
    payload: any;
    topic?: string;
  }): Promise<void> {
    if (!isModernNodeId(event.nodeId)) return;
    if (event.topic?.startsWith('aeroponics/v1/')) {
      await this.mapV1ToDeviceAlias(event.nodeId, 'ack', event.payload);
    }
    await this.handleNodeAck(event.nodeId, event.payload);
  }

  /**
   * Gateway-level ACKs are emitted on aeroponics/device/{id}/ack/{commandId}
   * and aeroponics/ack/{commandId}. Admission ACCEPTED is emitted separately;
   * only a real RF acknowledgement is persisted through PumpCommandService.
   */
  @OnEvent(MQTT_EVENTS.COMMAND_ACK)
  async handleCommandAckEvent(event: {
    nodeId?: number;
    topic?: string;
    commandId?: string;
    payload?: any;
  }): Promise<void> {
    const commandId = event.commandId ?? event.payload?.command_id;
    if (!commandId) return;

    if (event.topic?.startsWith('aeroponics/v1/') && event.nodeId) {
      await this.mapV1ToDeviceAlias(event.nodeId, 'ack', event.payload);
    }

    const status = String(event.payload?.status ?? '').toUpperCase();
    // S4-WS-05 / Track V3: `acked` is true ONLY for a genuine RF
    // acknowledgement — never for an admission (ACCEPTED) decision.
    const acked =
      event.payload?.acked === true || status === 'RF_ACKED';

    if (status === 'ACCEPTED') {
      // Lifecycle admission is emitted separately so the dashboard never
      // renders "Đã nhận lệnh (RF)" before the RF transmission actually
      // completes (Finding #7).
      this.eventEmitter.emit(
        MQTT_EVENTS.COMMAND_ACCEPTED,
        new CommandAcceptedEvent(
          event.nodeId ?? event.payload?.node_id ?? 0,
          commandId,
          status,
          new Date(),
        ),
      );
    }

    // An explicit boolean `acked` (true or false) or a status-derived ACK
    // drives the RF acknowledgement pipeline; ACCEPTED is never passed through.
    if (
      typeof event.payload?.acked === 'boolean' ||
      status === 'RF_ACKED'
    ) {
      await this.handleNodeAck(event.payload?.node_id ?? 0, {
        ...event.payload,
        command_id: commandId,
        acked,
      });
    }
  }

  @OnEvent(MQTT_EVENTS.NODE_SNAPSHOT)
  async handleNodeSnapshotEvent(event: {
    nodeId: number;
    payload: any;
    deviceId?: string;
    receivedAt?: Date;
  }): Promise<void> {
    if (!isModernNodeId(event.nodeId)) return;
    try {
      await this.nodeService.handleSnapshot(
        event.nodeId,
        event.payload,
        event.deviceId,
        event.receivedAt,
      );

      // Correlate last_command_id if reported in snapshot
      const cmdId = event.payload?.last_command_id;
      const cmdRes = event.payload?.last_command_result;
      if (cmdId && cmdRes) {
        const acked = cmdRes === 'RF_ACKED';
        await this.handleNodeAck(event.nodeId, {
          command_id: cmdId,
          acked,
          status: cmdRes,
        });
      }
    } catch (err: any) {
      this.logger.error(
        `Failed to process snapshot for node #${event.nodeId}: ${err.message}`,
        err.stack,
      );
    }
  }

  @OnEvent(MQTT_EVENTS.NODE_FAULT)
  async handleNodeFaultEvent(event: {
    nodeId: number;
    payload: any;
    topic?: string;
  }): Promise<void> {
    if (!isModernNodeId(event.nodeId)) return;
    if (event.topic?.startsWith('aeroponics/v1/')) {
      await this.mapV1ToDeviceAlias(event.nodeId, 'fault', event.payload);
    }
    await this.handleNodeFault(event.nodeId, event.payload);
  }

  /**
   * Publish a v1 inbound frame once on the legacy device namespace for old
   * subscribers. The backend never republishes the frame to the v1 namespace.
   */
  async mapV1ToDeviceAlias(
    nodeId: number,
    eventType: 'ack' | 'telemetry' | 'flow' | 'event' | 'fault',
    payload: any,
  ): Promise<void> {
    const deviceId = this.configService.get<string>(
      'MQTT_DEVICE_ID',
      'esp32_device',
    );
    const deviceTopic = `aeroponics/device/${deviceId}`;
    const topics: Record<typeof eventType, string> = {
      ack: `${deviceTopic}/ack/${payload?.command_id ?? 'unknown'}`,
      telemetry: `${deviceTopic}/telemetry`,
      flow: `${deviceTopic}/flow`,
      event: `${deviceTopic}/event`,
      fault: `${deviceTopic}/fault`,
    };

    try {
      await this.mqttService.publish(topics[eventType], {
        ...payload,
        node_id: nodeId,
      });
    } catch (error: any) {
      this.logger.warn(
        `Failed to publish v1 alias for node #${nodeId} to ${topics[eventType]}: ${error.message}`,
      );
    }
  }

  @OnEvent(MQTT_EVENTS.GATEWAY_SCAN_RESULTS)
  async handleGatewayScanResultsEvent(event: {
    deviceId: string;
    payload: any;
    receivedAt?: Date;
  }): Promise<void> {
    this.logger.log(
      `Received RF scan results from gateway "${event.deviceId}": ${event.payload?.nodes?.length ?? 0} node(s) found`,
    );
    this.eventEmitter.emit('gateway.scan_results', event);
  }

  /**
   * Handle gateway heartbeat from aeroponics/gateway/{gatewayId}/heartbeat
   */
  async handleGatewayHeartbeat(
    gatewayId: string,
    payload: any,
    receivedAt?: Date,
  ): Promise<DeviceStatus> {
    try {
      let status = await this.deviceStatusRepo.findOne({
        where: { device_id: gatewayId },
      });

      if (!status) {
        status = this.deviceStatusRepo.create({ device_id: gatewayId });
      }

      const wasOfflineOrNew = !status.status || status.status === 'offline';
      const rtcWasInvalid = status.rtc_valid === false;

      status.status = payload.status ?? 'online';
      status.uptime_s =
        payload.uptime_s !== undefined ? String(payload.uptime_s) : '0';
      status.rssi_dbm = payload.rssi_dbm ?? null;
      status.free_heap_b = payload.free_heap_b ?? null;
      status.ntp_synced = Boolean(payload.ntp_synced);
      status.rtc_valid = Boolean(payload.rtc_valid);
      status.time_source = payload.time_source ?? null;
      status.last_sync_unix_time_utc =
        payload.last_sync_unix_time_utc !== undefined && payload.last_sync_unix_time_utc !== null
          ? String(payload.last_sync_unix_time_utc)
          : null;
      status.last_seen_at = receivedAt ?? new Date();

      const saved = await this.deviceStatusRepo.save(status);
      this.logger.debug(
        `DeviceStatus updated for gateway "${gatewayId}": status=${saved.status}, uptime=${saved.uptime_s}s`,
      );

      this.eventEmitter.emit('device.status_changed', {
        deviceId: saved.device_id,
        status: saved.status,
        uptime_s: Number(saved.uptime_s || 0),
        rssi_dbm: saved.rssi_dbm,
        free_heap_b: saved.free_heap_b,
        ntpSynced: saved.ntp_synced,
        rtcValid: saved.rtc_valid,
        timeSource: saved.time_source,
        lastSyncUnixTimeUtc: saved.last_sync_unix_time_utc,
        lastSeenAt: saved.last_seen_at ? saved.last_seen_at.toISOString() : new Date().toISOString(),
      });

      if (
        this.clockSyncService &&
        saved.status === 'online' &&
        (wasOfflineOrNew || !saved.rtc_valid)
      ) {
        this.clockSyncService.pushTimeToDevice(saved.device_id).catch((err: any) => {
          this.logger.warn(
            `Auto clock sync push to "${saved.device_id}" failed: ${err.message ?? err}`,
          );
        });
      }

      return saved;
    } catch (err: any) {
      this.logger.error(
        `Failed to persist gateway heartbeat for "${gatewayId}": ${err.message}`,
        err.stack,
      );
      throw err;
    }
  }

  /**
   * Handle node telemetry from aeroponics/node/{nodeId}/telemetry
   */
  async handleNodeTelemetry(nodeId: number, payload: any): Promise<void> {
    try {
      await this.nodeService.handleTelemetry(nodeId, payload);
    } catch (err: any) {
      this.logger.error(
        `Failed to process telemetry for node #${nodeId}: ${err.message}`,
        err.stack,
      );
    }
  }

  /**
   * Handle node flow event from aeroponics/node/{nodeId}/flow
   */
  async handleNodeFlow(nodeId: number, payload: any): Promise<FlowEvent | null> {
    try {
      const flowRate =
        typeof payload.flow_rate_lpm === 'number'
          ? payload.flow_rate_lpm
          : parseFloat(payload.flow_rate_lpm || 0);

      const dto: RecordFlowEventDto = {
        node_id: nodeId,
        flow_rate_lpm: flowRate,
        litres_total:
          payload.litres_total !== undefined ? String(payload.litres_total) : undefined,
        pulse_count:
          payload.pulse_count !== undefined ? String(payload.pulse_count) : undefined,
        delivered_volume_ml: payload.delivered_volume_ml,
        sample_window_ms: payload.sample_window_ms,
        sensor_calibration_id: payload.sensor_calibration_id,
        flow_confirmed: payload.flow_confirmed,
        flow_stability_pct:
          payload.flow_stability_pct !== undefined
            ? String(payload.flow_stability_pct)
            : undefined,
        quality_flag: payload.quality_flag,
        fault_code: payload.fault_code,
        boot_session_id: payload.boot_session_id,
        rf_seq: payload.rf_seq,
        node_timestamp_ms:
          payload.node_timestamp_ms !== undefined
            ? String(payload.node_timestamp_ms)
            : undefined,
        gateway_timestamp_ms:
          payload.gateway_timestamp_ms !== undefined
            ? String(payload.gateway_timestamp_ms)
            : undefined,
        group_id: payload.group_id,
        command_id: payload.command_id,
      };

      return await this.flowService.recordFlowEvent(dto);
    } catch (err: any) {
      this.logger.error(
        `Failed to record flow event for node #${nodeId}: ${err.message}`,
        err.stack,
      );
      return null;
    }
  }

  /**
   * Handle node RF ACK from aeroponics/node/{nodeId}/ack
   * Calls PumpCommandService.handleRfAck to transition command outcome.
   *
   * S4-WS-05 / Track V3: `payload.acked` is the authoritative RF evidence and is
   * read first; `payload.status` is only a backward-compatible fallback for
   * legacy firmware that does not publish the boolean field. An admission
   * status such as `ACCEPTED` therefore never resolves to `acked = true`.
   */
  async handleNodeAck(nodeId: number, payload: any): Promise<PumpCommand | null> {
    const commandId = payload.command_id || payload.commandId;
    if (!commandId) {
      this.logger.warn(
        `Received ACK on node #${nodeId} without command_id. Discarding payload: ${JSON.stringify(payload)}`,
      );
      return null;
    }

    const acked =
      payload.acked === true || payload.status === 'RF_ACKED';

    const meta = {
      latencyMs: payload.latency_ms ?? payload.latencyMs,
      nodeTimestampMs:
        payload.node_timestamp_ms !== undefined
          ? String(payload.node_timestamp_ms)
          : undefined,
      gatewayTimestampMs:
        payload.gateway_timestamp_ms !== undefined
          ? String(payload.gateway_timestamp_ms)
          : undefined,
    };

    try {
      const updated = await this.pumpCommandService.handleRfAck(
        commandId,
        acked,
        meta,
      );
      this.logger.log(
        `RF ACK processed for node #${nodeId}, command "${commandId}": outcome=${updated.outcome}, acked=${acked}`,
      );
      return updated;
    } catch (err: any) {
      this.logger.error(
        `Failed to handle RF ACK for command "${commandId}" on node #${nodeId}: ${err.message}`,
        err.stack,
      );
      return null;
    }
  }

  /**
   * Handle node fault from aeroponics/node/{nodeId}/fault
   */
  async handleNodeFault(nodeId: number, payload: any): Promise<void> {
    const reason =
      payload.reason ||
      payload.fault_code ||
      `Node #${nodeId} fault reported via MQTT`;

    try {
      await this.nodeService.updateHealth(nodeId, NodeHealthStatus.FAULT, {
        reason,
      });

      if (payload.command_id) {
        if (UUID_REGEX.test(payload.command_id)) {
          await this.pumpCommandService.handleFault(payload.command_id, reason);
        } else {
          this.logger.warn(
            `Node #${nodeId} fault reported non-UUID command_id "${payload.command_id}". Skipping command fault association.`,
          );
        }
      }
    } catch (err: any) {
      this.logger.error(
        `Failed to process fault for node #${nodeId}: ${err.message}`,
        err.stack,
      );
    }
  }
}
