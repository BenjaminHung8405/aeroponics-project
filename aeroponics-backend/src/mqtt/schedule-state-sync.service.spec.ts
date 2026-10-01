import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';
import { EventEmitter2 } from '@nestjs/event-emitter';

import { ScheduleStateSyncService } from './schedule-state-sync.service';
import { GroupScheduleSyncService } from './group-schedule-sync.service';
import { DeviceStatus } from '../device/entities/device_status.entity';
import { Device } from '../device/entities/device.entity';
import { ControlSlot, ControlSlotTargetType } from '../control-slot/entities/control_slot.entity';
import { GroupTreatmentAssignment } from '../group/entities/group_treatment_assignment.entity';
import { TreatmentVersion } from '../treatment/entities/treatment_version.entity';

describe('ScheduleStateSyncService', () => {
  let service: ScheduleStateSyncService;
  let groupScheduleSync: GroupScheduleSyncService;
  let eventEmitter: EventEmitter2;

  let mockDeviceStatusRepo: any;
  let mockDeviceRepo: any;
  let mockControlSlotRepo: any;
  let mockGroupTreatmentRepo: any;
  let mockTreatmentVersionRepo: any;
  let controlSlots: any[];

  const validReport = {
    device_id: 'aero_s3_test',
    timestamp: 1790831900,
    slots_reconciled: true,
    active_slots: [{ idx: 1, type: 'GROUP', id: 1 }],
    groups: [
      {
        group_id: 1,
        state: 'ACTIVE',
        phase: 'COOLING_DOWN',
        phase_remaining_s: 24,
        profile: {
          active: { treatment_version: 1, spray_day_s: 15, cooldown_day_s: 30, spray_night_s: 15, cooldown_night_s: 30 },
          pending: { has_pending: false, treatment_version: 0, spray_day_s: 0, cooldown_day_s: 0, spray_night_s: 0, cooldown_night_s: 0 },
        },
      },
      { group_id: 2, state: 'UNASSIGNED', profile: { active: { treatment_version: 0, spray_day_s: 0, cooldown_day_s: 0, spray_night_s: 0, cooldown_night_s: 0 }, pending: { has_pending: false, treatment_version: 0, spray_day_s: 0, cooldown_day_s: 0, spray_night_s: 0, cooldown_night_s: 0 } } },
      { group_id: 3, state: 'UNASSIGNED', profile: { active: { treatment_version: 0, spray_day_s: 0, cooldown_day_s: 0, spray_night_s: 0, cooldown_night_s: 0 }, pending: { has_pending: false, treatment_version: 0, spray_day_s: 0, cooldown_day_s: 0, spray_night_s: 0, cooldown_night_s: 0 } } },
      { group_id: 4, state: 'UNASSIGNED', profile: { active: { treatment_version: 0, spray_day_s: 0, cooldown_day_s: 0, spray_night_s: 0, cooldown_night_s: 0 }, pending: { has_pending: false, treatment_version: 0, spray_day_s: 0, cooldown_day_s: 0, spray_night_s: 0, cooldown_night_s: 0 } } },
    ],
  };

  beforeEach(async () => {
    controlSlots = [{ device_id: 'aero_s3_test', slot_index: 1, target_type: ControlSlotTargetType.GROUP, target_id: 1 }];
    mockDeviceStatusRepo = {
      findOne: jest.fn().mockResolvedValue({ device_id: 'aero_s3_test', schedule_sync_state: null }),
      save: jest.fn().mockImplementation((status) => Promise.resolve(status)),
      find: jest.fn().mockResolvedValue([]),
    };
    mockDeviceRepo = {
      findOne: jest.fn().mockResolvedValue({ device_id: 'aero_s3_test' }),
    };
    mockControlSlotRepo = {
      find: jest.fn().mockImplementation((options: any) => {
        const targetId = Number(options?.where?.target_id);
        return Promise.resolve(controlSlots.filter((slot) =>
          slot.device_id === options?.where?.device_id &&
          slot.target_type === options?.where?.target_type &&
          Number(slot.target_id) === targetId));
      }),
    };
    mockGroupTreatmentRepo = {
      findOne: jest.fn().mockResolvedValue({ group_id: 1, treatment_version_id: 2, active: true }),
    };
    mockTreatmentVersionRepo = {
      findOne: jest.fn().mockResolvedValue({ id: 2, version_num: 1, spray_day_s: 15, cooldown_day_s: 30, spray_night_s: 15, cooldown_night_s: 30 }),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        ScheduleStateSyncService,
        {
          provide: GroupScheduleSyncService,
          useValue: { syncGroup: jest.fn().mockResolvedValue(undefined) },
        },
        { provide: getRepositoryToken(DeviceStatus), useValue: mockDeviceStatusRepo },
        { provide: getRepositoryToken(Device), useValue: mockDeviceRepo },
        { provide: getRepositoryToken(ControlSlot), useValue: mockControlSlotRepo },
        { provide: getRepositoryToken(GroupTreatmentAssignment), useValue: mockGroupTreatmentRepo },
        { provide: getRepositoryToken(TreatmentVersion), useValue: mockTreatmentVersionRepo },
        { provide: EventEmitter2, useValue: { emit: jest.fn() } },
      ],
    }).compile();

    service = module.get<ScheduleStateSyncService>(ScheduleStateSyncService);
    groupScheduleSync = module.get<GroupScheduleSyncService>(GroupScheduleSyncService);
    eventEmitter = module.get<EventEmitter2>(EventEmitter2);
  });

  afterEach(() => {
    jest.restoreAllMocks();
  });

  it('should be defined', () => {
    expect(service).toBeDefined();
  });

  it('should persist reported_schedule_state to JSONB', async () => {
    await service.handleScheduleState({ deviceId: 'aero_s3_test', payload: validReport, receivedAt: new Date() });
    expect(mockDeviceStatusRepo.save).toHaveBeenCalledWith(
      expect.objectContaining({ reported_schedule_state: validReport }),
    );
  });

  it('should scope desired state by Control Slots: group not in device slot -> active:false', async () => {
    controlSlots = [{ device_id: 'aero_s3_test', target_type: ControlSlotTargetType.GROUP, target_id: 2 }];
    const desired = await service['getDesiredGroupState']('aero_s3_test', 1);
    expect(desired.active).toBe(false);
  });

  it('should set desired state active:true when group in device slot and has assignment', async () => {
    const desired = await service['getDesiredGroupState']('aero_s3_test', 1);
    expect(desired.active).toBe(true);
    expect(desired.profile.treatment_version).toBe(1);
    expect(desired.profile.spray_day_s).toBe(15);
  });

  it('should recognize IN_SYNC_PENDING_BOUNDARY when pending profile matches desired', async () => {
    const reportWithPending = {
      ...validReport,
      groups: [
        {
          group_id: 1,
          state: 'ACTIVE',
          profile: {
            active: { treatment_version: 0, spray_day_s: 10, cooldown_day_s: 25, spray_night_s: 10, cooldown_night_s: 25 },
            pending: { has_pending: true, treatment_version: 1, spray_day_s: 15, cooldown_day_s: 30, spray_night_s: 15, cooldown_night_s: 30 },
          },
        },
        ...validReport.groups.slice(1),
      ],
    };
    await service.handleScheduleState({ deviceId: 'aero_s3_test', payload: reportWithPending, receivedAt: new Date() });
    expect(mockDeviceStatusRepo.save).toHaveBeenCalledWith(
      expect.objectContaining({ schedule_sync_state: 'IN_SYNC_PENDING_BOUNDARY' }),
    );
  });

  it('should detect DRIFTED when both active and pending profiles mismatch desired', async () => {
    const reportMismatch = {
      ...validReport,
      groups: [
        {
          group_id: 1,
          state: 'ACTIVE',
          profile: {
            active: { treatment_version: 0, spray_day_s: 10, cooldown_day_s: 25, spray_night_s: 10, cooldown_night_s: 25 },
            pending: { has_pending: false, treatment_version: 0, spray_day_s: 0, cooldown_day_s: 0, spray_night_s: 0, cooldown_night_s: 0 },
          },
        },
        ...validReport.groups.slice(1),
      ],
    };
    await service.handleScheduleState({ deviceId: 'aero_s3_test', payload: reportMismatch, receivedAt: new Date() });
    expect(mockDeviceStatusRepo.save).toHaveBeenCalledWith(
      expect.objectContaining({ schedule_sync_state: 'SYNCING' }),
    );
  });

  it('should call syncGroup with correct deviceId when drift detected', async () => {
    const reportMismatch = {
      ...validReport,
      groups: [
        {
          group_id: 1,
          state: 'ACTIVE',
          profile: {
            active: { treatment_version: 999, spray_day_s: 99, cooldown_day_s: 99, spray_night_s: 99, cooldown_night_s: 99 },
            pending: { has_pending: false, treatment_version: 0, spray_day_s: 0, cooldown_day_s: 0, spray_night_s: 0, cooldown_night_s: 0 },
          },
        },
        ...validReport.groups.slice(1),
      ],
    };
    await service.handleScheduleState({ deviceId: 'aero_s3_test', payload: reportMismatch, receivedAt: new Date() });
    expect(groupScheduleSync.syncGroup).toHaveBeenCalledWith(1, 'aero_s3_test');
  });

  it('should enforce 10s cooldown between auto-resync attempts', async () => {
    const reportMismatch = {
      ...validReport,
      groups: [
        { group_id: 1, state: 'ACTIVE', profile: { active: { treatment_version: 999, spray_day_s: 99, cooldown_day_s: 99, spray_night_s: 99, cooldown_night_s: 99 }, pending: { has_pending: false, treatment_version: 0, spray_day_s: 0, cooldown_day_s: 0, spray_night_s: 0, cooldown_night_s: 0 } } },
        ...validReport.groups.slice(1),
      ],
    };
    jest.spyOn(groupScheduleSync, 'syncGroup').mockClear();
    await service.handleScheduleState({ deviceId: 'aero_s3_test', payload: reportMismatch, receivedAt: new Date() });
    expect(groupScheduleSync.syncGroup).toHaveBeenCalledTimes(1);
    await service.handleScheduleState({ deviceId: 'aero_s3_test', payload: reportMismatch, receivedAt: new Date() });
    expect(groupScheduleSync.syncGroup).toHaveBeenCalledTimes(1);
  });

  it('should latch DRIFTED_LATCHED after 3 auto-retry attempts in 60s', async () => {
    const reportMismatch = {
      ...validReport,
      groups: [
        { group_id: 1, state: 'ACTIVE', profile: { active: { treatment_version: 999, spray_day_s: 99, cooldown_day_s: 99, spray_night_s: 99, cooldown_night_s: 99 }, pending: { has_pending: false, treatment_version: 0, spray_day_s: 0, cooldown_day_s: 0, spray_night_s: 0, cooldown_night_s: 0 } } },
        ...validReport.groups.slice(1),
      ],
    };
    jest.spyOn(Date, 'now').mockReturnValueOnce(1000).mockReturnValueOnce(11001).mockReturnValueOnce(22002).mockReturnValueOnce(33003);
    await service.handleScheduleState({ deviceId: 'aero_s3_test', payload: reportMismatch, receivedAt: new Date() });
    await service.handleScheduleState({ deviceId: 'aero_s3_test', payload: reportMismatch, receivedAt: new Date() });
    await service.handleScheduleState({ deviceId: 'aero_s3_test', payload: reportMismatch, receivedAt: new Date() });
    await service.handleScheduleState({ deviceId: 'aero_s3_test', payload: reportMismatch, receivedAt: new Date() });
    expect(mockDeviceStatusRepo.save).toHaveBeenLastCalledWith(
      expect.objectContaining({ schedule_sync_state: 'DRIFTED_LATCHED' }),
    );
  });

  it('should allow retryManually to unlock DRIFTED_LATCHED', async () => {
    mockDeviceStatusRepo.findOne.mockResolvedValueOnce({ device_id: 'aero_s3_test', schedule_sync_state: 'DRIFTED_LATCHED' });
    await service.retryManually('aero_s3_test');
    expect(mockDeviceStatusRepo.save).toHaveBeenCalledWith(
      expect.objectContaining({ schedule_sync_state: 'SYNCING' }),
    );
  });

  it('should clear latch when IN_SYNC report arrives', async () => {
    service['retry'].set('aero_s3_test', { attempts: [1000, 2000, 3000], syncing: false, latched: true });
    await service.handleScheduleState({ deviceId: 'aero_s3_test', payload: validReport, receivedAt: new Date() });
    const retry = service['retry'].get('aero_s3_test');
    expect(retry.latched).toBe(false);
    expect(retry.attempts.length).toBe(0);
  });

  it('should mark stale report as UNCONFIRMED when schedule_sync_updated_at > 180s old', async () => {
    const staleDate = new Date(Date.now() - 200000);
    mockDeviceStatusRepo.find.mockResolvedValueOnce([{ device_id: 'aero_s3_test', schedule_sync_state: 'IN_SYNC', schedule_sync_updated_at: staleDate }]);
    await service.checkScheduleSyncFreshness();
    expect(mockDeviceStatusRepo.save).toHaveBeenCalledWith(
      expect.objectContaining({ schedule_sync_state: 'UNCONFIRMED' }),
    );
  });

  it('should preserve DRIFTED_LATCHED when checking freshness', async () => {
    const staleDate = new Date(Date.now() - 200000);
    mockDeviceStatusRepo.find.mockResolvedValueOnce([{ device_id: 'aero_s3_test', schedule_sync_state: 'DRIFTED_LATCHED', schedule_sync_updated_at: staleDate }]);
    await service.checkScheduleSyncFreshness();
    expect(mockDeviceStatusRepo.save).not.toHaveBeenCalledWith(
      expect.objectContaining({ schedule_sync_state: 'UNCONFIRMED' }),
    );
  });
});
