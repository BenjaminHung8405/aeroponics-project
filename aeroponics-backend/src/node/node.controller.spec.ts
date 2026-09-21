import { Test, TestingModule } from '@nestjs/testing';
import { NodeController } from './node.controller';
import { NodeService, NodeStatusResponse } from './node.service';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import {
  CalibrationStatus,
  NodeHealthStatus,
} from './entities/node_registry.entity';

describe('NodeController (S3-E3)', () => {
  let controller: NodeController;
  let service: jest.Mocked<NodeService>;

  const mockNodeStatus: NodeStatusResponse = {
    node_id: 4,
    display_name: 'Node 01',
    cached_group_id: 1,
    sensor_serial: 'SEN-01',
    calibration_status: CalibrationStatus.CALIBRATED,
    schedule_state: 'IDLE',
    override_state: 'NONE',
    last_boot_session_id: 1,
    last_seen_at: new Date(),
    health_status: NodeHealthStatus.OK,
    is_stale: false,
    stale_for_ms: 5000,
    rf_protocol: 'AGU_LEGACY_SCI',
    last_scan_id: null,
    last_rf_rtt_ms: 120,
    last_discovered_at: new Date(),
    discovery_status: 'ONLINE',
    active_calibration: {
      id: 10,
      version_num: 1,
      pulses_per_litre: '450.5000',
      reference_volume_ml: 1000,
      calibrated_at: new Date(),
      calibrated_by: 'Engineer T',
    },
  };

  beforeEach(async () => {
    const mockNodeService = {
      getAllNodesStatus: jest.fn().mockResolvedValue([mockNodeStatus]),
      getNodeStatus: jest.fn().mockResolvedValue(mockNodeStatus),
      resetFault: jest.fn().mockResolvedValue({} as any),
    };

    const module: TestingModule = await Test.createTestingModule({
      controllers: [NodeController],
      providers: [{ provide: NodeService, useValue: mockNodeService }],
    }).compile();

    controller = module.get<NodeController>(NodeController);
    service = module.get(NodeService);
  });

  it('should have JwtAuthGuard applied at controller level', () => {
    const guards = Reflect.getMetadata('__guards__', NodeController);
    expect(guards).toBeDefined();
    expect(guards.length).toBeGreaterThan(0);
    expect(guards[0]).toBe(JwtAuthGuard);
  });

  it('should call getAllNodesStatus on GET /api/node', async () => {
    const result = await controller.getAllNodes();
    expect(service.getAllNodesStatus).toHaveBeenCalled();
    expect(result).toEqual([mockNodeStatus]);
  });

  it('should call getNodeStatus on GET /api/node/:id', async () => {
    const result = await controller.getNodeById(1);
    expect(service.getNodeStatus).toHaveBeenCalledWith(1);
    expect(result).toEqual(mockNodeStatus);
  });

  it('should call resetFault on POST /api/node/:id/fault-reset', async () => {
    const result = await controller.resetFault(1);
    expect(service.resetFault).toHaveBeenCalledWith(1);
    expect(service.getNodeStatus).toHaveBeenCalledWith(1);
    expect(result).toEqual(mockNodeStatus);
  });
});
