import { Test, TestingModule } from '@nestjs/testing';
import { TreatmentController } from './treatment.controller';
import { TreatmentService } from './treatment.service';
import { Treatment } from './entities/treatment.entity';
import {
  TreatmentVersion,
  TreatmentVersionStatus,
} from './entities/treatment_version.entity';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';

describe('TreatmentController', () => {
  let controller: TreatmentController;
  let service: jest.Mocked<TreatmentService>;

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
    name: 'Công thức Xà Lách Thủy Canh',
    is_archived: false,
    created_at: new Date('2026-03-01T00:00:00Z'),
    updated_at: new Date('2026-03-01T00:00:00Z'),
    versions: [mockTreatmentVersion],
  };

  beforeEach(async () => {
    const mockService = {
      create: jest.fn(),
      getById: jest.fn(),
      list: jest.fn(),
      addVersion: jest.fn(),
      publishVersion: jest.fn(),
      clone: jest.fn(),
      archive: jest.fn(),
    };

    const module: TestingModule = await Test.createTestingModule({
      controllers: [TreatmentController],
      providers: [
        {
          provide: TreatmentService,
          useValue: mockService,
        },
      ],
    })
      .overrideGuard(JwtAuthGuard)
      .useValue({ canActivate: () => true })
      .compile();

    controller = module.get<TreatmentController>(TreatmentController);
    service = module.get(TreatmentService);
  });

  it('should be defined', () => {
    expect(controller).toBeDefined();
  });

  describe('Metadata & Security Guards', () => {
    it('should be decorated with @Controller("api/treatment")', () => {
      const path = Reflect.getMetadata('path', TreatmentController);
      expect(path).toBe('api/treatment');
    });

    it('should have JwtAuthGuard applied at controller level', () => {
      const guards = Reflect.getMetadata('__guards__', TreatmentController);
      expect(guards).toBeDefined();
      expect(guards).toContain(JwtAuthGuard);
    });
  });

  describe('create', () => {
    it('should delegate to treatmentService.create and return created treatment', async () => {
      service.create.mockResolvedValueOnce(mockTreatment);

      const dto = { name: 'Công thức Xà Lách Thủy Canh' };
      const result = await controller.create(dto);

      expect(service.create).toHaveBeenCalledWith(dto);
      expect(result).toEqual(mockTreatment);
    });
  });

  describe('list', () => {
    it('should delegate to treatmentService.list with query parameters', async () => {
      const listResponse = { items: [mockTreatment], total: 1 };
      service.list.mockResolvedValueOnce(listResponse);

      const query = { limit: 10, offset: 0, is_archived: false };
      const result = await controller.list(query);

      expect(service.list).toHaveBeenCalledWith(query);
      expect(result).toEqual(listResponse);
    });
  });

  describe('getById', () => {
    it('should delegate to treatmentService.getById with parsed ID', async () => {
      service.getById.mockResolvedValueOnce(mockTreatment);

      const result = await controller.getById(1);

      expect(service.getById).toHaveBeenCalledWith(1);
      expect(result).toEqual(mockTreatment);
    });
  });

  describe('addVersion', () => {
    it('should delegate to treatmentService.addVersion with ID and DTO', async () => {
      const newVersion = {
        ...mockTreatmentVersion,
        id: 11,
        version_num: 2,
      };
      service.addVersion.mockResolvedValueOnce(newVersion);

      const dto = {
        spray_day_s: 25,
        cooldown_day_s: 250,
        spray_night_s: 15,
        cooldown_night_s: 500,
      };
      const result = await controller.addVersion(1, dto);

      expect(service.addVersion).toHaveBeenCalledWith(1, dto);
      expect(result).toEqual(newVersion);
    });
  });

  describe('publishVersion', () => {
    it('should delegate to treatmentService.publishVersion with treatmentId and versionId', async () => {
      const publishedVersion = {
        ...mockTreatmentVersion,
        status: TreatmentVersionStatus.PUBLISHED,
        published_at: new Date(),
      };
      service.publishVersion.mockResolvedValueOnce(publishedVersion);

      const result = await controller.publishVersion(1, 10);

      expect(service.publishVersion).toHaveBeenCalledWith(1, 10);
      expect(result.status).toBe(TreatmentVersionStatus.PUBLISHED);
    });
  });

  describe('clone', () => {
    it('should delegate to treatmentService.clone with treatmentId and clone DTO', async () => {
      const clonedTreatment = {
        ...mockTreatment,
        id: 2,
        name: 'Công thức Xà Lách Thủy Canh (Copy)',
      };
      service.clone.mockResolvedValueOnce(clonedTreatment);

      const dto = { name: 'Công thức Xà Lách Thủy Canh (Copy)' };
      const result = await controller.clone(1, dto);

      expect(service.clone).toHaveBeenCalledWith(1, dto);
      expect(result).toEqual(clonedTreatment);
    });
  });

  describe('archive', () => {
    it('should delegate to treatmentService.archive with treatmentId', async () => {
      const archivedTreatment = {
        ...mockTreatment,
        is_archived: true,
      };
      service.archive.mockResolvedValueOnce(archivedTreatment);

      const result = await controller.archive(1);

      expect(service.archive).toHaveBeenCalledWith(1);
      expect(result.is_archived).toBe(true);
    });
  });
});
