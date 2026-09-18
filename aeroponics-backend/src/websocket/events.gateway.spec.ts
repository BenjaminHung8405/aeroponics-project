import { Test, TestingModule } from '@nestjs/testing';
import { WebSocket } from 'ws';

import { EventsGateway } from './events.gateway';
import { NodeService } from '../node/node.service';
import { GroupService } from '../group/group.service';
import { DeviceService } from '../device/device.service';
import {
  NodeTelemetryReceivedEvent,
  NodeStalenessAlertEvent,
  NodeHealthChangedEvent,
  NodeFaultResetEvent,
} from '../node/events/node.events';
import { NodeHealthStatus } from '../node/entities/node_registry.entity';
import { FlowEventRecordedEvent } from '../flow/events/flow.events';
import {
  PumpCommandSentEvent,
  PumpCommandAckedEvent,
  PumpCommandFlowConfirmedEvent,
  PumpCommandFaultEvent,
} from '../pump-command/events/pump-command.events';
import {
  PumpAction,
  PumpCommandOutcome,
} from '../pump-command/entities/pump_command.entity';
import {
  GroupAssignedEvent,
  GroupUnassignedEvent,
} from '../group/events/group.events';

describe('EventsGateway & Sprint 3 WebSocket Events (S3-I2)', () => {
  let gateway: EventsGateway;
  let mockNodeService: any;
  let mockGroupService: any;

  beforeEach(async () => {
    mockNodeService = {
      checkStaleness: jest.fn().mockResolvedValue([]),
    };

    mockGroupService = {
      getGroupStatus: jest.fn().mockResolvedValue({
        group_id: 1,
        treatment: {
          treatment_version_id: 2,
        },
        current_phase: 'DAY',
        next_transition_at: new Date('2026-09-13T18:00:00.000Z'),
      }),
    };

    const mockDeviceService = {
      checkDeviceStaleness: jest.fn().mockResolvedValue(undefined),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        EventsGateway,
        {
          provide: NodeService,
          useValue: mockNodeService,
        },
        {
          provide: GroupService,
          useValue: mockGroupService,
        },
        {
          provide: DeviceService,
          useValue: mockDeviceService,
        },
      ],
    }).compile();

    gateway = module.get<EventsGateway>(EventsGateway);
  });

  afterEach(() => {
    gateway.onModuleDestroy();
  });

  function createMockWebSocket(readyState: number = WebSocket.OPEN) {
    const listeners: Record<string, ((...args: any[]) => void)[]> = {};
    return {
      readyState,
      send: jest.fn(),
      close: jest.fn(),
      on: jest.fn((event: string, callback: (...args: any[]) => void) => {
        if (!listeners[event]) listeners[event] = [];
        listeners[event].push(callback);
      }),
      emit: (event: string, ...args: any[]) => {
        if (listeners[event]) {
          listeners[event].forEach((cb) => cb(...args));
        }
      },
    } as unknown as WebSocket & { emit: (e: string, ...a: any[]) => void; send: jest.Mock };
  }

  describe('Connection & Lifecycle Management', () => {
    it('should register connected native WebSocket client and send welcome frame', () => {
      const client = createMockWebSocket();
      gateway.handleConnection(client);

      expect(gateway.getClientCount()).toBe(1);
      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"connected"'),
      );
    });

    it('should unregister client upon disconnect', () => {
      const client = createMockWebSocket();
      gateway.handleConnection(client);
      expect(gateway.getClientCount()).toBe(1);

      gateway.handleDisconnect(client);
      expect(gateway.getClientCount()).toBe(0);
    });

    it('should handle client close and error events gracefully', () => {
      const client = createMockWebSocket();
      gateway.handleConnection(client);
      expect(gateway.getClientCount()).toBe(1);

      // Trigger close event
      (client as any).emit('close');
      expect(gateway.getClientCount()).toBe(0);
    });

    it('should broadcast message to all open connected clients', () => {
      const client1 = createMockWebSocket(WebSocket.OPEN);
      const client2 = createMockWebSocket(WebSocket.OPEN);
      const closedClient = createMockWebSocket(WebSocket.CLOSED);

      gateway.handleConnection(client1);
      gateway.handleConnection(client2);
      gateway.handleConnection(closedClient);

      gateway.broadcast('test_event', { value: 42 });

      expect(client1.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"test_event"'),
      );
      expect(client2.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"test_event"'),
      );
      // Closed client should not receive send
      expect(closedClient.send).toHaveBeenCalledTimes(0);
    });
  });

  describe('Track J Real-time Broadcast Events', () => {
    let client: any;

    beforeEach(() => {
      client = createMockWebSocket(WebSocket.OPEN);
      gateway.handleConnection(client);
      jest.clearAllMocks();
    });

    it('should broadcast "node_telemetry" event on node.telemetry', () => {
      const now = new Date();
      gateway.handleNodeTelemetry(
        new NodeTelemetryReceivedEvent(
          1,
          {
            schedule_state: 'DAY_SPRAY',
            override_state: 'NONE',
            sensor_serial: 'SN-1001',
            boot_session_id: 5,
          },
          now,
        ),
      );

      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"node_telemetry"'),
      );
      const sentPayload = JSON.parse(client.send.mock.calls[0][0]);
      expect(sentPayload.data).toEqual(
        expect.objectContaining({
          nodeId: 1,
          health: 'OK',
          scheduleState: 'DAY_SPRAY',
          overrideState: 'NONE',
          sensorSerial: 'SN-1001',
          bootSessionId: 5,
        }),
      );
    });

    it('should broadcast "node_telemetry" with health update on node.health_changed', () => {
      const now = new Date();
      gateway.handleNodeHealthChanged(
        new NodeHealthChangedEvent(
          2,
          NodeHealthStatus.OK,
          NodeHealthStatus.FAULT,
          'Over current',
          now,
        ),
      );

      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"node_telemetry"'),
      );
      const sentPayload = JSON.parse(client.send.mock.calls[0][0]);
      expect(sentPayload.data).toEqual(
        expect.objectContaining({
          nodeId: 2,
          health: 'FAULT',
          previousHealth: 'OK',
          reason: 'Over current',
        }),
      );
    });

    it('should broadcast "node_telemetry" on node.fault_reset', () => {
      const now = new Date();
      gateway.handleNodeFaultReset(new NodeFaultResetEvent(2, 'operator', now));

      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"node_telemetry"'),
      );
      const sentPayload = JSON.parse(client.send.mock.calls[0][0]);
      expect(sentPayload.data).toEqual(
        expect.objectContaining({
          nodeId: 2,
          health: 'OK',
          resetBy: 'operator',
        }),
      );
    });

    it('should broadcast "node_flow" event on flow.event_recorded', () => {
      const now = new Date();
      const mockFlowEvent: any = {
        node_id: 3,
        litres_total: '2.500',
        flow_rate_lpm: '4.20',
        is_fault: false,
        fault_code: 'NONE',
        flow_confirmed: true,
        sample_window_ms: 1000,
        time: now,
      };

      gateway.handleFlowEventRecorded(
        new FlowEventRecordedEvent(mockFlowEvent, now),
      );

      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"node_flow"'),
      );
      const sentPayload = JSON.parse(client.send.mock.calls[0][0]);
      expect(sentPayload.data).toEqual(
        expect.objectContaining({
          nodeId: 3,
          litresTotal: '2.500',
          flowRateLpm: 4.2,
          isFault: false,
          flowConfirmed: true,
        }),
      );
    });

    it('should broadcast "pump_command_update" on pump.command.sent', () => {
      const now = new Date();
      gateway.handlePumpCommandSent(
        new PumpCommandSentEvent(
          'cmd-100',
          1,
          1,
          PumpAction.ON,
          101,
          30000,
          now,
        ),
      );

      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"pump_command_update"'),
      );
      const sentPayload = JSON.parse(client.send.mock.calls[0][0]);
      expect(sentPayload.data).toEqual(
        expect.objectContaining({
          commandId: 'cmd-100',
          nodeId: 1,
          outcome: 'PENDING',
          action: 'ON',
          runLeaseMs: 30000,
        }),
      );
    });

    it('should broadcast "pump_command_update" on pump.command.acked', () => {
      const now = new Date();
      gateway.handlePumpCommandAcked(
        new PumpCommandAckedEvent(
          'cmd-100',
          1,
          PumpCommandOutcome.RF_ACKED,
          now,
          75,
        ),
      );

      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"pump_command_update"'),
      );
      const sentPayload = JSON.parse(client.send.mock.calls[0][0]);
      expect(sentPayload.data).toEqual(
        expect.objectContaining({
          commandId: 'cmd-100',
          nodeId: 1,
          outcome: 'RF_ACKED',
          latencyMs: 75,
        }),
      );
    });

    it('should broadcast "pump_command_update" on pump.command.flow_confirmed', () => {
      const now = new Date();
      gateway.handlePumpCommandFlowConfirmed(
        new PumpCommandFlowConfirmedEvent('cmd-100', 1, '3.50', now),
      );

      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"pump_command_update"'),
      );
      const sentPayload = JSON.parse(client.send.mock.calls[0][0]);
      expect(sentPayload.data).toEqual(
        expect.objectContaining({
          commandId: 'cmd-100',
          nodeId: 1,
          outcome: 'FLOW_CONFIRMED',
          flowRateLpm: '3.50',
        }),
      );
    });

    it('should broadcast "pump_command_update" on pump.command.fault', () => {
      const now = new Date();
      gateway.handlePumpCommandFault(
        new PumpCommandFaultEvent(
          'cmd-err-1',
          3,
          PumpCommandOutcome.FAULT_NO_FLOW,
          'Zero pulses observed',
          now,
        ),
      );

      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"pump_command_update"'),
      );
      const sentPayload = JSON.parse(client.send.mock.calls[0][0]);
      expect(sentPayload.data).toEqual(
        expect.objectContaining({
          commandId: 'cmd-err-1',
          nodeId: 3,
          outcome: PumpCommandOutcome.FAULT_NO_FLOW,
          faultReason: 'Zero pulses observed',
        }),
      );
    });

    it('should broadcast "group_status" on group.assigned and group.unassigned', async () => {
      const now = new Date();
      await gateway.handleGroupAssigned(
        new GroupAssignedEvent(1, 1, 2, [1, 2], now),
      );

      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"group_status"'),
      );
      let sentPayload = JSON.parse(client.send.mock.calls[0][0]);
      expect(sentPayload.data).toEqual(
        expect.objectContaining({
          groupId: 1,
          treatmentVersionId: 2,
          phase: 'DAY',
          nodeIds: [1, 2],
        }),
      );

      jest.clearAllMocks();
      gateway.handleGroupUnassigned(new GroupUnassignedEvent(1, 1, now));

      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"group_status"'),
      );
      sentPayload = JSON.parse(client.send.mock.calls[0][0]);
      expect(sentPayload.data).toEqual(
        expect.objectContaining({
          groupId: 1,
          treatmentVersionId: null,
          phase: 'UNASSIGNED',
        }),
      );
    });
  });

  describe('Staleness Alert & Monitoring (S3-I2 Requirement 2)', () => {
    it('should broadcast "staleness_alert" when NodeService emits staleness alert (last_seen > STALE_THRESHOLD)', () => {
      const client = createMockWebSocket(WebSocket.OPEN);
      gateway.handleConnection(client);
      jest.clearAllMocks();

      const lastSeen = new Date(Date.now() - 135000);
      const detectedAt = new Date();

      // NodeService detects node #1 silent for 135s (threshold = 120s) and emits staleness_alert
      const alertEvent = new NodeStalenessAlertEvent(
        1,
        lastSeen,
        135000,
        detectedAt,
      );

      gateway.handleStalenessAlert(alertEvent);

      expect(client.send).toHaveBeenCalledWith(
        expect.stringContaining('"event":"staleness_alert"'),
      );

      const sentPayload = JSON.parse(client.send.mock.calls[0][0]);
      expect(sentPayload.data).toEqual({
        nodeId: 1,
        lastSeenAt: lastSeen.toISOString(),
        staleForMs: 135000,
      });
    });
  });
});
