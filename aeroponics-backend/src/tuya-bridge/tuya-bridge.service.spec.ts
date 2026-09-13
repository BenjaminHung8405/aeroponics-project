import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';
import { ConfigService } from '@nestjs/config';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { HttpException, HttpStatus, BadGatewayException, RequestTimeoutException } from '@nestjs/common';
import { TuyaBridgeService } from './tuya-bridge.service';
import {
  MeasurementReading,
  MeasurementTriggerType,
} from './entities/measurement_reading.entity';
import {
  TuyaMeasurementSession,
  TuyaSessionStatus,
  TuyaSessionTriggerType,
} from './entities/tuya_measurement_session.entity';
import { SeasonService } from '../season/season.service';
import { ITuyaDevice, TUYA_CLIENT_FACTORY } from './tuya-client.interface';
import * as fs from 'fs';
import * as path from 'path';

describe('TuyaBridgeService (S3-H1)', () => {
  let service: TuyaBridgeService;
  let sessionRepo: any;
  let readingRepo: any;
  let seasonService: any;
  let eventEmitter: any;
  let configService: any;
  let mockDevice: jest.Mocked<ITuyaDevice>;

  const mockConfig: Record<string, any> = {
    TUYA_DEVICE_IP: '192.168.1.150',
    TUYA_DEVICE_ID: 'dev_tuya_phw218_test',
    TUYA_LOCAL_KEY: 'secret_key_16chr',
    TUYA_SENSOR_ID: 'ph-w218-01',
    TUYA_ON_DEMAND_TIMEOUT_MS: 5000,
    TUYA_COOLDOWN_WINDOW_MS: 60000,
  };

  beforeEach(async () => {
    mockDevice = {
      connect: jest.fn().mockResolvedValue(true),
      disconnect: jest.fn(),
      isConnected: jest.fn().mockReturnValue(true),
      get: jest.fn().mockResolvedValue({
        dps: {
          '101': 685, // pH 6.85
          '102': 1850, // EC 1850
          '103': 925, // TDS 925
          '104': 245, // Temp 24.5
          '105': 1200, // Salinity
          '106': 380, // ORP
          '107': 125, // Turbidity
          '108': 95, // Battery
        },
      }),
    };

    sessionRepo = {
      create: jest.fn().mockImplementation((data) => ({
        session_id: '123e4567-e89b-12d3-a456-426614174000',
        ...data,
      })),
      save: jest.fn().mockImplementation(async (data) => data),
    };

    readingRepo = {
      create: jest.fn().mockImplementation((data) => ({
        time: new Date(),
        ...data,
      })),
      save: jest.fn().mockImplementation(async (data) => data),
      findOne: jest.fn(),
      createQueryBuilder: jest.fn(),
    };

    seasonService = {
      getActive: jest.fn().mockResolvedValue({ id: 1, name: 'Season 2026' }),
    };

    eventEmitter = {
      emit: jest.fn(),
    };

    configService = {
      get: jest.fn().mockImplementation((key: string) => mockConfig[key]),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        TuyaBridgeService,
        {
          provide: getRepositoryToken(TuyaMeasurementSession),
          useValue: sessionRepo,
        },
        {
          provide: getRepositoryToken(MeasurementReading),
          useValue: readingRepo,
        },
        {
          provide: ConfigService,
          useValue: configService,
        },
        {
          provide: SeasonService,
          useValue: seasonService,
        },
        {
          provide: EventEmitter2,
          useValue: eventEmitter,
        },
        {
          provide: TUYA_CLIENT_FACTORY,
          useValue: () => mockDevice,
        },
      ],
    }).compile();

    service = module.get<TuyaBridgeService>(TuyaBridgeService);
    service.resetStateForTesting();
  });

  describe('HARD RULE S3-TUYA-ON-DEMAND-04: Zero Polling Loop Inspection', () => {
    it('should NOT contain setInterval, setInterval polling or recursive timeouts in tuya-bridge source', () => {
      const serviceFilePath = path.join(__dirname, 'tuya-bridge.service.ts');
      const fileContent = fs.readFileSync(serviceFilePath, 'utf8');

      expect(fileContent).not.toMatch(/setInterval/);
      expect(fileContent).not.toMatch(/startPolling/);
    });
  });

  describe('measureOnDemand', () => {
    it('should execute on-demand measurement successfully, save reading and complete session', async () => {
      const response = await service.measureOnDemand(
        'operator_admin',
        MeasurementTriggerType.ON_DEMAND,
      );

      // Verify Session creation
      expect(sessionRepo.create).toHaveBeenCalledWith(
        expect.objectContaining({
          sensor_id: 'ph-w218-01',
          trigger_type: TuyaSessionTriggerType.ON_DEMAND,
          season_id: 1,
          status: TuyaSessionStatus.PENDING,
          triggered_by_user_id: 'operator_admin',
        }),
      );

      // Verify Device interactions
      expect(mockDevice.connect).toHaveBeenCalledTimes(1);
      expect(mockDevice.get).toHaveBeenCalledWith({ schema: true });
      expect(mockDevice.disconnect).toHaveBeenCalledTimes(1);

      // Verify Reading creation & persistence
      expect(readingRepo.create).toHaveBeenCalledWith(
        expect.objectContaining({
          session_id: '123e4567-e89b-12d3-a456-426614174000',
          sensor_id: 'ph-w218-01',
          trigger_type: MeasurementTriggerType.ON_DEMAND,
          ph_value: '6.85',
          ec_value: 1850,
          tds_value: 925,
          temperature_c: '24.5',
          salinity_ppm: 1200,
          orp_mv: 380,
          turbidity_ntu: '12.50',
          battery_pct: 95,
          triggered_by_user_id: 'operator_admin',
        }),
      );
      expect(readingRepo.save).toHaveBeenCalledTimes(1);

      // Verify Session updated to COMPLETED
      expect(sessionRepo.save).toHaveBeenCalledWith(
        expect.objectContaining({
          status: TuyaSessionStatus.COMPLETED,
          completed_at: expect.any(Date),
        }),
      );

      // Verify Event emitted
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'measurement.recorded',
        expect.any(Object),
      );

      // Verify Response format
      expect(response.ph_value).toBe('6.85');
      expect(response.temperature_c).toBe('24.5');
      expect(response.ec_value).toBe(1850);
    });

    it('should reject duplicate trigger within 60s cooldown window with HTTP 429', async () => {
      // First call succeeds
      await service.measureOnDemand('user_1');

      // Immediate second call should be rejected with 429
      try {
        await service.measureOnDemand('user_1');
        fail('Should have thrown HttpException');
      } catch (err: any) {
        expect(err).toBeInstanceOf(HttpException);
        expect(err.getStatus()).toBe(HttpStatus.TOO_MANY_REQUESTS);
        expect(err.message).toContain('Measurement rate limit exceeded');
      }
    });

    it('should reject concurrent measurement attempts with HTTP 429', async () => {
      // Simulate an ongoing long measurement
      mockDevice.get.mockImplementation(
        () => new Promise((resolve) => setTimeout(() => resolve({ dps: {} }), 100)),
      );

      const call1 = service.measureOnDemand('user_1');
      try {
        await service.measureOnDemand('user_2');
        fail('Should have thrown HttpException');
      } catch (err: any) {
        expect(err).toBeInstanceOf(HttpException);
        expect(err.getStatus()).toBe(HttpStatus.TOO_MANY_REQUESTS);
        expect(err.message).toContain('already in progress');
      }

      await call1;
    });

    it('HARD RULE S3-TUYA-07: should mark session FAILED and disconnect device when Tuya throws', async () => {
      mockDevice.get.mockRejectedValue(new Error('Connection refused'));

      await expect(service.measureOnDemand('user_1')).rejects.toThrow(
        BadGatewayException,
      );

      // Verify Session marked FAILED
      expect(sessionRepo.save).toHaveBeenCalledWith(
        expect.objectContaining({
          status: TuyaSessionStatus.FAILED,
          error_message: expect.stringContaining('Connection refused'),
        }),
      );

      // Verify device was disconnected
      expect(mockDevice.disconnect).toHaveBeenCalledTimes(1);

      // Verify failure event emitted
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'measurement.failed',
        expect.any(Object),
      );
    });

    it('should throw RequestTimeoutException when Tuya device times out', async () => {
      mockDevice.get.mockRejectedValue(new Error('Tuya operation timed out'));

      await expect(service.measureOnDemand('user_1')).rejects.toThrow(
        RequestTimeoutException,
      );
    });

    it('HARD RULE S3-TUYA-01: should redact local key if present in error message', async () => {
      mockDevice.get.mockRejectedValue(
        new Error('Failed handshake with secret_key_16chr on device'),
      );

      await expect(service.measureOnDemand('user_1')).rejects.toThrow(
        BadGatewayException,
      );

      expect(sessionRepo.save).toHaveBeenCalledWith(
        expect.objectContaining({
          status: TuyaSessionStatus.FAILED,
          error_message: 'Failed handshake with [REDACTED_KEY] on device',
        }),
      );
    });

    it('should throw BadGatewayException if deviceId or localKey is missing in config', async () => {
      configService.get.mockImplementation((key: string) => {
        if (key === 'TUYA_LOCAL_KEY') return undefined;
        return mockConfig[key];
      });

      await expect(service.measureOnDemand('user_1')).rejects.toThrow(
        BadGatewayException,
      );
      expect(mockDevice.connect).not.toHaveBeenCalled();
    });
  });

  describe('getLatest', () => {
    it('should return latest reading when available', async () => {
      const mockReading = {
        time: new Date(),
        session_id: 'uuid-1',
        sensor_id: 'ph-w218-01',
        trigger_type: MeasurementTriggerType.ON_DEMAND,
        ph_value: '6.50',
        ec_value: 1800,
        tds_value: 900,
        temperature_c: '25.0',
        salinity_ppm: 1100,
        orp_mv: 350,
        turbidity_ntu: '1.20',
        battery_pct: 90,
        calibrated_at: null,
        triggered_by_user_id: 'admin',
      };

      readingRepo.findOne.mockResolvedValue(mockReading);

      const result = await service.getLatest();
      expect(result).not.toBeNull();
      expect(result?.ph_value).toBe('6.50');
      expect(result?.ec_value).toBe(1800);
      expect(readingRepo.findOne).toHaveBeenCalledWith({
        where: { sensor_id: 'ph-w218-01' },
        order: { time: 'DESC' },
      });
    });

    it('should return null if no readings exist', async () => {
      readingRepo.findOne.mockResolvedValue(null);

      const result = await service.getLatest();
      expect(result).toBeNull();
    });
  });

  describe('getHistory', () => {
    it('should query history with pagination and trigger_type filter', async () => {
      const qb: any = {
        where: jest.fn().mockReturnThis(),
        andWhere: jest.fn().mockReturnThis(),
        orderBy: jest.fn().mockReturnThis(),
        skip: jest.fn().mockReturnThis(),
        take: jest.fn().mockReturnThis(),
        getManyAndCount: jest.fn().mockResolvedValue([
          [
            {
              time: new Date(),
              session_id: 'uuid-1',
              sensor_id: 'ph-w218-01',
              trigger_type: MeasurementTriggerType.ON_DEMAND,
              ph_value: '6.50',
              ec_value: 1800,
              tds_value: 900,
              temperature_c: '25.0',
              salinity_ppm: 1100,
              orp_mv: 350,
              turbidity_ntu: '1.20',
              battery_pct: 90,
              calibrated_at: null,
              triggered_by_user_id: 'admin',
            },
          ],
          1,
        ]),
      };

      readingRepo.createQueryBuilder.mockReturnValue(qb);

      const result = await service.getHistory(
        10,
        0,
        MeasurementTriggerType.ON_DEMAND,
      );

      expect(qb.where).toHaveBeenCalledWith('reading.sensor_id = :sensorId', {
        sensorId: 'ph-w218-01',
      });
      expect(qb.andWhere).toHaveBeenCalledWith(
        'reading.trigger_type = :triggerType',
        { triggerType: MeasurementTriggerType.ON_DEMAND },
      );
      expect(qb.skip).toHaveBeenCalledWith(0);
      expect(qb.take).toHaveBeenCalledWith(10);
      expect(result.total).toBe(1);
      expect(result.readings.length).toBe(1);
    });
  });
});
