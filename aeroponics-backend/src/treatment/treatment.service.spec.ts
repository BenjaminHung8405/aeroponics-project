import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';
import { DataSource, QueryFailedError, Repository } from 'typeorm';
import { EventEmitter2 } from '@nestjs/event-emitter';
import {
  ConflictException,
  NotFoundException,
  BadRequestException,
} from '@nestjs/common';
import { TreatmentService } from './treatment.service';
import { Treatment } from './entities/treatment.entity';
import {
  TreatmentVersion,
  TreatmentVersionStatus,
} from './entities/treatment_version.entity';
import {
  TreatmentCreatedEvent,
  TreatmentVersionCreatedEvent,
  TreatmentVersionPublishedEvent,
  TreatmentClonedEvent,
  TreatmentArchivedEvent,
} from './events/treatment.events';

describe('TreatmentService', () => {
  let service: TreatmentService;
  let treatmentRepository: jest.Mocked<Repository<Treatment>>;
  let dataSource: jest.Mocked<DataSource>;
  let eventEmitter: jest.Mocked<EventEmitter2>;

  const mockTreatmentVersion: TreatmentVersion = {
    id: 10,
    treatment_id: 1,
    version_num: 1,
    status: TreatmentVersionStatus.DRAFT,
    spray_day_s: 30,
    cooldown_day_s: 300,
    spray_night_s: 15,
    cooldown_night_s: 600,
    created_by: 'Operator 1',
    created_at: new Date('2026-03-01T00:00:00Z'),
    published_at: null,
    treatment: null as unknown as Treatment,
  };

  const mockTreatment: Treatment = {
    id: 1,
    name: 'Công thức Cải Xoăn Tiêu Chuẩn',
    is_archived: false,
    created_at: new Date('2026-03-01T00:00:00Z'),
    updated_at: new Date('2026-03-01T00:00:00Z'),
    versions: [mockTreatmentVersion],
  };

  beforeEach(async () => {
    const mockTreatRepo = {
      create: jest.fn(),
      save: jest.fn(),
      findOne: jest.fn(),
      createQueryBuilder: jest.fn(),
    };

    const mockVerRepo = {
      create: jest.fn(),
      save: jest.fn(),
      findOne: jest.fn(),
      createQueryBuilder: jest.fn(),
    };

    const mockDs = {
      transaction: jest.fn(),
    };

    const mockEe = {
      emit: jest.fn(),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        TreatmentService,
        {
          provide: getRepositoryToken(Treatment),
          useValue: mockTreatRepo,
        },
        {
          provide: getRepositoryToken(TreatmentVersion),
          useValue: mockVerRepo,
        },
        {
          provide: DataSource,
          useValue: mockDs,
        },
        {
          provide: EventEmitter2,
          useValue: mockEe,
        },
      ],
    }).compile();

    service = module.get<TreatmentService>(TreatmentService);
    treatmentRepository = module.get(getRepositoryToken(Treatment));
    dataSource = module.get(DataSource);
    eventEmitter = module.get(EventEmitter2);
  });

  it('should be defined', () => {
    expect(service).toBeDefined();
  });

  describe('create', () => {
    it('should create a treatment without initial version if no spray params provided', async () => {
      const mockManager = {
        create: jest.fn().mockReturnValue({ ...mockTreatment, versions: [] }),
        save: jest.fn().mockResolvedValue({ ...mockTreatment, versions: [] }),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      const result = await service.create({ name: 'Chỉ tạo container' });

      expect(mockManager.create).toHaveBeenCalledWith(Treatment, {
        name: 'Chỉ tạo container',
        is_archived: false,
      });
      expect(mockManager.save).toHaveBeenCalled();
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'treatment.created',
        expect.any(TreatmentCreatedEvent),
      );
      expect(result.name).toBe(mockTreatment.name);
    });

    it('should create a treatment and version 1 as DRAFT if spray params are provided', async () => {
      const mockManager = {
        create: jest
          .fn()
          .mockReturnValueOnce({ ...mockTreatment, id: 1 }) // for Treatment
          .mockReturnValueOnce(mockTreatmentVersion),        // for TreatmentVersion
        save: jest
          .fn()
          .mockResolvedValueOnce({ ...mockTreatment, id: 1 }) // for Treatment
          .mockResolvedValueOnce(mockTreatmentVersion),        // for TreatmentVersion
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      const result = await service.create({
        name: 'Cải Xoăn Full',
        spray_day_s: 30,
        cooldown_day_s: 300,
        spray_night_s: 15,
        cooldown_night_s: 600,
        created_by: 'Operator 1',
      });

      expect(mockManager.create).toHaveBeenCalledTimes(2);
      expect(mockManager.save).toHaveBeenCalledTimes(2);
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'treatment.version.created',
        expect.any(TreatmentVersionCreatedEvent),
      );
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'treatment.created',
        expect.any(TreatmentCreatedEvent),
      );
      expect(result.versions).toHaveLength(1);
      expect(result.versions[0].status).toBe(TreatmentVersionStatus.DRAFT);
    });
  });

  describe('getById', () => {
    it('should return treatment with sorted versions when found', async () => {
      const qb: any = {
        leftJoinAndSelect: jest.fn().mockReturnThis(),
        where: jest.fn().mockReturnThis(),
        orderBy: jest.fn().mockReturnThis(),
        getOne: jest.fn().mockResolvedValue(mockTreatment),
      };
      treatmentRepository.createQueryBuilder.mockReturnValue(qb);

      const result = await service.getById(1);

      expect(treatmentRepository.createQueryBuilder).toHaveBeenCalledWith('treatment');
      expect(qb.leftJoinAndSelect).toHaveBeenCalledWith('treatment.versions', 'versions');
      expect(qb.where).toHaveBeenCalledWith('treatment.id = :id', { id: 1 });
      expect(qb.orderBy).toHaveBeenCalledWith('versions.version_num', 'ASC');
      expect(result).toEqual(mockTreatment);
    });

    it('should throw NotFoundException when treatment not found', async () => {
      const qb: any = {
        leftJoinAndSelect: jest.fn().mockReturnThis(),
        where: jest.fn().mockReturnThis(),
        orderBy: jest.fn().mockReturnThis(),
        getOne: jest.fn().mockResolvedValue(null),
      };
      treatmentRepository.createQueryBuilder.mockReturnValue(qb);

      await expect(service.getById(999)).rejects.toThrow(NotFoundException);
    });
  });

  describe('list', () => {
    it('should return paginated treatments with default limit and offset', async () => {
      const qb: any = {
        leftJoinAndSelect: jest.fn().mockReturnThis(),
        orderBy: jest.fn().mockReturnThis(),
        addOrderBy: jest.fn().mockReturnThis(),
        take: jest.fn().mockReturnThis(),
        skip: jest.fn().mockReturnThis(),
        where: jest.fn().mockReturnThis(),
        getManyAndCount: jest.fn().mockResolvedValue([[mockTreatment], 1]),
      };
      treatmentRepository.createQueryBuilder.mockReturnValue(qb);

      const result = await service.list();

      expect(qb.take).toHaveBeenCalledWith(20);
      expect(qb.skip).toHaveBeenCalledWith(0);
      expect(result.items).toHaveLength(1);
      expect(result.total).toBe(1);
    });

    it('should apply is_archived filter when specified', async () => {
      const qb: any = {
        leftJoinAndSelect: jest.fn().mockReturnThis(),
        orderBy: jest.fn().mockReturnThis(),
        addOrderBy: jest.fn().mockReturnThis(),
        take: jest.fn().mockReturnThis(),
        skip: jest.fn().mockReturnThis(),
        where: jest.fn().mockReturnThis(),
        getManyAndCount: jest.fn().mockResolvedValue([[], 0]),
      };
      treatmentRepository.createQueryBuilder.mockReturnValue(qb);

      await service.list({ is_archived: false, limit: 10, offset: 5 });

      expect(qb.where).toHaveBeenCalledWith('treatment.is_archived = :isArchived', {
        isArchived: false,
      });
      expect(qb.take).toHaveBeenCalledWith(10);
      expect(qb.skip).toHaveBeenCalledWith(5);
    });
  });

  describe('addVersion', () => {
    it('should add a new version with incremented version_num as DRAFT', async () => {
      const qb: any = {
        select: jest.fn().mockReturnThis(),
        where: jest.fn().mockReturnThis(),
        getRawOne: jest.fn().mockResolvedValue({ max: 2 }),
      };

      const newVersion: TreatmentVersion = {
        ...mockTreatmentVersion,
        id: 11,
        version_num: 3,
        status: TreatmentVersionStatus.DRAFT,
      };

      const mockManager = {
        findOne: jest.fn().mockResolvedValue(mockTreatment),
        createQueryBuilder: jest.fn().mockReturnValue(qb),
        create: jest.fn().mockReturnValue(newVersion),
        save: jest.fn().mockResolvedValue(newVersion),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      const result = await service.addVersion(1, {
        spray_day_s: 25,
        cooldown_day_s: 250,
        spray_night_s: 15,
        cooldown_night_s: 500,
      });

      expect(mockManager.create).toHaveBeenCalledWith(
        TreatmentVersion,
        expect.objectContaining({
          treatment_id: 1,
          version_num: 3,
          status: TreatmentVersionStatus.DRAFT,
          published_at: null,
        }),
      );
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'treatment.version.created',
        expect.any(TreatmentVersionCreatedEvent),
      );
      expect(result.version_num).toBe(3);
      expect(result.status).toBe(TreatmentVersionStatus.DRAFT);
    });

    it('should throw NotFoundException if treatment does not exist', async () => {
      const mockManager = {
        findOne: jest.fn().mockResolvedValue(null),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      await expect(
        service.addVersion(999, {
          spray_day_s: 25,
          cooldown_day_s: 250,
          spray_night_s: 15,
          cooldown_night_s: 500,
        }),
      ).rejects.toThrow(NotFoundException);
    });

    it('should throw BadRequestException if treatment is archived', async () => {
      const mockManager = {
        findOne: jest.fn().mockResolvedValue({ ...mockTreatment, is_archived: true }),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      await expect(
        service.addVersion(1, {
          spray_day_s: 25,
          cooldown_day_s: 250,
          spray_night_s: 15,
          cooldown_night_s: 500,
        }),
      ).rejects.toThrow(BadRequestException);
    });

    it('should throw ConflictException if unique constraint 23505 occurs', async () => {
      const qb: any = {
        select: jest.fn().mockReturnThis(),
        where: jest.fn().mockReturnThis(),
        getRawOne: jest.fn().mockResolvedValue({ max: 1 }),
      };

      const queryError = new QueryFailedError('query', [], new Error('Duplicate'));
      (queryError as any).driverError = { code: '23505' };

      const mockManager = {
        findOne: jest.fn().mockResolvedValue(mockTreatment),
        createQueryBuilder: jest.fn().mockReturnValue(qb),
        create: jest.fn().mockReturnValue(mockTreatmentVersion),
        save: jest.fn().mockRejectedValue(queryError),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      await expect(
        service.addVersion(1, {
          spray_day_s: 25,
          cooldown_day_s: 250,
          spray_night_s: 15,
          cooldown_night_s: 500,
        }),
      ).rejects.toThrow(ConflictException);
    });
  });

  describe('publishVersion', () => {
    it('should lock with pessimistic_write, publish DRAFT version, and set published_at', async () => {
      const draftVersion: TreatmentVersion = {
        ...mockTreatmentVersion,
        status: TreatmentVersionStatus.DRAFT,
        published_at: null,
      };

      const publishedVersion: TreatmentVersion = {
        ...draftVersion,
        status: TreatmentVersionStatus.PUBLISHED,
        published_at: new Date('2026-03-02T10:00:00Z'),
      };

      const mockManager = {
        findOne: jest.fn().mockResolvedValue(draftVersion),
        save: jest.fn().mockResolvedValue(publishedVersion),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      const result = await service.publishVersion(1, 10);

      expect(mockManager.findOne).toHaveBeenCalledWith(TreatmentVersion, {
        where: { id: 10, treatment_id: 1 },
        lock: { mode: 'pessimistic_write' },
      });
      expect(result.status).toBe(TreatmentVersionStatus.PUBLISHED);
      expect(result.published_at).toBeDefined();
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'treatment.version.published',
        expect.any(TreatmentVersionPublishedEvent),
      );
    });

    it('should throw ConflictException (409) if version is already PUBLISHED', async () => {
      const alreadyPublishedVersion: TreatmentVersion = {
        ...mockTreatmentVersion,
        status: TreatmentVersionStatus.PUBLISHED,
        published_at: new Date('2026-03-01T12:00:00Z'),
      };

      const mockManager = {
        findOne: jest.fn().mockResolvedValue(alreadyPublishedVersion),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      await expect(service.publishVersion(1, 10)).rejects.toThrow(
        ConflictException,
      );
    });

    it('should throw BadRequestException if version is ARCHIVED', async () => {
      const archivedVersion: TreatmentVersion = {
        ...mockTreatmentVersion,
        status: TreatmentVersionStatus.ARCHIVED,
      };

      const mockManager = {
        findOne: jest.fn().mockResolvedValue(archivedVersion),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      await expect(service.publishVersion(1, 10)).rejects.toThrow(
        BadRequestException,
      );
    });

    it('should throw NotFoundException if version does not exist', async () => {
      const mockManager = {
        findOne: jest.fn().mockResolvedValue(null),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      await expect(service.publishVersion(1, 999)).rejects.toThrow(
        NotFoundException,
      );
    });
  });

  describe('clone', () => {
    it('should clone treatment and all versions with status reset to DRAFT and published_at reset to null', async () => {
      const originalVersions: TreatmentVersion[] = [
        {
          ...mockTreatmentVersion,
          id: 10,
          version_num: 1,
          status: TreatmentVersionStatus.PUBLISHED, // Was published!
          published_at: new Date('2026-03-01T12:00:00Z'),
        },
        {
          ...mockTreatmentVersion,
          id: 11,
          version_num: 2,
          status: TreatmentVersionStatus.PUBLISHED, // Was published!
          published_at: new Date('2026-03-02T12:00:00Z'),
        },
      ];

      const originalTreatment: Treatment = {
        ...mockTreatment,
        id: 1,
        name: 'Công thức Gốc',
        versions: originalVersions,
      };

      const mockManager = {
        findOne: jest.fn().mockResolvedValue(originalTreatment),
        create: jest.fn().mockImplementation((entityClass, data) => ({
          ...data,
          id: entityClass === Treatment ? 2 : 100,
        })),
        save: jest.fn().mockImplementation((entityClass, data) => data),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      const result = await service.clone(1);

      expect(result.name).toBe('Công thức Gốc (Copy)');
      expect(result.versions).toHaveLength(2);

      // Verify all cloned versions MUST BE DRAFT and published_at MUST BE NULL
      for (const v of result.versions) {
        expect(v.status).toBe(TreatmentVersionStatus.DRAFT);
        expect(v.published_at).toBeNull();
      }

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'treatment.cloned',
        expect.any(TreatmentClonedEvent),
      );
    });

    it('should use custom name if provided in CloneTreatmentDto', async () => {
      const originalTreatment: Treatment = {
        ...mockTreatment,
        versions: [],
      };

      const mockManager = {
        findOne: jest.fn().mockResolvedValue(originalTreatment),
        create: jest.fn().mockImplementation((_, data) => ({ ...data, id: 3 })),
        save: jest.fn().mockImplementation((_, data) => data),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      const result = await service.clone(1, { name: 'Công thức Tùy Biến' });

      expect(result.name).toBe('Công thức Tùy Biến');
    });

    it('should throw NotFoundException if original treatment does not exist', async () => {
      const mockManager = {
        findOne: jest.fn().mockResolvedValue(null),
      };

      dataSource.transaction.mockImplementationOnce(async (cb: any) =>
        cb(mockManager),
      );

      await expect(service.clone(999)).rejects.toThrow(NotFoundException);
    });
  });

  describe('archive', () => {
    it('should set is_archived = true and emit event', async () => {
      treatmentRepository.findOne.mockResolvedValueOnce({
        ...mockTreatment,
        is_archived: false,
      });
      treatmentRepository.save.mockResolvedValueOnce({
        ...mockTreatment,
        is_archived: true,
      });

      const result = await service.archive(1);

      expect(result.is_archived).toBe(true);
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'treatment.archived',
        expect.any(TreatmentArchivedEvent),
      );
    });

    it('should throw NotFoundException if treatment does not exist', async () => {
      treatmentRepository.findOne.mockResolvedValueOnce(null);

      await expect(service.archive(999)).rejects.toThrow(NotFoundException);
    });

    it('should be idempotent if already archived', async () => {
      const alreadyArchived = { ...mockTreatment, is_archived: true };
      treatmentRepository.findOne.mockResolvedValueOnce(alreadyArchived);

      const result = await service.archive(1);

      expect(result.is_archived).toBe(true);
      expect(treatmentRepository.save).not.toHaveBeenCalled();
    });
  });
});
