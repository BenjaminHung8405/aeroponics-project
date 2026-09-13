import { Test, TestingModule } from '@nestjs/testing';
import { FlowController } from './flow.controller';
import { FlowService } from './flow.service';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { CalibrationStatusEnum } from '../node/entities/sensor_calibration.entity';

describe('FlowController (S3-G2)', () => {
  let controller: FlowController;
  let flowService: jest.Mocked<FlowService>;

  const mockCalibration = {
    id: 15,
    node_id: 1,
    sensor_serial: 'SEN-N1-FLOW',
    version_num: 2,
    pulses_per_litre: '460.0000',
    reference_volume_ml: 1000,
    trial_count: 3,
    mean_pulses: '460.00',
    variance: '0.0000',
    repeatability_pct: '1.00',
    operating_conditions: null,
    status: CalibrationStatusEnum.ACTIVE,
    calibrated_by: 'lead_operator',
    calibrated_at: new Date(),
  };

  const mockCalibrationResponse = {
    node_id: 1,
    active_calibration: mockCalibration,
    history: [mockCalibration],
    total_versions: 1,
  };

  const mockHistoryResponse = {
    node_id: 1,
    hours: 24,
    since: new Date(),
    summary: {
      total_events: 10,
      total_delivered_volume_ml: 2500,
      total_litres: 2.5,
      average_flow_rate_lpm: 2.15,
      max_flow_rate_lpm: 2.4,
      confirmed_events: 10,
      fault_events: 0,
      flow_confirmation_rate_pct: 100.0,
    },
    events: [],
  };

  beforeEach(async () => {
    const mockService = {
      getHistory: jest.fn().mockResolvedValue(mockHistoryResponse),
      getCalibration: jest.fn().mockResolvedValue(mockCalibrationResponse),
      updateCalibration: jest.fn().mockResolvedValue(mockCalibration),
    };

    const module: TestingModule = await Test.createTestingModule({
      controllers: [FlowController],
      providers: [{ provide: FlowService, useValue: mockService }],
    }).compile();

    controller = module.get<FlowController>(FlowController);
    flowService = module.get(FlowService);
  });

  it('SECURITY INVARIANT: should have JwtAuthGuard applied at controller level (100% endpoints protected)', () => {
    const guards = Reflect.getMetadata('__guards__', FlowController);
    expect(guards).toBeDefined();
    expect(guards.length).toBeGreaterThan(0);
    expect(guards[0]).toBe(JwtAuthGuard);
  });

  describe('GET /api/node/:id/flow', () => {
    it('should call flowService.getHistory with parsed id and query params', async () => {
      const query = { hours: 48, limit: 100 };
      const result = await controller.getFlowHistory(1, query);

      expect(flowService.getHistory).toHaveBeenCalledWith(1, 48, 100);
      expect(result).toEqual(mockHistoryResponse);
    });

    it('should forward default query parameters', async () => {
      const query = { hours: 24, limit: 500 };
      await controller.getFlowHistory(2, query);

      expect(flowService.getHistory).toHaveBeenCalledWith(2, 24, 500);
    });
  });

  describe('GET /api/node/:id/calibration', () => {
    it('should call flowService.getCalibration with parsed node id', async () => {
      const result = await controller.getCalibration(1);

      expect(flowService.getCalibration).toHaveBeenCalledWith(1);
      expect(result).toEqual(mockCalibrationResponse);
    });
  });

  describe('PUT /api/node/:id/calibration', () => {
    it('should call flowService.updateCalibration with user from JWT req.user.username', async () => {
      const dto = {
        pulses_per_litre: 460.0,
        getEffectivePulsesPerLitre: () => 460.0,
      };
      const req = { user: { username: 'lead_operator', sub: 'user-uuid-1' } };

      const result = await controller.updateCalibration(1, dto as any, req);

      expect(flowService.updateCalibration).toHaveBeenCalledWith(
        1,
        dto,
        'lead_operator',
      );
      expect(result).toEqual(mockCalibration);
    });

    it('should fallback to req.user.sub when username is not present', async () => {
      const dto = {
        pulses_per_litre: 460.0,
        getEffectivePulsesPerLitre: () => 460.0,
      };
      const req = { user: { sub: 'admin-uuid' } };

      await controller.updateCalibration(2, dto as any, req);

      expect(flowService.updateCalibration).toHaveBeenCalledWith(
        2,
        dto,
        'admin-uuid',
      );
    });

    it('should fallback to dto.calibrated_by or operator when user is not in req', async () => {
      const dto = {
        pulses_per_litre: 460.0,
        calibrated_by: 'custom_operator',
        getEffectivePulsesPerLitre: () => 460.0,
      };
      const req = {};

      await controller.updateCalibration(3, dto as any, req);

      expect(flowService.updateCalibration).toHaveBeenCalledWith(
        3,
        dto,
        'custom_operator',
      );
    });
  });
});
