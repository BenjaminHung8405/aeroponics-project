import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';
import { DataSource, Repository } from 'typeorm';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { BadRequestException, NotFoundException } from '@nestjs/common';
import { FlowService } from './flow.service';
import { FlowEvent, FlowFaultCode } from './entities/flow_event.entity';
import {
  SensorCalibration,
  CalibrationStatusEnum,
} from '../node/entities/sensor_calibration.entity';
import {
  NodeRegistry,
  CalibrationStatus,
  NodeHealthStatus,
  ScheduleState,
  OverrideState,
} from '../node/entities/node_registry.entity';
import { Season } from '../season/entities/season.entity';
import { UpdateCalibrationDto } from './dto/update-calibration.dto';

describe('FlowService (S3-G1)', () => {
  let service: FlowService;
  let flowRepo: jest.Mocked<Repository<FlowEvent>>;
  let calibrationRepo: jest.Mocked<Repository<SensorCalibration>>;
  let nodeRepo: jest.Mocked<Repository<NodeRegistry>>;
  let seasonRepo: jest.Mocked<Repository<Season>>;
  let dataSource: jest.Mocked<DataSource>;
  let eventEmitter: jest.Mocked<EventEmitter2>;

  const mockNodeRegistry: NodeRegistry = {
    node_id: 1,
    display_name: 'Station 01',
    sensor_serial: 'SEN-N1-FLOW',
    active_sensor_calibration_id: 10,
    calibration_status: CalibrationStatus.CALIBRATED,
    schedule_state: ScheduleState.IDLE,
    override_state: OverrideState.NONE,
    last_boot_session_id: 1,
    last_seen_at: new Date(),
    health_status: NodeHealthStatus.OK,
    cached_group_id: 1,
    created_at: new Date(),
    updated_at: new Date(),
    active_sensor_calibration: null as any,
  };

  const mockActiveCalibration: SensorCalibration = {
    id: 10,
    node_id: 1,
    sensor_serial: 'SEN-N1-FLOW',
    version_num: 1,
    pulses_per_litre: '450.5000',
    reference_volume_ml: 1000,
    trial_count: 3,
    mean_pulses: '450.50',
    variance: '0.0000',
    repeatability_pct: '1.00',
    operating_conditions: null,
    status: CalibrationStatusEnum.ACTIVE,
    calibrated_by: 'Engineer A',
    calibrated_at: new Date(),
  };

  beforeEach(async () => {
    const mockFlowRepo = {
      find: jest.fn(),
      create: jest.fn((dto) => ({ ...dto, time: new Date() })),
      save: jest.fn((entity) => Promise.resolve(entity)),
    };

    const mockCalibrationRepo = {
      findOne: jest.fn(),
      find: jest.fn(),
      create: jest.fn((dto) => ({ id: 11, ...dto })),
      save: jest.fn((entity) => Promise.resolve(entity)),
    };

    const mockNodeRepo = {
      findOne: jest.fn(),
    };

    const mockSeasonRepo = {
      findOne: jest.fn(),
    };

    const mockQueryRunner = {
      connect: jest.fn(),
      startTransaction: jest.fn(),
      commitTransaction: jest.fn(),
      rollbackTransaction: jest.fn(),
      release: jest.fn(),
      manager: {
        findOne: jest.fn(),
        create: jest.fn((_, dto) => ({ id: 11, ...dto })),
        save: jest.fn((entity) => Promise.resolve(entity)),
        createQueryBuilder: jest.fn(() => ({
          update: jest.fn().mockReturnThis(),
          set: jest.fn().mockReturnThis(),
          where: jest.fn().mockReturnThis(),
          execute: jest.fn().mockResolvedValue({ affected: 1 }),
        })),
      },
    };

    const mockDataSource = {
      transaction: jest.fn(async (cb) => cb(mockQueryRunner.manager)),
      createQueryRunner: jest.fn(() => mockQueryRunner),
    };

    const mockEventEmitter = {
      emit: jest.fn(),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        FlowService,
        { provide: getRepositoryToken(FlowEvent), useValue: mockFlowRepo },
        {
          provide: getRepositoryToken(SensorCalibration),
          useValue: mockCalibrationRepo,
        },
        { provide: getRepositoryToken(NodeRegistry), useValue: mockNodeRepo },
        { provide: getRepositoryToken(Season), useValue: mockSeasonRepo },
        { provide: DataSource, useValue: mockDataSource },
        { provide: EventEmitter2, useValue: mockEventEmitter },
      ],
    }).compile();

    service = module.get<FlowService>(FlowService);
    flowRepo = module.get(getRepositoryToken(FlowEvent));
    calibrationRepo = module.get(getRepositoryToken(SensorCalibration));
    nodeRepo = module.get(getRepositoryToken(NodeRegistry));
    seasonRepo = module.get(getRepositoryToken(Season));
    dataSource = module.get(DataSource);
    eventEmitter = module.get(EventEmitter2);
  });

  describe('validateNodeId', () => {
    it('should REJECT node IDs outside 1..4 with BadRequestException', async () => {
      await expect(service.getHistory(0)).rejects.toThrow(BadRequestException);
      await expect(service.getHistory(5)).rejects.toThrow(BadRequestException);
      await expect(service.getHistory(-1)).rejects.toThrow(BadRequestException);
    });
  });

  describe('getHistory (S3-G1)', () => {
    it('should default to hours = 24 and limit = 500 when not provided', async () => {
      flowRepo.find.mockResolvedValue([]);
      const result = await service.getHistory(1);
      expect(result.hours).toBe(24);
      expect(result.node_id).toBe(1);
      expect(flowRepo.find).toHaveBeenCalledWith(
        expect.objectContaining({
          take: 500,
        }),
      );
    });

    it('should accept maximum hours = 720 (30 days)', async () => {
      flowRepo.find.mockResolvedValue([]);
      const result = await service.getHistory(2, 720);
      expect(result.hours).toBe(720);
      expect(result.node_id).toBe(2);
    });

    it('CRITICAL: should REJECT hours = 721 with BadRequestException (400)', async () => {
      await expect(service.getHistory(1, 721)).rejects.toThrow(
        BadRequestException,
      );
    });

    it('should REJECT hours <= 0 with BadRequestException', async () => {
      await expect(service.getHistory(1, 0)).rejects.toThrow(
        BadRequestException,
      );
      await expect(service.getHistory(1, -10)).rejects.toThrow(
        BadRequestException,
      );
    });

    it('should calculate accurate summary metrics over queried events', async () => {
      const mockEvents = [
        {
          time: new Date(),
          season_id: 1,
          node_id: 1,
          delivered_volume_ml: 250,
          flow_rate_lpm: '2.40',
          flow_confirmed: true,
          is_fault: false,
        } as FlowEvent,
        {
          time: new Date(),
          season_id: 1,
          node_id: 1,
          delivered_volume_ml: 150,
          flow_rate_lpm: '1.80',
          flow_confirmed: true,
          is_fault: false,
        } as FlowEvent,
        {
          time: new Date(),
          season_id: 1,
          node_id: 1,
          delivered_volume_ml: 0,
          flow_rate_lpm: '0.00',
          flow_confirmed: false,
          is_fault: true,
        } as FlowEvent,
      ];
      flowRepo.find.mockResolvedValue(mockEvents);

      const result = await service.getHistory(1, 24);
      expect(result.summary.total_events).toBe(3);
      expect(result.summary.total_delivered_volume_ml).toBe(400);
      expect(result.summary.total_litres).toBe(0.4);
      expect(result.summary.average_flow_rate_lpm).toBe(2.1); // (2.40 + 1.80) / 2
      expect(result.summary.max_flow_rate_lpm).toBe(2.4);
      expect(result.summary.confirmed_events).toBe(2);
      expect(result.summary.fault_events).toBe(1);
      expect(result.summary.flow_confirmation_rate_pct).toBe(66.67);
    });
  });

  describe('getCalibration (S3-G1)', () => {
    it('should return active calibration and historical versions in descending order', async () => {
      calibrationRepo.findOne.mockResolvedValue(mockActiveCalibration);
      calibrationRepo.find.mockResolvedValue([
        mockActiveCalibration,
        { ...mockActiveCalibration, id: 9, version_num: 0, status: CalibrationStatusEnum.SUPERSEDED },
      ]);

      const result = await service.getCalibration(1);
      expect(result.node_id).toBe(1);
      expect(result.active_calibration).toEqual(mockActiveCalibration);
      expect(result.total_versions).toBe(2);
      expect(result.history.length).toBe(2);
    });

    it('should return null active_calibration if uncalibrated', async () => {
      calibrationRepo.findOne.mockResolvedValue(null);
      calibrationRepo.find.mockResolvedValue([]);

      const result = await service.getCalibration(2);
      expect(result.node_id).toBe(2);
      expect(result.active_calibration).toBeNull();
      expect(result.total_versions).toBe(0);
    });
  });

  describe('updateCalibration (S3-G1)', () => {
    it('should REJECT pulses <= 0 or >= 10000 with BadRequestException', async () => {
      const dtoZero: UpdateCalibrationDto = {
        pulses_per_litre: 0,
        getEffectivePulsesPerLitre: () => 0,
      };
      await expect(service.updateCalibration(1, dtoZero)).rejects.toThrow(
        BadRequestException,
      );

      const dtoOver: UpdateCalibrationDto = {
        pulses_per_litre: 10000,
        getEffectivePulsesPerLitre: () => 10000,
      };
      await expect(service.updateCalibration(1, dtoOver)).rejects.toThrow(
        BadRequestException,
      );
    });

    it('should throw NotFoundException if node is not found in registry', async () => {
      nodeRepo.findOne.mockResolvedValue(null);
      const dto: UpdateCalibrationDto = {
        pulses_per_litre: 450,
        getEffectivePulsesPerLitre: () => 450,
      };
      await expect(service.updateCalibration(1, dto)).rejects.toThrow(
        NotFoundException,
      );
    });

    it('INVARIANT: should execute in transaction, increment version_num and supersede previous active', async () => {
      nodeRepo.findOne.mockResolvedValue(mockNodeRegistry);

      const qr = dataSource.createQueryRunner();
      qr.manager.findOne = jest
        .fn()
        .mockResolvedValueOnce(mockActiveCalibration) // previousActive
        .mockResolvedValueOnce(mockActiveCalibration); // maxVersionRecord

      const dto: UpdateCalibrationDto = {
        pulses_per_litre: 480.25,
        calibrated_by: 'Senior Architect',
        reference_volume_ml: 1000,
        getEffectivePulsesPerLitre: () => 480.25,
      };

      const result = await service.updateCalibration(1, dto, 'Senior Architect');

      expect(dataSource.transaction).toHaveBeenCalled();
      expect(qr.manager.create).toHaveBeenCalledWith(
        SensorCalibration,
        expect.objectContaining({
          node_id: 1,
          version_num: 2, // 1 + 1
          pulses_per_litre: '480.2500',
          calibrated_by: 'Senior Architect',
          status: CalibrationStatusEnum.ACTIVE,
        }),
      );
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'flow.calibration_updated',
        expect.objectContaining({
          nodeId: 1,
          calibratedBy: 'Senior Architect',
          previousCalibrationId: 10,
        }),
      );
      expect(result).toBeDefined();
    });
  });

  describe('recordFlowEvent (Safety Invariant)', () => {
    it('HARD RULE S2-FLOW-04: should flag OVER_RANGE_FAULT when flow_rate_lpm > 6.0', async () => {
      seasonRepo.findOne.mockResolvedValue({ id: 1 } as Season);
      calibrationRepo.findOne.mockResolvedValue(mockActiveCalibration);

      const result = await service.recordFlowEvent({
        node_id: 1,
        flow_rate_lpm: 7.5,
        delivered_volume_ml: 500,
      });

      expect(result.is_fault).toBe(true);
      expect(result.fault_code).toBe(FlowFaultCode.OVER_RANGE_FAULT);
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'flow.over_range',
        expect.objectContaining({
          nodeId: 1,
          flowRateLpm: 7.5,
          faultCode: FlowFaultCode.OVER_RANGE_FAULT,
        }),
      );
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'flow.event_recorded',
        expect.any(Object),
      );
    });

    it('should record normal flow event when flow_rate_lpm <= 6.0', async () => {
      seasonRepo.findOne.mockResolvedValue({ id: 1 } as Season);
      calibrationRepo.findOne.mockResolvedValue(mockActiveCalibration);

      const result = await service.recordFlowEvent({
        node_id: 1,
        flow_rate_lpm: 2.2,
        delivered_volume_ml: 220,
        flow_confirmed: true,
      });

      expect(result.is_fault).toBe(false);
      expect(result.fault_code).toBe(FlowFaultCode.NONE);
      expect(result.flow_confirmed).toBe(true);
    });
  });
});
