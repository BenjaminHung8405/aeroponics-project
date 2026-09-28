import { Test, TestingModule } from '@nestjs/testing';
import { ConfigService } from '@nestjs/config';
import { getRepositoryToken } from '@nestjs/typeorm';
import { EventEmitter2 } from '@nestjs/event-emitter';

import { MqttRouterService } from './mqtt-router.service';
import { MqttService } from './mqtt.service';
import { MQTT_EVENTS } from './mqtt.constants';
import { DeviceStatus } from '../device/entities/device_status.entity';
import { NodeService } from '../node/node.service';
import { FlowService } from '../flow/flow.service';
import { PumpCommandService } from '../pump-command/pump-command.service';
import { CommandAcceptedEvent } from '../pump-command/events/pump-command.events';
import { NodeHealthStatus } from '../node/entities/node_registry.entity';
import { PumpCommandOutcome } from '../pump-command/entities/pump_command.entity';

describe('MqttRouterService & Sprint 3 MQTT Routing (S3-I1)', () => {
  let routerService: MqttRouterService;
  let mqttService: MqttService;
  let eventEmitter: EventEmitter2;

  let mockDeviceStatusRepo: any;
  let mockNodeService: any;
  let mockFlowService: any;
  let mockPumpCommandService: any;

  beforeEach(async () => {
    mockDeviceStatusRepo = {
      findOne: jest.fn(),
      create: jest.fn().mockImplementation((dto) => ({ ...dto })),
      save: jest.fn().mockImplementation(async (entity) => ({ ...entity })),
    };

    mockNodeService = {
      handleTelemetry: jest.fn().mockResolvedValue({}),
      handleSnapshot: jest.fn().mockResolvedValue({}),
      updateHealth: jest.fn().mockResolvedValue({}),
    };

    mockFlowService = {
      recordFlowEvent: jest.fn().mockResolvedValue({ id: 1, flow_rate_lpm: '3.50' }),
    };

    mockPumpCommandService = {
      handleRfAck: jest.fn().mockImplementation(async (cmdId, acked, meta) => ({
        command_id: cmdId,
        outcome: acked ? PumpCommandOutcome.RF_ACKED : PumpCommandOutcome.FAULT_NO_ACK,
        command_to_ack_latency_ms: meta?.latencyMs ?? 50,
      })),
      handleFault: jest.fn().mockResolvedValue({}),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        MqttRouterService,
        MqttService,
        EventEmitter2,
        {
          provide: getRepositoryToken(DeviceStatus),
          useValue: mockDeviceStatusRepo,
        },
        {
          provide: NodeService,
          useValue: mockNodeService,
        },
        {
          provide: FlowService,
          useValue: mockFlowService,
        },
        {
          provide: PumpCommandService,
          useValue: mockPumpCommandService,
        },
        {
          provide: ConfigService,
          useValue: {
            get: jest.fn((key: string, def?: any) => {
              if (key === 'NODE_ENV') return 'test';
              return def;
            }),
          },
        },
      ],
    }).compile();

    routerService = module.get<MqttRouterService>(MqttRouterService);
    mqttService = module.get<MqttService>(MqttService);
    eventEmitter = module.get<EventEmitter2>(EventEmitter2);
  });

  describe('Node ID Boundary Enforcement', () => {
    it('should DISCARD node_id=16 on the legacy AGU node namespace', () => {
      const emitSpy = jest.spyOn(eventEmitter, 'emit');
      const loggerWarnSpy = jest.spyOn((mqttService as any).logger, 'warn');

      const buffer = Buffer.from(JSON.stringify({ schedule_state: 'DAY_SPRAY' }));
      mqttService.handleMessage('aeroponics/node/16/telemetry', buffer);

      expect(loggerWarnSpy).toHaveBeenCalledWith(
        expect.stringContaining('unsupported AGU legacy node_id "16"'),
      );
      expect(emitSpy).not.toHaveBeenCalledWith(MQTT_EVENTS.NODE_TELEMETRY, expect.anything());
    });

    it('should DISCARD message with non-numeric node_id and log warning', () => {
      const emitSpy = jest.spyOn(eventEmitter, 'emit');
      const buffer = Buffer.from(JSON.stringify({ flow_rate_lpm: 2.5 }));
      mqttService.handleMessage('aeroponics/node/invalid_node/flow', buffer);

      expect(emitSpy).not.toHaveBeenCalledWith(
        MQTT_EVENTS.NODE_FLOW,
        expect.anything(),
      );
      expect(mockFlowService.recordFlowEvent).not.toHaveBeenCalled();
    });

    it('should DISCARD message with node_id=0 (below minimum 1)', () => {
      const emitSpy = jest.spyOn(eventEmitter, 'emit');
      const buffer = Buffer.from(JSON.stringify({ command_id: 'cmd-1', acked: true }));
      mqttService.handleMessage('aeroponics/node/0/ack', buffer);

      expect(emitSpy).not.toHaveBeenCalledWith(
        MQTT_EVENTS.NODE_ACK,
        expect.anything(),
      );
      expect(mockPumpCommandService.handleRfAck).not.toHaveBeenCalled();
    });

    it('should ACCEPT valid AGU legacy node_id=7 and dispatch event', () => {
      const emitSpy = jest.spyOn(eventEmitter, 'emit');
      const buffer = Buffer.from(JSON.stringify({ schedule_state: 'IDLE' }));
      mqttService.handleMessage('aeroponics/node/7/telemetry', buffer);

      expect(emitSpy).toHaveBeenCalledWith(
        MQTT_EVENTS.NODE_TELEMETRY,
        expect.objectContaining({
          nodeId: 7,
          payload: { schedule_state: 'IDLE' },
        }),
      );
    });

    it('should ACCEPT modern node_id=15 on the v1 namespace', () => {
      const emitSpy = jest.spyOn(eventEmitter, 'emit');
      const buffer = Buffer.from(JSON.stringify({ schedule_state: 'IDLE' }));
      mqttService.handleMessage('aeroponics/v1/node/15/telemetry', buffer);

      expect(emitSpy).toHaveBeenCalledWith(
        MQTT_EVENTS.NODE_TELEMETRY,
        expect.objectContaining({ nodeId: 15 }),
      );
    });
  });

  describe('aeroponics/node/+/ack Handler & DB Outcome Update', () => {
    it('should handle mock ACK and call PumpCommandService.handleRfAck to update outcome to RF_ACKED', async () => {
      const payload = {
        command_id: '123e4567-e89b-12d3-a456-426614174000',
        acked: true,
        latency_ms: 85,
        node_timestamp_ms: '10500',
        gateway_timestamp_ms: '10590',
      };

      const result = await routerService.handleNodeAck(4, payload);

      expect(mockPumpCommandService.handleRfAck).toHaveBeenCalledWith(
        '123e4567-e89b-12d3-a456-426614174000',
        true,
        {
          latencyMs: 85,
          nodeTimestampMs: '10500',
          gatewayTimestampMs: '10590',
        },
      );

      expect(result).toBeDefined();
      expect(result?.outcome).toBe(PumpCommandOutcome.RF_ACKED);
    });

    it('should handle ACK with status=ACCEPTED and acked=false', async () => {
      const payload = {
        command_id: '123e4567-e89b-12d3-a456-426614174001',
        status: 'ACCEPTED',
      };

      const result = await routerService.handleNodeAck(5, payload);

      // ACCEPTED is not RF_ACKED: per S5 only RF_ACKED sets acked=true
      expect(mockPumpCommandService.handleRfAck).toHaveBeenCalledWith(
        '123e4567-e89b-12d3-a456-426614174001',
        false,
        expect.anything(),
      );
      expect(result?.outcome).toBe(PumpCommandOutcome.FAULT_NO_ACK);
    });

    it('should handle NACK/timeout (acked: false) and update outcome to FAULT_NO_ACK', async () => {
      const payload = {
        command_id: '123e4567-e89b-12d3-a456-426614174002',
        acked: false,
      };

      const result = await routerService.handleNodeAck(6, payload);

      expect(mockPumpCommandService.handleRfAck).toHaveBeenCalledWith(
        '123e4567-e89b-12d3-a456-426614174002',
        false,
        expect.anything(),
      );
      expect(result?.outcome).toBe(PumpCommandOutcome.FAULT_NO_ACK);
    });

    it('should discard ACK payload missing command_id with a warning', async () => {
      const payload = { acked: true };
      const result = await routerService.handleNodeAck(4, payload);

      expect(result).toBeNull();
      expect(mockPumpCommandService.handleRfAck).not.toHaveBeenCalled();
    });

    it('should handle ACK with non-UUID command_id and call handleRfAck', async () => {
      const payload = { command_id: 'non-uuid-cmd-id', acked: true };
      const result = await routerService.handleNodeAck(4, payload);

      // command_id validation is relaxed: any non-empty string is accepted
      expect(result).toBeDefined();
      expect(mockPumpCommandService.handleRfAck).toHaveBeenCalledWith(
        'non-uuid-cmd-id',
        true,
        expect.anything(),
      );
      expect(result?.outcome).toBe(PumpCommandOutcome.RF_ACKED);
    });
  });

  describe('Admission ACK vs RF_ACKED Separation (S5)', () => {
    it('ACCEPTED emits COMMAND_ACCEPTED and is not treated as an RF ACK', async () => {
      const emitSpy = jest.spyOn(eventEmitter, 'emit');

      await routerService.handleCommandAckEvent({
        topic: 'aeroponics/v1/node/4/ack',
        nodeId: 4,
        commandId: 'rf-cmd-1',
        payload: {
          command_id: 'rf-cmd-1',
          node_id: 4,
          status: 'ACCEPTED',
        },
      });

      expect(emitSpy).toHaveBeenCalledWith(
        MQTT_EVENTS.COMMAND_ACCEPTED,
        expect.objectContaining({
          nodeId: 4,
          commandId: 'rf-cmd-1',
          status: 'ACCEPTED',
        }),
      );
      const acceptedEvent = emitSpy.mock.calls.find(
        ([eventName]) => eventName === MQTT_EVENTS.COMMAND_ACCEPTED,
      )?.[1];
      expect(acceptedEvent).toBeInstanceOf(CommandAcceptedEvent);
      // Lifecycle admission must not mark the command RF_ACKED / FAULT_NO_ACK
      expect(mockPumpCommandService.handleRfAck).not.toHaveBeenCalled();
    });

    it('RF_ACKED still persists the real RF acknowledgement', async () => {
      const emitSpy = jest.spyOn(eventEmitter, 'emit');

      await routerService.handleCommandAckEvent({
        topic: 'aeroponics/v1/node/4/ack',
        nodeId: 4,
        commandId: 'rf-cmd-1',
        payload: {
          command_id: 'rf-cmd-1',
          node_id: 4,
          status: 'RF_ACKED',
        },
      });

      expect(mockPumpCommandService.handleRfAck).toHaveBeenCalledWith(
        'rf-cmd-1',
        true,
        expect.anything(),
      );
      expect(emitSpy).not.toHaveBeenCalledWith(
        MQTT_EVENTS.COMMAND_ACCEPTED,
        expect.anything(),
      );
    });
  });

  describe('aeroponics/node/+/telemetry Handler', () => {
    it('should route telemetry to NodeService.handleTelemetry', async () => {
      const payload = {
        schedule_state: 'DAY_SPRAY',
        override_state: 'NONE',
        boot_session_id: 42,
        sensor_serial: 'YF-S201-01',
      };

      await routerService.handleNodeTelemetry(1, payload);

      expect(mockNodeService.handleTelemetry).toHaveBeenCalledWith(1, payload);
    });
  });

  describe('aeroponics/node/+/flow Handler', () => {
    it('should format DTO and route to FlowService.recordFlowEvent', async () => {
      const payload = {
        flow_rate_lpm: 3.45,
        litres_total: '0.450',
        pulse_count: '210',
        delivered_volume_ml: 450,
        flow_confirmed: true,
      };

      await routerService.handleNodeFlow(5, payload);

      expect(mockFlowService.recordFlowEvent).toHaveBeenCalledWith(
        expect.objectContaining({
          node_id: 5,
          flow_rate_lpm: 3.45,
          litres_total: '0.450',
          delivered_volume_ml: 450,
          flow_confirmed: true,
        }),
      );
    });
  });

  describe('aeroponics/node/+/fault Handler', () => {
    it('should update node health to FAULT and handle command fault if command_id present', async () => {
      const payload = {
        fault_code: 'OVER_CURRENT',
        reason: 'Pump motor stall detected',
        command_id: '123e4567-e89b-12d3-a456-426614174003',
      };

      await routerService.handleNodeFault(3, payload);

      expect(mockNodeService.updateHealth).toHaveBeenCalledWith(
        3,
        NodeHealthStatus.FAULT,
        { reason: 'Pump motor stall detected' },
      );
      expect(mockPumpCommandService.handleFault).toHaveBeenCalledWith(
        '123e4567-e89b-12d3-a456-426614174003',
        'Pump motor stall detected',
      );
    });

    it('should update node health to FAULT but ignore non-UUID command_id', async () => {
      const payload = {
        fault_code: 'OVER_CURRENT',
        reason: 'Pump motor stall detected',
        command_id: 'non-uuid-fault-cmd',
      };

      await routerService.handleNodeFault(3, payload);

      expect(mockNodeService.updateHealth).toHaveBeenCalledWith(
        3,
        NodeHealthStatus.FAULT,
        { reason: 'Pump motor stall detected' },
      );
      expect(mockPumpCommandService.handleFault).not.toHaveBeenCalled();
    });
  });

  describe('aeroponics/gateway/+/heartbeat Handler', () => {
    it('should create or update DeviceStatus for gateway', async () => {
      const payload = {
        status: 'online',
        uptime_s: 3600,
        rssi_dbm: -58,
        free_heap_b: 185000,
        ntp_synced: true,
        rtc_valid: true,
      };

      const result = await routerService.handleGatewayHeartbeat('esp32_gw_01', payload);

      expect(mockDeviceStatusRepo.save).toHaveBeenCalledWith(
        expect.objectContaining({
          device_id: 'esp32_gw_01',
          status: 'online',
          uptime_s: '3600',
          rssi_dbm: -58,
          free_heap_b: 185000,
          ntp_synced: true,
          rtc_valid: true,
        }),
      );
      expect(result.status).toBe('online');
    });
  });

  describe('Hard Rule S3-MQTT-05: Resilient onMessage', () => {
    it('should not crash when receiving non-JSON binary buffer', () => {
      const emitSpy = jest.spyOn(eventEmitter, 'emit');
      const garbageBuffer = Buffer.from([0xff, 0xfe, 0x00, 0x12]);

      expect(() => {
        mqttService.handleMessage('aeroponics/node/7/telemetry', garbageBuffer);
      }).not.toThrow();

      expect(emitSpy).toHaveBeenCalledWith(
        MQTT_EVENTS.PARSE_ERROR,
        expect.objectContaining({
          topic: 'aeroponics/node/7/telemetry',
        }),
      );
    });
  });

  describe('Legacy AGU Node (4–7) Snapshot Pipeline', () => {
    it.each([4, 5, 6, 7])(
      'should route NODE_SNAPSHOT and call handleSnapshot for legacy AGU node #%i',
      async (nodeId) => {
        const payload = {
          health_status: 'ONLINE',
          ping_rtt_ms: 42,
          boot_session_id: 7,
        };

        await routerService.handleNodeSnapshotEvent({
          nodeId,
          payload,
          deviceId: 'esp32_gw_01',
          receivedAt: new Date(),
        });

        expect(mockNodeService.handleSnapshot).toHaveBeenCalledWith(
          nodeId,
          payload,
          'esp32_gw_01',
          expect.any(Date),
        );
      },
    );

    it('should call handleSnapshot when NODE_SNAPSHOT event is emitted via MQTT for device-scoped topic', () => {
      const payload = {
        health_status: 'ONLINE',
        ping_rtt_ms: 38,
        last_ping_ok: true,
      };
      const emitSpy = jest.spyOn(eventEmitter, 'emit');
      const buffer = Buffer.from(JSON.stringify(payload));

      mqttService.handleMessage(
        'aeroponics/device/esp32_gw_01/telemetry/node/5/snapshot',
        buffer,
      );

      expect(emitSpy).toHaveBeenCalledWith(
        MQTT_EVENTS.NODE_SNAPSHOT,
        expect.objectContaining({
          nodeId: 5,
          deviceId: 'esp32_gw_01',
          payload,
        }),
      );
    });

    it('should call handleSnapshot for the short telemetry/node snapshot topic', () => {
      const payload = { health_status: 'OK', ping_rtt_ms: 55 };
      const emitSpy = jest.spyOn(eventEmitter, 'emit');
      const buffer = Buffer.from(JSON.stringify(payload));

      mqttService.handleMessage('aeroponics/telemetry/node/6/snapshot', buffer);

      expect(emitSpy).toHaveBeenCalledWith(
        MQTT_EVENTS.NODE_SNAPSHOT,
        expect.objectContaining({ nodeId: 6, payload }),
      );
    });

    it('should route snapshot for node_id=8 (within expanded legacy range 1..15)', () => {
      const emitSpy = jest.spyOn(eventEmitter, 'emit');
      const payload = { health_status: 'ONLINE' };
      const buffer = Buffer.from(JSON.stringify(payload));

      mqttService.handleMessage(
        'aeroponics/device/gw/telemetry/node/8/snapshot',
        buffer,
      );

      expect(emitSpy).toHaveBeenCalledWith(
        MQTT_EVENTS.NODE_SNAPSHOT,
        expect.objectContaining({ nodeId: 8, payload }),
      );
    });

    it('should silently drop snapshot for node_id outside range 1..15 (e.g. node 0 or 16)', () => {
      const emitSpy = jest.spyOn(eventEmitter, 'emit');
      const buffer = Buffer.from(JSON.stringify({ health_status: 'ONLINE' }));

      mqttService.handleMessage(
        'aeroponics/device/gw/telemetry/node/16/snapshot',
        buffer,
      );

      expect(emitSpy).not.toHaveBeenCalledWith(
        MQTT_EVENTS.NODE_SNAPSHOT,
        expect.anything(),
      );
    });

    it('should correlate last_command_id from snapshot and call handleRfAck', async () => {
      const cmdId = '123e4567-e89b-12d3-a456-426614174099';
      const payload = {
        health_status: 'ONLINE',
        ping_rtt_ms: 30,
        last_command_id: cmdId,
        last_command_result: 'RF_ACKED',
      };

      await routerService.handleNodeSnapshotEvent({
        nodeId: 7,
        payload,
        deviceId: 'esp32_gw_01',
        receivedAt: new Date(),
      });

      expect(mockNodeService.handleSnapshot).toHaveBeenCalledWith(
        7,
        payload,
        'esp32_gw_01',
        expect.any(Date),
      );
      expect(mockPumpCommandService.handleRfAck).toHaveBeenCalledWith(
        cmdId,
        true,
        expect.anything(),
      );
    });
  });
});
