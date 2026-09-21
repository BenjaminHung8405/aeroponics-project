import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';
import { ConfigService } from '@nestjs/config';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { DataSource } from 'typeorm';
import { BadRequestException } from '@nestjs/common';

import { PumpCommandService } from './pump-command.service';
import {
  PumpCommand,
  PumpAction,
  CommandSource,
  PumpCommandOutcome,
} from './entities/pump_command.entity';
import { PumpFeedbackEvent } from './entities/pump_feedback_event.entity';
import { PumpStateEvent } from './entities/pump_state_event.entity';
import { FlowEvent } from '../flow/entities/flow_event.entity';
import { SensorCalibration } from '../node/entities/sensor_calibration.entity';
import { SeasonService } from '../season/season.service';
import { MqttService } from '../mqtt/mqtt.service';

describe('PumpCommandService (S3-F1, S3-F2, S3-F3)', () => {
  let service: PumpCommandService;
  let commandRepo: any;
  let feedbackRepo: any;
  let flowRepo: any;
  let calibrationRepo: any;
  let seasonService: any;
  let mqttService: any;
  let configService: any;
  let eventEmitter: any;

  const mockActiveSeason = {
    id: 1,
    name: 'Season 1',
    status: 'ACTIVE',
  };

  beforeEach(async () => {
    commandRepo = {
      create: jest.fn().mockImplementation((dto) => ({ ...dto })),
      save: jest.fn().mockImplementation(async (entity) => ({ ...entity })),
      findOne: jest.fn(),
      find: jest.fn().mockResolvedValue([]),
    };

    feedbackRepo = {
      create: jest.fn().mockImplementation((dto) => ({ ...dto })),
      save: jest.fn().mockImplementation(async (entity) => ({ ...entity })),
    };

    flowRepo = {
      create: jest.fn().mockImplementation((dto) => ({ ...dto })),
      save: jest.fn().mockImplementation(async (entity) => ({ ...entity })),
    };

    calibrationRepo = {
      findOne: jest.fn().mockResolvedValue({ id: 10, node_id: 4, status: 'ACTIVE' }),
    };

    seasonService = {
      getActive: jest.fn().mockResolvedValue(mockActiveSeason),
    };

    mqttService = {
      publish: jest.fn().mockResolvedValue(undefined),
      isConnected: jest.fn().mockReturnValue(true),
    };

    configService = {
      get: jest.fn().mockImplementation((key, defaultVal) => {
        if (key === 'MQTT_ANTIREPLAY_WINDOW_MS') return 60000;
        return defaultVal;
      }),
    };

    eventEmitter = {
      emit: jest.fn(),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        PumpCommandService,
        {
          provide: getRepositoryToken(PumpCommand),
          useValue: commandRepo,
        },
        {
          provide: getRepositoryToken(PumpFeedbackEvent),
          useValue: feedbackRepo,
        },
        {
          provide: getRepositoryToken(PumpStateEvent),
          useValue: {},
        },
        {
          provide: getRepositoryToken(FlowEvent),
          useValue: flowRepo,
        },
        {
          provide: getRepositoryToken(SensorCalibration),
          useValue: calibrationRepo,
        },
        {
          provide: SeasonService,
          useValue: seasonService,
        },
        {
          provide: MqttService,
          useValue: mqttService,
        },
        {
          provide: ConfigService,
          useValue: configService,
        },
        {
          provide: EventEmitter2,
          useValue: eventEmitter,
        },
        {
          provide: DataSource,
          useValue: {},
        },
      ],
    }).compile();

    service = module.get<PumpCommandService>(PumpCommandService);
  });

  describe('S3-F1: sendCommand', () => {
    it('should generate UUID v4 command_id, monotonic rf_seq per node, publish to correct topic and save row with outcome PENDING', async () => {
      const cmd1 = await service.sendCommand(4, 1, PumpAction.ON, 10, {
        runLeaseMs: 30000,
      });

      expect(cmd1.command_id).toBeDefined();
      // Verify UUID v4 format: 8-4-4-4-12 hex chars
      expect(cmd1.command_id).toMatch(
        /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i,
      );
      expect(cmd1.node_id).toBe(4);
      expect(cmd1.outcome).toBe(PumpCommandOutcome.PENDING);
      expect(cmd1.run_lease_ms).toBe(30000);
      expect(cmd1.rf_seq).toBeDefined();

      // Verify MQTT publish
      expect(mqttService.publish).toHaveBeenCalledWith(
        'aeroponics/device/esp32_device/command/node/4/override',
        expect.objectContaining({
          command_id: cmd1.command_id,
          version: 1,
          desired_state: PumpAction.ON,
          source: CommandSource.MANUAL_OVERRIDE,
          run_lease_ms: 30000,
          rf_seq: cmd1.rf_seq,
          node_id: 4,
        }),
      );

      // Verify second command on the same node has strictly monotonic rf_seq
      const cmd2 = await service.sendCommand(4, 1, PumpAction.ON, 10);
      expect(cmd2.rf_seq).toBe(cmd1.rf_seq + 1);
      expect(cmd2.rf_seq).not.toBe(cmd1.rf_seq);
      expect(cmd2.command_id).not.toBe(cmd1.command_id);

      // Verify event emission
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'pump.command.sent',
        expect.objectContaining({
          commandId: cmd1.command_id,
          nodeId: 4,
        }),
      );
    });

    it('should throw BadRequestException if no active season exists', async () => {
      seasonService.getActive.mockResolvedValueOnce(null);

      await expect(
        service.sendCommand(4, 1, PumpAction.ON, 10),
      ).rejects.toThrow(BadRequestException);
    });

    it('should throw BadRequestException if node_id is outside physical IDs 4..7', async () => {
      await expect(
        service.sendCommand(8, 1, PumpAction.ON, 10),
      ).rejects.toThrow(BadRequestException);

      await expect(
        service.sendCommand(0, 1, PumpAction.ON, 10),
      ).rejects.toThrow(BadRequestException);
    });
  });

  describe('S3-F2: State Machine Lifecycle & Deadman Timer', () => {
    it('should handle handleRfAck with acked=true transitioning outcome to RF_ACKED', async () => {
      const existingCmd = {
        command_id: 'cmd-uuid-1',
        node_id: 4,
        outcome: PumpCommandOutcome.PENDING,
        time: new Date(Date.now() - 150),
      };
      commandRepo.findOne.mockResolvedValueOnce(existingCmd);

      const updated = await service.handleRfAck('cmd-uuid-1', true, {
        latencyMs: 150,
      });

      expect(updated.outcome).toBe(PumpCommandOutcome.RF_ACKED);
      expect(updated.acked_at).toBeDefined();
      expect(updated.command_to_ack_latency_ms).toBe(150);
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'pump.command.acked',
        expect.objectContaining({ outcome: PumpCommandOutcome.RF_ACKED }),
      );
    });

    it('should handle handleRfAck with acked=false transitioning outcome to FAULT_NO_ACK', async () => {
      const existingCmd = {
        command_id: 'cmd-uuid-2',
        node_id: 5,
        outcome: PumpCommandOutcome.PENDING,
      };
      commandRepo.findOne.mockResolvedValueOnce(existingCmd);

      const updated = await service.handleRfAck('cmd-uuid-2', false);

      expect(updated.outcome).toBe(PumpCommandOutcome.FAULT_NO_ACK);
      expect(updated.fault_reason).toBeDefined();
    });

    it('should record pump feedback on handlePumpFeedback', async () => {
      const existingCmd = {
        command_id: 'cmd-uuid-3',
        node_id: 4,
        season_id: 1,
        group_id: 1,
        action: PumpAction.ON,
        rf_seq: 101,
        outcome: PumpCommandOutcome.RF_ACKED,
      };
      commandRepo.findOne.mockResolvedValueOnce(existingCmd);

      const updated = await service.handlePumpFeedback('cmd-uuid-3', {
        driverFeedback: 'ON',
        loadFeedback: 'ON',
        currentMa: 850,
      });

      expect(updated.feedback_at).toBeDefined();
      expect(feedbackRepo.create).toHaveBeenCalledWith(
        expect.objectContaining({
          command_id: 'cmd-uuid-3',
          driver_feedback: 'ON',
          driver_feedback_mismatch: false,
          current_ma: 850,
        }),
      );
      expect(feedbackRepo.save).toHaveBeenCalled();
    });

    it('INVARIANT: handleFlowConfirmed MUST reject if command is NOT in RF_ACKED outcome (e.g. still PENDING)', async () => {
      const pendingCmd = {
        command_id: 'cmd-uuid-pending',
        node_id: 4,
        outcome: PumpCommandOutcome.PENDING,
      };
      commandRepo.findOne.mockResolvedValueOnce(pendingCmd);

      await expect(
        service.handleFlowConfirmed('cmd-uuid-pending', {
          flowRateLpm: 2.5,
        }),
      ).rejects.toThrow(BadRequestException);

      // Verify flow event was NOT created
      expect(flowRepo.save).not.toHaveBeenCalled();
    });

    it('should successfully update to FLOW_CONFIRMED and save flow_event after RF_ACKED', async () => {
      const ackedCmd = {
        command_id: 'cmd-uuid-acked',
        node_id: 4,
        season_id: 1,
        group_id: 1,
        outcome: PumpCommandOutcome.RF_ACKED,
        acked_at: new Date(Date.now() - 500),
        time: new Date(Date.now() - 650),
        rf_seq: 105,
      };
      commandRepo.findOne.mockResolvedValueOnce(ackedCmd);

      const updated = await service.handleFlowConfirmed('cmd-uuid-acked', {
        flowRateLpm: 2.35,
        deliveredVolumeMl: 450,
        litresTotal: '12.450',
      });

      expect(updated.outcome).toBe(PumpCommandOutcome.FLOW_CONFIRMED);
      expect(updated.flow_confirmed_at).toBeDefined();
      expect(flowRepo.create).toHaveBeenCalledWith(
        expect.objectContaining({
          command_id: 'cmd-uuid-acked',
          flow_rate_lpm: '2.35',
          flow_confirmed: true,
          delivered_volume_ml: 450,
        }),
      );
      expect(flowRepo.save).toHaveBeenCalled();
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'pump.command.flow_confirmed',
        expect.anything(),
      );
    });

    it('Deadman / onModuleDestroy: all PENDING commands MUST receive FAULT_BACKEND_DISCONNECT outcome', async () => {
      const pendingCmd1 = {
        command_id: 'cmd-p1',
        outcome: PumpCommandOutcome.PENDING,
      };
      const pendingCmd2 = {
        command_id: 'cmd-p2',
        outcome: PumpCommandOutcome.PENDING,
      };

      commandRepo.find.mockResolvedValueOnce([pendingCmd1, pendingCmd2]);

      await service.onModuleDestroy();

      expect(pendingCmd1.outcome).toBe(
        PumpCommandOutcome.FAULT_BACKEND_DISCONNECT,
      );
      expect(pendingCmd2.outcome).toBe(
        PumpCommandOutcome.FAULT_BACKEND_DISCONNECT,
      );
      expect(commandRepo.save).toHaveBeenCalledTimes(2);
    });
  });

  describe('S3-F3: Anti-Replay Engine', () => {
    it('should reject duplicate rf_seq within configurable window for the same node', () => {
      const nodeId = 1;
      const rfSeq = 42;

      // First attempt: Accepted
      const accepted = service.checkAndRecordSequence(nodeId, rfSeq);
      expect(accepted).toBe(true);

      // Second attempt with identical rf_seq on same node within window: Rejected
      const rejected = service.checkAndRecordSequence(nodeId, rfSeq);
      expect(rejected).toBe(false);

      // Different rf_seq on same node: Accepted
      const acceptedNext = service.checkAndRecordSequence(nodeId, rfSeq + 1);
      expect(acceptedNext).toBe(true);

      // Same rf_seq but on different node: Accepted
      const acceptedOtherNode = service.checkAndRecordSequence(2, rfSeq);
      expect(acceptedOtherNode).toBe(true);
    });

    it('should support custom configurable window via MQTT_ANTIREPLAY_WINDOW_MS', () => {
      configService.get.mockImplementation((key: string) => {
        if (key === 'MQTT_ANTIREPLAY_WINDOW_MS') return 10000;
        return 60000;
      });

      const accepted = service.checkAndRecordSequence(3, 999);
      expect(accepted).toBe(true);
      expect(configService.get).toHaveBeenCalledWith(
        'MQTT_ANTIREPLAY_WINDOW_MS',
        60000,
      );
    });
  });

  describe('S3-F4: getNodeCommands pagination', () => {
    it('should paginate commands with limit and offset', async () => {
      commandRepo.find.mockResolvedValueOnce([
        { command_id: 'c1', node_id: 4 },
        { command_id: 'c2', node_id: 4 },
      ]);

      const result = await service.getNodeCommands(4, 10, 5);

      expect(result).toHaveLength(2);
      expect(commandRepo.find).toHaveBeenCalledWith({
        where: { node_id: 4 },
        order: { time: 'DESC' },
        take: 10,
        skip: 5,
      });
    });

    it('should throw BadRequestException if nodeId is invalid in getNodeCommands', async () => {
      await expect(service.getNodeCommands(0)).rejects.toThrow(
        BadRequestException,
      );
      await expect(service.getNodeCommands(99)).rejects.toThrow(
        BadRequestException,
      );
    });
  });
});
