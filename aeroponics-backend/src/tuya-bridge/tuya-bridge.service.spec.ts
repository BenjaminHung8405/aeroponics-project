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
import { SystemSetting } from '../system-setting/entities/system_setting.entity';
import { SeasonService } from '../season/season.service';
import { MqttService } from '../mqtt/mqtt.service';
import * as fs from 'fs';
import * as path from 'path';

describe('TuyaBridgeService (S3-H1)', () => {
  let service: TuyaBridgeService;
  let sessionRepo: any;
  let readingRepo: any;
  let settingRepo: any;
  let seasonService: any;
  let eventEmitter: EventEmitter2;
  let configService: any;
  let mockMqttService: any;

  const mockConfig: Record<string, any> = {
    TUYA_DEVICE_IP: '192.168.1.150',
    TUYA_DEVICE_ID: 'dev_tuya_phw218_test',
    TUYA_LOCAL_KEY: 'secret_key_16chr',
    TUYA_SENSOR_ID: 'ph-w218-01',
    TUYA_ON_DEMAND_TIMEOUT_MS: 500,
    TUYA_COOLDOWN_WINDOW_MS: 60000,
    TUYA_BRIDGE_ENABLED: true,
  };

  beforeEach(async () => {
    eventEmitter = new EventEmitter2();

    mockMqttService = {
      publish: jest.fn().mockImplementation(async (_topic: string, payload: any) => {
        // Asynchronously emit mock measurement reading back
        setTimeout(() => {
          eventEmitter.emit(`measurement.session.${payload.session_id}`, {
            reading: {
              time: new Date(),
              session_id: payload.session_id,
              sensor_id: payload.sensor_id,
              trigger_type: payload.trigger_type,
              ph_value: '6.85',
              ec_value: 1850,
              tds_value: 925,
              temperature_c: '24.5',
              salinity_ppm: 1200,
              orp_mv: 380,
              turbidity_ntu: '12.50',
              battery_pct: 95,
              calibrated_at: null,
              triggered_by_user_id: payload.operator,
            },
          });
        }, 10);
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

    settingRepo = {
      findOne: jest.fn().mockResolvedValue({
        key: 'tuya_bridge_enabled',
        value: { enabled: true, reason: 'Test mode' },
        updated_at: new Date(),
        updated_by: 'admin',
      }),
      create: jest.fn().mockImplementation((data) => data),
      save: jest.fn().mockImplementation(async (data) => data),
    };

    seasonService = {
      getActive: jest.fn().mockResolvedValue({ id: 1, name: 'Season 2026' }),
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
          provide: getRepositoryToken(SystemSetting),
          useValue: settingRepo,
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
          provide: MqttService,
          useValue: mockMqttService,
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
    it('should execute on-demand measurement successfully via MQTT and complete session', async () => {
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

      // Verify MQTT publish triggered
      expect(mockMqttService.publish).toHaveBeenCalledWith(
        'aeroponics/sensors/ph-w218-01/command/trigger',
        expect.objectContaining({
          session_id: '123e4567-e89b-12d3-a456-426614174000',
          sensor_id: 'ph-w218-01',
          trigger_type: MeasurementTriggerType.ON_DEMAND,
          operator: 'operator_admin',
        }),
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
      // Delay MQTT response to test concurrency
      mockMqttService.publish.mockImplementation(async () => {
        // does not respond immediately
      });

      const call1 = service.measureOnDemand('user_1');
      try {
        await service.measureOnDemand('user_2');
        fail('Should have thrown HttpException');
      } catch (err: any) {
        expect(err).toBeInstanceOf(HttpException);
        expect(err.getStatus()).toBe(HttpStatus.TOO_MANY_REQUESTS);
        expect(err.message).toContain('already in progress');
      }

      // Cleanup pending promise by emitting error
      eventEmitter.emit('measurement.session.123e4567-e89b-12d3-a456-426614174000', {
        error: 'aborted',
      });
      await expect(call1).rejects.toThrow();
    });

    it('HARD RULE S3-TUYA-07: should mark session FAILED when Edge Bridge reports error', async () => {
      mockMqttService.publish.mockImplementation(async (_topic: string, payload: any) => {
        setTimeout(() => {
          eventEmitter.emit(`measurement.session.${payload.session_id}`, {
            error: 'Connection refused by sensor in local LAN',
          });
        }, 10);
      });

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
    });

    it('should throw RequestTimeoutException when Edge Bridge times out', async () => {
      mockMqttService.publish.mockImplementation(async () => {
        // Simulate no MQTT response (timeout)
      });
      // Set short timeout for this test
      configService.get.mockImplementation((key: string) => {
        if (key === 'TUYA_ON_DEMAND_TIMEOUT_MS') return 50;
        return mockConfig[key];
      });

      await expect(service.measureOnDemand('user_1')).rejects.toThrow(
        RequestTimeoutException,
      );
    });

    it('HARD RULE S3-TUYA-01: should redact local key if present in error message', async () => {
      mockMqttService.publish.mockImplementation(async (_topic: string, payload: any) => {
        setTimeout(() => {
          eventEmitter.emit(`measurement.session.${payload.session_id}`, {
            error: 'Failed handshake with secret_key_16chr on device',
          });
        }, 10);
      });

      await expect(service.measureOnDemand('user_1')).rejects.toThrow(
        BadGatewayException,
      );

      expect(sessionRepo.save).toHaveBeenCalledWith(
        expect.objectContaining({
          status: TuyaSessionStatus.FAILED,
          error_message: expect.stringContaining('Failed handshake with [REDACTED_KEY] on device'),
        }),
      );
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

  describe('S3-H3: Enable/Disable & Probe Protection Mode', () => {
    it('should reject measurement with HTTP 409 when runtime is disabled', async () => {
      service.resetStateForTesting(false); // Disable runtime

      await expect(service.measureOnDemand('farmer')).rejects.toThrow(HttpException);

      try {
        await service.measureOnDemand('farmer');
      } catch (err: any) {
        expect(err.getStatus()).toBe(HttpStatus.CONFLICT);
        const res = err.getResponse();
        expect(res.errorCode).toBe('TUYA_BRIDGE_DISABLED');
        expect(res.message).toContain('đang ở chế độ TẮT');
      }

      // Verify ZERO MQTT publish attempted (probe protection)
      expect(mockMqttService.publish).not.toHaveBeenCalled();
    });

    it('should reject measurement when static environment config is disabled', async () => {
      configService.get.mockImplementation((key: string) => {
        if (key === 'TUYA_BRIDGE_ENABLED') return false;
        return mockConfig[key];
      });
      service.resetStateForTesting(true); // runtime is true, but static is false

      await expect(service.measureOnDemand('farmer')).rejects.toThrow(HttpException);
      expect(mockMqttService.publish).not.toHaveBeenCalled();
    });

    it('should return complete status report via getStatus()', async () => {
      service.resetStateForTesting(true);
      const status = await service.getStatus();

      expect(status.enabled).toBe(true);
      expect(status.static_enabled).toBe(true);
      expect(status.runtime_enabled).toBe(true);
      expect(status.sensor_id).toBe('ph-w218-01');
      expect(status.is_measuring).toBe(false);
      expect(status.cooldown_remaining_s).toBe(0);
    });

    it('should toggle bridge status, persist to DB and update runtime state', async () => {
      const updated = await service.setBridgeEnabled(
        { enabled: false, reason: 'Lưu trữ đầu dò vào dung dịch KCl' },
        'admin_user',
      );

      expect(updated.runtime_enabled).toBe(false);
      expect(updated.enabled).toBe(false);
      expect(updated.reason).toBe('Lưu trữ đầu dò vào dung dịch KCl');
      expect(settingRepo.save).toHaveBeenCalled();
    });

    it('should safely skip end-of-season snapshot when bridge is disabled', async () => {
      service.resetStateForTesting(false);

      // Trigger season ended event
      await service.handleSeasonEnded({
        seasonId: 99,
        name: 'Vụ Cải Kale Thử Nghiệm',
        endedAt: new Date(),
      } as any);

      // Verify no MQTT publish or measurement initiated
      expect(mockMqttService.publish).not.toHaveBeenCalled();
    });
  });
});
