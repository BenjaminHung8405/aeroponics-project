import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';
import { DataSource, QueryFailedError, Repository } from 'typeorm';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { ConflictException, NotFoundException, BadRequestException } from '@nestjs/common';
import { SeasonService } from './season.service';
import { Season, SeasonStatus } from './entities/season.entity';
import { SeasonCreatedEvent, SeasonEndedEvent } from './events/season.events';

describe('SeasonService', () => {
  let service: SeasonService;
  let repository: jest.Mocked<Repository<Season>>;
  let dataSource: jest.Mocked<DataSource>;
  let eventEmitter: jest.Mocked<EventEmitter2>;

  const mockSeason: Season = {
    id: 1,
    name: 'Vụ Cải Kale Xuân Hè 2026',
    started_at: new Date('2026-03-01T00:00:00Z'),
    ended_at: null,
    status: SeasonStatus.ACTIVE,
    notes: 'Khu A',
    created_at: new Date('2026-03-01T00:00:00Z'),
    updated_at: new Date('2026-03-01T00:00:00Z'),
    treatment_assignments: [],
    node_assignments: [],
    pump_commands: [],
    flow_events: [],
  };

  beforeEach(async () => {
    const mockRepo = {
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
        SeasonService,
        {
          provide: getRepositoryToken(Season),
          useValue: mockRepo,
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

    service = module.get<SeasonService>(SeasonService);
    repository = module.get(getRepositoryToken(Season));
    dataSource = module.get(DataSource);
    eventEmitter = module.get(EventEmitter2);
  });

  it('should be defined', () => {
    expect(service).toBeDefined();
  });

  describe('create', () => {
    it('should create and activate a new season when no active season exists', async () => {
      repository.findOne.mockResolvedValueOnce(null);
      repository.create.mockReturnValueOnce({
        ...mockSeason,
        id: undefined as unknown as number,
      });
      repository.save.mockResolvedValueOnce(mockSeason);

      const result = await service.create({
        name: 'Vụ Cải Kale Xuân Hè 2026',
        notes: 'Khu A',
      });

      expect(repository.findOne).toHaveBeenCalledWith({
        where: { status: SeasonStatus.ACTIVE },
      });
      expect(repository.save).toHaveBeenCalled();
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'season.created',
        expect.any(SeasonCreatedEvent),
      );
      expect(result).toEqual(mockSeason);
    });

    it('should throw ConflictException if an active season already exists', async () => {
      repository.findOne.mockResolvedValueOnce(mockSeason);

      await expect(
        service.create({
          name: 'Vụ Cà Chua Bi 2026',
        }),
      ).rejects.toThrow(ConflictException);

      expect(repository.save).not.toHaveBeenCalled();
      expect(eventEmitter.emit).not.toHaveBeenCalled();
    });

    it('should catch database unique constraint violation (code 23505) and throw ConflictException', async () => {
      repository.findOne.mockResolvedValueOnce(null);
      repository.create.mockReturnValueOnce({
        ...mockSeason,
        id: undefined as unknown as number,
      });

      const pgUniqueError = new QueryFailedError(
        'query',
        [],
        new Error('duplicate key value violates unique constraint "uq_seasons_one_active"'),
      );
      (pgUniqueError as unknown as { driverError: { code: string } }).driverError = {
        code: '23505',
      };

      repository.save.mockRejectedValueOnce(pgUniqueError);

      await expect(
        service.create({
          name: 'Vụ Cải Thao Diễn Đồng Thời',
        }),
      ).rejects.toThrow(ConflictException);
    });

    it('should rethrow unexpected database errors', async () => {
      repository.findOne.mockResolvedValueOnce(null);
      repository.create.mockReturnValueOnce({
        ...mockSeason,
        id: undefined as unknown as number,
      });
      repository.save.mockRejectedValueOnce(new Error('Connection lost'));

      await expect(
        service.create({
          name: 'Vụ Test Lỗi DB',
        }),
      ).rejects.toThrow('Connection lost');
    });
  });

  describe('getActive', () => {
    it('should return active season when present', async () => {
      repository.findOne.mockResolvedValueOnce(mockSeason);

      const result = await service.getActive();
      expect(result).toEqual(mockSeason);
      expect(repository.findOne).toHaveBeenCalledWith({
        where: { status: SeasonStatus.ACTIVE },
      });
    });

    it('should return null when no season is active', async () => {
      repository.findOne.mockResolvedValueOnce(null);

      const result = await service.getActive();
      expect(result).toBeNull();
    });
  });

  describe('getById', () => {
    it('should return season when found', async () => {
      repository.findOne.mockResolvedValueOnce(mockSeason);

      const result = await service.getById(1);
      expect(result).toEqual(mockSeason);
      expect(repository.findOne).toHaveBeenCalledWith({ where: { id: 1 } });
    });

    it('should throw NotFoundException when season not found', async () => {
      repository.findOne.mockResolvedValueOnce(null);

      await expect(service.getById(999)).rejects.toThrow(NotFoundException);
    });
  });

  describe('list', () => {
    it('should return paginated list of seasons', async () => {
      const qb: any = {
        orderBy: jest.fn().mockReturnThis(),
        addOrderBy: jest.fn().mockReturnThis(),
        take: jest.fn().mockReturnThis(),
        skip: jest.fn().mockReturnThis(),
        where: jest.fn().mockReturnThis(),
        getManyAndCount: jest.fn().mockResolvedValue([[mockSeason], 1]),
      };
      repository.createQueryBuilder.mockReturnValue(qb);

      const result = await service.list({ limit: 10, offset: 0 });

      expect(qb.take).toHaveBeenCalledWith(10);
      expect(qb.skip).toHaveBeenCalledWith(0);
      expect(result).toEqual({ items: [mockSeason], total: 1 });
    });

    it('should filter by status when provided', async () => {
      const qb: any = {
        orderBy: jest.fn().mockReturnThis(),
        addOrderBy: jest.fn().mockReturnThis(),
        take: jest.fn().mockReturnThis(),
        skip: jest.fn().mockReturnThis(),
        where: jest.fn().mockReturnThis(),
        getManyAndCount: jest.fn().mockResolvedValue([[mockSeason], 1]),
      };
      repository.createQueryBuilder.mockReturnValue(qb);

      await service.list({ status: SeasonStatus.ACTIVE });

      expect(qb.where).toHaveBeenCalledWith('season.status = :status', {
        status: SeasonStatus.ACTIVE,
      });
    });
  });

  describe('endSeason', () => {
    it('should successfully end an active season inside transaction', async () => {
      const activeSeasonCopy = { ...mockSeason, status: SeasonStatus.ACTIVE };
      const endedSeason = {
        ...activeSeasonCopy,
        status: SeasonStatus.ENDED,
        ended_at: new Date('2026-06-30T00:00:00Z'),
        notes: 'Khu A\n[End Notes]: Thu hoạch 120kg',
      };

      const mockEntityManager = {
        findOne: jest.fn().mockResolvedValue(activeSeasonCopy),
        save: jest.fn().mockResolvedValue(endedSeason),
      };

      dataSource.transaction.mockImplementation(async (callback: any) => {
        return callback(mockEntityManager);
      });

      const result = await service.endSeason(1, { notes: 'Thu hoạch 120kg' });

      expect(mockEntityManager.findOne).toHaveBeenCalledWith(Season, {
        where: { id: 1 },
        lock: { mode: 'pessimistic_write' },
      });
      expect(mockEntityManager.save).toHaveBeenCalled();
      expect(eventEmitter.emit).toHaveBeenCalledWith(
        'season.ended',
        expect.any(SeasonEndedEvent),
      );
      expect(result.status).toBe(SeasonStatus.ENDED);
      expect(result.notes).toContain('Thu hoạch 120kg');
    });

    it('should throw NotFoundException if season to end does not exist', async () => {
      const mockEntityManager = {
        findOne: jest.fn().mockResolvedValue(null),
      };

      dataSource.transaction.mockImplementation(async (callback: any) => {
        return callback(mockEntityManager);
      });

      await expect(service.endSeason(99)).rejects.toThrow(NotFoundException);
      expect(eventEmitter.emit).not.toHaveBeenCalled();
    });

    it('should throw BadRequestException if season is already ended (prevent double-end)', async () => {
      const alreadyEndedSeason = {
        ...mockSeason,
        status: SeasonStatus.ENDED,
        ended_at: new Date('2026-06-01T00:00:00Z'),
      };

      const mockEntityManager = {
        findOne: jest.fn().mockResolvedValue(alreadyEndedSeason),
      };

      dataSource.transaction.mockImplementation(async (callback: any) => {
        return callback(mockEntityManager);
      });

      await expect(service.endSeason(1)).rejects.toThrow(BadRequestException);
      expect(eventEmitter.emit).not.toHaveBeenCalled();
    });
  });
});
