import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';
import { DataSource, Repository } from 'typeorm';
import { EventEmitter2 } from '@nestjs/event-emitter';
import {
  BadRequestException,
  ConflictException,
} from '@nestjs/common';
import { DateTime } from 'luxon';

import { GroupService } from './group.service';
import { TimerGroup, TimerGroupStatus } from './entities/timer_group.entity';
import { GroupTreatmentAssignment } from './entities/group_treatment_assignment.entity';
import { GroupNodeAssignment } from './entities/group_node_assignment.entity';
import {
  TreatmentVersion,
  TreatmentVersionStatus,
} from '../treatment/entities/treatment_version.entity';
import { NodeRegistry } from '../node/entities/node_registry.entity';
import { SeasonService } from '../season/season.service';
import { Season, SeasonStatus } from '../season/entities/season.entity';

describe('GroupService (S3-E1)', () => {
  let service: GroupService;
  let groupRepo: jest.Mocked<Repository<TimerGroup>>;
  let treatmentAssignRepo: jest.Mocked<Repository<GroupTreatmentAssignment>>;
  let nodeAssignRepo: jest.Mocked<Repository<GroupNodeAssignment>>;
  let treatmentVersionRepo: jest.Mocked<Repository<TreatmentVersion>>;
  let nodeRegistryRepo: jest.Mocked<Repository<NodeRegistry>>;
  let seasonService: jest.Mocked<SeasonService>;
  let dataSource: jest.Mocked<DataSource>;
  let eventEmitter: jest.Mocked<EventEmitter2>;

  beforeEach(async () => {
    const mockRepo = () => ({
      find: jest.fn().mockResolvedValue([]),
      findOne: jest.fn(),
      create: jest.fn(),
      save: jest.fn(),
      update: jest.fn(),
      createQueryBuilder: jest.fn(() => ({
        update: jest.fn().mockReturnThis(),
        set: jest.fn().mockReturnThis(),
        where: jest.fn().mockReturnThis(),
        execute: jest.fn().mockResolvedValue({ affected: 1 }),
      })),
    });

    const mockSeasonService = {
      getActive: jest.fn(),
    };

    const mockEventEmitter = {
      emit: jest.fn(),
    };

    const mockDataSource = {
      transaction: jest.fn((cb) => {
        const managerMock = {
          createQueryBuilder: jest.fn(() => ({
            update: jest.fn().mockReturnThis(),
            set: jest.fn().mockReturnThis(),
            where: jest.fn().mockReturnThis(),
            execute: jest.fn().mockResolvedValue({ affected: 1 }),
          })),
          create: jest.fn((entity, val) => val),
          save: jest.fn().mockImplementation((val) => Promise.resolve(val)),
        };
        return cb(managerMock);
      }),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        GroupService,
        { provide: getRepositoryToken(TimerGroup), useValue: mockRepo() },
        {
          provide: getRepositoryToken(GroupTreatmentAssignment),
          useValue: mockRepo(),
        },
        {
          provide: getRepositoryToken(GroupNodeAssignment),
          useValue: mockRepo(),
        },
        {
          provide: getRepositoryToken(TreatmentVersion),
          useValue: mockRepo(),
        },
        { provide: getRepositoryToken(NodeRegistry), useValue: mockRepo() },
        { provide: SeasonService, useValue: mockSeasonService },
        { provide: DataSource, useValue: mockDataSource },
        { provide: EventEmitter2, useValue: mockEventEmitter },
      ],
    }).compile();

    service = module.get<GroupService>(GroupService);
    groupRepo = module.get(getRepositoryToken(TimerGroup));
    treatmentAssignRepo = module.get(
      getRepositoryToken(GroupTreatmentAssignment),
    );
    nodeAssignRepo = module.get(getRepositoryToken(GroupNodeAssignment));
    treatmentVersionRepo = module.get(
      getRepositoryToken(TreatmentVersion),
    );
    nodeRegistryRepo = module.get(getRepositoryToken(NodeRegistry));
    seasonService = module.get(SeasonService);
    dataSource = module.get(DataSource);
    eventEmitter = module.get(EventEmitter2);
  });

  describe('calculateCurrentPhase (Luxon ICT Timezone)', () => {
    it('should identify 06:00:00.000 ICT as DAY phase boundary', () => {
      // 2026-09-13 06:00:00 ICT = 2026-09-12 23:00:00 UTC
      const date = DateTime.fromISO('2026-09-13T06:00:00.000+07:00').toJSDate();
      const result = service.calculateCurrentPhase(date);

      expect(result.phase).toBe('DAY');
      // Next transition should be 18:00 today
      const expectedNext = DateTime.fromISO(
        '2026-09-13T18:00:00.000+07:00',
      ).toISO();
      expect(result.nextTransitionAt).toBe(expectedNext);
    });

    it('should identify 05:59:59.999 ICT as NIGHT phase boundary', () => {
      const date = DateTime.fromISO('2026-09-13T05:59:59.999+07:00').toJSDate();
      const result = service.calculateCurrentPhase(date);

      expect(result.phase).toBe('NIGHT');
      // Next transition should be 06:00 today
      const expectedNext = DateTime.fromISO(
        '2026-09-13T06:00:00.000+07:00',
      ).toISO();
      expect(result.nextTransitionAt).toBe(expectedNext);
    });

    it('should identify 17:59:59.999 ICT as DAY phase boundary', () => {
      const date = DateTime.fromISO('2026-09-13T17:59:59.999+07:00').toJSDate();
      const result = service.calculateCurrentPhase(date);

      expect(result.phase).toBe('DAY');
      const expectedNext = DateTime.fromISO(
        '2026-09-13T18:00:00.000+07:00',
      ).toISO();
      expect(result.nextTransitionAt).toBe(expectedNext);
    });

    it('should identify 18:00:00.000 ICT as NIGHT phase boundary', () => {
      const date = DateTime.fromISO('2026-09-13T18:00:00.000+07:00').toJSDate();
      const result = service.calculateCurrentPhase(date);

      expect(result.phase).toBe('NIGHT');
      // Next transition should be 06:00 tomorrow
      const expectedNext = DateTime.fromISO(
        '2026-09-14T06:00:00.000+07:00',
      ).toISO();
      expect(result.nextTransitionAt).toBe(expectedNext);
    });
  });

  describe('assignTreatmentVersion', () => {
    const mockSeason: Season = {
      id: 1,
      name: 'Season Autumn 2026',
      status: SeasonStatus.ACTIVE,
      started_at: new Date(),
      ended_at: null,
      notes: null,
      created_at: new Date(),
      updated_at: new Date(),
      treatment_assignments: [],
      node_assignments: [],
      pump_commands: [],
      flow_events: [],
    };

    const mockGroup: TimerGroup = {
      group_id: 1,
      name: 'Group 1',
      status: TimerGroupStatus.UNASSIGNED,
      created_at: new Date(),
      updated_at: new Date(),
      treatment_assignments: [],
      node_assignments: [],
    };

    const mockPublishedVersion: TreatmentVersion = {
      id: 5,
      treatment_id: 1,
      version_num: 1,
      status: TreatmentVersionStatus.PUBLISHED,
      spray_day_s: 15,
      cooldown_day_s: 300,
      spray_night_s: 10,
      cooldown_night_s: 600,
      published_at: new Date(),
      created_by: null,
      created_at: new Date(),
      treatment: {
        id: 1,
        name: 'Lettuce Formula',
        is_archived: false,
        created_at: new Date(),
        updated_at: new Date(),
        versions: [],
      },
    };

    it('should successfully assign published treatment version and nodes to group', async () => {
      groupRepo.findOne.mockResolvedValue(mockGroup);
      seasonService.getActive.mockResolvedValue(mockSeason);
      treatmentVersionRepo.findOne.mockResolvedValue(mockPublishedVersion);
      nodeAssignRepo.find.mockResolvedValue([]); // No conflicts

      const result = await service.assignTreatmentVersion(1, {
        treatment_version_id: 5,
        node_ids: [1, 2],
      });

      expect(dataSource.transaction).toHaveBeenCalled();
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'group.assigned',
        expect.objectContaining({
          groupId: 1,
          seasonId: 1,
          treatmentVersionId: 5,
          nodeIds: [1, 2],
        }),
      );
      expect(result.group_id).toBe(1);
    });

    it('should reject invalid group_id (< 1 or > 4)', async () => {
      await expect(
        service.assignTreatmentVersion(0, {
          treatment_version_id: 5,
          node_ids: [1],
        }),
      ).rejects.toThrow(BadRequestException);

      await expect(
        service.assignTreatmentVersion(5, {
          treatment_version_id: 5,
          node_ids: [1],
        }),
      ).rejects.toThrow(BadRequestException);
    });

    it('should reject assignment when there is no active season', async () => {
      groupRepo.findOne.mockResolvedValue(mockGroup);
      seasonService.getActive.mockResolvedValue(null);

      await expect(
        service.assignTreatmentVersion(1, {
          treatment_version_id: 5,
          node_ids: [1],
        }),
      ).rejects.toThrow(BadRequestException);
    });

    it('should reject assignment when treatment version is not PUBLISHED (e.g. DRAFT)', async () => {
      groupRepo.findOne.mockResolvedValue(mockGroup);
      seasonService.getActive.mockResolvedValue(mockSeason);
      const draftVersion = {
        ...mockPublishedVersion,
        status: TreatmentVersionStatus.DRAFT,
      };
      treatmentVersionRepo.findOne.mockResolvedValue(draftVersion);

      await expect(
        service.assignTreatmentVersion(1, {
          treatment_version_id: 5,
          node_ids: [1],
        }),
      ).rejects.toThrow(BadRequestException);
    });

    it('should reject assignment when a requested node is already actively assigned to another group', async () => {
      groupRepo.findOne.mockResolvedValue(mockGroup);
      seasonService.getActive.mockResolvedValue(mockSeason);
      treatmentVersionRepo.findOne.mockResolvedValue(mockPublishedVersion);

      // Node 2 is already actively assigned to Group 2
      const existingAssignment: GroupNodeAssignment = {
        id: 10,
        group_id: 2,
        node_id: 2,
        season_id: 1,
        active: true,
        effective_from: new Date(),
        effective_to: null,
        group: mockGroup,
        season: mockSeason,
      };
      nodeAssignRepo.find.mockResolvedValue([existingAssignment]);

      await expect(
        service.assignTreatmentVersion(1, {
          treatment_version_id: 5,
          node_ids: [1, 2],
        }),
      ).rejects.toThrow(ConflictException);
    });
  });

  describe('unassign', () => {
    it('should successfully unassign group and clear node cached_group_id', async () => {
      const mockActiveGroup: TimerGroup = {
        group_id: 2,
        name: 'Group 2',
        status: TimerGroupStatus.ACTIVE,
        created_at: new Date(),
        updated_at: new Date(),
        treatment_assignments: [],
        node_assignments: [],
      };

      groupRepo.findOne.mockResolvedValue(mockActiveGroup);
      seasonService.getActive.mockResolvedValue({ id: 1 } as Season);

      const result = await service.unassign(2);

      expect(dataSource.transaction).toHaveBeenCalled();
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'group.unassigned',
        expect.objectContaining({
          groupId: 2,
          seasonId: 1,
        }),
      );
      expect(result.group_id).toBe(2);
    });
  });

  describe('getGroupStatus and getAllGroupsStatus', () => {
    it('should return complete status for all groups', async () => {
      const mockGroups: TimerGroup[] = [
        {
          group_id: 1,
          name: 'Group 1',
          status: TimerGroupStatus.ACTIVE,
          created_at: new Date(),
          updated_at: new Date(),
          treatment_assignments: [],
          node_assignments: [],
        },
      ];

      groupRepo.find.mockResolvedValue(mockGroups);
      treatmentAssignRepo.findOne.mockResolvedValue(null);
      nodeRegistryRepo.find.mockResolvedValue([]);

      const result = await service.getAllGroupsStatus();
      expect(result.length).toBe(1);
      expect(result[0].group_id).toBe(1);
    });
  });
});
