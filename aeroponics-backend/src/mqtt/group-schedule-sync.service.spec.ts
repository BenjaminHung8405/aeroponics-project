import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';

import { GroupScheduleSyncService } from './group-schedule-sync.service';
import { MqttService } from './mqtt.service';
import { DeviceStatus } from '../device/entities/device_status.entity';
import { TreatmentVersion } from '../treatment/entities/treatment_version.entity';
import { GroupTreatmentAssignment } from '../group/entities/group_treatment_assignment.entity';
import { GroupNodeAssignment } from '../group/entities/group_node_assignment.entity';
import { TimerGroup, TimerGroupStatus } from '../group/entities/timer_group.entity';
import { GroupAssignedEvent } from '../group/events/group.events';

describe('GroupScheduleSyncService', () => {
  let service: GroupScheduleSyncService;
  let mqttService: MqttService;

  let mockDeviceStatusRepo: any;
  let mockTreatmentVersionRepo: any;
  let mockGroupTreatmentRepo: any;
  let mockGroupNodeRepo: any;
  let mockTimerGroupRepo: any;

  beforeEach(async () => {
    mockDeviceStatusRepo = {
      find: jest.fn().mockResolvedValue([{ device_id: 'aero_s3_b81f3fbbcf3c', status: 'online' }]),
    };
    mockTreatmentVersionRepo = {
      findOne: jest.fn().mockResolvedValue({
        id: 2,
        version_num: 1,
        spray_day_s: 10,
        cooldown_day_s: 30,
        spray_night_s: 10,
        cooldown_night_s: 30,
      }),
    };
    mockGroupTreatmentRepo = {
      findOne: jest.fn().mockResolvedValue({
        id: 1,
        group_id: 1,
        treatment_version_id: 2,
        season_id: 3,
        active: true,
      }),
    };
    mockGroupNodeRepo = {
      find: jest.fn().mockResolvedValue([
        { id: 1, group_id: 1, node_id: 8, active: true },
      ]),
    };
    mockTimerGroupRepo = {
      find: jest.fn().mockResolvedValue([
        { group_id: 1, status: TimerGroupStatus.ACTIVE },
      ]),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        GroupScheduleSyncService,
        {
          provide: MqttService,
          useValue: {
            publish: jest.fn().mockResolvedValue(undefined),
            isConnected: jest.fn().mockReturnValue(true),
          },
        },
        {
          provide: getRepositoryToken(DeviceStatus),
          useValue: mockDeviceStatusRepo,
        },
        {
          provide: getRepositoryToken(TreatmentVersion),
          useValue: mockTreatmentVersionRepo,
        },
        {
          provide: getRepositoryToken(GroupTreatmentAssignment),
          useValue: mockGroupTreatmentRepo,
        },
        {
          provide: getRepositoryToken(GroupNodeAssignment),
          useValue: mockGroupNodeRepo,
        },
        {
          provide: getRepositoryToken(TimerGroup),
          useValue: mockTimerGroupRepo,
        },
      ],
    }).compile();

    service = module.get<GroupScheduleSyncService>(GroupScheduleSyncService);
    mqttService = module.get<MqttService>(MqttService);
  });

  it('should be defined', () => {
    expect(service).toBeDefined();
  });

  it('should publish treatment config and assignment downlinks on group.assigned event', async () => {
    const event = new GroupAssignedEvent(1, 3, 2, [8], new Date());
    await service.handleGroupAssigned(event);

    expect(mqttService.publish).toHaveBeenCalledTimes(2);

    // Call 1: Treatment config
    expect(mqttService.publish).toHaveBeenNthCalledWith(
      1,
      'aeroponics/device/aero_s3_b81f3fbbcf3c/command/config/treatment',
      expect.objectContaining({
        group_id: 1,
        season_id: 3,
        treatment_version_id: 2,
        treatment_version: 1,
        treatment_status: 'PUBLISHED',
        schedule: {
          spray_day_s: 10,
          cooldown_day_s: 30,
          spray_night_s: 10,
          cooldown_night_s: 30,
        },
      }),
    );

    // Call 2: Node assignment
    expect(mqttService.publish).toHaveBeenNthCalledWith(
      2,
      'aeroponics/device/aero_s3_b81f3fbbcf3c/command/config/assignment',
      expect.objectContaining({
        node_id: 8,
        group_id: 1,
      }),
    );
  });

  it('should sync all active groups when gateway reports online status', async () => {
    await service.handleDeviceStatus({
      deviceId: 'aero_s3_b81f3fbbcf3c',
      payload: { status: 'online' },
    });

    expect(mqttService.publish).toHaveBeenCalledTimes(2);
  });
});
