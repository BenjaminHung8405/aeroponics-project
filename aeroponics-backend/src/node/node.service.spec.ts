import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';
import { DataSource, Repository } from 'typeorm';
import { ConfigService } from '@nestjs/config';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { BadRequestException } from '@nestjs/common';

import { NodeService } from './node.service';
import {
  NodeRegistry,
  NodeHealthStatus,
  CalibrationStatus,
  ScheduleState,
  OverrideState,
} from './entities/node_registry.entity';
import { SensorCalibration } from './entities/sensor_calibration.entity';

describe('NodeService (S3-E2)', () => {
  let service: NodeService;
  let nodeRepo: jest.Mocked<Repository<NodeRegistry>>;
  let eventEmitter: jest.Mocked<EventEmitter2>;
  let dataSource: jest.Mocked<DataSource>;

  beforeEach(async () => {
    const mockRepo = () => ({
      find: jest.fn(),
      findOne: jest.fn(),
      create: jest.fn((val) => val),
      save: jest.fn((val) => Promise.resolve(val)),
      update: jest.fn(),
      createQueryBuilder: jest.fn(() => ({
        update: jest.fn().mockReturnThis(),
        set: jest.fn().mockReturnThis(),
        where: jest.fn().mockReturnThis(),
        execute: jest.fn().mockResolvedValue({ affected: 1 }),
      })),
    });

    const mockConfigService = {
      get: jest.fn((key: string, defaultVal: any) => {
        if (key === 'STALE_THRESHOLD_MS') return 120000;
        return defaultVal;
      }),
    };

    const mockEventEmitter = {
      emit: jest.fn(),
    };

    const mockDataSource = {
      transaction: jest.fn((cb) => {
        const managerMock = {
          findOne: jest.fn(),
          create: jest.fn((entity, val) => ({ id: 99, ...val })),
          save: jest.fn((val) => Promise.resolve({ id: 99, ...val })),
          createQueryBuilder: jest.fn(() => ({
            update: jest.fn().mockReturnThis(),
            set: jest.fn().mockReturnThis(),
            where: jest.fn().mockReturnThis(),
            execute: jest.fn().mockResolvedValue({ affected: 1 }),
          })),
        };
        return cb(managerMock);
      }),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        NodeService,
        { provide: getRepositoryToken(NodeRegistry), useValue: mockRepo() },
        {
          provide: getRepositoryToken(SensorCalibration),
          useValue: mockRepo(),
        },
        { provide: ConfigService, useValue: mockConfigService },
        { provide: DataSource, useValue: mockDataSource },
        { provide: EventEmitter2, useValue: mockEventEmitter },
      ],
    }).compile();

    service = module.get<NodeService>(NodeService);
    nodeRepo = module.get(getRepositoryToken(NodeRegistry));
    eventEmitter = module.get(EventEmitter2);
    dataSource = module.get(DataSource);
  });

  describe('Fault Latching Invariant (updateHealth & resetFault)', () => {
    const mockFaultNode: NodeRegistry = {
      node_id: 4,
      display_name: 'Node 01',
      cached_group_id: 1,
      sensor_serial: 'SEN-01',
      active_sensor_calibration_id: null,
      calibration_status: CalibrationStatus.UNCALIBRATED,
      schedule_state: ScheduleState.IDLE,
      override_state: OverrideState.NONE,
      last_boot_session_id: 1,
      last_seen_at: new Date(),
      health_status: NodeHealthStatus.FAULT,
      created_at: new Date(),
      updated_at: new Date(),
      active_sensor_calibration: null,
    };

    it('should reject transition from FAULT to OK without explicit reset', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...mockFaultNode });

      await expect(
        service.updateHealth(4, NodeHealthStatus.OK),
      ).rejects.toThrow(BadRequestException);

      expect(nodeRepo.save).not.toHaveBeenCalled();
    });

    it('should allow transition from FAULT to OK when explicitReset is true', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...mockFaultNode });

      const updated = await service.updateHealth(4, NodeHealthStatus.OK, {
        explicitReset: true,
        reason: 'Operator cleared physical fault',
      });

      expect(updated.health_status).toBe(NodeHealthStatus.OK);
      expect(nodeRepo.save).toHaveBeenCalled();
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'node.health_changed',
        expect.objectContaining({
          nodeId: 4,
          previousStatus: NodeHealthStatus.FAULT,
          newStatus: NodeHealthStatus.OK,
        }),
      );
    });

    it('should reset fault via explicit resetFault method', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...mockFaultNode });

      const updated = await service.resetFault(4, 'Lead Engineer');

      expect(updated.health_status).toBe(NodeHealthStatus.OK);
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'node.fault_reset',
        expect.objectContaining({
          nodeId: 4,
          resetBy: 'Lead Engineer',
        }),
      );
    });
  });

  describe('Staleness Detection & Telemetry (handleTelemetry)', () => {
    it('should emit staleness_alert when node reconnects after exceeding STALE_THRESHOLD_MS', async () => {
      const now = Date.now();
      // Node was last seen 150 seconds ago (threshold is 120s)
      const staleLastSeen = new Date(now - 150000);

      const existingNode: NodeRegistry = {
        node_id: 5,
        display_name: 'Node 02',
        cached_group_id: 1,
        sensor_serial: null,
        active_sensor_calibration_id: null,
        calibration_status: CalibrationStatus.UNCALIBRATED,
        schedule_state: ScheduleState.SPRAYING,
        override_state: OverrideState.NONE,
        last_boot_session_id: 1,
        last_seen_at: staleLastSeen,
        health_status: NodeHealthStatus.STALE,
        created_at: new Date(),
        updated_at: new Date(),
        active_sensor_calibration: null,
      };

      nodeRepo.findOne.mockResolvedValue(existingNode);

      await service.handleTelemetry(5, {
        schedule_state: ScheduleState.IDLE,
        boot_session_id: 2,
      });

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'staleness_alert',
        expect.objectContaining({
          nodeId: 5,
          lastSeenAt: staleLastSeen,
        }),
      );
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'node.telemetry',
        expect.objectContaining({
          nodeId: 5,
        }),
      );
    });

    it('should identify stale nodes and emit alert in checkStaleness with mock time advance', async () => {
      const now = Date.now();
      const staleNode: NodeRegistry = {
        node_id: 6,
        display_name: 'Node 03',
        cached_group_id: 2,
        sensor_serial: null,
        active_sensor_calibration_id: null,
        calibration_status: CalibrationStatus.UNCALIBRATED,
        schedule_state: ScheduleState.IDLE,
        override_state: OverrideState.NONE,
        last_boot_session_id: 1,
        last_seen_at: new Date(now - 130000), // 130s ago (> 120s)
        health_status: NodeHealthStatus.OK,
        created_at: new Date(),
        updated_at: new Date(),
        active_sensor_calibration: null,
      };

      nodeRepo.find.mockResolvedValue([staleNode]);

      const staleResult = await service.checkStaleness();

      expect(staleResult.length).toBe(1);
      expect(staleResult[0].health_status).toBe(NodeHealthStatus.STALE);
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'staleness_alert',
        expect.objectContaining({
          nodeId: 6,
        }),
      );
    });
  });

  describe('updateCalibration', () => {
    const mockNode: NodeRegistry = {
      node_id: 4,
      display_name: 'Node 01',
      cached_group_id: 1,
      sensor_serial: 'SEN-01',
      active_sensor_calibration_id: 10,
      calibration_status: CalibrationStatus.CALIBRATED,
      schedule_state: ScheduleState.IDLE,
      override_state: OverrideState.NONE,
      last_boot_session_id: 1,
      last_seen_at: new Date(),
      health_status: NodeHealthStatus.OK,
      created_at: new Date(),
      updated_at: new Date(),
      active_sensor_calibration: null,
    };

    it('should reject invalid pulses_per_litre (<= 0 or >= 10000)', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...mockNode });

      const dtoZero = {
        calibration_pulses_per_litre: 0,
        getEffectivePulsesPerLitre: () => 0,
      };
      await expect(
        service.updateCalibration(4, dtoZero as any),
      ).rejects.toThrow(BadRequestException);

      const dto10000 = {
        calibration_pulses_per_litre: 10000,
        getEffectivePulsesPerLitre: () => 10000,
      };
      await expect(
        service.updateCalibration(4, dto10000 as any),
      ).rejects.toThrow(BadRequestException);
    });

    it('should successfully update calibration, create new version and emit event', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...mockNode });

      const dto = {
        calibration_pulses_per_litre: 450.5,
        sensor_serial: 'SEN-01-V2',
        calibrated_by: 'Engineer T',
        getEffectivePulsesPerLitre: () => 450.5,
      };

      const result = await service.updateCalibration(4, dto as any);

      expect(dataSource.transaction).toHaveBeenCalled();
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'node.calibration_updated',
        expect.objectContaining({
          nodeId: 4,
          pulsesPerLitre: '450.5000',
        }),
      );
      expect(result.node_id).toBe(4);
    });
  });

  describe('Node ID validation boundaries', () => {
    it('should reject nodeId outside physical IDs 4..7', async () => {
      await expect(service.getNodeStatus(0)).rejects.toThrow(
        BadRequestException,
      );
      await expect(service.getNodeStatus(8)).rejects.toThrow(
        BadRequestException,
      );
    });
  });
});
