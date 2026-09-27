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
      rf_protocol: null,
      last_scan_id: null,
      last_rf_rtt_ms: null,
      last_discovered_at: null,
      discovery_status: null,
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
        rf_protocol: null,
        last_scan_id: null,
        last_rf_rtt_ms: null,
        last_discovered_at: null,
        discovery_status: null,
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
        rf_protocol: null,
        last_scan_id: null,
        last_rf_rtt_ms: null,
        last_discovered_at: null,
        discovery_status: null,
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
      rf_protocol: null,
      last_scan_id: null,
      last_rf_rtt_ms: null,
      last_discovered_at: null,
      discovery_status: null,
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
    it('should reject nodeId outside modern IDs 1..15', async () => {
      await expect(service.getNodeStatus(0)).rejects.toThrow(
        BadRequestException,
      );
      await expect(service.getNodeStatus(16)).rejects.toThrow(
        BadRequestException,
      );
    });
  });

  describe('handleSnapshot — discovery_status normalization (AGU legacy fix)', () => {
    const baseNode: NodeRegistry = {
      node_id: 4,
      display_name: 'Node 04',
      cached_group_id: null,
      sensor_serial: null,
      active_sensor_calibration_id: null,
      calibration_status: CalibrationStatus.UNCALIBRATED,
      schedule_state: ScheduleState.IDLE,
      override_state: OverrideState.NONE,
      last_boot_session_id: null,
      last_seen_at: null,
      health_status: NodeHealthStatus.OK,
      rf_protocol: null,
      last_scan_id: null,
      last_rf_rtt_ms: null,
      last_discovered_at: null,
      discovery_status: null,
      created_at: new Date(),
      updated_at: new Date(),
      active_sensor_calibration: null,
    };

    it('should set discovery_status to ONLINE when firmware sends health_status=ONLINE', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...baseNode });

      const result = await service.handleSnapshot(4, { health_status: 'ONLINE' });

      expect(result.discovery_status).toBe('ONLINE');
    });

    it('should map firmware health_status=OK → discovery_status=ONLINE (AGU legacy normalization)', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...baseNode });

      const result = await service.handleSnapshot(4, { health_status: 'OK' });

      expect(result.discovery_status).toBe('ONLINE');
    });

    it('should map firmware health_status=STALE → discovery_status=STALE', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...baseNode });

      const result = await service.handleSnapshot(5, { health_status: 'STALE' });

      expect(result.discovery_status).toBe('STALE');
    });

    it('should map firmware health_status=OFFLINE → discovery_status=OFFLINE (pre-ping state)', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...baseNode });

      const result = await service.handleSnapshot(4, { health_status: 'OFFLINE' });

      expect(result.discovery_status).toBe('OFFLINE');
    });

    it('should map firmware health_status=FAULT → discovery_status=FAULT', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...baseNode });

      const result = await service.handleSnapshot(6, { health_status: 'FAULT' });

      expect(result.discovery_status).toBe('FAULT');
    });

    it('should default to ONLINE when health_status is absent', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...baseNode });

      const result = await service.handleSnapshot(6, {});

      expect(result.discovery_status).toBe('ONLINE');
    });

    it('should persist ping_rtt_ms from snapshot payload', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...baseNode });

      const result = await service.handleSnapshot(7, {
        health_status: 'ONLINE',
        ping_rtt_ms: 42,
      });

      expect(result.last_rf_rtt_ms).toBe(42);
    });

    it('should update last_seen_at from receivedAt argument', async () => {
      const receivedAt = new Date('2026-09-27T10:00:00Z');
      nodeRepo.findOne.mockResolvedValue({ ...baseNode });

      const result = await service.handleSnapshot(4, { health_status: 'ONLINE' }, 'gw-01', receivedAt);

      expect(result.last_seen_at).toEqual(receivedAt);
    });

    it('should auto-register node if not found in registry, then process snapshot', async () => {
      // First findOne returns null (node unknown), register path creates it
      nodeRepo.findOne
        .mockResolvedValueOnce(null)          // handleSnapshot findOne → not found
        .mockResolvedValueOnce(null);         // register → findOne → not found
      nodeRepo.create.mockImplementation((val) => ({ ...baseNode, ...val }) as NodeRegistry);

      const result = await service.handleSnapshot(4, { health_status: 'ONLINE' });

      expect(nodeRepo.save).toHaveBeenCalled();
      expect(result.discovery_status).toBe('ONLINE');
    });

    it('should emit node.telemetry event after processing snapshot', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...baseNode });

      await service.handleSnapshot(4, { health_status: 'ONLINE' });

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'node.telemetry',
        expect.objectContaining({ nodeId: 4 }),
      );
    });

    it('should set rf_protocol=AGU_LEGACY_SCI for legacy node IDs', async () => {
      nodeRepo.findOne.mockResolvedValue({ ...baseNode });

      const result = await service.handleSnapshot(4, { health_status: 'ONLINE' });

      expect(result.rf_protocol).toBe('AGU_LEGACY_SCI');
    });
  });
});
