import { Injectable, Logger } from '@nestjs/common';
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

@Injectable()
export class MqttRouterService {
  private readonly logger = new Logger(MqttRouterService.name);

  constructor(
    @InjectRepository(DeviceStatus)
    private readonly deviceStatusRepo: Repository<DeviceStatus>,
    private readonly nodeService: NodeService,
    private readonly flowService: FlowService,
    private readonly pumpCommandService: PumpCommandService,
    private readonly eventEmitter: EventEmitter2,
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
  }): Promise<void> {
    await this.handleNodeTelemetry(event.nodeId, event.payload);
  }

  @OnEvent(MQTT_EVENTS.NODE_FLOW)
  async handleNodeFlowEvent(event: {
    nodeId: number;
    payload: any;
  }): Promise<void> {
    await this.handleNodeFlow(event.nodeId, event.payload);
  }

  @OnEvent(MQTT_EVENTS.NODE_ACK)
  async handleNodeAckEvent(event: {
    nodeId: number;
    payload: any;
  }): Promise<void> {
    await this.handleNodeAck(event.nodeId, event.payload);
  }

  @OnEvent(MQTT_EVENTS.NODE_FAULT)
  async handleNodeFaultEvent(event: {
    nodeId: number;
    payload: any;
  }): Promise<void> {
    await this.handleNodeFault(event.nodeId, event.payload);
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

      status.status = payload.status ?? 'online';
      status.uptime_s =
        payload.uptime_s !== undefined ? String(payload.uptime_s) : '0';
      status.rssi_dbm = payload.rssi_dbm ?? null;
      status.free_heap_b = payload.free_heap_b ?? null;
      status.ntp_synced = Boolean(payload.ntp_synced);
      status.rtc_valid = Boolean(payload.rtc_valid);
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
        lastSeenAt: saved.last_seen_at ? saved.last_seen_at.toISOString() : new Date().toISOString(),
      });

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
      payload.acked !== undefined
        ? Boolean(payload.acked)
        : payload.status === 'ACCEPTED' ||
          payload.status === 'COMPLETED' ||
          payload.status === 'OK';

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
        await this.pumpCommandService.handleFault(payload.command_id, reason);
      }
    } catch (err: any) {
      this.logger.error(
        `Failed to process fault for node #${nodeId}: ${err.message}`,
        err.stack,
      );
    }
  }
}
