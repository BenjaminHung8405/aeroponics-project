import { Test, TestingModule } from '@nestjs/testing';
import { TuyaBridgeController } from './tuya-bridge.controller';
import { TuyaBridgeService } from './tuya-bridge.service';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { MeasurementTriggerType } from './entities/measurement_reading.entity';

describe('TuyaBridgeController (S3-H2)', () => {
  let controller: TuyaBridgeController;
  let service: any;

  const mockReading = {
    time: new Date(),
    session_id: '123e4567-e89b-12d3-a456-426614174000',
    sensor_id: 'ph-w218-01',
    trigger_type: MeasurementTriggerType.ON_DEMAND,
    ph_value: '6.85',
    ec_value: 1850,
    tds_value: 925,
    temperature_c: '24.5',
    salinity_ppm: 1200,
    orp_mv: 380,
    turbidity_ntu: '1.25',
    battery_pct: 95,
    calibrated_at: null,
    triggered_by_user_id: 'admin_test',
  };

  beforeEach(async () => {
    service = {
      measureOnDemand: jest.fn().mockResolvedValue(mockReading),
      getLatest: jest.fn().mockResolvedValue(mockReading),
      getHistory: jest.fn().mockResolvedValue({
        readings: [mockReading],
        total: 1,
        limit: 20,
        offset: 0,
      }),
      getStatus: jest.fn().mockResolvedValue({
        enabled: false,
        static_enabled: true,
        runtime_enabled: false,
        sensor_id: 'ph-w218-01',
        is_measuring: false,
        cooldown_remaining_s: 0,
        reason: 'Probe protection mode',
      }),
      setBridgeEnabled: jest.fn().mockResolvedValue({
        enabled: true,
        static_enabled: true,
        runtime_enabled: true,
        sensor_id: 'ph-w218-01',
        is_measuring: false,
        cooldown_remaining_s: 0,
        reason: 'Active measurement',
      }),
    };

    const module: TestingModule = await Test.createTestingModule({
      controllers: [TuyaBridgeController],
      providers: [
        {
          provide: TuyaBridgeService,
          useValue: service,
        },
      ],
    })
      .overrideGuard(JwtAuthGuard)
      .useValue({ canActivate: jest.fn().mockReturnValue(true) })
      .compile();

    controller = module.get<TuyaBridgeController>(TuyaBridgeController);
  });

  describe('Security: JwtAuthGuard enforcement', () => {
    it('should be protected by JwtAuthGuard at class level', () => {
      const guards = Reflect.getMetadata('__guards__', TuyaBridgeController);
      expect(guards).toBeDefined();
      expect(guards.length).toBeGreaterThan(0);
      expect(guards[0]).toBe(JwtAuthGuard);
    });
  });

  describe('POST /api/measurement/trigger', () => {
    it('should trigger on-demand measurement and extract operator from user context', async () => {
      const req = { user: { username: 'dr_green' } };
      const dto = { trigger_type: MeasurementTriggerType.ON_DEMAND };

      const result = await controller.triggerMeasurement(dto, req);

      expect(service.measureOnDemand).toHaveBeenCalledWith(
        'dr_green',
        MeasurementTriggerType.ON_DEMAND,
      );
      expect(result).toEqual(mockReading);
    });

    it('should fallback to operator when request user is undefined', async () => {
      const req = {};
      const dto = {};

      const result = await controller.triggerMeasurement(dto, req);

      expect(service.measureOnDemand).toHaveBeenCalledWith('operator', undefined);
      expect(result).toEqual(mockReading);
    });
  });

  describe('GET /api/measurement/latest', () => {
    it('should return the latest reading from service', async () => {
      const result = await controller.getLatestMeasurement();
      expect(service.getLatest).toHaveBeenCalledTimes(1);
      expect(result).toEqual(mockReading);
    });
  });

  describe('GET /api/measurement/history', () => {
    it('should pass query parameters to service', async () => {
      const query = {
        limit: 15,
        offset: 5,
        trigger_type: MeasurementTriggerType.ON_DEMAND,
      };

      const result = await controller.getMeasurementHistory(query);

      expect(service.getHistory).toHaveBeenCalledWith(
        15,
        5,
        MeasurementTriggerType.ON_DEMAND,
      );
      expect(result.readings).toHaveLength(1);
      expect(result.total).toBe(1);
    });
  });

  describe('GET /api/measurement/status', () => {
    it('should return bridge operational status', async () => {
      const result = await controller.getBridgeStatus();
      expect(service.getStatus).toHaveBeenCalledTimes(1);
      expect(result.enabled).toBe(false);
      expect(result.sensor_id).toBe('ph-w218-01');
    });
  });

  describe('POST /api/measurement/toggle', () => {
    it('should toggle bridge enabled state with operator user context', async () => {
      const req = { user: { username: 'dr_green' } };
      const dto = { enabled: true, reason: 'Thí nghiệm pha dinh dưỡng' };

      const result = await controller.toggleBridge(dto, req);

      expect(service.setBridgeEnabled).toHaveBeenCalledWith(dto, 'dr_green');
      expect(result.enabled).toBe(true);
    });
  });
});
