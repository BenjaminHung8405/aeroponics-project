import {
  WebSocketGateway,
  WebSocketServer,
  OnGatewayInit,
  OnGatewayConnection,
  OnGatewayDisconnect,
} from '@nestjs/websockets';
import { Logger, OnModuleInit, OnModuleDestroy } from '@nestjs/common';
import { OnEvent } from '@nestjs/event-emitter';
import { WebSocket, Server } from 'ws';

import { NodeService } from '../node/node.service';
import { GroupService } from '../group/group.service';
import { DeviceService } from '../device/device.service';
import {
  NodeTelemetryReceivedEvent,
  NodeStalenessAlertEvent,
  NodeHealthChangedEvent,
  NodeFaultResetEvent,
} from '../node/events/node.events';
import { FlowEventRecordedEvent } from '../flow/events/flow.events';
import {
  PumpCommandSentEvent,
  PumpCommandAckedEvent,
  PumpCommandFlowConfirmedEvent,
  PumpCommandFaultEvent,
} from '../pump-command/events/pump-command.events';
import {
  GroupAssignedEvent,
  GroupUnassignedEvent,
} from '../group/events/group.events';

export interface WebSocketBroadcastMessage {
  event: string;
  data: any;
  timestamp: string;
}

@WebSocketGateway({ path: '/ws' })
export class EventsGateway
  implements
    OnGatewayInit,
    OnGatewayConnection,
    OnGatewayDisconnect,
    OnModuleInit,
    OnModuleDestroy
{
  private readonly logger = new Logger(EventsGateway.name);

  @WebSocketServer()
  server!: Server;

  private readonly clients = new Set<WebSocket>();
  private stalenessTimer: NodeJS.Timeout | null = null;

  constructor(
    private readonly nodeService: NodeService,
    private readonly groupService: GroupService,
    private readonly deviceService: DeviceService,
  ) {}

  @OnEvent('rf_scan.started')
  handleRfScanStarted(event: any): void { this.broadcast('rf_scan_started', event); }

  @OnEvent('rf_scan.progress')
  handleRfScanProgress(event: any): void { this.broadcast('rf_scan_progress', event); }

  @OnEvent('rf_scan.completed')
  handleRfScanCompleted(event: any): void { this.broadcast('rf_scan_completed', event); }

  @OnEvent('rf_scan.failed')
  handleRfScanFailed(event: any): void { this.broadcast('rf_scan_failed', event); }

  @OnEvent('water_quality.telemetry')
  handleWaterQualityTelemetry(event: any): void {
    this.broadcast('water_quality_telemetry', event);
  }

  @OnEvent('water_quality.status')
  handleWaterQualityStatus(event: any): void {
    this.broadcast('water_quality_status', event);
  }

  @OnEvent('device.schedule_sync_changed')
  handleScheduleSyncChanged(event: any): void {
    this.broadcast('device_schedule_sync', event);
  }

  onModuleInit(): void {
    // Automated staleness detection check loop (every 15s)
    this.stalenessTimer = setInterval(async () => {
      try {
        await this.nodeService.checkStaleness();
        await this.deviceService.checkDeviceStaleness();
      } catch (err: any) {
        this.logger.error(
          `Periodic staleness detection encountered an error: ${err.message}`,
        );
      }
    }, 15000);
  }

  onModuleDestroy(): void {
    if (this.stalenessTimer) {
      clearInterval(this.stalenessTimer);
      this.stalenessTimer = null;
    }

    for (const client of this.clients) {
      try {
        client.close(1000, 'Server shutting down');
      } catch {}
    }
    this.clients.clear();
  }

  afterInit(_server: Server): void {
    this.logger.log('Native WebSocket EventsGateway initialized on path "/ws".');
  }

  handleConnection(client: WebSocket, ..._args: any[]): void {
    this.clients.add(client);
    this.logger.log(
      `WebSocket client connected. Active connections: ${this.clients.size}`,
    );

    client.on('close', () => {
      this.clients.delete(client);
    });

    client.on('error', (err) => {
      this.logger.warn(`WebSocket client connection error: ${err.message}`);
      this.clients.delete(client);
    });

    // Send welcome confirmation
    try {
      if (client.readyState === WebSocket.OPEN) {
        client.send(
          JSON.stringify({
            event: 'connected',
            data: { message: 'Aeroponics Real-time WebSocket Gateway Connected' },
            timestamp: new Date().toISOString(),
          }),
        );
      }
    } catch (err: any) {
      this.logger.warn(`Failed to send welcome message: ${err.message}`);
    }
  }

  handleDisconnect(client: WebSocket): void {
    this.clients.delete(client);
    this.logger.log(
      `WebSocket client disconnected. Active connections: ${this.clients.size}`,
    );
  }

  /**
   * Broadcast message to all connected native WebSocket clients.
   */
  public broadcast(event: string, data: any): void {
    const message: WebSocketBroadcastMessage = {
      event,
      data,
      timestamp: new Date().toISOString(),
    };

    const payload = JSON.stringify(message);

    for (const client of this.clients) {
      if (client.readyState === WebSocket.OPEN) {
        try {
          client.send(payload);
        } catch (err: any) {
          this.logger.warn(
            `Failed to broadcast "${event}" to client: ${err.message}`,
          );
          this.clients.delete(client);
        }
      }
    }
  }

  /**
   * Get active connection count
   */
  public getClientCount(): number {
    return this.clients.size;
  }

  // ─────────────────────────────────────────────────────────────────────────────
  // EVENT LISTENERS (Sprint 3 Track J Specification)
  // ─────────────────────────────────────────────────────────────────────────────

  /**
   * 1. node_telemetry: { nodeId, health, lastSeenAt, ... }
   */
  @OnEvent('node.telemetry')
  handleNodeTelemetry(event: NodeTelemetryReceivedEvent): void {
    this.broadcast('node_telemetry', {
      nodeId: event.nodeId,
      health: event.telemetry.health ?? 'OK',
      discoveryStatus: event.telemetry.discovery_status,
      isStale: Boolean(event.telemetry.is_stale),
      lastSeenAt: event.timestamp,
      scheduleState: event.telemetry.schedule_state,
      overrideState: event.telemetry.override_state,
      sensorSerial: event.telemetry.sensor_serial,
      bootSessionId: event.telemetry.boot_session_id,
    });
  }

  @OnEvent('node.health_changed')
  handleNodeHealthChanged(event: NodeHealthChangedEvent): void {
    this.broadcast('node_telemetry', {
      nodeId: event.nodeId,
      health: event.newStatus,
      previousHealth: event.previousStatus,
      lastSeenAt: event.timestamp,
      reason: event.reason,
    });
  }

  @OnEvent('node.fault_reset')
  handleNodeFaultReset(event: NodeFaultResetEvent): void {
    this.broadcast('node_telemetry', {
      nodeId: event.nodeId,
      health: 'OK',
      lastSeenAt: event.timestamp,
      resetBy: event.resetBy,
    });
  }

  /**
   * 2. node_flow: { nodeId, litresTotal, flowRateLpm, isFault }
   */
  @OnEvent('flow.event_recorded')
  handleFlowEventRecorded(event: FlowEventRecordedEvent): void {
    // Track L: Only broadcast `node_flow` when the evidence pipeline has
    // actually reached FLOW_CONFIRMED. Non-confirmed flow samples/telemetry
    // are intentionally not pushed to subscribers to keep `node_flow` as an
    // authoritative confirmation signal for the dashboard RUNNING status.
    const flow = event.event;
    if (!flow.flow_confirmed) {
      return;
    }

    this.broadcast('node_flow', {
      nodeId: flow.node_id,
      litresTotal: flow.litres_total,
      flowRateLpm: parseFloat(flow.flow_rate_lpm) || 0,
      isFault: flow.is_fault,
      faultCode: flow.fault_code,
      flowConfirmed: flow.flow_confirmed,
      sampleWindowMs: flow.sample_window_ms,
      time: flow.time,
    });
  }

  /**
   * Track L: Authoritative FLOW_CONFIRMED broadcast from the evidence pipeline.
   *
   * This is emitted only when the pump command confirmation lifecycle reaches
   * the `FLOW_CONFIRMED` stage. Dashboard NodeCard consumes `node_flow` with
   * `flowConfirmed: true` to display the RUNNING status.
   */
  @OnEvent('pump.command.flow_confirmed')
  handleFlowConfirmed(event: PumpCommandFlowConfirmedEvent): void {
    this.broadcast('node_flow', {
      nodeId: event.nodeId,
      litresTotal: '0.000',
      flowRateLpm: parseFloat(event.flowRateLpm) || 0,
      isFault: false,
      faultCode: 'NONE',
      flowConfirmed: true,
      sampleWindowMs: 1000,
      time: event.confirmedAt,
    });
  }

  /**
   * 3. pump_command_update: { commandId, nodeId, outcome, ackedAt, flowConfirmedAt }
   */
  @OnEvent('pump.command.sent')
  handlePumpCommandSent(event: PumpCommandSentEvent): void {
    this.broadcast('pump_command_update', {
      commandId: event.commandId,
      nodeId: event.nodeId,
      outcome: 'PENDING',
      action: event.action,
      runLeaseMs: event.runLeaseMs,
      sentAt: event.sentAt,
      flowConfirmedAt: null,
    });
  }

  @OnEvent('pump.command.acked')
  handlePumpCommandAcked(event: PumpCommandAckedEvent): void {
    this.broadcast('pump_command_update', {
      commandId: event.commandId,
      nodeId: event.nodeId,
      outcome: event.outcome,
      ackedAt: event.ackedAt,
      latencyMs: event.latencyMs,
      flowConfirmedAt: null,
    });
  }

  @OnEvent('pump.command.flow_confirmed')
  handlePumpCommandFlowConfirmed(event: PumpCommandFlowConfirmedEvent): void {
    this.broadcast('pump_command_update', {
      commandId: event.commandId,
      nodeId: event.nodeId,
      outcome: 'FLOW_CONFIRMED',
      flowRateLpm: event.flowRateLpm,
      flowConfirmedAt: event.confirmedAt,
    });
  }

  @OnEvent('pump.command.fault')
  handlePumpCommandFault(event: PumpCommandFaultEvent): void {
    this.broadcast('pump_command_update', {
      commandId: event.commandId,
      nodeId: event.nodeId,
      outcome: event.outcome,
      faultReason: event.reason,
      faultAt: event.faultedAt,
      flowConfirmedAt: null,
    });
  }

  /**
   * 4. group_status: { groupId, treatmentVersionId, phase, nextTransitionAt }
   */
  @OnEvent('group.assigned')
  async handleGroupAssigned(event: GroupAssignedEvent): Promise<void> {
    try {
      const status = await this.groupService.getGroupStatus(event.groupId);
      this.broadcast('group_status', {
        groupId: event.groupId,
        treatmentVersionId:
          event.treatmentVersionId ??
          status.treatment?.treatment_version_id ??
          null,
        phase: status.current_phase,
        nextTransitionAt: status.next_transition_at,
        nodeIds: event.nodeIds,
      });
    } catch (err: any) {
      this.logger.error(
        `Failed to broadcast group_status for group #${event.groupId}: ${err.message}`,
      );
    }
  }

  @OnEvent('group.unassigned')
  handleGroupUnassigned(event: GroupUnassignedEvent): void {
    this.broadcast('group_status', {
      groupId: event.groupId,
      treatmentVersionId: null,
      phase: 'UNASSIGNED',
      nextTransitionAt: null,
      unassignedAt: event.unassignedAt,
    });
  }

  /**
   * 5. staleness_alert: { nodeId, lastSeenAt, staleForMs }
   */
  @OnEvent('staleness_alert')
  handleStalenessAlert(event: NodeStalenessAlertEvent): void {
    this.broadcast('staleness_alert', {
      nodeId: event.nodeId,
      lastSeenAt: event.lastSeenAt,
      staleForMs: event.staleForMs,
    });
  }

  /**
   * 6. device_status: { deviceId, status, uptime_s, rssi_dbm, free_heap_b, ntpSynced, rtcValid, lastSeenAt }
   */
  @OnEvent('device.status_changed')
  handleDeviceStatusChanged(event: any): void {
    this.broadcast('device_status', event);
  }
}
