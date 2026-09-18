import { Test, TestingModule } from '@nestjs/testing';
import { SeasonController } from './season.controller';
import { SeasonService } from './season.service';
import { Season, SeasonStatus } from './entities/season.entity';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';

describe('SeasonController', () => {
  let controller: SeasonController;
  let service: jest.Mocked<SeasonService>;

  const mockSeason: Season = {
    id: 1,
    name: 'Vụ Cải Kale Xuân Hè 2026',
    started_at: new Date('2026-03-01T00:00:00Z'),
    ended_at: null,
    status: SeasonStatus.ACTIVE,
    notes: 'Khu A',
    target_ec: 1.6,
    target_ph: 6.0,
    created_at: new Date('2026-03-01T00:00:00Z'),
    updated_at: new Date('2026-03-01T00:00:00Z'),
    treatment_assignments: [],
    node_assignments: [],
    pump_commands: [],
    flow_events: [],
  };

  beforeEach(async () => {
    const mockService = {
      create: jest.fn(),
      getActive: jest.fn(),
      getById: jest.fn(),
      list: jest.fn(),
      endSeason: jest.fn(),
    };

    const module: TestingModule = await Test.createTestingModule({
      controllers: [SeasonController],
      providers: [
        {
          provide: SeasonService,
          useValue: mockService,
        },
      ],
    })
      .overrideGuard(JwtAuthGuard)
      .useValue({ canActivate: () => true })
      .compile();

    controller = module.get<SeasonController>(SeasonController);
    service = module.get(SeasonService);
  });

  it('should be defined', () => {
    expect(controller).toBeDefined();
  });

  describe('Metadata & Security Guards', () => {
    it('should be decorated with @Controller("api/season")', () => {
      const path = Reflect.getMetadata('path', SeasonController);
      expect(path).toBe('api/season');
    });

    it('should have JwtAuthGuard applied at controller level', () => {
      const guards = Reflect.getMetadata('__guards__', SeasonController);
      expect(guards).toBeDefined();
      expect(guards).toContain(JwtAuthGuard);
    });
  });

  describe('create', () => {
    it('should delegate to seasonService.create and return created season', async () => {
      service.create.mockResolvedValueOnce(mockSeason);

      const dto = { name: 'Vụ Cải Kale Xuân Hè 2026', notes: 'Khu A' };
      const result = await controller.create(dto);

      expect(service.create).toHaveBeenCalledWith(dto);
      expect(result).toEqual(mockSeason);
    });
  });

  describe('getActive', () => {
    it('should delegate to seasonService.getActive', async () => {
      service.getActive.mockResolvedValueOnce(mockSeason);

      const result = await controller.getActive();
      expect(service.getActive).toHaveBeenCalled();
      expect(result).toEqual(mockSeason);
    });

    it('should return null when no active season', async () => {
      service.getActive.mockResolvedValueOnce(null);

      const result = await controller.getActive();
      expect(result).toBeNull();
    });
  });

  describe('getById', () => {
    it('should delegate to seasonService.getById with parsed ID', async () => {
      service.getById.mockResolvedValueOnce(mockSeason);

      const result = await controller.getById(1);
      expect(service.getById).toHaveBeenCalledWith(1);
      expect(result).toEqual(mockSeason);
    });
  });

  describe('endSeason', () => {
    it('should delegate to seasonService.endSeason with ID and DTO', async () => {
      const endedSeason = {
        ...mockSeason,
        status: SeasonStatus.ENDED,
        ended_at: new Date('2026-06-30T00:00:00Z'),
      };
      service.endSeason.mockResolvedValueOnce(endedSeason);

      const dto = { notes: 'Hoàn thành thu hoạch' };
      const result = await controller.endSeason(1, dto);

      expect(service.endSeason).toHaveBeenCalledWith(1, dto);
      expect(result).toEqual(endedSeason);
    });
  });

  describe('list', () => {
    it('should delegate to seasonService.list with query parameters', async () => {
      const listResponse = { items: [mockSeason], total: 1 };
      service.list.mockResolvedValueOnce(listResponse);

      const query = { status: SeasonStatus.ACTIVE, limit: 10, offset: 0 };
      const result = await controller.list(query);

      expect(service.list).toHaveBeenCalledWith(query);
      expect(result).toEqual(listResponse);
    });
  });
});
