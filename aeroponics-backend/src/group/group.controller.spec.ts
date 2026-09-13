import { Test, TestingModule } from '@nestjs/testing';
import { GroupController } from './group.controller';
import { GroupService } from './group.service';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { TimerGroupStatus } from './entities/timer_group.entity';
import { GroupStatusResponse } from './group.types';

describe('GroupController (S3-E3)', () => {
  let controller: GroupController;
  let service: jest.Mocked<GroupService>;

  const mockGroupStatus: GroupStatusResponse = {
    group_id: 1,
    name: 'Group 1',
    status: TimerGroupStatus.ACTIVE,
    current_phase: 'DAY',
    next_transition_at: '2026-09-13T18:00:00.000+07:00',
    treatment: {
      treatment_id: 1,
      treatment_name: 'Standard Formula',
      treatment_version_id: 2,
      version_num: 1,
      spray_day_s: 15,
      cooldown_day_s: 300,
      spray_night_s: 10,
      cooldown_night_s: 600,
    },
    nodes: [],
  };

  beforeEach(async () => {
    const mockGroupService = {
      getAllGroupsStatus: jest.fn().mockResolvedValue([mockGroupStatus]),
      getGroupStatus: jest.fn().mockResolvedValue(mockGroupStatus),
      assignTreatmentVersion: jest.fn().mockResolvedValue(mockGroupStatus),
      unassign: jest
        .fn()
        .mockResolvedValue({ ...mockGroupStatus, status: TimerGroupStatus.UNASSIGNED }),
    };

    const module: TestingModule = await Test.createTestingModule({
      controllers: [GroupController],
      providers: [{ provide: GroupService, useValue: mockGroupService }],
    }).compile();

    controller = module.get<GroupController>(GroupController);
    service = module.get(GroupService);
  });

  it('should have JwtAuthGuard applied at controller level', () => {
    const guards = Reflect.getMetadata('__guards__', GroupController);
    expect(guards).toBeDefined();
    expect(guards.length).toBeGreaterThan(0);
    expect(guards[0]).toBe(JwtAuthGuard);
  });

  it('should call getAllGroupsStatus on GET /api/group', async () => {
    const result = await controller.getAllGroups();
    expect(service.getAllGroupsStatus).toHaveBeenCalled();
    expect(result).toEqual([mockGroupStatus]);
  });

  it('should call getGroupStatus on GET /api/group/:id', async () => {
    const result = await controller.getGroupById(1);
    expect(service.getGroupStatus).toHaveBeenCalledWith(1);
    expect(result).toEqual(mockGroupStatus);
  });

  it('should call assignTreatmentVersion on PUT /api/group/:id/assign', async () => {
    const dto = { treatment_version_id: 2, node_ids: [1, 2] };
    const result = await controller.assignTreatmentAndNodes(1, dto);
    expect(service.assignTreatmentVersion).toHaveBeenCalledWith(1, dto);
    expect(result).toEqual(mockGroupStatus);
  });

  it('should call unassign on DELETE /api/group/:id/assign', async () => {
    const result = await controller.unassignGroup(1);
    expect(service.unassign).toHaveBeenCalledWith(1);
    expect(result.status).toBe(TimerGroupStatus.UNASSIGNED);
  });
});
